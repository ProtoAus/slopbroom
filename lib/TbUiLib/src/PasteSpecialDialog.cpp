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

#include "ui/PasteSpecialDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "mdl/Map_CopyPaste.h"
#include "ui/BorderLine.h"
#include "ui/DialogButtonLayout.h"
#include "ui/DialogHeader.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{
namespace
{

QDoubleSpinBox* makeCoordinateSpinBox()
{
  auto* spinBox = new QDoubleSpinBox{};
  spinBox->setRange(-1000000.0, 1000000.0);
  spinBox->setDecimals(3);
  spinBox->setSingleStep(1.0);
  spinBox->setValue(0.0);
  return spinBox;
}

} // namespace

PasteSpecialDialog::PasteSpecialDialog(
  QWidget* parent, const std::optional<vm::bbox3d>& selectionBounds)
  : QDialog{parent}
{
  createGui(selectionBounds);
}

void PasteSpecialDialog::createGui(const std::optional<vm::bbox3d>& selectionBounds)
{
  setWindowTitle(tr("Paste Special"));
  setWindowIconTB(this);

  m_copies = new QSpinBox{};
  m_copies->setRange(1, 9999);
  m_copies->setValue(1);

  auto* copiesLayout = new QHBoxLayout{};
  copiesLayout->setContentsMargins(0, 0, 0, 0);
  copiesLayout->addWidget(new QLabel{tr("Number of copies to paste:")});
  copiesLayout->addWidget(m_copies, 1);

  m_startAtCenter = new QCheckBox{tr("Start at center of original")};
  m_startAtCenter->setChecked(true);
  m_groupCopies = new QCheckBox{tr("Group copies")};

  m_offsetX = makeCoordinateSpinBox();
  m_offsetY = makeCoordinateSpinBox();
  m_offsetZ = makeCoordinateSpinBox();
  m_grabX = new QPushButton{"<"};
  m_grabY = new QPushButton{"<"};
  m_grabZ = new QPushButton{"<"};

  m_rotationX = makeCoordinateSpinBox();
  m_rotationY = makeCoordinateSpinBox();
  m_rotationZ = makeCoordinateSpinBox();

  // The "grab" buttons fill an offset axis with the source selection's size, so N copies
  // tile edge-to-edge. Only available when there is a selection to measure.
  const auto hasBounds = selectionBounds.has_value();
  const auto size = hasBounds ? selectionBounds->size() : vm::vec3d{0, 0, 0};
  for (auto* button : {m_grabX, m_grabY, m_grabZ})
  {
    button->setEnabled(hasBounds);
    button->setMaximumWidth(30);
  }
  connect(m_grabX, &QPushButton::clicked, this, [this, size]() {
    m_offsetX->setValue(size.x());
  });
  connect(m_grabY, &QPushButton::clicked, this, [this, size]() {
    m_offsetY->setValue(size.y());
  });
  connect(m_grabZ, &QPushButton::clicked, this, [this, size]() {
    m_offsetZ->setValue(size.z());
  });

  auto* transformLayout = new QGridLayout{};
  transformLayout->setContentsMargins(0, 0, 0, 0);
  transformLayout->addWidget(new QLabel{tr("Offset (accumulative):")}, 0, 0, 1, 3);
  transformLayout->addWidget(new QLabel{tr("Rotation (accumulative):")}, 0, 4, 1, 2);

  const QString axisLabels[3] = {tr("X:"), tr("Y:"), tr("Z:")};
  QDoubleSpinBox* offsetSpins[3] = {m_offsetX, m_offsetY, m_offsetZ};
  QPushButton* grabButtons[3] = {m_grabX, m_grabY, m_grabZ};
  QDoubleSpinBox* rotationSpins[3] = {m_rotationX, m_rotationY, m_rotationZ};
  for (int i = 0; i < 3; ++i)
  {
    const auto row = i + 1;
    transformLayout->addWidget(new QLabel{axisLabels[i]}, row, 0);
    transformLayout->addWidget(offsetSpins[i], row, 1);
    transformLayout->addWidget(grabButtons[i], row, 2);
    transformLayout->addWidget(new QLabel{axisLabels[i]}, row, 4);
    transformLayout->addWidget(rotationSpins[i], row, 5);
  }
  transformLayout->setColumnStretch(1, 1);
  transformLayout->setColumnMinimumWidth(3, LayoutConstants::WideHMargin);
  transformLayout->setColumnStretch(5, 1);

  m_makeNamesUnique = new QCheckBox{tr("Make pasted entity names unique")};
  m_makeNamesUnique->setChecked(true);

  m_addPrefix = new QCheckBox{tr("Add this prefix to all named entities:")};
  m_prefix = new QLineEdit{};
  m_prefix->setEnabled(false);
  connect(m_addPrefix, &QCheckBox::toggled, m_prefix, &QLineEdit::setEnabled);

  auto* prefixLayout = new QHBoxLayout{};
  prefixLayout->setContentsMargins(0, 0, 0, 0);
  prefixLayout->addWidget(m_addPrefix);
  prefixLayout->addWidget(m_prefix, 1);

  auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel};
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* innerLayout = new QVBoxLayout{};
  innerLayout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  innerLayout->setSpacing(LayoutConstants::NarrowVMargin);
  innerLayout->addLayout(copiesLayout);
  innerLayout->addWidget(m_startAtCenter);
  innerLayout->addWidget(m_groupCopies);
  innerLayout->addSpacing(LayoutConstants::WideVMargin);
  innerLayout->addLayout(transformLayout);
  innerLayout->addSpacing(LayoutConstants::WideVMargin);
  innerLayout->addWidget(m_makeNamesUnique);
  innerLayout->addLayout(prefixLayout);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);
  outerLayout->addWidget(new DialogHeader{tr("Paste Special")});
  outerLayout->addWidget(new BorderLine{});
  outerLayout->addLayout(innerLayout);
  outerLayout->addLayout(wrapDialogButtonBox(buttonBox));

  setLayout(outerLayout);
}

mdl::PasteSpecialOptions PasteSpecialDialog::options() const
{
  auto options = mdl::PasteSpecialOptions{};
  options.numCopies = m_copies->value();
  options.startAtCenter = m_startAtCenter->isChecked();
  options.groupCopies = m_groupCopies->isChecked();
  options.offset = vm::vec3d{m_offsetX->value(), m_offsetY->value(), m_offsetZ->value()};
  options.rotation =
    vm::vec3d{m_rotationX->value(), m_rotationY->value(), m_rotationZ->value()};
  options.makeNamesUnique = m_makeNamesUnique->isChecked();
  options.namePrefix =
    m_addPrefix->isChecked() ? m_prefix->text().toStdString() : std::string{};
  return options;
}

} // namespace tb::ui
