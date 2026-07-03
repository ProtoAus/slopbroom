/*
 Copyright (C) 2025 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdl/Map_CopyPaste.h"

#include "Logger.h"
#include "SimpleParserStatus.h"
#include "Uuid.h"
#include "mdl/ApplyAndSwap.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushFaceReader.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinitionUtils.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/LinkedGroupUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/ModelUtils.h"
#include "mdl/NodeReader.h"
#include "mdl/NodeWriter.h"
#include "mdl/PasteType.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"

#include "kd/contracts.h"
#include "kd/overload.h"
#include "kd/ranges/to.h"
#include "kd/vector_utils.h"

#include "vm/mat.h"
#include "vm/scalar.h"
#include "vm/vec.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace tb::mdl
{
namespace
{

auto extractNodesToPaste(const std::vector<Node*>& nodes, Node* parent)
{
  auto nodesToDetach = std::vector<Node*>{};
  auto nodesToDelete = std::vector<Node*>{};
  auto nodesToAdd = std::map<Node*, std::vector<Node*>>{};

  for (auto* node : nodes)
  {
    node->accept(kdl::overload(
      [&](auto&& thisLambda, WorldNode& worldNode) {
        worldNode.visitChildren(thisLambda);
        nodesToDelete.push_back(&worldNode);
      },
      [&](auto&& thisLambda, LayerNode& layerNode) {
        layerNode.visitChildren(thisLambda);
        nodesToDetach.push_back(&layerNode);
        nodesToDelete.push_back(&layerNode);
      },
      [&](GroupNode& groupNode) {
        nodesToDetach.push_back(&groupNode);
        nodesToAdd[parent].push_back(&groupNode);
      },
      [&](auto&& thisLambda, EntityNode& entityNode) {
        if (isWorldspawn(entityNode.entity().classname()))
        {
          entityNode.visitChildren(thisLambda);
          nodesToDetach.push_back(&entityNode);
          nodesToDelete.push_back(&entityNode);
        }
        else
        {
          nodesToDetach.push_back(&entityNode);
          nodesToAdd[parent].push_back(&entityNode);
        }
      },
      [&](BrushNode& brushNode) {
        nodesToDetach.push_back(&brushNode);
        nodesToAdd[parent].push_back(&brushNode);
      },
      [&](PatchNode& patchNode) {
        nodesToDetach.push_back(&patchNode);
        nodesToAdd[parent].push_back(&patchNode);
      }));
  }

  for (auto* node : nodesToDetach)
  {
    if (auto* nodeParent = node->parent())
    {
      nodeParent->removeChild(node);
    }
  }
  kdl::vec_clear_and_delete(nodesToDelete);

  return nodesToAdd;
}

std::vector<IdType> allPersistentGroupIds(const Node& root)
{
  auto result = std::vector<IdType>{};
  root.accept(kdl::overload(
    [](auto&& thisLambda, const WorldNode& worldNode) {
      worldNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, const LayerNode& layerNode) {
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, const GroupNode& groupNode) {
      if (const auto persistentId = groupNode.persistentId())
      {
        result.push_back(*persistentId);
      }
      groupNode.visitChildren(thisLambda);
    },
    [](const EntityNode&) {},
    [](const BrushNode&) {},
    [](const PatchNode&) {}));
  return result;
}

void fixRedundantPersistentIds(
  const std::map<Node*, std::vector<Node*>>& nodesToAdd,
  const std::vector<IdType>& existingPersistentGroupIds)
{
  auto persistentGroupIds = kdl::vector_set{existingPersistentGroupIds};
  for (auto& [newParent, nodesToAddToParent] : nodesToAdd)
  {
    for (auto* node : nodesToAddToParent)
    {
      node->accept(kdl::overload(
        [&](auto&& thisLambda, WorldNode& worldNode) {
          worldNode.visitChildren(thisLambda);
        },
        [&](auto&& thisLambda, LayerNode& layerNode) {
          layerNode.visitChildren(thisLambda);
        },
        [&](auto&& thisLambda, GroupNode& groupNode) {
          if (const auto persistentGroupId = groupNode.persistentId())
          {
            if (!persistentGroupIds.insert(*persistentGroupId).second)
            {
              // a group with this ID is already in the map or being pasted
              groupNode.resetPersistentId();
            }
          }
          groupNode.visitChildren(thisLambda);
        },
        [](EntityNode&) {},
        [](BrushNode&) {},
        [](PatchNode&) {}));
    }
  }
}

void fixRecursiveLinkedGroups(
  const std::map<Node*, std::vector<Node*>>& nodesToAdd, Logger& logger)
{
  for (auto& [newParent, nodesToAddToParent] : nodesToAdd)
  {
    const auto linkedGroupIds = kdl::vec_sort(collectParentLinkedGroupIds(*newParent));
    for (auto* node : nodesToAddToParent)
    {
      node->accept(kdl::overload(
        [&](auto&& thisLambda, WorldNode& worldNode) {
          worldNode.visitChildren(thisLambda);
        },
        [&](auto&& thisLambda, LayerNode& layerNode) {
          layerNode.visitChildren(thisLambda);
        },
        [&](auto&& thisLambda, GroupNode& groupNode) {
          const auto& linkId = groupNode.linkId();
          if (std::ranges::binary_search(linkedGroupIds, linkId))
          {
            logger.warn() << "Unlinking recursive linked group with ID '" << linkId
                          << "'";

            auto group = groupNode.group();
            group.setTransformation(vm::mat4x4d::identity());
            groupNode.setGroup(std::move(group));
            groupNode.setLinkId(generateUuid());
          }
          groupNode.visitChildren(thisLambda);
        },
        [](EntityNode&) {},
        [](BrushNode&) {},
        [](PatchNode&) {}));
    }
  }
}

void copyAndSetLinkIds(
  const std::map<Node*, std::vector<Node*>>& nodesToAdd,
  WorldNode& worldNode,
  Logger& logger)
{
  const auto errors = copyAndSetLinkIdsBeforeAddingNodes(nodesToAdd, worldNode);
  for (const auto& error : errors)
  {
    logger.warn() << "Could not paste linked groups: " + error.msg;
  }
}

bool pasteNodes(Map& map, const std::vector<Node*>& nodes)
{
  const auto nodesToAdd = extractNodesToPaste(nodes, parentForNodes(map));
  fixRedundantPersistentIds(nodesToAdd, allPersistentGroupIds(map.worldNode()));
  fixRecursiveLinkedGroups(nodesToAdd, map.logger());
  copyAndSetLinkIds(nodesToAdd, map.worldNode(), map.logger());

  auto transaction = Transaction{map, "Paste Nodes"};

  const auto addedNodes = addNodes(map, nodesToAdd);
  if (addedNodes.empty())
  {
    transaction.cancel();
    return false;
  }

  deselectAll(map);
  selectNodes(map, collectSelectableNodes(addedNodes, map.editorContext()));
  transaction.commit();

  return true;
}

bool pasteBrushFaces(Map& map, const std::vector<BrushFace>& faces)
{
  contract_pre(!faces.empty());

  const auto update = copyAllExceptContentFlags(faces.back().attributes());
  return setBrushFaceAttributes(map, update);
}

// Collect every entity node in the given (possibly nested) node set, descending into groups.
std::vector<EntityNode*> collectCopyEntities(const std::vector<Node*>& nodes)
{
  auto result = std::vector<EntityNode*>{};
  for (auto* node : nodes)
  {
    node->accept(kdl::overload(
      [](WorldNode&) {},
      [](LayerNode&) {},
      [&](auto&& thisLambda, GroupNode& groupNode) {
        groupNode.visitChildren(thisLambda);
      },
      [&](EntityNode& entityNode) { result.push_back(&entityNode); },
      [](BrushNode&) {},
      [](PatchNode&) {}));
  }
  return result;
}

// All non-empty targetname values currently present anywhere in the map.
std::unordered_set<std::string> collectTargetnames(const WorldNode& world)
{
  auto names = std::unordered_set<std::string>{};
  world.accept(kdl::overload(
    [&](auto&& thisLambda, const WorldNode& worldNode) {
      worldNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, const LayerNode& layerNode) {
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, const GroupNode& groupNode) {
      groupNode.visitChildren(thisLambda);
    },
    [&](const EntityNode& entityNode) {
      if (const auto* tn = entityNode.entity().property(EntityPropertyKeys::Targetname))
      {
        if (!tn->empty())
        {
          names.insert(*tn);
        }
      }
    },
    [](const BrushNode&) {},
    [](const PatchNode&) {}));
  return names;
}

// Return base if free, else base with its trailing integer incremented until unused
// ("door"->"door1", "door01"->"door02", preserving zero-padding width).
std::string makeUniqueName(
  const std::string& base, const std::unordered_set<std::string>& used)
{
  if (!used.contains(base))
  {
    return base;
  }

  auto i = base.size();
  while (i > 0 && std::isdigit(static_cast<unsigned char>(base[i - 1])))
  {
    --i;
  }
  const auto stem = base.substr(0, i);
  const auto digits = base.substr(i);
  const auto width = digits.size();

  auto next = std::size_t{1};
  if (!digits.empty() && digits.size() <= 9)
  {
    next = static_cast<std::size_t>(std::stoul(digits)) + 1;
  }

  for (;; ++next)
  {
    auto num = std::to_string(next);
    if (num.size() < width)
    {
      num = std::string(width - num.size(), '0') + num;
    }
    auto candidate = stem + num;
    if (!used.contains(candidate))
    {
      return candidate;
    }
  }
}

// Give each pasted entity's targetname a fresh (optionally prefixed, optionally uniqued)
// value and rewrite intra-copy link-source references (target/killtarget/...) to match.
void uniquifyPastedEntityNames(
  Map& map,
  const std::vector<Node*>& copyNodes,
  const bool makeUnique,
  const std::string& prefix)
{
  const auto entities = collectCopyEntities(copyNodes);
  if (entities.empty())
  {
    return;
  }

  // Names taken elsewhere in the map; exclude this copy's own current names so a name that
  // does not collide externally can be kept as-is.
  auto used = collectTargetnames(map.worldNode());
  for (auto* entityNode : entities)
  {
    if (const auto* tn = entityNode->entity().property(EntityPropertyKeys::Targetname))
    {
      used.erase(*tn);
    }
  }

  auto renameMap = std::unordered_map<std::string, std::string>{};
  for (auto* entityNode : entities)
  {
    const auto* tn = entityNode->entity().property(EntityPropertyKeys::Targetname);
    if (!tn || tn->empty() || renameMap.contains(*tn))
    {
      continue;
    }
    auto base = prefix.empty() ? *tn : prefix + *tn;
    auto newName = makeUnique ? makeUniqueName(base, used) : base;
    used.insert(newName);
    renameMap.emplace(*tn, std::move(newName));
  }
  if (renameMap.empty())
  {
    return;
  }

  const auto isReference = [](const Entity& entity, const std::string& key) {
    return key == EntityPropertyKeys::Target || key == EntityPropertyKeys::Killtarget
           || isLinkSourceProperty(entity.definition(), key);
  };

  applyAndSwap(
    map,
    "Uniquify Entity Names",
    entities,
    collectContainingGroups(kdl::vec_static_cast<Node*>(entities)),
    kdl::overload(
      [](Layer&) { return true; },
      [](Group&) { return true; },
      [&](Entity& entity) {
        if (const auto* tn = entity.property(EntityPropertyKeys::Targetname))
        {
          if (const auto it = renameMap.find(*tn); it != renameMap.end())
          {
            entity.addOrUpdateProperty(EntityPropertyKeys::Targetname, it->second);
          }
        }
        for (const auto& key : entity.propertyKeys())
        {
          if (!isReference(entity, key))
          {
            continue;
          }
          if (const auto* value = entity.property(key))
          {
            if (const auto it = renameMap.find(*value); it != renameMap.end())
            {
              entity.addOrUpdateProperty(key, it->second);
            }
          }
        }
        return true;
      },
      [](Brush&) { return true; },
      [](BezierPatch&) { return true; }));
}

} // namespace

std::string serializeSelectedNodes(Map& map)
{
  auto stream = std::stringstream{};
  auto writer = NodeWriter{map.worldNode(), stream};
  writer.writeNodes(map.selection().nodes, map.taskManager());
  return stream.str();
}

std::string serializeSelectedBrushFaces(Map& map)
{
  auto stream = std::stringstream{};
  auto writer = NodeWriter{map.worldNode(), stream};
  writer.writeBrushFaces(
    map.selection().brushFaces | std::views::transform([](const auto& h) {
      return h.face();
    }) | kdl::ranges::to<std::vector>(),
    map.taskManager());
  return stream.str();
}

PasteType paste(Map& map, const std::string& str)
{
  auto parserStatus = SimpleParserStatus{map.logger()};

  // Try parsing as entities, then as brushes, in all compatible formats
  return NodeReader::read(
           str,
           map.worldNode().mapFormat(),
           map.worldBounds(),
           map.worldNode().entityPropertyConfig(),
           parserStatus,
           map.taskManager())
         | kdl::transform([&](auto nodes) {
             return pasteNodes(map, nodes) ? PasteType::Node : PasteType::Failed;
           })
         | kdl::or_else([&](const auto& nodeError) {
             // Try parsing as brush faces
             auto reader = BrushFaceReader{str, map.worldNode().mapFormat()};
             return reader.read(map.worldBounds(), parserStatus)
                    | kdl::transform([&](const auto& faces) {
                        return !faces.empty() && pasteBrushFaces(map, faces)
                                 ? PasteType::BrushFace
                                 : PasteType::Failed;
                      })
                    | kdl::transform_error([&](const auto& faceError) {
                        map.logger().error()
                          << "Could not parse clipboard contents as nodes: "
                          << nodeError.msg;
                        map.logger().error()
                          << "Could not parse clipboard contents as faces: "
                          << faceError.msg;
                        return PasteType::Failed;
                      });
           })
         | kdl::value();
}

PasteType pasteSpecial(
  Map& map, const std::string& str, const PasteSpecialOptions& options)
{
  // Captured before the first paste (which deselects the source): the rotation pivot.
  const auto sourceBounds = map.selectionBounds();
  const auto numCopies = std::max(1, options.numCopies);
  const auto applyTransform =
    options.offset != vm::vec3d{0, 0, 0} || options.rotation != vm::vec3d{0, 0, 0};

  auto transaction = Transaction{map, "Paste Special"};

  auto allCopyNodes = std::vector<Node*>{};
  for (int k = 1; k <= numCopies; ++k)
  {
    if (paste(map, str) != PasteType::Node)
    {
      if (k == 1)
      {
        transaction.cancel();
        return PasteType::Failed;
      }
      break; // clipboard is node data but a later copy failed to add; stop here
    }

    // Copy the freshly-pasted (and now selected) top-level nodes before the next paste
    // deselects them.
    const auto copyNodes = map.selection().nodes;

    if (applyTransform)
    {
      const auto kf = double(k);
      const auto pivot = sourceBounds ? (options.startAtCenter ? sourceBounds->center()
                                                               : sourceBounds->min)
                                      : vm::vec3d{0, 0, 0};
      const auto rotation =
        vm::rotation_matrix(vm::vec3d{0, 0, 1}, vm::to_radians(options.rotation.z() * kf))
        * vm::rotation_matrix(
          vm::vec3d{0, 1, 0}, vm::to_radians(options.rotation.y() * kf))
        * vm::rotation_matrix(
          vm::vec3d{1, 0, 0}, vm::to_radians(options.rotation.x() * kf));
      const auto transform = vm::translation_matrix(options.offset * kf)
                             * vm::translation_matrix(pivot) * rotation
                             * vm::translation_matrix(-pivot);
      transformSelection(map, "Paste Special", transform);
    }

    if (options.makeNamesUnique || !options.namePrefix.empty())
    {
      uniquifyPastedEntityNames(
        map, copyNodes, options.makeNamesUnique, options.namePrefix);
    }

    allCopyNodes.insert(allCopyNodes.end(), copyNodes.begin(), copyNodes.end());
  }

  if (allCopyNodes.empty())
  {
    transaction.cancel();
    return PasteType::Failed;
  }

  deselectAll(map);
  selectNodes(map, allCopyNodes);
  if (options.groupCopies)
  {
    groupSelectedNodes(map, "Paste Special");
  }

  transaction.commit();
  return PasteType::Node;
}

} // namespace tb::mdl
