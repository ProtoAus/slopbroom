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

#include "gl/VertexArray.h"
#include "mdl/CompiledBsp.h"
#include "render/Renderable.h"

#include <string>
#include <vector>

namespace tb::gl
{
class Material;
class MaterialManager;
} // namespace tb::gl

namespace tb::render
{
class RenderBatch;

/**
 * Renders a compiled .bsp with its baked lightmaps (texture x lightmap x 2, matching
 * FTE's overbright) as the "lit preview" of the current map. The geometry is the
 * compiled snapshot, not the live brushes; MapRenderer suppresses the normal opaque
 * world faces while this is shown.
 */
class CompiledBspRenderer : public DirectRenderable
{
public:
  struct LightmapUVAttrName
  {
    static inline const std::string name{"LightmapUV"};
  };

private:
  struct Batch
  {
    const gl::Material* material;
    size_t page;
    gl::VertexArray vertexArray;
  };

  struct Page
  {
    size_t width = 0;
    size_t height = 0;
    std::vector<unsigned char> rgba;
    unsigned int textureId = 0;
  };

  std::vector<Batch> m_batches;
  std::vector<Page> m_pages;
  // GL texture ids from a previous load, deleted at the next prepare (the GL
  // interface is only available during rendering)
  std::vector<unsigned int> m_texturesToDelete;
  bool m_valid = false;

public:
  CompiledBspRenderer();
  ~CompiledBspRenderer() override;

  /** Replaces the rendered data; materials are resolved by miptex name. */
  void setData(mdl::CompiledBsp bsp, gl::MaterialManager& materialManager);
  void clear();
  bool valid() const;

  void render(RenderBatch& renderBatch);

private:
  void prepare(gl::Gl& gl, gl::VboManager& vboManager) override;
  void render(RenderContext& context) override;
};

} // namespace tb::render
