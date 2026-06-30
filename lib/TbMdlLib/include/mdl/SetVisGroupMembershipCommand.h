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

#pragma once

#include "Macros.h"
#include "mdl/IdType.h"
#include "mdl/UndoableCommand.h"

#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace tb::mdl
{
class Node;

/**
 * Undoable change to which VisGroups a set of nodes belong to. Mirrors
 * SetVisibilityCommand: the target membership per node is supplied; doPerformDo captures
 * the prior membership and applies the new, doPerformUndo restores it. Fires
 * editorContextDidChange (re-render with the new visibility) + visGroupsDidChange (refresh
 * the panel's object counts) on both do and undo.
 */
class SetVisGroupMembershipCommand : public UndoableCommand
{
private:
  std::vector<std::tuple<Node*, std::set<IdType>>> m_newState;
  std::vector<std::tuple<Node*, std::set<IdType>>> m_oldState;

public:
  SetVisGroupMembershipCommand(
    std::string name, std::vector<std::tuple<Node*, std::set<IdType>>> newState);

private:
  bool doPerformDo(Map& map) override;
  bool doPerformUndo(Map& map) override;

  deleteCopyAndMove(SetVisGroupMembershipCommand);
};

} // namespace tb::mdl
