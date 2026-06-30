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

#include "gl/MaterialRenderFunc.h"

#include "gl/GlInterface.h"
#include "gl/Material.h"
#include "gl/ShaderProgram.h"
#include "gl/Texture.h"

namespace tb::gl
{

MaterialRenderFunc::~MaterialRenderFunc() = default;
void MaterialRenderFunc::before(Gl&, const Material*) {}
void MaterialRenderFunc::after(Gl&, const Material*) {}

DefaultMaterialRenderFunc::DefaultMaterialRenderFunc(
  const int minFilter, const int magFilter)
  : m_minFilter{minFilter}
  , m_magFilter{magFilter}
{
}

void DefaultMaterialRenderFunc::before(Gl& gl, const Material* material)
{
  if (material)
  {
    material->activate(gl, m_minFilter, m_magFilter);
  }
}

void DefaultMaterialRenderFunc::after(Gl& gl, const Material* material)
{
  if (material)
  {
    material->deactivate(gl);
  }
}

ModelMaterialRenderFunc::ModelMaterialRenderFunc(
  const int minFilter, const int magFilter, ShaderProgram& shader, const Blend blend)
  : DefaultMaterialRenderFunc{minFilter, magFilter}
  , m_shader{shader}
  , m_blend{blend}
{
}

void ModelMaterialRenderFunc::before(Gl& gl, const Material* material)
{
  // Activates the material (which, for a material with its own blend func, pushes
  // GL_COLOR_BUFFER_BIT and sets glBlendFunc).
  DefaultMaterialRenderFunc::before(gl, material);

  switch (m_blend)
  {
  case Blend::FromMaterial:
    m_translucent =
      material && material->blendFunc().enable == MaterialBlendFunc::Enable::UseFactors;
    break;
  case Blend::Masked:
    m_translucent = false;
    break;
  case Blend::Alpha:
  case Blend::Additive:
    m_translucent = true;
    break;
  }

  m_shader.set(gl, "EnableMasked", !m_translucent);

  if (m_translucent)
  {
    // Save blend + depth-write state ourselves: Material::activate only push/pops color state
    // when the material has its own blend func, and never touches depth-write. Nested LIFO
    // with the material's own push (popped in after()).
    gl.pushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.enable(GL_BLEND);
    if (m_blend == Blend::Alpha)
    {
      gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else if (m_blend == Blend::Additive)
    {
      gl.blendFunc(GL_SRC_ALPHA, GL_ONE);
    }
    // FromMaterial: keep the factors Material::activate already set.
    gl.depthMask(GL_FALSE);
  }
}

void ModelMaterialRenderFunc::after(Gl& gl, const Material* material)
{
  if (m_translucent)
  {
    gl.popAttrib();
  }
  DefaultMaterialRenderFunc::after(gl, material);
}

} // namespace tb::gl
