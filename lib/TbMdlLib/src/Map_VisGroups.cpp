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

#include "mdl/Map_VisGroups.h"

#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Selection.h"
#include "mdl/SetVisGroupMembershipCommand.h"
#include "mdl/VisGroup.h"
#include "mdl/VisGroupCommand.h"
#include "mdl/VisGroupManager.h"

#include <memory>
#include <set>
#include <tuple>
#include <vector>

namespace tb::mdl
{

namespace
{
std::vector<std::tuple<const Node*, std::set<IdType>>> captureMembership(
  const VisGroupManager& manager)
{
  auto result = std::vector<std::tuple<const Node*, std::set<IdType>>>{};
  for (const auto& [node, ids] : manager.allMemberships())
  {
    result.emplace_back(node, ids);
  }
  return result;
}
} // namespace

// Create/rename/delete are undoable via VisGroupCommand (it snapshots the group list +
// membership before/after, so the change rides the Ctrl-Z chain). The eye toggle + color are
// direct (view state, not undoable) and bump the modification count so a save captures them.

IdType createVisGroup(Map& map, std::string name)
{
  auto& manager = map.visGroupManager();
  auto groupsBefore = manager.groups();
  auto membershipBefore = captureMembership(manager);
  const auto id = manager.createGroup(std::move(name));
  map.executeAndStore(std::make_unique<VisGroupCommand>(
    "Create VisGroup",
    std::move(groupsBefore),
    std::move(membershipBefore),
    manager.groups(),
    captureMembership(manager)));
  return id;
}

void deleteVisGroup(Map& map, const IdType id)
{
  auto& manager = map.visGroupManager();
  auto groupsBefore = manager.groups();
  auto membershipBefore = captureMembership(manager);
  manager.deleteGroup(id);
  map.executeAndStore(std::make_unique<VisGroupCommand>(
    "Delete VisGroup",
    std::move(groupsBefore),
    std::move(membershipBefore),
    manager.groups(),
    captureMembership(manager)));
}

void renameVisGroup(Map& map, const IdType id, std::string name)
{
  auto& manager = map.visGroupManager();
  auto groupsBefore = manager.groups();
  auto membershipBefore = captureMembership(manager);
  manager.renameGroup(id, std::move(name));
  map.executeAndStore(std::make_unique<VisGroupCommand>(
    "Rename VisGroup",
    std::move(groupsBefore),
    std::move(membershipBefore),
    manager.groups(),
    captureMembership(manager)));
}

void setVisGroupVisible(Map& map, const IdType id, const bool visible)
{
  map.visGroupManager().setGroupVisible(id, visible);
  map.incModificationCount();
  // Only the renderer needs to know — the group LIST is unchanged, so the panel must not
  // reload here (it would re-enter the QListWidget item-changed signal that triggered us).
  map.editorContextDidChangeNotifier();
}

void setVisGroupColor(Map& map, const IdType id, std::optional<Color> color)
{
  map.visGroupManager().setGroupColor(id, std::move(color));
  map.incModificationCount();
  map.visGroupsDidChangeNotifier();      // refresh the swatch in the panel
  map.editorContextDidChangeNotifier();  // re-validate the renderer so the tint updates live
}

void addSelectedToVisGroup(Map& map, const IdType id)
{
  const auto& nodes = map.selection().nodes;
  if (nodes.empty() || !map.visGroupManager().hasGroup(id))
  {
    return;
  }

  auto newState = std::vector<std::tuple<Node*, std::set<IdType>>>{};
  newState.reserve(nodes.size());
  for (auto* node : nodes)
  {
    auto ids = map.visGroupManager().membership(node);
    ids.insert(id);
    newState.emplace_back(node, std::move(ids));
  }

  map.executeAndStore(std::make_unique<SetVisGroupMembershipCommand>(
    "Add to VisGroup", std::move(newState)));
}

void removeSelectedFromVisGroup(Map& map, const IdType id)
{
  const auto& nodes = map.selection().nodes;
  if (nodes.empty())
  {
    return;
  }

  auto newState = std::vector<std::tuple<Node*, std::set<IdType>>>{};
  newState.reserve(nodes.size());
  for (auto* node : nodes)
  {
    auto ids = map.visGroupManager().membership(node);
    ids.erase(id);
    newState.emplace_back(node, std::move(ids));
  }

  map.executeAndStore(std::make_unique<SetVisGroupMembershipCommand>(
    "Remove from VisGroup", std::move(newState)));
}

void selectVisGroupMembers(Map& map, const IdType id)
{
  const auto members = map.visGroupManager().members(id);

  auto nodes = std::vector<Node*>{};
  nodes.reserve(members.size());
  for (const auto* node : members)
  {
    nodes.push_back(const_cast<Node*>(node));
  }

  deselectAll(map);
  selectNodes(map, nodes);
}

} // namespace tb::mdl
