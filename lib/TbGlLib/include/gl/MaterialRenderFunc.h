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

namespace tb::gl
{
class Gl;
class Material;
class ShaderProgram;

class MaterialRenderFunc
{
public:
  virtual ~MaterialRenderFunc();
  virtual void before(Gl& gl, const Material* material);
  virtual void after(Gl& gl, const Material* material);
};

class DefaultMaterialRenderFunc : public MaterialRenderFunc
{
private:
  int m_minFilter;
  int m_magFilter;

public:
  DefaultMaterialRenderFunc(int minFilter, int magFilter);

  void before(Gl& gl, const Material* material) override;
  void after(Gl& gl, const Material* material) override;
};

/**
 * Material render func for the entity-model shader. Per material it drives the shader's
 * `EnableMasked` uniform and the GL blend / depth-write state so translucent materials (image
 * sprites) blend smoothly instead of being hard alpha-tested. The per-entity `Blend` override
 * lets a caller force a mode for HL/Source env_sprite rendermodes; FromMaterial honours the
 * material's own blend func (translucent if it set one, masked/opaque otherwise).
 */
class ModelMaterialRenderFunc : public DefaultMaterialRenderFunc
{
public:
  enum class Blend
  {
    FromMaterial,
    Masked,
    Alpha,
    Additive,
  };

private:
  ShaderProgram& m_shader;
  Blend m_blend;
  bool m_translucent = false;

public:
  ModelMaterialRenderFunc(
    int minFilter, int magFilter, ShaderProgram& shader, Blend blend = Blend::FromMaterial);

  void before(Gl& gl, const Material* material) override;
  void after(Gl& gl, const Material* material) override;
};

} // namespace tb::gl
