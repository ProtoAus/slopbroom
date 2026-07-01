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

#include "mdl/VisGroupManager.h"

#include <algorithm>

namespace tb::mdl
{

VisGroupManager::VisGroupManager() = default;
VisGroupManager::~VisGroupManager() = default;

const std::vector<VisGroup>& VisGroupManager::groups() const
{
  return m_groups;
}

const VisGroup* VisGroupManager::group(const IdType id) const
{
  const auto it = std::find_if(
    m_groups.begin(), m_groups.end(), [&](const auto& g) { return g.id == id; });
  return it != m_groups.end() ? &*it : nullptr;
}

bool VisGroupManager::hasGroup(const IdType id) const
{
  return group(id) != nullptr;
}

IdType VisGroupManager::createGroup(std::string name)
{
  const auto id = m_nextId++;
  m_groups.push_back(VisGroup{id, std::move(name), std::nullopt, true});
  return id;
}

void VisGroupManager::addGroup(VisGroup group)
{
  m_nextId = std::max(m_nextId, group.id + 1);
  m_groups.push_back(std::move(group));
  rebuildHiddenSet();
}

void VisGroupManager::deleteGroup(const IdType id)
{
  m_groups.erase(
    std::remove_if(
      m_groups.begin(), m_groups.end(), [&](const auto& g) { return g.id == id; }),
    m_groups.end());

  for (auto it = m_membership.begin(); it != m_membership.end();)
  {
    it->second.erase(id);
    it = it->second.empty() ? m_membership.erase(it) : std::next(it);
  }
  rebuildHiddenSet();
}

void VisGroupManager::renameGroup(const IdType id, std::string name)
{
  for (auto& g : m_groups)
  {
    if (g.id == id)
    {
      g.name = std::move(name);
      return;
    }
  }
}

void VisGroupManager::setGroupColor(const IdType id, std::optional<Color> color)
{
  for (auto& g : m_groups)
  {
    if (g.id == id)
    {
      g.color = std::move(color);
      return;
    }
  }
}

void VisGroupManager::setGroupVisible(const IdType id, const bool visible)
{
  for (auto& g : m_groups)
  {
    if (g.id == id)
    {
      if (g.visible != visible)
      {
        g.visible = visible;
        rebuildHiddenSet();
      }
      return;
    }
  }
}

std::set<IdType> VisGroupManager::membership(const Node* node) const
{
  const auto it = m_membership.find(node);
  return it != m_membership.end() ? it->second : std::set<IdType>{};
}

void VisGroupManager::setMembership(const Node* node, std::set<IdType> ids)
{
  if (ids.empty())
  {
    m_membership.erase(node);
  }
  else
  {
    m_membership[node] = std::move(ids);
  }
  rebuildHiddenSet();
}

void VisGroupManager::addMembers(const std::vector<Node*>& nodes, const IdType id)
{
  for (auto* node : nodes)
  {
    m_membership[node].insert(id);
  }
  rebuildHiddenSet();
}

void VisGroupManager::removeMembers(const std::vector<Node*>& nodes, const IdType id)
{
  for (auto* node : nodes)
  {
    const auto it = m_membership.find(node);
    if (it != m_membership.end())
    {
      it->second.erase(id);
      if (it->second.empty())
      {
        m_membership.erase(it);
      }
    }
  }
  rebuildHiddenSet();
}

std::vector<const Node*> VisGroupManager::members(const IdType id) const
{
  auto result = std::vector<const Node*>{};
  for (const auto& [node, ids] : m_membership)
  {
    if (ids.count(id) != 0)
    {
      result.push_back(node);
    }
  }
  return result;
}

size_t VisGroupManager::memberCount(const IdType id) const
{
  auto count = size_t{0};
  for (const auto& [node, ids] : m_membership)
  {
    if (ids.count(id) != 0)
    {
      ++count;
    }
  }
  return count;
}

const std::unordered_map<const Node*, std::set<IdType>>& VisGroupManager::
  allMemberships() const
{
  return m_membership;
}

std::optional<Color> VisGroupManager::nodeColor(const Node* node) const
{
  // membership() returns a std::set (ordered) → this is the lowest-id colored group.
  for (const auto id : membership(node))
  {
    if (const auto* g = group(id); g != nullptr && g->color.has_value())
    {
      return g->color;
    }
  }
  return std::nullopt;
}

bool VisGroupManager::isHidden(const Node& node) const
{
  return m_hiddenNodes.find(&node) != m_hiddenNodes.end();
}

bool VisGroupManager::isPseudoGroupVisible(const Node* groupNode) const
{
  return m_hiddenPseudoGroups.find(groupNode) == m_hiddenPseudoGroups.end();
}

void VisGroupManager::setPseudoGroupVisible(const Node* groupNode, const bool visible)
{
  if (visible)
  {
    m_hiddenPseudoGroups.erase(groupNode);
  }
  else
  {
    m_hiddenPseudoGroups.insert(groupNode);
  }
  rebuildHiddenSet();
}

const std::unordered_set<const Node*>& VisGroupManager::hiddenPseudoGroups() const
{
  return m_hiddenPseudoGroups;
}

void VisGroupManager::nodeWillBeRemoved(const Node* node)
{
  m_membership.erase(node);
  m_hiddenNodes.erase(node);
  m_hiddenPseudoGroups.erase(node);
}

void VisGroupManager::clear()
{
  m_groups.clear();
  m_membership.clear();
  m_hiddenNodes.clear();
  m_hiddenPseudoGroups.clear();
  m_nextId = 1;
}

void VisGroupManager::rebuildHiddenSet()
{
  m_hiddenNodes.clear();

  // Pseudo-hidden group nodes hide unconditionally (independent of real-visgroup membership);
  // EditorContext's ancestor walk then hides each hidden group's whole subtree.
  for (const auto* node : m_hiddenPseudoGroups)
  {
    m_hiddenNodes.insert(node);
  }

  auto hiddenGroups = std::set<IdType>{};
  for (const auto& g : m_groups)
  {
    if (!g.visible)
    {
      hiddenGroups.insert(g.id);
    }
  }
  if (hiddenGroups.empty())
  {
    return;
  }

  for (const auto& [node, ids] : m_membership)
  {
    for (const auto id : ids)
    {
      if (hiddenGroups.count(id) != 0)
      {
        m_hiddenNodes.insert(node);
        break;
      }
    }
  }
}

} // namespace tb::mdl
