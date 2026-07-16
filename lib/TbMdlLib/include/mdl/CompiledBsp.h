/*
 Copyright (C) 2026 Kristian Duske

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

#include "Result.h"

#include "vm/vec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tb::mdl
{

/**
 * Render-ready data extracted from a compiled Quake .bsp for the in-editor lit
 * preview: the map's faces triangulated and grouped by texture name, with texture
 * coordinates in TEXEL space (the renderer divides by the resolved material's size,
 * so -notex stub sizes don't matter) and lightmap coordinates into a packed atlas.
 */
struct CompiledBspVertex
{
  vm::vec3f position;
  vm::vec2f uvTexels;
  vm::vec2f lmUV;
};

struct CompiledBspBatch
{
  std::string materialName;
  size_t atlasPage;
  std::vector<CompiledBspVertex> vertices; // triangle list
};

struct CompiledBspAtlasPage
{
  size_t width;
  size_t height;
  std::vector<unsigned char> rgba;
};

struct CompiledBsp
{
  std::vector<CompiledBspBatch> batches;
  std::vector<CompiledBspAtlasPage> atlasPages;
};

/**
 * Loads a compiled bsp29 file with its baked lighting. Lighting is taken from the
 * BSPX RGBLIGHTING lump when present (the -novanilla pipeline), else the classic
 * mono lump 8. Lightmap layout comes from BSPX DECOUPLED_LM when present (world-unit
 * luxels), else the classic texinfo-derived 1/16 layout.
 */
Result<CompiledBsp> loadCompiledBsp(const std::filesystem::path& path);

} // namespace tb::mdl
