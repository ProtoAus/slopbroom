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

#include "ui/BrushBuilderUtils.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"

#include "vm/vec.h"

namespace tb::ui
{

std::optional<vm::vec2f> defaultTextureScaleOverride()
{
  if (const auto scale = pref(Preferences::DefaultTextureScale); scale > 0.0f)
  {
    return vm::vec2f{scale, scale};
  }
  return std::nullopt;
}

mdl::BrushFaceAttributes defaultBrushFaceAttributes(const mdl::Map& map)
{
  auto attribs = map.gameInfo().gameConfig.faceAttribsConfig.defaults;
  if (const auto scale = defaultTextureScaleOverride())
  {
    attribs.setScale(*scale);
  }
  return attribs;
}

} // namespace tb::ui
