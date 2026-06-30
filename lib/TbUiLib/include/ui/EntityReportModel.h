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

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QString>

#include <vector>

namespace tb::mdl
{
class EntityNodeBase;
} // namespace tb::mdl

namespace tb::ui
{
class MapDocument;

/**
 * Flat table of every entity in the map (Class / Name / Target), as listed by the
 * Entity Report window. Worldspawn is intentionally omitted. The whole list is rebuilt
 * via begin/endResetModel on any map change, so the dialog must never cache row pointers
 * across an event-loop turn.
 */
class EntityReportModel : public QAbstractTableModel
{
  Q_OBJECT
public:
  enum Column
  {
    ColumnClass = 0,
    ColumnName = 1,
    ColumnTarget = 2,
    ColumnCount = 3
  };

  explicit EntityReportModel(MapDocument& document, QObject* parent = nullptr);

  /** Rebuild the row list from the current map (full reset). */
  void updateFromMap();

  const std::vector<mdl::EntityNodeBase*>& entities() const;
  mdl::EntityNodeBase* entityAtRow(int row) const;

  int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
  int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  MapDocument& m_document;
  std::vector<mdl::EntityNodeBase*> m_entities;
};

enum class EntityTypeFilter
{
  All,
  BrushEntities,
  PointEntities
};

/**
 * Multi-criteria filter for the Entity Report table. Qt's built-in single-string filter
 * is insufficient, so filterAcceptsRow() AND-combines: include-hidden gate, entity-type
 * radio, by-class dropdown, and a by-key/value text match (any key OR value, substring or
 * exact, case-insensitive). lessThan() sorts each column case-insensitively.
 */
class EntityReportFilterProxyModel : public QSortFilterProxyModel
{
  Q_OBJECT
public:
  explicit EntityReportFilterProxyModel(QObject* parent = nullptr);

  void setTypeFilter(EntityTypeFilter typeFilter);
  void setClassFilter(const QString& classname); // empty == any class
  void setKeyValueFilter(const QString& text);
  void setExactMatch(bool exact);
  void setIncludeHidden(bool includeHidden);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
  EntityReportModel* sourceReportModel() const;

  EntityTypeFilter m_typeFilter = EntityTypeFilter::All;
  QString m_classFilter;
  QString m_keyValueFilter;
  bool m_exact = false;
  bool m_includeHidden = false;
};

} // namespace tb::ui
