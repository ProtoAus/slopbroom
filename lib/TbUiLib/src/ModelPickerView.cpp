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

#include "ui/ModelPickerView.h"

#include "Color.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "fs/FileSystem.h"
#include "fs/PathInfo.h"
#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "gl/ActiveShader.h"
#include "gl/FontManager.h"
#include "gl/MaterialIndexRangeRenderer.h"
#include "gl/MaterialRenderFunc.h"
#include "gl/Shaders.h"
#include "gl/TextureFont.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/Map.h"
#include "mdl/ModelSpecification.h"
#include "render/Transformation.h"
#include "ui/MapDocument.h"

#include "kd/contracts.h"
#include "kd/path_utils.h"
#include "kd/result.h"
#include "kd/string_compare.h"
#include "kd/string_utils.h"
#include "kd/vector_utils.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

namespace tb::ui
{

ModelPickerView::ModelPickerView(
  AppController& appController,
  QScrollBar* scrollBar,
  MapDocument& document,
  std::vector<std::filesystem::path> extensions)
  : CellView{appController, scrollBar}
  , m_document{document}
  , m_extensions{std::move(extensions)}
{
  const auto hRotation = vm::quatf{vm::vec3f{0, 0, 1}, vm::to_radians(-30.0f)};
  const auto vRotation = vm::quatf{vm::vec3f{0, 1, 0}, vm::to_radians(20.0f)};
  m_rotation = vRotation * hRotation;

  m_notifierConnection += m_document.resourcesWereProcessedNotifier.connect(
    this, &ModelPickerView::resourcesWereProcessed);
}

ModelPickerView::~ModelPickerView()
{
  clear();
}

const std::vector<std::filesystem::path>& ModelPickerView::modelExtensions()
{
  // FTE *runtime* model formats only — the ones a prop's model key can actually be. Source
  // / interchange formats (.obj/.dae/.fbx/.glb/.gltf/.ase/.smd) are intentionally excluded:
  // the engine can't load them as a model, and a decompile folder full of .smd would flood
  // (and can crash) the eager-loading picker.
  static const auto extensions = std::vector<std::filesystem::path>{
    ".iqm", ".mdl", ".md3", ".md2", ".spr", ".dkm", ".fm"};
  return extensions;
}

const std::vector<std::filesystem::path>& ModelPickerView::spriteAndModelExtensions()
{
  // env_sprite can be a .spr/.mdl model OR a loose image rendered as a billboard sprite
  // (FTE and TB's image-sprite loader both support this), so add the common image formats.
  static const auto extensions = [] {
    auto exts = modelExtensions();
    for (const auto& image : {".png", ".jpg", ".jpeg", ".tga", ".bmp"})
    {
      exts.emplace_back(image);
    }
    return exts;
  }();
  return extensions;
}

const std::vector<std::filesystem::path>& ModelPickerView::extensions() const
{
  return m_extensions;
}

void ModelPickerView::setFolder(std::filesystem::path folder)
{
  if (folder != m_folder)
  {
    m_folder = std::move(folder);
    invalidate();
    update();
  }
}

void ModelPickerView::setFilterText(const std::string& filterText)
{
  if (filterText != m_filterText)
  {
    m_filterText = filterText;
    invalidate();
    update();
  }
}

void ModelPickerView::setExtensionFilter(std::filesystem::path extension)
{
  auto lowered = kdl::path_to_lower(std::move(extension));
  if (lowered != m_extensionFilter)
  {
    m_extensionFilter = std::move(lowered);
    invalidate();
    update();
  }
}

void ModelPickerView::doInitLayout(Layout& layout)
{
  layout.setOuterMargin(5.0f);
  layout.setGroupMargin(5.0f);
  layout.setRowMargin(5.0f);
  layout.setCellMargin(5.0f);
  layout.setCellWidth(93.0f, 93.0f);
  layout.setCellHeight(64.0f, 128.0f);
  layout.setMaxUpScale(1.5f);
}

void ModelPickerView::doReloadLayout(Layout& layout)
{
  const auto& fontPath = pref(Preferences::RendererFontPath);
  const auto fontSize = pref(Preferences::BrowserFontSize);
  contract_assert(fontSize > 0);
  const auto font = gl::FontDescriptor{fontPath, static_cast<size_t>(fontSize)};

  if (m_folder.empty())
  {
    return;
  }

  const auto& gameFs = m_document.map().gameFileSystem();
  // Flat: only the selected folder's direct models. Never descend into subfolders (which
  // for messy model trees can hold decompile-source files / crash-prone models). The folder
  // dropdown lists every model-containing subfolder, so the user navigates one at a time.
  auto paths = gameFs.find(
                 m_folder,
                 fs::TraversalMode::Flat,
                 fs::makeExtensionPathMatcher(m_extensions))
               | kdl::value_or(std::vector<std::filesystem::path>{});
  paths = kdl::vec_sort(std::move(paths));

  for (const auto& modelPath : paths)
  {
    // Skip directories that happen to match a model extension (extension matcher ignores
    // file-vs-dir).
    if (gameFs.pathInfo(modelPath) != fs::PathInfo::File)
    {
      continue;
    }
    // Apply the active extension filter, if any.
    if (
      !m_extensionFilter.empty()
      && kdl::path_to_lower(modelPath.extension()) != m_extensionFilter)
    {
      continue;
    }
    addModelToLayout(layout, modelPath, font);
  }
}

void ModelPickerView::addModelToLayout(
  Layout& layout,
  const std::filesystem::path& modelPath,
  const gl::FontDescriptor& font)
{
  const auto name = modelPath.filename().string();

  if (
    !m_filterText.empty()
    && !std::ranges::all_of(kdl::str_split(m_filterText, " "), [&](const auto& pattern) {
         return kdl::ci::str_contains(name, pattern);
       }))
  {
    return;
  }

  auto& map = m_document.map();
  const auto maxCellWidth = layout.maxCellWidth();
  const auto actualFont = fontManager().selectFontSize(font, name, maxCellWidth, 5);
  const auto actualSize = fontManager().font(actualFont).measure(name);

  const auto spec = mdl::ModelSpecification{modelPath, 0, 0};
  const auto modelScale = vm::vec3f{1, 1, 1};

  auto* modelRenderer = static_cast<gl::MaterialRenderer*>(nullptr);
  // Placeholder box until the model finishes async-loading (then resourcesWereProcessed
  // re-lays-out with the real bounds).
  auto bounds = vm::bbox3f{vm::vec3f{-8, -8, -8}, vm::vec3f{8, 8, 8}};
  auto modelOrientation = mdl::Orientation::Oriented;

  const auto& entityModelManager = map.entityModelManager();
  const auto* model = entityModelManager.model(spec.path);
  const auto* modelData = model ? model->data() : nullptr;
  const auto* modelFrame = modelData ? modelData->frame(spec.frameIndex) : nullptr;
  if (modelFrame)
  {
    modelRenderer = entityModelManager.renderer(spec);
    modelOrientation = modelData->orientation();
    bounds = modelFrame->bounds();
  }

  const auto scalingMatrix = vm::scaling_matrix(modelScale);
  const auto center = bounds.center();
  const auto scaledCenter = scalingMatrix * center;
  const auto transform = vm::translation_matrix(scaledCenter)
                         * vm::rotation_matrix(m_rotation) * scalingMatrix
                         * vm::translation_matrix(-center);

  const auto rotatedBounds = bounds.transform(transform);
  const auto rotatedBoundsSize = rotatedBounds.size();

  layout.addItem(
    ModelCellData{
      modelPath,
      modelRenderer,
      modelOrientation,
      actualFont,
      bounds,
      transform,
      modelScale},
    name,
    rotatedBoundsSize.y(),
    rotatedBoundsSize.z(),
    actualSize.x(),
    static_cast<float>(font.size()) + 2.0f);
}

void ModelPickerView::resourcesWereProcessed(const std::vector<gl::ResourceId>&)
{
  invalidate();
}

void ModelPickerView::doClear() {}

void ModelPickerView::doRender(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  const auto viewLeft = static_cast<float>(0);
  const auto viewTop = static_cast<float>(size().height());
  const auto viewRight = static_cast<float>(size().width());
  const auto viewBottom = static_cast<float>(0);

  const auto projection =
    vm::ortho_matrix(-1024.0f, 1024.0f, viewLeft, viewTop, viewRight, viewBottom);
  const auto view =
    vm::view_matrix(CameraDirection, CameraUp) * vm::translation_matrix(CameraPosition);
  auto transformation = render::Transformation{gl, projection, view};

  renderModels(gl, layout, y, height, transformation);
}

bool ModelPickerView::shouldRenderFocusIndicator() const
{
  return false;
}

const Color& ModelPickerView::getBackgroundColor()
{
  return pref(Preferences::BrowserBackgroundColor);
}

void ModelPickerView::renderModels(
  gl::Gl& gl,
  Layout& layout,
  const float y,
  const float height,
  render::Transformation& transformation)
{
  gl.frontFace(GL_CW);

  auto& entityModelManager = m_document.map().entityModelManager();
  entityModelManager.prepare(gl, vboManager());

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::EntityModelShader};
  shader.set("ApplyTinting", false);
  shader.set("Brightness", pref(Preferences::Brightness));
  shader.set("GrayScale", false);
  // Default masked/opaque; RenderAmt must never be left at the GLSL default (0.0) or a
  // translucent sprite thumbnail would vanish. The render func flips EnableMasked per material.
  shader.set("EnableMasked", true);
  shader.set("RenderAmt", 1.0f);

  shader.set("CameraPosition", CameraPosition);
  shader.set("CameraDirection", CameraDirection);
  shader.set("CameraRight", vm::cross(CameraDirection, CameraUp));
  shader.set("CameraUp", CameraUp);
  shader.set("ViewMatrix", transformation.viewMatrix());

  for (const auto& group : layout.groups())
  {
    if (group.intersectsY(y, height))
    {
      for (const auto& row : group.rows())
      {
        if (row.intersectsY(y, height))
        {
          for (const auto& cell : row.cells())
          {
            if (auto* modelRenderer = cellData(cell).modelRenderer)
            {
              shader.set(
                "Orientation", static_cast<int>(cellData(cell).modelOrientation));

              const auto itemTrans = itemTransformation(cell, y, height);
              shader.set("ModelMatrix", itemTrans);

              const auto multMatrix =
                render::MultiplyModelMatrix{transformation, itemTrans};

              auto renderFunc = gl::ModelMaterialRenderFunc{
                pref(Preferences::TextureMinFilter),
                pref(Preferences::TextureMagFilter),
                shader.program()};
              modelRenderer->render(gl, shader.program(), renderFunc);
            }
          }
        }
      }
    }
  }
}

vm::mat4x4f ModelPickerView::itemTransformation(
  const Cell& cell, const float y, const float height) const
{
  const auto& cellData = this->cellData(cell);

  const auto offset =
    vm::vec3f{0.0f, cell.itemBounds().left(), height - (cell.itemBounds().bottom() - y)};
  const auto scaling = cell.scale();
  const auto& rotatedBounds = cellData.bounds.transform(cellData.transform);
  const auto rotationOffset =
    vm::vec3f{0.0f, -rotatedBounds.min.y(), -rotatedBounds.min.z()};

  return vm::translation_matrix(offset) * vm::scaling_matrix(vm::vec3f::fill(scaling))
         * vm::translation_matrix(rotationOffset) * cellData.transform;
}

void ModelPickerView::doLeftClick(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
  {
    emit modelSelected(cellData(*cell).modelPath);
  }
}

QString ModelPickerView::tooltip(const Cell& cell)
{
  return QString::fromStdString(cellData(cell).modelPath.filename().string());
}

const ModelCellData& ModelPickerView::cellData(const Cell& cell) const
{
  return cell.itemAs<ModelCellData>();
}

} // namespace tb::ui
