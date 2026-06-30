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

#include "ui/EntityReportModel.h"

#include <QFont>

#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"

#include "kd/overload.h"

namespace tb::ui
{
namespace
{

QString propOrEmpty(const mdl::EntityNodeBase& node, const std::string& key)
{
  const auto* value = node.entity().property(key);
  return value ? QString::fromStdString(*value) : QString{};
}

} // namespace

// EntityReportModel

EntityReportModel::EntityReportModel(MapDocument& document, QObject* parent)
  : QAbstractTableModel{parent}
  , m_document{document}
{
}

void EntityReportModel::updateFromMap()
{
  beginResetModel();
  m_entities.clear();

  // Walk the whole tree; collect every point + brush entity. WorldNode (worldspawn) is
  // deliberately not pushed. A brush entity is an EntityNode with BrushNode children, so
  // the leaf arms below are reached but do nothing.
  m_document.map().worldNode().accept(kdl::overload(
    [&](auto&& self, mdl::WorldNode& worldNode) { worldNode.visitChildren(self); },
    [&](auto&& self, mdl::LayerNode& layerNode) { layerNode.visitChildren(self); },
    [&](auto&& self, mdl::GroupNode& groupNode) { groupNode.visitChildren(self); },
    [&](auto&& self, mdl::EntityNode& entityNode) {
      m_entities.push_back(&entityNode);
      entityNode.visitChildren(self);
    },
    [&](mdl::BrushNode&) {},
    [&](mdl::PatchNode&) {}));

  endResetModel();
}

const std::vector<mdl::EntityNodeBase*>& EntityReportModel::entities() const
{
  return m_entities;
}

mdl::EntityNodeBase* EntityReportModel::entityAtRow(const int row) const
{
  return (row >= 0 && row < static_cast<int>(m_entities.size()))
           ? m_entities[static_cast<size_t>(row)]
           : nullptr;
}

int EntityReportModel::rowCount(const QModelIndex& parent) const
{
  return parent.isValid() ? 0 : static_cast<int>(m_entities.size());
}

int EntityReportModel::columnCount(const QModelIndex& parent) const
{
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant EntityReportModel::data(const QModelIndex& index, const int role) const
{
  auto* node = entityAtRow(index.row());
  if (!node)
  {
    return QVariant{};
  }

  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
    case ColumnClass:
      return QString::fromStdString(node->entity().classname());
    case ColumnName:
      return propOrEmpty(*node, mdl::EntityPropertyKeys::Targetname);
    case ColumnTarget:
      return propOrEmpty(*node, mdl::EntityPropertyKeys::Target);
    default:
      return QVariant{};
    }
  }
  if (role == Qt::FontRole && node->hidden())
  {
    // hidden entities are shown italic (matches the issue browser)
    auto italicFont = QFont{};
    italicFont.setItalic(true);
    return italicFont;
  }
  return QVariant{};
}

QVariant EntityReportModel::headerData(
  const int section, const Qt::Orientation orientation, const int role) const
{
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
  {
    return QVariant{};
  }
  switch (section)
  {
  case ColumnClass:
    return tr("Class");
  case ColumnName:
    return tr("Name");
  case ColumnTarget:
    return tr("Target");
  default:
    return QVariant{};
  }
}

// EntityReportFilterProxyModel

EntityReportFilterProxyModel::EntityReportFilterProxyModel(QObject* parent)
  : QSortFilterProxyModel{parent}
{
}

EntityReportModel* EntityReportFilterProxyModel::sourceReportModel() const
{
  return qobject_cast<EntityReportModel*>(sourceModel());
}

void EntityReportFilterProxyModel::setTypeFilter(const EntityTypeFilter typeFilter)
{
  m_typeFilter = typeFilter;
  invalidateFilter();
}

void EntityReportFilterProxyModel::setClassFilter(const QString& classname)
{
  m_classFilter = classname;
  invalidateFilter();
}

void EntityReportFilterProxyModel::setKeyValueFilter(const QString& text)
{
  m_keyValueFilter = text;
  invalidateFilter();
}

void EntityReportFilterProxyModel::setExactMatch(const bool exact)
{
  m_exact = exact;
  invalidateFilter();
}

void EntityReportFilterProxyModel::setIncludeHidden(const bool includeHidden)
{
  m_includeHidden = includeHidden;
  invalidateFilter();
}

bool EntityReportFilterProxyModel::filterAcceptsRow(
  const int sourceRow, const QModelIndex& /* sourceParent */) const
{
  auto* model = sourceReportModel();
  if (!model)
  {
    return true;
  }
  auto* node = model->entityAtRow(sourceRow);
  if (!node)
  {
    return false;
  }
  const auto& entity = node->entity();

  if (!m_includeHidden && node->hidden())
  {
    return false;
  }

  const auto isPoint = entity.pointEntity();
  if (m_typeFilter == EntityTypeFilter::PointEntities && !isPoint)
  {
    return false;
  }
  if (m_typeFilter == EntityTypeFilter::BrushEntities && isPoint)
  {
    return false;
  }

  if (
    !m_classFilter.isEmpty()
    && QString::fromStdString(entity.classname())
         .compare(m_classFilter, Qt::CaseInsensitive)
         != 0)
  {
    return false;
  }

  if (!m_keyValueFilter.isEmpty())
  {
    auto match = false;
    for (const auto& property : entity.properties())
    {
      const auto key = QString::fromStdString(property.key());
      const auto value = QString::fromStdString(property.value());
      const auto hit =
        m_exact ? (key.compare(m_keyValueFilter, Qt::CaseInsensitive) == 0
                   || value.compare(m_keyValueFilter, Qt::CaseInsensitive) == 0)
                : (key.contains(m_keyValueFilter, Qt::CaseInsensitive)
                   || value.contains(m_keyValueFilter, Qt::CaseInsensitive));
      if (hit)
      {
        match = true;
        break;
      }
    }
    if (!match)
    {
      return false;
    }
  }

  return true;
}

bool EntityReportFilterProxyModel::lessThan(
  const QModelIndex& left, const QModelIndex& right) const
{
  const auto leftText = sourceModel()->data(left, Qt::DisplayRole).toString();
  const auto rightText = sourceModel()->data(right, Qt::DisplayRole).toString();
  return leftText.compare(rightText, Qt::CaseInsensitive) < 0;
}

} // namespace tb::ui
