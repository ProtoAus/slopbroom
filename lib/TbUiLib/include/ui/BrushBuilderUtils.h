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

#include "vm/vec.h"

#include <optional>

namespace tb::mdl
{
class BrushFaceAttributes;
class Map;
} // namespace tb::mdl

namespace tb::ui
{

/**
 * The user's default texture-scale override as {s, s}, or nullopt when the
 * `Preferences::DefaultTextureScale` preference is at its sentinel value 0 (meaning
 * "use the game's configured default"). This is the single place the sentinel is
 * interpreted.
 */
std::optional<vm::vec2f> defaultTextureScaleOverride();

/**
 * The default brush face attributes used to seed newly created (and reset) faces.
 *
 * This is the game's configured `faceAttribsConfig.defaults` with the texture scale
 * overridden by `defaultTextureScaleOverride()` when the user preference is set. A
 * value of 0 (the preference default) means "use the game default unchanged", so
 * existing behavior is preserved out of the box.
 */
mdl::BrushFaceAttributes defaultBrushFaceAttributes(const mdl::Map& map);

} // namespace tb::ui
