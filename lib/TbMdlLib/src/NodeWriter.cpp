/*
 Copyright (C) 2010 Kristian Duske

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

#include "mdl/NodeWriter.h"

#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/MapFileSerializer.h"
#include "mdl/Node.h"
#include "mdl/NodeSerializer.h"
#include "mdl/PatchNode.h"
#include "mdl/VisGroupManager.h"
#include "mdl/WorldNode.h"

#include "kd/contracts.h"
#include "kd/overload.h"
#include "kd/ranges/to.h"
#include "kd/string_format.h"
#include "kd/string_utils.h"
#include "kd/vector_utils.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tb::mdl
{
namespace
{

std::string joinVisGroupIds(const std::set<IdType>& ids)
{
  auto strs = std::vector<std::string>{};
  strs.reserve(ids.size());
  for (const auto id : ids)
  {
    strs.push_back(kdl::str_to_string(id));
  }
  return kdl::str_join(strs, " ");
}

// Append the inline "_tb_visgroups" membership property to an entity/group's properties.
void appendVisGroups(
  std::vector<EntityProperty>& properties,
  const Node& node,
  const VisGroupManager* visGroups)
{
  if (visGroups)
  {
    const auto ids = visGroups->membership(&node);
    if (!ids.empty())
    {
      properties.emplace_back(EntityPropertyKeys::TbVisGroups, joinVisGroupIds(ids));
    }
  }
}

// Build the worldspawn VisGroup properties: one "_tb_visgroup_def_<id>" per group plus the
// raw-brush membership table "_tb_visgroup_brushes". Entities/groups carry their membership
// inline (see appendVisGroups), so only raw brushes/patches go in the table. A brush whose
// parent is a brush-entity has no stable address and is dropped (documented limitation).
std::vector<EntityProperty> precomputeWorldVisGroupProperties(
  const WorldNode& world, const VisGroupManager& visGroups)
{
  auto properties = std::vector<EntityProperty>{};

  for (const auto& group : visGroups.groups())
  {
    // "<visible> <rrggbb|none> <name>" — colour is a single token so the name can stay free
    // trailing text; "none" marks a group with no assigned colour.
    const auto colorStr = group.color ? colorToHex(*group.color) : std::string{"none"};
    auto value =
      std::string{group.visible ? "1" : "0"} + " " + colorStr + " " + group.name;
    properties.emplace_back(
      EntityPropertyKeys::TbVisGroupDefPrefix + kdl::str_to_string(group.id),
      std::move(value));
  }

  auto brushEntries = std::vector<std::string>{};
  auto groupEntries = std::vector<std::string>{};
  for (const auto& [node, ids] : visGroups.allMemberships())
  {
    // A GroupNode drops arbitrary props on read, so it can't be inline → its own table,
    // keyed by persistentId (no ordinal — the id is already unique).
    if (const auto* groupNode = dynamic_cast<const GroupNode*>(node))
    {
      if (const auto pid = groupNode->persistentId())
      {
        groupEntries.push_back(kdl::str_to_string(*pid) + "=" + joinVisGroupIds(ids));
      }
      continue;
    }

    const auto isGeometry = dynamic_cast<const BrushNode*>(node) != nullptr
                            || dynamic_cast<const PatchNode*>(node) != nullptr;
    if (!isGeometry)
    {
      continue; // entities carry inline _tb_visgroups (see appendVisGroups)
    }

    const auto* parent = node->parent();
    auto cid = std::optional<IdType>{};
    if (const auto* layer = dynamic_cast<const LayerNode*>(parent))
    {
      cid = (layer == world.defaultLayer()) ? IdType{0} : layer->persistentId().value_or(0);
    }
    else if (const auto* groupNode = dynamic_cast<const GroupNode*>(parent))
    {
      cid = groupNode->persistentId();
    }
    if (!cid)
    {
      continue; // brush is a child of a brush-entity → not addressable, drop
    }

    auto ord = size_t{0};
    for (const auto* sibling : parent->children())
    {
      if (sibling == node)
      {
        break;
      }
      if (dynamic_cast<const BrushNode*>(sibling) != nullptr
          || dynamic_cast<const PatchNode*>(sibling) != nullptr)
      {
        ++ord;
      }
    }

    brushEntries.push_back(
      kdl::str_to_string(*cid) + "/" + kdl::str_to_string(ord) + "="
      + joinVisGroupIds(ids));
  }
  if (!brushEntries.empty())
  {
    properties.emplace_back(
      EntityPropertyKeys::TbVisGroupBrushes, kdl::str_join(brushEntries, ";"));
  }
  if (!groupEntries.empty())
  {
    properties.emplace_back(
      EntityPropertyKeys::TbVisGroupGroups, kdl::str_join(groupEntries, ";"));
  }

  // Pseudo-VisGroup (per-group) hidden state: the persistentIds of GroupNodes toggled off.
  auto pseudoHiddenIds = std::vector<std::string>{};
  for (const auto* node : visGroups.hiddenPseudoGroups())
  {
    if (const auto* groupNode = dynamic_cast<const GroupNode*>(node))
    {
      if (const auto pid = groupNode->persistentId())
      {
        pseudoHiddenIds.push_back(kdl::str_to_string(*pid));
      }
    }
  }
  if (!pseudoHiddenIds.empty())
  {
    properties.emplace_back(
      EntityPropertyKeys::TbVisGroupGroupsHidden, kdl::str_join(pseudoHiddenIds, ";"));
  }

  return properties;
}

void doWriteNodes(
  NodeSerializer& serializer,
  const std::vector<Node*>& nodes,
  const VisGroupManager* visGroups,
  const Node* parent = nullptr)
{
  auto parentStack = std::vector<const Node*>{parent};
  const auto parentProperties = [&]() {
    contract_pre(!parentStack.empty());

    return serializer.parentProperties(parentStack.back());
  };

  for (const auto* node : nodes)
  {
    node->accept(kdl::overload(
      [](const WorldNode&) {},
      [](const LayerNode&) {},
      [&](auto&& thisLambda, const GroupNode& groupNode) {
        auto properties = parentProperties();
        appendVisGroups(properties, groupNode, visGroups);
        serializer.group(groupNode, properties);

        parentStack.push_back(&groupNode);
        groupNode.visitChildren(thisLambda);
        parentStack.pop_back();
      },
      [&](const EntityNode& entityNode) {
        auto extraProperties = parentProperties();
        const auto& protectedProperties = entityNode.entity().protectedProperties();
        if (!protectedProperties.empty())
        {
          const auto escapedProperties = protectedProperties
                                         | std::views::transform([](const auto& key) {
                                             return kdl::str_escape(key, ";");
                                           })
                                         | kdl::ranges::to<std::vector>();
          extraProperties.emplace_back(
            EntityPropertyKeys::TbProtectedEntityProperties,
            kdl::str_join(escapedProperties, ";"));
        }
        appendVisGroups(extraProperties, entityNode, visGroups);
        serializer.entity(
          entityNode, entityNode.entity().properties(), extraProperties, entityNode);
      },
      [](const BrushNode&) {},
      [](const PatchNode&) {}));
  }
}

} // namespace

NodeWriter::NodeWriter(const WorldNode& world, std::ostream& stream)
  : NodeWriter{world, MapFileSerializer::create(world.mapFormat(), stream)}
{
}

NodeWriter::NodeWriter(const WorldNode& world, std::unique_ptr<NodeSerializer> serializer)
  : m_world{world}
  , m_serializer{std::move(serializer)}
{
}

NodeWriter::~NodeWriter() = default;

void NodeWriter::setExporting(const bool exporting)
{
  m_serializer->setExporting(exporting);
}

void NodeWriter::setStripTbProperties(const bool stripTbProperties)
{
  m_serializer->setStripTbProperties(stripTbProperties);
}

void NodeWriter::setVisGroupManager(const VisGroupManager* visGroups)
{
  m_visGroupManager = visGroups;
}

void NodeWriter::writeMap(kdl::task_manager& taskManager)
{
  m_serializer->beginFile({&m_world}, taskManager);
  if (
    m_visGroupManager != nullptr
    && (!m_visGroupManager->groups().empty()
        || !m_visGroupManager->hiddenPseudoGroups().empty()))
  {
    m_serializer->setVisGroupWorldProperties(
      precomputeWorldVisGroupProperties(m_world, *m_visGroupManager));
  }
  writeDefaultLayer();
  writeCustomLayers();
  m_serializer->endFile();
}

void NodeWriter::writeDefaultLayer()
{
  m_serializer->defaultLayer(m_world);

  if (!(m_serializer->exporting() && m_world.defaultLayer()->layer().omitFromExport()))
  {
    doWriteNodes(*m_serializer, m_world.defaultLayer()->children(), m_visGroupManager);
  }
}

void NodeWriter::writeCustomLayers()
{
  for (auto* layerNode : m_world.customLayers())
  {
    writeCustomLayer(*layerNode);
  }
}

void NodeWriter::writeCustomLayer(const LayerNode& layerNode)
{
  if (!(m_serializer->exporting() && layerNode.layer().omitFromExport()))
  {
    m_serializer->customLayer(layerNode);
    doWriteNodes(*m_serializer, layerNode.children(), m_visGroupManager, &layerNode);
  }
}

void NodeWriter::writeNodes(
  const std::vector<Node*>& nodes, kdl::task_manager& taskManager)
{
  m_serializer->beginFile(kdl::vec_static_cast<const Node*>(nodes), taskManager);

  // Assort nodes according to their type and, in case of brushes, whether they are entity
  // or world brushes.
  std::vector<Node*> groups;
  std::vector<Node*> entities;
  std::vector<BrushNode*> worldBrushes;
  EntityBrushesMap entityBrushes;

  for (auto* node : nodes)
  {
    node->accept(kdl::overload(
      [](WorldNode&) {},
      [](LayerNode&) {},
      [&](GroupNode& groupNode) { groups.push_back(&groupNode); },
      [&](EntityNode& entityNode) { entities.push_back(&entityNode); },
      [&](BrushNode& brushNode) {
        if (auto* entityNode = dynamic_cast<EntityNode*>(brushNode.parent()))
        {
          entityBrushes[entityNode].push_back(&brushNode);
        }
        else
        {
          worldBrushes.push_back(&brushNode);
        }
      },
      [](PatchNode&) {}));
  }

  writeWorldBrushes(worldBrushes);
  writeEntityBrushes(entityBrushes);

  doWriteNodes(*m_serializer, groups, nullptr);
  doWriteNodes(*m_serializer, entities, nullptr);

  m_serializer->endFile();
}

void NodeWriter::writeWorldBrushes(const std::vector<BrushNode*>& brushes)
{
  if (!brushes.empty())
  {
    m_serializer->entity(m_world, m_world.entity().properties(), {}, brushes);
  }
}

void NodeWriter::writeEntityBrushes(const EntityBrushesMap& entityBrushes)
{
  for (const auto& [entityNode, brushes] : entityBrushes)
  {
    m_serializer->entity(*entityNode, entityNode->entity().properties(), {}, brushes);
  }
}

void NodeWriter::writeBrushFaces(
  const std::vector<BrushFace>& faces, kdl::task_manager& taskManager)
{
  m_serializer->beginFile({}, taskManager);
  m_serializer->brushFaces(faces);
  m_serializer->endFile();
}

} // namespace tb::mdl
