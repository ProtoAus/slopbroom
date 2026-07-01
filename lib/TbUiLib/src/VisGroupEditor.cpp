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

#include "ui/VisGroupEditor.h"

#include <QColorDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>
#include <QVariant>

#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_VisGroups.h"
#include "mdl/ModelUtils.h"
#include "mdl/Node.h"
#include "mdl/Selection.h"
#include "mdl/VisGroup.h"
#include "mdl/VisGroupManager.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/QColorUtils.h"

#include <optional>

namespace tb::ui
{
namespace
{
// Item-data roles distinguishing a derived "pseudo-VisGroup" (per GroupNode) row from a real
// VisGroup row. Real rows store the VisGroup id in Qt::UserRole; pseudo rows store 0 there, a
// true PseudoRole flag, and the GroupNode* (as a qulonglong) in NodePtrRole.
constexpr auto PseudoRole = Qt::UserRole + 1;
constexpr auto NodePtrRole = Qt::UserRole + 2;

QIcon makeColorIcon(const std::optional<Color>& color)
{
  // Always produce a swatch (a neutral one when no color is assigned) so every row has a
  // clickable color square — double-clicking it opens the picker even to set a first color.
  auto pixmap = QPixmap{14, 14};
  pixmap.fill(color ? toQColor(*color) : QColor{128, 128, 128});
  return QIcon{pixmap};
}
} // namespace

VisGroupEditor::VisGroupEditor(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
{
  createGui();
  connectObservers();
  reload();
}

void VisGroupEditor::createGui()
{
  m_list = new QListWidget{};
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  m_list->setIconSize(QSize{14, 14}); // a predictable color-swatch column on the left
  connect(m_list, &QListWidget::itemChanged, this, &VisGroupEditor::onItemChanged);
  connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
    updateButtons();
  });
  // Double-click handled in eventFilter so we can tell the swatch from the name.
  m_list->viewport()->installEventFilter(this);

  // Delete the selected group with the Delete key while the list has focus; double-click the
  // name to rename, the swatch to recolor — so no Delete/Rename/Color buttons are needed.
  auto* deleteShortcut = new QShortcut{QKeySequence::Delete, m_list};
  deleteShortcut->setContext(Qt::WidgetShortcut);
  connect(deleteShortcut, &QShortcut::activated, this, &VisGroupEditor::onDeleteGroup);

  m_newButton = new QPushButton{tr("New")};
  m_addButton = new QPushButton{tr("Add Sel")};
  m_removeButton = new QPushButton{tr("Rem Sel")};
  m_selectButton = new QPushButton{tr("Select")};
  m_showAllButton = new QPushButton{tr("Show All")};
  m_hideAllButton = new QPushButton{tr("Hide All")};

  connect(m_newButton, &QPushButton::clicked, this, &VisGroupEditor::onNewGroup);
  connect(m_addButton, &QPushButton::clicked, this, &VisGroupEditor::onAddSelected);
  connect(m_removeButton, &QPushButton::clicked, this, &VisGroupEditor::onRemoveSelected);
  connect(m_selectButton, &QPushButton::clicked, this, &VisGroupEditor::onSelectMembers);
  connect(m_showAllButton, &QPushButton::clicked, this, &VisGroupEditor::onShowAll);
  connect(m_hideAllButton, &QPushButton::clicked, this, &VisGroupEditor::onHideAll);

  // two rows of three
  auto* row1 = new QHBoxLayout{};
  row1->setContentsMargins(0, 0, 0, 0);
  row1->addWidget(m_newButton);
  row1->addWidget(m_addButton);
  row1->addWidget(m_removeButton);

  auto* row2 = new QHBoxLayout{};
  row2->setContentsMargins(0, 0, 0, 0);
  row2->addWidget(m_selectButton);
  row2->addWidget(m_showAllButton);
  row2->addWidget(m_hideAllButton);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_list, 1);
  layout->addLayout(row1);
  layout->addLayout(row2);
  setLayout(layout);
}

bool VisGroupEditor::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonDblClick)
  {
    const auto pos = static_cast<QMouseEvent*>(event)->position().toPoint();
    if (auto* item = m_list->itemAt(pos))
    {
      m_list->setCurrentItem(item);

      if (item->data(PseudoRole).toBool())
      {
        return true; // pseudo rows: auto-colour swatch is display-only, no rename in v1
      }

      // The swatch (decoration icon) is drawn just right of the visibility checkbox. Treat a
      // double-click in [checkbox-end, checkbox-end + icon-width] as "recolor", the rest as
      // "rename". (A fixed left-edge threshold landed on the checkbox, never the swatch.)
      const auto itemLeft = m_list->visualItemRect(item).left();
      const auto checkW = m_list->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, m_list);
      const auto iconW = m_list->iconSize().width();
      if (pos.x() > itemLeft + checkW - 2 && pos.x() < itemLeft + checkW + iconW + 12)
      {
        onSetColor(); // double-clicked the color swatch
      }
      else
      {
        onRenameGroup(); // double-clicked the name
      }
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void VisGroupEditor::connectObservers()
{
  auto& map = m_document.map();
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect([this]() { reload(); });
  m_notifierConnection += map.visGroupsDidChangeNotifier.connect([this]() { reload(); });
  m_notifierConnection += m_document.nodesWereRemovedNotifier.connect(
    [this](const std::vector<mdl::Node*>&) { reload(); });
  // Newly created groups must appear as pseudo-group rows.
  m_notifierConnection += m_document.nodesWereAddedNotifier.connect(
    [this](const std::vector<mdl::Node*>&) { reload(); });
  m_notifierConnection +=
    m_document.selectionDidChangeNotifier.connect([this](const auto&) { updateButtons(); });
}

void VisGroupEditor::reload()
{
  m_updating = true;
  m_list->clear();

  const auto& manager = m_document.map().visGroupManager();
  for (const auto& group : manager.groups())
  {
    auto* item = new QListWidgetItem{};
    item->setText(QString{"%1 (%2)"}
                    .arg(QString::fromStdString(group.name))
                    .arg(manager.memberCount(group.id)));
    item->setIcon(makeColorIcon(group.color));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(group.visible ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(group.id));
    m_list->addItem(item);
  }

  // Pseudo-VisGroups: every GroupNode in the map, derived live (not stored). Each gets a distinct
  // auto-colour swatch + a visibility checkbox; toggling hides/shows the group's whole subtree.
  const auto groupNodes = mdl::collectGroups({&m_document.map().worldNode()});
  if (!groupNodes.empty())
  {
    auto* header = new QListWidgetItem{tr("Groups")};
    header->setFlags(Qt::NoItemFlags); // non-selectable, non-checkable separator
    m_list->addItem(header);

    for (auto* groupNode : groupNodes)
    {
      const auto autoColor =
        groupNode->persistentId()
          ? std::optional<Color>{mdl::autoGroupColor(*groupNode->persistentId())}
          : std::nullopt;

      auto* item = new QListWidgetItem{};
      item->setText(QString::fromStdString(groupNode->name()));
      item->setIcon(makeColorIcon(autoColor));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(
        manager.isPseudoGroupVisible(groupNode) ? Qt::Checked : Qt::Unchecked);
      item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(0));
      item->setData(PseudoRole, true);
      item->setData(
        NodePtrRole,
        QVariant::fromValue<qulonglong>(
          reinterpret_cast<qulonglong>(static_cast<mdl::Node*>(groupNode))));
      m_list->addItem(item);
    }
  }

  m_updating = false;
  updateButtons();
}

std::size_t VisGroupEditor::currentGroupId() const
{
  auto* item = m_list->currentItem();
  if (item == nullptr || item->data(PseudoRole).toBool())
  {
    return 0; // pseudo (derived-group) rows carry no real VisGroup id
  }
  return static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
}

void VisGroupEditor::updateButtons()
{
  const auto hasGroup = m_list->currentItem() != nullptr;
  const auto hasSelection = m_document.map().selection().hasNodes();
  const auto hasAnyGroups = !m_document.map().visGroupManager().groups().empty();
  m_addButton->setEnabled(hasGroup && hasSelection);
  m_removeButton->setEnabled(hasGroup && hasSelection);
  m_selectButton->setEnabled(hasGroup);
  m_showAllButton->setEnabled(hasAnyGroups);
  m_hideAllButton->setEnabled(hasAnyGroups);
}

void VisGroupEditor::onItemChanged(QListWidgetItem* item)
{
  if (m_updating || item == nullptr)
  {
    return;
  }
  const auto visible = item->checkState() == Qt::Checked;
  if (item->data(PseudoRole).toBool())
  {
    if (auto* node =
          reinterpret_cast<mdl::Node*>(item->data(NodePtrRole).toULongLong());
        node != nullptr)
    {
      mdl::setPseudoGroupVisible(m_document.map(), node, visible);
    }
    return;
  }
  const auto id = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
  mdl::setVisGroupVisible(m_document.map(), id, visible);
}

void VisGroupEditor::onNewGroup()
{
  auto ok = false;
  const auto name = QInputDialog::getText(
    this, tr("New VisGroup"), tr("Name:"), QLineEdit::Normal, tr("VisGroup"), &ok);
  if (ok && !name.isEmpty())
  {
    mdl::createVisGroup(m_document.map(), name.toStdString());
  }
}

void VisGroupEditor::onDeleteGroup()
{
  if (const auto id = currentGroupId(); id != 0)
  {
    mdl::deleteVisGroup(m_document.map(), id);
  }
}

void VisGroupEditor::onRenameGroup()
{
  const auto id = currentGroupId();
  if (id == 0)
  {
    return;
  }
  const auto* group = m_document.map().visGroupManager().group(id);
  const auto current = group ? QString::fromStdString(group->name) : QString{};

  auto ok = false;
  const auto name = QInputDialog::getText(
    this, tr("Rename VisGroup"), tr("Name:"), QLineEdit::Normal, current, &ok);
  if (ok && !name.isEmpty())
  {
    mdl::renameVisGroup(m_document.map(), id, name.toStdString());
  }
}

void VisGroupEditor::onAddSelected()
{
  if (const auto id = currentGroupId(); id != 0)
  {
    mdl::addSelectedToVisGroup(m_document.map(), id);
  }
}

void VisGroupEditor::onRemoveSelected()
{
  if (const auto id = currentGroupId(); id != 0)
  {
    mdl::removeSelectedFromVisGroup(m_document.map(), id);
  }
}

void VisGroupEditor::onSelectMembers()
{
  if (const auto id = currentGroupId(); id != 0)
  {
    mdl::selectVisGroupMembers(m_document.map(), id);
  }
}

void VisGroupEditor::onSetColor()
{
  const auto id = currentGroupId();
  if (id == 0)
  {
    return;
  }

  const auto* group = m_document.map().visGroupManager().group(id);
  const auto initial =
    (group && group->color) ? toQColor(*group->color) : QColor{Qt::white};

  const auto picked = QColorDialog::getColor(initial, this, tr("VisGroup Color"));
  if (picked.isValid())
  {
    mdl::setVisGroupColor(m_document.map(), id, fromQColor(picked));
  }
}

void VisGroupEditor::onShowAll()
{
  auto& map = m_document.map();
  for (const auto& group : map.visGroupManager().groups())
  {
    mdl::setVisGroupVisible(map, group.id, true);
  }
  reload(); // the visibility toggle doesn't fire visGroupsDidChange, so refresh the checkboxes
}

void VisGroupEditor::onHideAll()
{
  auto& map = m_document.map();
  for (const auto& group : map.visGroupManager().groups())
  {
    mdl::setVisGroupVisible(map, group.id, false);
  }
  reload();
}

} // namespace tb::ui
