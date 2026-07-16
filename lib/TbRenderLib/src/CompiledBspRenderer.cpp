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

#include "render/CompiledBspRenderer.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/ActiveShader.h"
#include "gl/GlInterface.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "gl/PrimType.h"
#include "gl/Shaders.h"
#include "gl/Texture.h"
#include "gl/VertexAttributeType.h"
#include "gl/VertexType.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"

#include <algorithm>
#include <utility>

namespace tb::render
{
namespace
{

using BspVertexSpec = gl::VertexType<
  gl::VertexAttributeTypes::P3,
  gl::VertexAttributeTypes::UV02,
  gl::GLVertexAttributeUser<CompiledBspRenderer::LightmapUVAttrName, GL_FLOAT, 2, false>>;
using BspVertex = BspVertexSpec::Vertex;

} // namespace

CompiledBspRenderer::CompiledBspRenderer() = default;
CompiledBspRenderer::~CompiledBspRenderer() = default;

void CompiledBspRenderer::setData(
  mdl::CompiledBsp bsp, gl::MaterialManager& materialManager)
{
  clear();

  for (auto& page : bsp.atlasPages)
  {
    m_pages.push_back(Page{page.width, page.height, std::move(page.rgba), 0});
  }

  for (auto& batch : bsp.batches)
  {
    auto vertices = std::vector<BspVertex>{};
    vertices.reserve(batch.vertices.size());
    for (const auto& vertex : batch.vertices)
    {
      vertices.emplace_back(vertex.position, vertex.uvTexels, vertex.lmUV);
    }
    m_batches.push_back(Batch{
      materialManager.material(batch.materialName),
      batch.atlasPage,
      gl::VertexArray::move(std::move(vertices))});
  }

  m_valid = !m_batches.empty();
}

void CompiledBspRenderer::clear()
{
  for (const auto& page : m_pages)
  {
    if (page.textureId != 0)
    {
      m_texturesToDelete.push_back(page.textureId);
    }
  }
  m_pages.clear();
  m_batches.clear();
  m_valid = false;
}

bool CompiledBspRenderer::valid() const
{
  return m_valid;
}

void CompiledBspRenderer::render(RenderBatch& renderBatch)
{
  if (m_valid)
  {
    renderBatch.add(this);
  }
}

void CompiledBspRenderer::prepare(gl::Gl& gl, gl::VboManager& vboManager)
{
  if (!m_texturesToDelete.empty())
  {
    gl.deleteTextures(
      GLsizei(m_texturesToDelete.size()), m_texturesToDelete.data());
    m_texturesToDelete.clear();
  }

  for (auto& page : m_pages)
  {
    if (page.textureId == 0 && !page.rgba.empty())
    {
      gl.genTextures(1, &page.textureId);
      gl.bindTexture(GL_TEXTURE_2D, page.textureId);
      gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl.texImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        GLsizei(page.width),
        GLsizei(page.height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        page.rgba.data());
      gl.bindTexture(GL_TEXTURE_2D, 0);
      page.rgba.clear();
      page.rgba.shrink_to_fit();
    }
  }

  for (auto& batch : m_batches)
  {
    if (!batch.vertexArray.prepared())
    {
      batch.vertexArray.prepare(gl, vboManager);
    }
  }
}

void CompiledBspRenderer::render(RenderContext& context)
{
  if (!m_valid)
  {
    return;
  }

  auto& gl = context.gl();
  auto& shaderManager = context.shaderManager();
  auto shader = gl::ActiveShader{gl, shaderManager, gl::Shaders::LitBspShader};
  auto& prefs = PreferenceManager::instance();

  // The compiled BSP's face winding is not TB's brush winding — skip culling.
  gl.disable(GL_CULL_FACE);
  gl.enable(GL_TEXTURE_2D);

  shader.set("Material", 0);
  shader.set("Lightmap", 1);
  shader.set("Brightness", prefs.get(Preferences::Brightness));
  shader.set("LightmapOnly", prefs.get(Preferences::ShowLightmapOnly));

  for (auto& batch : m_batches)
  {
    gl.activeTexture(GL_TEXTURE1);
    gl.bindTexture(
      GL_TEXTURE_2D, batch.page < m_pages.size() ? m_pages[batch.page].textureId : 0);
    gl.activeTexture(GL_TEXTURE0);

    const auto* texture = gl::getTexture(batch.material);
    if (texture)
    {
      batch.material->activate(
        gl,
        context.minFilterMode(),
        context.magFilterMode(),
        context.anisotropy(),
        context.lodBias());
      shader.set("ApplyMaterial", true);
      shader.set(
        "TextureSize",
        vm::vec2f{
          float(std::max(texture->width(), size_t{1})),
          float(std::max(texture->height(), size_t{1}))});
      shader.set("EnableMasked", texture->mask() == gl::TextureMask::On);
    }
    else
    {
      shader.set("ApplyMaterial", false);
      shader.set("TextureSize", vm::vec2f{64.0f, 64.0f});
      shader.set("EnableMasked", false);
    }

    if (batch.vertexArray.setup(gl, shader.program()))
    {
      batch.vertexArray.render(gl, gl::PrimType::Triangles);
      batch.vertexArray.cleanup(gl, shader.program());
    }

    if (texture)
    {
      batch.material->deactivate(gl);
    }
  }

  gl.activeTexture(GL_TEXTURE1);
  gl.bindTexture(GL_TEXTURE_2D, 0);
  gl.activeTexture(GL_TEXTURE0);
  gl.enable(GL_CULL_FACE);
}

} // namespace tb::render
