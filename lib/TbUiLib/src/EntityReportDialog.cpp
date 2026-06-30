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

#include "ui/EntityReportDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include "mdl/Entity.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Map.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "ui/EntityReportModel.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"

#include <set>
#include <vector>

namespace tb::ui
{

EntityReportDialog::EntityReportDialog(
  MapWindow& mapWindow, MapDocument& document, QWidget* parent)
  : QDialog{parent}
  , m_mapWindow{mapWindow}
  , m_document{document}
{
  setWindowTitle(tr("Entity Report"));
  createGui();
  connectObservers();
  refreshNow();
  resize(640, 480);
}

EntityReportDialog::~EntityReportDialog() = default;

void EntityReportDialog::createGui()
{
  m_model = new EntityReportModel{m_document, this};
  m_proxy = new EntityReportFilterProxyModel{this};
  m_proxy->setSourceModel(m_model);

  m_table = new QTableView{};
  m_table->setModel(m_proxy);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setSortingEnabled(true);
  m_table->verticalHeader()->setVisible(false);
  auto* header = m_table->horizontalHeader();
  header->setSectionResizeMode(
    EntityReportModel::ColumnClass, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(
    EntityReportModel::ColumnName, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(EntityReportModel::ColumnTarget, QHeaderView::Stretch);
  m_table->sortByColumn(EntityReportModel::ColumnClass, Qt::AscendingOrder);

  connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
    goToSelection();
  });
  connect(
    m_table->selectionModel(),
    &QItemSelectionModel::selectionChanged,
    this,
    [this](const QItemSelection&, const QItemSelection&) { updateButtonState(); });

  // buttons (right column)
  m_goToButton = new QPushButton{tr("Go to")};
  m_markButton = new QPushButton{tr("Mark")};
  m_deleteButton = new QPushButton{tr("Delete")};
  m_propertiesButton = new QPushButton{tr("Properties...")};
  m_hideButton = new QPushButton{tr("Hide")};
  m_unhideButton = new QPushButton{tr("Unhide")};
  m_closeButton = new QPushButton{tr("Close")};

  connect(m_goToButton, &QPushButton::clicked, this, &EntityReportDialog::goToSelection);
  connect(m_markButton, &QPushButton::clicked, this, &EntityReportDialog::markSelection);
  connect(
    m_deleteButton, &QPushButton::clicked, this, &EntityReportDialog::deleteSelection);
  connect(
    m_propertiesButton,
    &QPushButton::clicked,
    this,
    &EntityReportDialog::showProperties);
  connect(m_hideButton, &QPushButton::clicked, this, &EntityReportDialog::hideSelection);
  connect(
    m_unhideButton, &QPushButton::clicked, this, &EntityReportDialog::unhideSelection);
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);

  auto* buttonLayout = new QVBoxLayout{};
  buttonLayout->addWidget(m_goToButton);
  buttonLayout->addWidget(m_markButton);
  buttonLayout->addWidget(m_deleteButton);
  buttonLayout->addWidget(m_propertiesButton);
  buttonLayout->addSpacing(8);
  buttonLayout->addWidget(m_hideButton);
  buttonLayout->addWidget(m_unhideButton);
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(m_closeButton);

  auto* topLayout = new QHBoxLayout{};
  topLayout->addWidget(m_table, 1);
  topLayout->addLayout(buttonLayout);

  // filter panel
  m_radioEverything = new QRadioButton{tr("Everything")};
  m_radioBrush = new QRadioButton{tr("Brush entities")};
  m_radioPoint = new QRadioButton{tr("Point entities")};
  m_radioEverything->setChecked(true);
  auto* typeGroup = new QButtonGroup{this};
  typeGroup->addButton(m_radioEverything);
  typeGroup->addButton(m_radioBrush);
  typeGroup->addButton(m_radioPoint);
  connect(m_radioEverything, &QRadioButton::toggled, this, [this](const bool on) {
    if (on)
    {
      m_proxy->setTypeFilter(EntityTypeFilter::All);
    }
  });
  connect(m_radioBrush, &QRadioButton::toggled, this, [this](const bool on) {
    if (on)
    {
      m_proxy->setTypeFilter(EntityTypeFilter::BrushEntities);
    }
  });
  connect(m_radioPoint, &QRadioButton::toggled, this, [this](const bool on) {
    if (on)
    {
      m_proxy->setTypeFilter(EntityTypeFilter::PointEntities);
    }
  });

  m_keyValueEdit = new QLineEdit{};
  m_keyValueEdit->setClearButtonEnabled(true);
  connect(m_keyValueEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    m_proxy->setKeyValueFilter(text);
  });
  m_exactCheck = new QCheckBox{tr("Exact")};
  connect(m_exactCheck, &QCheckBox::toggled, this, [this](const bool checked) {
    m_proxy->setExactMatch(checked);
  });

  m_classCombo = new QComboBox{};
  m_classCombo->addItem(tr("(all classes)"), QString{});
  connect(m_classCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    m_proxy->setClassFilter(m_classCombo->currentData().toString());
  });
  m_includeHiddenCheck = new QCheckBox{tr("Include hidden objects")};
  connect(m_includeHiddenCheck, &QCheckBox::toggled, this, [this](const bool checked) {
    m_proxy->setIncludeHidden(checked);
  });

  auto* typeRow = new QHBoxLayout{};
  typeRow->addWidget(m_radioEverything);
  typeRow->addWidget(m_radioBrush);
  typeRow->addWidget(m_radioPoint);
  typeRow->addStretch(1);

  auto* kvRow = new QHBoxLayout{};
  kvRow->addWidget(new QLabel{tr("By key/value:")});
  kvRow->addWidget(m_keyValueEdit, 1);
  kvRow->addWidget(m_exactCheck);

  auto* classRow = new QHBoxLayout{};
  classRow->addWidget(new QLabel{tr("By class:")});
  classRow->addWidget(m_classCombo, 1);
  classRow->addWidget(m_includeHiddenCheck);

  auto* filterLayout = new QVBoxLayout{};
  filterLayout->addLayout(typeRow);
  filterLayout->addLayout(kvRow);
  filterLayout->addLayout(classRow);
  auto* filterBox = new QGroupBox{tr("Filter")};
  filterBox->setLayout(filterLayout);

  auto* mainLayout = new QVBoxLayout{};
  mainLayout->addLayout(topLayout, 1);
  mainLayout->addWidget(filterBox);
  setLayout(mainLayout);
}

void EntityReportDialog::connectObservers()
{
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect([this]() { scheduleRefresh(); });
  m_notifierConnection +=
    m_document.documentDidChangeNotifier.connect([this]() { scheduleRefresh(); });
  m_notifierConnection += m_document.nodesWereAddedNotifier.connect(
    [this](const std::vector<mdl::Node*>&) { scheduleRefresh(); });
  m_notifierConnection += m_document.nodesWereRemovedNotifier.connect(
    [this](const std::vector<mdl::Node*>&) { scheduleRefresh(); });
  m_notifierConnection += m_document.nodesDidChangeNotifier.connect(
    [this](const std::vector<mdl::Node*>&) { scheduleRefresh(); });
  m_notifierConnection += m_document.nodeVisibilityDidChangeNotifier.connect(
    [this](const std::vector<mdl::Node*>&) { scheduleRefresh(); });
}

void EntityReportDialog::scheduleRefresh()
{
  // Coalesce the burst of notifiers a single edit fires into one rebuild, and run it after
  // the current command finishes so node pointers are valid when refreshNow() reads them.
  if (m_refreshScheduled)
  {
    return;
  }
  m_refreshScheduled = true;
  QTimer::singleShot(0, this, [this]() {
    m_refreshScheduled = false;
    refreshNow();
  });
}

void EntityReportDialog::refreshNow()
{
  m_model->updateFromMap();
  refreshClassFilterCombo();
  updateButtonState();
}

void EntityReportDialog::refreshClassFilterCombo()
{
  const auto previous = m_classCombo->currentData().toString();

  auto classes = std::set<QString>{};
  for (const auto* node : m_model->entities())
  {
    classes.insert(QString::fromStdString(node->entity().classname()));
  }

  const auto blocker = QSignalBlocker{m_classCombo};
  m_classCombo->clear();
  m_classCombo->addItem(tr("(all classes)"), QString{});
  for (const auto& classname : classes)
  {
    m_classCombo->addItem(classname, classname);
  }

  const auto restored = previous.isEmpty() ? 0 : m_classCombo->findData(previous);
  m_classCombo->setCurrentIndex(restored < 0 ? 0 : restored);
  // keep the proxy in sync with whatever ended up selected (the signal was blocked)
  m_proxy->setClassFilter(m_classCombo->currentData().toString());
}

void EntityReportDialog::updateButtonState()
{
  const auto hasSelection = m_table->selectionModel()->hasSelection();
  m_goToButton->setEnabled(hasSelection);
  m_markButton->setEnabled(hasSelection);
  m_deleteButton->setEnabled(hasSelection);
  m_propertiesButton->setEnabled(hasSelection);
  m_hideButton->setEnabled(hasSelection);
  m_unhideButton->setEnabled(hasSelection);
}

std::vector<mdl::Node*> EntityReportDialog::selectedNodes() const
{
  auto nodes = std::vector<mdl::Node*>{};
  const auto rows = m_table->selectionModel()->selectedRows();
  nodes.reserve(static_cast<size_t>(rows.size()));
  for (const auto& proxyIndex : rows)
  {
    const auto sourceRow = m_proxy->mapToSource(proxyIndex).row();
    if (auto* node = m_model->entityAtRow(sourceRow))
    {
      nodes.push_back(node); // EntityNodeBase* -> Node*
    }
  }
  return nodes;
}

void EntityReportDialog::markSelection()
{
  const auto nodes = selectedNodes();
  if (nodes.empty())
  {
    return;
  }
  auto& map = m_document.map();
  mdl::deselectAll(map);
  mdl::selectNodes(map, nodes);
}

void EntityReportDialog::goToSelection()
{
  markSelection();
  m_mapWindow.focusCameraOnSelection();
}

void EntityReportDialog::deleteSelection()
{
  const auto nodes = selectedNodes();
  if (nodes.empty())
  {
    return;
  }
  mdl::removeNodes(m_document.map(), nodes);
}

void EntityReportDialog::hideSelection()
{
  const auto nodes = selectedNodes();
  if (nodes.empty())
  {
    return;
  }
  mdl::hideNodes(m_document.map(), nodes);
}

void EntityReportDialog::unhideSelection()
{
  const auto nodes = selectedNodes();
  if (nodes.empty())
  {
    return;
  }
  mdl::showNodes(m_document.map(), nodes);
}

void EntityReportDialog::showProperties()
{
  markSelection();
  m_mapWindow.switchToInspectorPage(InspectorPage::Entity);
}

} // namespace tb::ui
