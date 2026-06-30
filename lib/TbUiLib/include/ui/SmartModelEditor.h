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
#include "ui/SmartPropertyEditor.h"

#include <filesystem>
#include <vector>

class QComboBox;
class QLineEdit;
class QScrollBar;

namespace tb
{
namespace mdl
{
class EntityNodeBase;
} // namespace mdl

namespace ui
{
class AppController;
class MapDocument;
class ModelPickerView;

/**
 * Model picker for the `model` key of prop_static / prop_detail. Embeds a folder dropdown
 * (auto-discovered subfolders of models/), a filter box, and a ModelPickerView 3D-thumbnail
 * grid; clicking a thumbnail writes the model's path (e.g. models/nature/tree1.iqm) to the
 * key. Mirrors SmartDecalEditor + EntityBrowser.
 */
class SmartModelEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  std::vector<std::filesystem::path> m_roots;
  std::vector<std::filesystem::path> m_extensions;
  QComboBox* m_folderChoice = nullptr;
  QComboBox* m_extensionChoice = nullptr;
  QLineEdit* m_filterBox = nullptr;
  QScrollBar* m_scrollBar = nullptr;
  ModelPickerView* m_view = nullptr;

  NotifierConnection m_notifierConnection;

public:
  // roots: the asset folders to browse, e.g. {"models"} for props or {"models","sprites"}
  // for env_sprite. The folder dropdown lists every subfolder of these that contains a
  // runtime model.
  SmartModelEditor(
    AppController& appController,
    MapDocument& document,
    std::vector<std::filesystem::path> roots,
    std::vector<std::filesystem::path> extensions,
    QWidget* parent = nullptr);

private:
  void createGui(AppController& appController);
  void reloadFolders();
  void onModelSelected(const std::filesystem::path& path);
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;
};

} // namespace ui
} // namespace tb
