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

#include "render/EntityModelRenderer.h"

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/ActiveShader.h"
#include "gl/Camera.h"
#include "gl/GlInterface.h"
#include "gl/MaterialIndexRangeRenderer.h"
#include "gl/MaterialRenderFunc.h"
#include "gl/Shaders.h"
#include "mdl/AssetUtils.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/Transformation.h"

#include "kd/result.h"

#include "vm/mat.h"

#include <algorithm>
#include <string>
#include <vector>

namespace tb::render
{
namespace
{

int intProperty(const mdl::Entity& entity, const std::string& key, const int defaultValue)
{
  if (const auto* value = entity.property(key); value && !value->empty())
  {
    try
    {
      return std::stoi(*value);
    }
    catch (const std::exception&)
    {
      return defaultValue;
    }
  }
  return defaultValue;
}

} // namespace

EntityModelRenderer::EntityModelRenderer(
  Logger& logger,
  mdl::EntityModelManager& entityModelManager,
  const mdl::EditorContext& editorContext)
  : m_logger{logger}
  , m_entityModelManager{entityModelManager}
  , m_editorContext{editorContext}
{
}

EntityModelRenderer::~EntityModelRenderer()
{
  clear();
}

void EntityModelRenderer::addEntity(const mdl::EntityNode& entityNode)
{
  const auto modelSpec =
    mdl::safeGetModelSpecification(m_logger, entityNode.entity().classname(), [&]() {
      return entityNode.entity().modelSpecification();
    });

  auto* renderer = m_entityModelManager.renderer(modelSpec);
  if (renderer != nullptr)
  {
    m_entities.emplace(&entityNode, renderer);
  }
}

void EntityModelRenderer::removeEntity(const mdl::EntityNode& entityNode)
{
  m_entities.erase(&entityNode);
}

void EntityModelRenderer::updateEntity(const mdl::EntityNode& entityNode)
{
  const auto modelSpec =
    mdl::safeGetModelSpecification(m_logger, entityNode.entity().classname(), [&]() {
      return entityNode.entity().modelSpecification();
    });

  auto* renderer = m_entityModelManager.renderer(modelSpec);
  auto it = m_entities.find(&entityNode);

  if (renderer == nullptr && it == std::end(m_entities))
  {
    return;
  }

  if (it == std::end(m_entities))
  {
    m_entities.emplace(&entityNode, renderer);
  }
  else
  {
    if (renderer == nullptr)
    {
      m_entities.erase(it);
    }
    else if (it->second != renderer)
    {
      it->second = renderer;
    }
  }
}

void EntityModelRenderer::clear()
{
  m_entities.clear();
}

bool EntityModelRenderer::applyTinting() const
{
  return m_applyTinting;
}

void EntityModelRenderer::setApplyTinting(const bool applyTinting)
{
  m_applyTinting = applyTinting;
}

const Color& EntityModelRenderer::tintColor() const
{
  return m_tintColor;
}

void EntityModelRenderer::setTintColor(const Color& tintColor)
{
  m_tintColor = tintColor;
}

bool EntityModelRenderer::showHiddenEntities() const
{
  return m_showHiddenEntities;
}

void EntityModelRenderer::setShowHiddenEntities(const bool showHiddenEntities)
{
  m_showHiddenEntities = showHiddenEntities;
}

void EntityModelRenderer::render(RenderBatch& renderBatch)
{
  renderBatch.add(this);
}

void EntityModelRenderer::prepare(gl::Gl& gl, gl::VboManager& vboManager)
{
  m_entityModelManager.prepare(gl, vboManager);
}

void EntityModelRenderer::render(RenderContext& renderContext)
{
  if (!m_entities.empty())
  {
    auto& gl = renderContext.gl();

    gl.enable(GL_TEXTURE_2D);
    gl.activeTexture(GL_TEXTURE0);

    auto& prefs = PreferenceManager::instance();
    auto shader =
      gl::ActiveShader{gl, renderContext.shaderManager(), gl::Shaders::EntityModelShader};
    shader.set("Brightness", prefs.get(Preferences::Brightness));
    shader.set("ApplyTinting", m_applyTinting);
    shader.set("TintColor", m_tintColor);
    shader.set("GrayScale", false);
    shader.set("Material", 0);
    // Safe defaults; overridden per material/entity below. Important: RenderAmt must never be
    // left at the GLSL default (0.0) or a blended material would vanish.
    shader.set("EnableMasked", true);
    shader.set("RenderAmt", 1.0f);
    shader.set("ShowSoftMapBounds", !renderContext.softMapBounds().is_empty());
    shader.set("SoftMapBoundsMin", renderContext.softMapBounds().min);
    shader.set("SoftMapBoundsMax", renderContext.softMapBounds().max);
    shader.set(
      "SoftMapBoundsColor",
      RgbaF{prefs.get(Preferences::SoftMapBoundsColor).to<RgbF>(), 0.1f});

    shader.set("CameraPosition", renderContext.camera().position());
    shader.set("CameraDirection", renderContext.camera().direction());
    shader.set("CameraRight", renderContext.camera().right());
    shader.set("CameraUp", renderContext.camera().up());
    shader.set("ViewMatrix", renderContext.camera().viewMatrix());

    const auto& propertyConfig = m_entities.begin()->first->entityPropertyConfig();
    const auto& defaultModelScaleExpression = propertyConfig.defaultModelScaleExpression;

    for (const auto& [entityNode, renderer] : m_entities)
    {
      if (!m_showHiddenEntities && !m_editorContext.visible(*entityNode))
      {
        continue;
      }

      const auto* model = entityNode->entity().model();
      const auto* modelData = model ? model->data() : nullptr;
      if (!modelData)
      {
        continue;
      }

      shader.set("Orientation", static_cast<int>(modelData->orientation()));

      const auto transformation = vm::mat4x4f{
        entityNode->entity().modelTransformation(defaultModelScaleExpression)};
      const auto multMatrix =
        MultiplyModelMatrix{renderContext.transformation(), transformation};

      shader.set("ModelMatrix", transformation);

      // HL/Source render FX: `rendermode` picks the blend, `renderamt` scales alpha,
      // `rendercolor` tints (mode 1 Color). No properties => Normal => the material's own
      // blend (the Phase-1 default: opaque models mask, image sprites blend).
      const auto& entity = entityNode->entity();
      const auto renderMode = intProperty(entity, "rendermode", 0);
      auto blend = gl::ModelMaterialRenderFunc::Blend::FromMaterial;
      auto renderAmt = 1.0f;
      auto applyTinting = m_applyTinting;
      auto tintColor = m_tintColor;
      if (renderMode != 0)
      {
        const auto amt =
          float(std::clamp(intProperty(entity, "renderamt", 255), 0, 255)) / 255.0f;
        switch (renderMode)
        {
        case 1: // Color: render flat-tinted by rendercolor
          blend = gl::ModelMaterialRenderFunc::Blend::Alpha;
          renderAmt = amt;
          // Don't fight a selection/lock tint; skip the FGD-default black ("0 0 0").
          if (!m_applyTinting)
          {
            if (const auto* rc = entity.property("rendercolor");
                rc != nullptr && !rc->empty() && *rc != "0 0 0")
            {
              // Tint by the render color on success; leave it untinted (and alpha-blended) if
              // the value is malformed.
              static_cast<void>(Color::parse(*rc) | kdl::transform([&](const auto& c) {
                applyTinting = true;
                tintColor = c;
              }));
            }
          }
          break;
        case 2: // Texture: alpha blend
          blend = gl::ModelMaterialRenderFunc::Blend::Alpha;
          renderAmt = amt;
          break;
        case 3: // Glow
        case 5: // Additive
          blend = gl::ModelMaterialRenderFunc::Blend::Additive;
          renderAmt = amt;
          break;
        case 4: // Solid (alphatest)
          blend = gl::ModelMaterialRenderFunc::Blend::Masked;
          break;
        default:
          break;
        }
      }

      shader.set("ApplyTinting", applyTinting);
      shader.set("TintColor", tintColor);
      shader.set("RenderAmt", renderAmt);

      auto renderFunc = gl::ModelMaterialRenderFunc{
        renderContext.minFilterMode(),
        renderContext.magFilterMode(),
        shader.program(),
        blend};
      renderer->render(gl, shader.program(), renderFunc);
    }
  }
}

} // namespace tb::render
