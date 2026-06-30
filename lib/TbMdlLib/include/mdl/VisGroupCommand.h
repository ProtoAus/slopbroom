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
#include "mdl/VisGroup.h"

#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace tb::mdl
{
class Node;

/**
 * Undoable create / rename / delete of a VisGroup. The whole group LIST and node membership
 * are snapshotted before and after the change; doPerformDo restores the "after" snapshot,
 * doPerformUndo the "before" one. Membership is included because deleting a group purges its
 * id from every member node, and undo must bring that back. Fires editorContextDidChange (so
 * the renderer re-tints/re-hides) + visGroupsDidChange (so the panel refreshes).
 */
class VisGroupCommand : public UndoableCommand
{
private:
  std::vector<VisGroup> m_groupsBefore;
  std::vector<VisGroup> m_groupsAfter;
  std::vector<std::tuple<const Node*, std::set<IdType>>> m_membershipBefore;
  std::vector<std::tuple<const Node*, std::set<IdType>>> m_membershipAfter;

public:
  VisGroupCommand(
    std::string name,
    std::vector<VisGroup> groupsBefore,
    std::vector<std::tuple<const Node*, std::set<IdType>>> membershipBefore,
    std::vector<VisGroup> groupsAfter,
    std::vector<std::tuple<const Node*, std::set<IdType>>> membershipAfter);

private:
  bool doPerformDo(Map& map) override;
  bool doPerformUndo(Map& map) override;

  deleteCopyAndMove(VisGroupCommand);
};

} // namespace tb::mdl
