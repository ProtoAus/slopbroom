/*
 Copyright (C) 2025 Kristian Duske

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

#include "vm/bbox.h"

#include <optional>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace tb::mdl
{
struct PasteSpecialOptions;
}

namespace tb::ui
{

/**
 * Hammer-style "Paste Special" dialog: number of copies, an accumulative offset/rotation,
 * grouping and entity-name uniquification. The offset "grab" buttons prefill each axis with
 * the source selection's size (edge-to-edge tiling), so they are enabled only when a
 * selection bounds is supplied.
 */
class PasteSpecialDialog : public QDialog
{
  Q_OBJECT
private:
  QSpinBox* m_copies = nullptr;
  QCheckBox* m_startAtCenter = nullptr;
  QCheckBox* m_groupCopies = nullptr;
  QDoubleSpinBox* m_offsetX = nullptr;
  QDoubleSpinBox* m_offsetY = nullptr;
  QDoubleSpinBox* m_offsetZ = nullptr;
  QPushButton* m_grabX = nullptr;
  QPushButton* m_grabY = nullptr;
  QPushButton* m_grabZ = nullptr;
  QDoubleSpinBox* m_rotationX = nullptr;
  QDoubleSpinBox* m_rotationY = nullptr;
  QDoubleSpinBox* m_rotationZ = nullptr;
  QCheckBox* m_makeNamesUnique = nullptr;
  QCheckBox* m_addPrefix = nullptr;
  QLineEdit* m_prefix = nullptr;

private:
  void createGui(const std::optional<vm::bbox3d>& selectionBounds);

public:
  explicit PasteSpecialDialog(
    QWidget* parent, const std::optional<vm::bbox3d>& selectionBounds = std::nullopt);

  mdl::PasteSpecialOptions options() const;
};

} // namespace tb::ui
