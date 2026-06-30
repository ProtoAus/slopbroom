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
#include <string_view>

namespace tb::mdl
{

/**
 * A Hammer-style VisGroup: a named, manually-assigned visibility group. An object may
 * belong to several VisGroups at once (multi-membership) and is hidden if ANY of its
 * groups is not visible. The group's `visible` flag is toggled from the VisGroups panel
 * and persisted with the map (like a layer's hidden flag).
 */
struct VisGroup
{
  IdType id = 0;
  std::string name;
  std::optional<Color> color;
  bool visible = true;
};

/** Serialize a color as a 6-digit "rrggbb" hex string (the single-token form used in the
 * persisted `_tb_visgroup_def_<id>` value). */
std::string colorToHex(const Color& color);

/** Parse a 6-digit "rrggbb" hex string into a color; std::nullopt if malformed. */
std::optional<Color> colorFromHex(std::string_view hex);

} // namespace tb::mdl
