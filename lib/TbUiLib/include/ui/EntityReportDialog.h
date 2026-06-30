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

#include <QDialog>

#include "NotifierConnection.h"

#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableView;

namespace tb::mdl
{
class Node;
} // namespace tb::mdl

namespace tb::ui
{
class EntityReportModel;
class EntityReportFilterProxyModel;
class MapDocument;
class MapWindow;

/**
 * Hammer-style Entity Report: a floating, non-modal window listing every entity in the
 * map (Class / Name / Target), with a filter panel and buttons to navigate, edit, hide
 * and delete entities. Owned by MapWindow as a QPointer member and shown non-modally.
 */
class EntityReportDialog : public QDialog
{
  Q_OBJECT
public:
  EntityReportDialog(
    MapWindow& mapWindow, MapDocument& document, QWidget* parent = nullptr);
  ~EntityReportDialog() override;

private:
  void createGui();
  void connectObservers();

  void scheduleRefresh();
  void refreshNow();
  void refreshClassFilterCombo();
  void updateButtonState();

  std::vector<mdl::Node*> selectedNodes() const;

  // button / row actions
  void markSelection();    // select the rows' entities in the map
  void goToSelection();    // mark + center the camera
  void deleteSelection();  // remove the entities (undoable)
  void hideSelection();    // hide the entities
  void unhideSelection();  // show the entities
  void showProperties();   // mark + reveal the Entity inspector

  MapWindow& m_mapWindow;
  MapDocument& m_document;

  EntityReportModel* m_model = nullptr;
  EntityReportFilterProxyModel* m_proxy = nullptr;
  QTableView* m_table = nullptr;

  QRadioButton* m_radioEverything = nullptr;
  QRadioButton* m_radioBrush = nullptr;
  QRadioButton* m_radioPoint = nullptr;
  QLineEdit* m_keyValueEdit = nullptr;
  QCheckBox* m_exactCheck = nullptr;
  QComboBox* m_classCombo = nullptr;
  QCheckBox* m_includeHiddenCheck = nullptr;

  QPushButton* m_goToButton = nullptr;
  QPushButton* m_markButton = nullptr;
  QPushButton* m_deleteButton = nullptr;
  QPushButton* m_hideButton = nullptr;
  QPushButton* m_unhideButton = nullptr;
  QPushButton* m_propertiesButton = nullptr;
  QPushButton* m_closeButton = nullptr;

  bool m_refreshScheduled = false;
  NotifierConnection m_notifierConnection;
};

} // namespace tb::ui
