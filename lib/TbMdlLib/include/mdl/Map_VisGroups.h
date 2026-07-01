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

#include "Color.h"
#include "mdl/IdType.h"

#include <optional>
#include <string>

namespace tb::mdl
{
class Map;
class Node;

// Free-function facade over VisGroupManager (mirrors Map_Layers). The UI calls these.
// Membership changes are undoable (SetVisGroupMembershipCommand); group create/rename/
// delete and the visibility toggle are direct manager mutations + notifiers (the toggle is
// view state, like setHiddenTags; create/rename/delete become undoable in a later phase).

IdType createVisGroup(Map& map, std::string name);
void deleteVisGroup(Map& map, IdType id);
void renameVisGroup(Map& map, IdType id, std::string name);
void setVisGroupVisible(Map& map, IdType id, bool visible);
void setVisGroupColor(Map& map, IdType id, std::optional<Color> color);

// Pseudo-VisGroup (per-GroupNode) visibility toggle — view state, non-undoable, like
// setVisGroupVisible.
void setPseudoGroupVisible(Map& map, Node* groupNode, bool visible);

void addSelectedToVisGroup(Map& map, IdType id);
void removeSelectedFromVisGroup(Map& map, IdType id);
void selectVisGroupMembers(Map& map, IdType id);

} // namespace tb::mdl
