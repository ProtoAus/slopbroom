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

#include "mdl/SetVisGroupMembershipCommand.h"

#include "mdl/Map.h"
#include "mdl/VisGroupManager.h"

namespace tb::mdl
{

SetVisGroupMembershipCommand::SetVisGroupMembershipCommand(
  std::string name, std::vector<std::tuple<Node*, std::set<IdType>>> newState)
  : UndoableCommand{std::move(name), true}
  , m_newState{std::move(newState)}
{
}

bool SetVisGroupMembershipCommand::doPerformDo(Map& map)
{
  auto& manager = map.visGroupManager();
  m_oldState.clear();
  m_oldState.reserve(m_newState.size());

  for (const auto& [node, ids] : m_newState)
  {
    m_oldState.emplace_back(node, manager.membership(node));
    manager.setMembership(node, ids);
  }

  map.editorContextDidChangeNotifier();
  map.visGroupsDidChangeNotifier();
  return true;
}

bool SetVisGroupMembershipCommand::doPerformUndo(Map& map)
{
  auto& manager = map.visGroupManager();
  for (const auto& [node, ids] : m_oldState)
  {
    manager.setMembership(node, ids);
  }

  map.editorContextDidChangeNotifier();
  map.visGroupsDidChangeNotifier();
  return true;
}

} // namespace tb::mdl
