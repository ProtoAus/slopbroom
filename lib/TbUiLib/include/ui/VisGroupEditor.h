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

#include <QWidget>

#include <cstddef>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace tb::ui
{
class MapDocument;

/**
 * The VisGroups panel (Map Inspector): a list of named visibility groups, each with a
 * checkbox that toggles whether the group's members are drawn, plus buttons to create /
 * delete / rename groups, add or remove the current selection, and select a group's
 * members. Drives mdl::Map_VisGroups; the hide itself happens in EditorContext::visible.
 */
class VisGroupEditor : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;

  QListWidget* m_list = nullptr;
  QPushButton* m_newButton = nullptr;
  QPushButton* m_addButton = nullptr;
  QPushButton* m_removeButton = nullptr;
  QPushButton* m_selectButton = nullptr;
  QPushButton* m_showAllButton = nullptr;
  QPushButton* m_hideAllButton = nullptr;

  bool m_updating = false;
  NotifierConnection m_notifierConnection;

public:
  explicit VisGroupEditor(MapDocument& document, QWidget* parent = nullptr);

private:
  void createGui();
  void connectObservers();

  void reload();
  void updateButtons();
  std::size_t currentGroupId() const; // 0 if no row selected

  void onItemChanged(QListWidgetItem* item); // visibility checkbox
  void onNewGroup();
  void onDeleteGroup();
  void onRenameGroup();
  void onAddSelected();
  void onRemoveSelected();
  void onSelectMembers();
  void onSetColor();
  void onShowAll();
  void onHideAll();

  // Double-click the color swatch -> color picker; double-click the name -> rename.
  bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace tb::ui
