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

#include "vm/vec.h"

#include <string>

namespace tb::mdl
{
class Map;

enum class PasteType;

std::string serializeSelectedNodes(Map& map);
std::string serializeSelectedBrushFaces(Map& map);

PasteType paste(Map& map, const std::string& str);

/**
 * Options for pasteSpecial (Hammer-style "Paste Special"): paste the clipboard N times
 * with an accumulative per-copy offset and rotation, optionally grouped, optionally with
 * uniqued entity names / a name prefix.
 */
struct PasteSpecialOptions
{
  int numCopies = 1;
  /** rotate each copy about the source selection's centre (true) or its min corner (false) */
  bool startAtCenter = true;
  /** collect all pasted copies into a single group */
  bool groupCopies = false;
  /** accumulative world-space translation; copy k is shifted by offset*k */
  vm::vec3d offset = vm::vec3d{0, 0, 0};
  /** accumulative rotation in Euler degrees (X, Y, Z); copy k is rotated by rotation*k */
  vm::vec3d rotation = vm::vec3d{0, 0, 0};
  /** bump each pasted targetname to a value unique across the map, rewiring intra-copy links */
  bool makeNamesUnique = true;
  /** if non-empty, prepend to every pasted entity's targetname */
  std::string namePrefix;
};

/**
 * Pastes the given serialized nodes numCopies times, applying the accumulative transform,
 * name-uniquification and grouping described by options. The whole operation is wrapped in
 * a single undoable transaction. The source selection (used as the rotation pivot) must be
 * current when this is called. Returns PasteType::Node on success.
 */
PasteType pasteSpecial(
  Map& map, const std::string& str, const PasteSpecialOptions& options);


} // namespace tb::mdl
