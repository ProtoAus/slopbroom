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

class QCheckBox;
class QComboBox;
class QPushButton;
class QSpinBox;

namespace tb::ui
{
class MapDocument;
class SweepTool;

/**
 * The Sweep tool's page. The destination cap is manipulated by the gizmo in the viewport,
 * so the page only carries the construction settings.
 */
class SweepToolPage : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  SweepTool& m_tool;

  QSpinBox* m_segments = nullptr;
  QComboBox* m_pathMode = nullptr;
  QSpinBox* m_iterations = nullptr;
  QCheckBox* m_snapToInteger = nullptr;
  QPushButton* m_resetButton = nullptr;

  NotifierConnection m_notifierConnection;

public:
  SweepToolPage(MapDocument& document, SweepTool& tool, QWidget* parent = nullptr);

private:
  void connectObservers();
  void createGui();

  void segmentsChanged(int value);
  void pathModeChanged(int index);
  void iterationsChanged(int value);
  void snapToIntegerChanged(bool checked);
  void resetClicked();
};

} // namespace tb::ui
