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

#include "mdl/VisGroupCommand.h"

#include "mdl/Map.h"
#include "mdl/VisGroupManager.h"

namespace tb::mdl
{
namespace
{
void applyState(
  Map& map,
  const std::vector<VisGroup>& groups,
  const std::vector<std::tuple<const Node*, std::set<IdType>>>& membership)
{
  auto& manager = map.visGroupManager();
  manager.clear();
  for (const auto& group : groups)
  {
    manager.addGroup(group);
  }
  for (const auto& [node, ids] : membership)
  {
    manager.setMembership(node, ids);
  }

  map.editorContextDidChangeNotifier();
  map.visGroupsDidChangeNotifier();
}
} // namespace

VisGroupCommand::VisGroupCommand(
  std::string name,
  std::vector<VisGroup> groupsBefore,
  std::vector<std::tuple<const Node*, std::set<IdType>>> membershipBefore,
  std::vector<VisGroup> groupsAfter,
  std::vector<std::tuple<const Node*, std::set<IdType>>> membershipAfter)
  : UndoableCommand{std::move(name), true}
  , m_groupsBefore{std::move(groupsBefore)}
  , m_groupsAfter{std::move(groupsAfter)}
  , m_membershipBefore{std::move(membershipBefore)}
  , m_membershipAfter{std::move(membershipAfter)}
{
}

bool VisGroupCommand::doPerformDo(Map& map)
{
  applyState(map, m_groupsAfter, m_membershipAfter);
  return true;
}

bool VisGroupCommand::doPerformUndo(Map& map)
{
  applyState(map, m_groupsBefore, m_membershipBefore);
  return true;
}

} // namespace tb::mdl
