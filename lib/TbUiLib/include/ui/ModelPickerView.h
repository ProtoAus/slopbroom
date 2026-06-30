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

#include "NotifierConnection.h"
#include "gl/FontDescriptor.h"
#include "ui/CellView.h"

#include "vm/bbox.h"
#include "vm/mat.h"
#include "vm/quat.h" // IWYU pragma: keep
#include "vm/vec.h"

#include <filesystem>
#include <string>
#include <vector>

class QScrollBar;

namespace tb
{
namespace gl
{
class MaterialRenderer;
class ResourceId;
} // namespace gl

namespace mdl
{
enum class Orientation;
} // namespace mdl

namespace render
{
class Transformation;
} // namespace render

namespace ui
{
class AppController;
class MapDocument;

struct ModelCellData
{
  std::filesystem::path modelPath;
  gl::MaterialRenderer* modelRenderer;
  mdl::Orientation modelOrientation;
  gl::FontDescriptor fontDescriptor;
  vm::bbox3f bounds;
  vm::mat4x4f transform;
  vm::vec3f modelScale;
};

/**
 * A CellView that renders a grid of 3D model thumbnails loaded from a folder (e.g.
 * models/nature), for the prop model picker. Mirrors EntityBrowserView's model-rendering,
 * but is keyed on model file paths rather than entity definitions, and emits modelSelected
 * on a left click. The texture on each preview relies on the Assimp/IQM game-root texture
 * resolution fix (loose model skins resolve from textures/...).
 */
class ModelPickerView : public CellView
{
  Q_OBJECT
private:
  static constexpr auto CameraPosition = vm::vec3f{256.0f, 0.0f, 0.0f};
  static constexpr auto CameraDirection = vm::vec3f{-1, 0, 0};
  static constexpr auto CameraUp = vm::vec3f{0, 0, 1};

  MapDocument& m_document;
  std::vector<std::filesystem::path> m_extensions; // which file types this picker lists
  std::filesystem::path m_folder;
  std::string m_filterText;
  std::filesystem::path m_extensionFilter; // empty = all extensions
  vm::quatf m_rotation;

  NotifierConnection m_notifierConnection;

public:
  ModelPickerView(
    AppController& appController,
    QScrollBar* scrollBar,
    MapDocument& document,
    std::vector<std::filesystem::path> extensions);
  ~ModelPickerView() override;

  const std::vector<std::filesystem::path>& extensions() const;

  // FTE runtime model formats (for props). The single source for the model-file list.
  static const std::vector<std::filesystem::path>& modelExtensions();
  // Models PLUS image-sprite formats (for env_sprite, which can be a .spr/.mdl model or a
  // loose image rendered as a billboard).
  static const std::vector<std::filesystem::path>& spriteAndModelExtensions();

  void setFolder(std::filesystem::path folder);
  void setFilterText(const std::string& filterText);
  void setExtensionFilter(std::filesystem::path extension);

private:
  void doInitLayout(Layout& layout) override;
  void doReloadLayout(Layout& layout) override;
  void addModelToLayout(
    Layout& layout,
    const std::filesystem::path& modelPath,
    const gl::FontDescriptor& font);

  void resourcesWereProcessed(const std::vector<gl::ResourceId>& resources);

  void doClear() override;
  void doRender(gl::Gl& gl, Layout& layout, float y, float height) override;
  void doLeftClick(Layout& layout, float x, float y) override;
  bool shouldRenderFocusIndicator() const override;
  const Color& getBackgroundColor() override;

  void renderModels(
    gl::Gl& gl,
    Layout& layout,
    float y,
    float height,
    render::Transformation& transformation);

  vm::mat4x4f itemTransformation(const Cell& cell, float y, float height) const;

  QString tooltip(const Cell& cell) override;

  const ModelCellData& cellData(const Cell& cell) const;
signals:
  void modelSelected(const std::filesystem::path& path);
};

} // namespace ui
} // namespace tb
