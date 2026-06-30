/*
 Copyright (C) 2026 Kristian Duske

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

#include "fs/TestEnvironment.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_VisGroups.h"
#include "mdl/Node.h"
#include "mdl/VisGroup.h"
#include "mdl/VisGroupManager.h"

#include "kd/k.h"
#include "kd/overload.h"

#include "vm/bbox.h"

#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{
namespace
{
// Find the (only) point entity in the default layer whose `key`==`value`.
const EntityNode* findEntity(const Map& map, const std::string& key, const std::string& value)
{
  for (const auto* child : map.worldNode().defaultLayer()->children())
  {
    if (const auto* entityNode = dynamic_cast<const EntityNode*>(child))
    {
      if (const auto* v = entityNode->entity().property(key); v != nullptr && *v == value)
      {
        return entityNode;
      }
    }
  }
  return nullptr;
}

// The first BrushNode child of the default layer.
const BrushNode* firstWorldBrush(const Map& map)
{
  for (const auto* child : map.worldNode().defaultLayer()->children())
  {
    if (const auto* brushNode = dynamic_cast<const BrushNode*>(child))
    {
      return brushNode;
    }
  }
  return nullptr;
}
} // namespace

TEST_CASE("Map_VisGroups_Persistence")
{
  const auto worldBounds = vm::bbox3d{8192.0};

  SECTION("group defs + entity + raw-brush membership round-trip through save/load")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "test.map";

    // group ids assigned by the manager (m_nextId starts at 1)
    auto idA = IdType{};
    auto idB = IdType{};

    {
      auto fixture = MapFixture{};
      auto& map = fixture.create(QuakeFixtureConfig);

      auto* entityNode = new EntityNode{Entity{{{"classname", "info_null"}, {"tag", "e1"}}}};

      auto builder = BrushBuilder{map.worldNode().mapFormat(), worldBounds};
      auto* brushNode = new BrushNode{builder.createCube(64.0, "none") | kdl::value()};

      addNodes(map, {{parentForNodes(map), {entityNode, brushNode}}});

      idA = createVisGroup(map, "Lighting");
      idB = createVisGroup(map, "Detail");
      setVisGroupVisible(map, idB, false);                          // B hidden
      map.visGroupManager().setGroupColor(idA, Color{RgbB{255, 128, 0}}); // A coloured
      // B intentionally left with no colour

      // entity in A; world brush in A and B
      map.visGroupManager().setMembership(entityNode, std::set<IdType>{idA});
      map.visGroupManager().setMembership(brushNode, std::set<IdType>{idA, idB});

      REQUIRE(map.saveAs(path));
    }

    // Reload into a fresh map — this runs Map::loadVisGroups().
    auto fixture2 = MapFixture{};
    auto& map2 = fixture2.load(path, QuakeFixtureConfig);
    const auto& mgr = map2.visGroupManager();

    // defs
    REQUIRE(mgr.groups().size() == 2u);
    REQUIRE(mgr.group(idA) != nullptr);
    REQUIRE(mgr.group(idB) != nullptr);
    CHECK(mgr.group(idA)->name == "Lighting");
    CHECK(mgr.group(idA)->visible == true);
    CHECK(mgr.group(idB)->name == "Detail");
    CHECK(mgr.group(idB)->visible == false);

    // colour round-trips for A (hex), stays empty for B
    REQUIRE(mgr.group(idA)->color.has_value());
    CHECK(colorToHex(*mgr.group(idA)->color) == "ff8000");
    CHECK(mgr.group(idB)->color == std::nullopt);

    // the runtime tree must carry no visgroup metadata after load
    CHECK(map2.worldNode().entity().property(EntityPropertyKeys::TbVisGroupBrushes) == nullptr);

    // entity membership (inline _tb_visgroups)
    const auto* entity2 = findEntity(map2, "tag", "e1");
    REQUIRE(entity2 != nullptr);
    CHECK(entity2->entity().property(EntityPropertyKeys::TbVisGroups) == nullptr); // stripped
    CHECK(mgr.membership(entity2) == std::set<IdType>{idA});

    // raw-brush membership (worldspawn cid/ord table)
    const auto* brush2 = firstWorldBrush(map2);
    REQUIRE(brush2 != nullptr);
    CHECK(mgr.membership(brush2) == std::set<IdType>{idA, idB});
  }

  SECTION("a visgroup-free map writes no _tb_visgroup properties")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "empty.map";

    auto fixture = MapFixture{};
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* entityNode = new EntityNode{Entity{{{"classname", "info_null"}}}};
    addNodes(map, {{parentForNodes(map), {entityNode}}});

    REQUIRE(map.saveAs(path));

    const auto contents = env.loadFile(path);
    CHECK(contents.find("_tb_visgroup") == std::string::npos);
  }
}

} // namespace tb::mdl
