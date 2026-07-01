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

#include "Color.h"
#include "mdl/IdType.h"
#include "mdl/VisGroup.h"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tb::mdl
{
class Node;

/**
 * Owns the map's VisGroups — Hammer-style named, multi-membership visibility groups.
 * Owned by Map (mirrors TagManager / EditorContext).
 *
 * The single node visibility bit (Hidden/Shown/Inherited) is used by Layers and cannot
 * express multi-membership, so VisGroups are a PARALLEL filter: membership is a per-node
 * set of group ids, and a node is "hidden by a visgroup" if it belongs to ANY group whose
 * `visible` flag is off. `EditorContext::visible()` consults `isHidden()`, which reads the
 * precomputed `m_hiddenNodes` set (rebuilt only when a group's visibility or a node's
 * membership changes — so the per-frame render/pick query is one hash-set lookup).
 *
 * This class is pure model state with no notifiers: the undoable command and the
 * Map_VisGroups facade mutate it and then fire the Map notifiers
 * (editorContextDidChange for the renderer, visGroupsDidChange for the panel).
 */
class VisGroupManager
{
private:
  std::vector<VisGroup> m_groups;
  IdType m_nextId = 1;
  std::unordered_map<const Node*, std::set<IdType>> m_membership;
  std::unordered_set<const Node*> m_hiddenNodes;
  std::unordered_set<const Node*> m_hiddenPseudoGroups;

public:
  VisGroupManager();
  ~VisGroupManager();

  // group definitions
  const std::vector<VisGroup>& groups() const;
  const VisGroup* group(IdType id) const;
  bool hasGroup(IdType id) const;

  IdType createGroup(std::string name);
  /** Insert a group with a caller-supplied id (used when loading a saved map). */
  void addGroup(VisGroup group);
  void deleteGroup(IdType id); // also purges membership of this id everywhere
  void renameGroup(IdType id, std::string name);
  void setGroupColor(IdType id, std::optional<Color> color);
  void setGroupVisible(IdType id, bool visible); // recomputes the hidden set

  // membership (a node -> the set of group ids it belongs to)
  std::set<IdType> membership(const Node* node) const;
  void setMembership(const Node* node, std::set<IdType> ids);
  void addMembers(const std::vector<Node*>& nodes, IdType id);
  void removeMembers(const std::vector<Node*>& nodes, IdType id);
  std::vector<const Node*> members(IdType id) const;
  size_t memberCount(IdType id) const;

  /** All node→group-ids entries (for serialization on save). */
  const std::unordered_map<const Node*, std::set<IdType>>& allMemberships() const;

  /** Color of the lowest-id visgroup this node belongs to that has a color set; nullopt if
   * none. Used by the renderer to tint a node's wireframe + label by its visgroup color. */
  std::optional<Color> nodeColor(const Node* node) const;

  /** True if the node belongs to at least one group whose visible flag is off. */
  bool isHidden(const Node& node) const;

  // Pseudo-VisGroups: every GroupNode is implicitly its own toggleable group (NOT a real
  // VisGroup — no membership, no color). Toggling one off adds the group node to the hidden set,
  // so EditorContext's ancestor walk hides its whole subtree. Visible by default; only the
  // hidden group nodes are stored.
  bool isPseudoGroupVisible(const Node* groupNode) const;
  void setPseudoGroupVisible(const Node* groupNode, bool visible); // recomputes the hidden set
  /** GroupNodes currently pseudo-hidden (for serialization on save). */
  const std::unordered_set<const Node*>& hiddenPseudoGroups() const;

  /** Drop a node from all bookkeeping (call when the node leaves the tree). */
  void nodeWillBeRemoved(const Node* node);

  /** Forget all groups + membership (new map / before load). */
  void clear();

private:
  void rebuildHiddenSet();
};

} // namespace tb::mdl
