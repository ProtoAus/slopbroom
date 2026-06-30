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

#include <QWidget>

#include "NotifierConnection.h"
#include "mdl/TagType.h"

#include <filesystem>
#include <utility>
#include <vector>

class QCheckBox;

namespace tb::ui
{
class MapDocument;

/**
 * A compact "Filter" sub-panel for the Map Inspector (sits below VisGroups). It surfaces
 * the most useful of TrenchBroom's existing view-filter toggles right next to VisGroups:
 * Show brushes, Show point entities, and one checkbox per smart tag.
 *
 * It deliberately mirrors ViewEditor's mechanism so it stays in sync with the toolbar
 * "View Options" popup and triggers the same redraw: the show-flags go through the global
 * Preferences (Preferences::ShowBrushes / ShowPointEntities), and tag visibility goes
 * through EditorContext::setHiddenTags. All of it is pure view state — not saved, not undoable.
 */
class ViewFilterPanel : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;

  QCheckBox* m_showBrushesCheckBox = nullptr;
  QCheckBox* m_showWorldBrushesCheckBox = nullptr;
  QCheckBox* m_showPointEntitiesCheckBox = nullptr;
  std::vector<std::pair<mdl::TagType::Type, QCheckBox*>> m_tagCheckBoxes;

  NotifierConnection m_notifierConnection;

public:
  explicit ViewFilterPanel(MapDocument& document, QWidget* parent = nullptr);

private:
  void connectObservers();
  void documentWasLoaded();
  void editorContextDidChange();
  void preferenceDidChange(const std::filesystem::path& path);

  void createGui();
  void refresh();

  void showBrushesChanged(bool checked);
  void showWorldBrushesChanged(bool checked);
  void showPointEntitiesChanged(bool checked);
  void showTagChanged(bool checked, mdl::TagType::Type tagType);
};

} // namespace tb::ui
