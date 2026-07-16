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

#include "mdl/CompiledBsp.h"

#include "Error.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace tb::mdl
{
namespace
{

// bsp29 lump indices
constexpr size_t LumpEntities = 0;
constexpr size_t LumpTextures = 2;
constexpr size_t LumpVertexes = 3;
constexpr size_t LumpTexinfo = 6;
constexpr size_t LumpFaces = 7;
constexpr size_t LumpLighting = 8;
constexpr size_t LumpEdges = 12;
constexpr size_t LumpSurfedges = 13;
constexpr size_t LumpCount = 15;

constexpr size_t AtlasSize = 2048;
constexpr size_t MaxAtlasPages = 16;

template <typename T>
T read(const std::vector<char>& data, const size_t offset)
{
  T result{};
  std::memcpy(&result, data.data() + offset, sizeof(T));
  return result;
}

struct Lump
{
  size_t offset;
  size_t length;
};

struct BspFace
{
  int32_t firstEdge;
  int16_t numEdges;
  int16_t texinfo;
  uint8_t styles[4];
  int32_t lightofs;
};

// The classic (non-decoupled) lightmap luxel size in texture units. Vanilla is 16, but an
// ericw `_lightmap_scale N` / `lightmap_scale N` worldspawn key changes it; the entity lump
// is our only record of N in a non-decoupled bsp, so parse worldspawn for it.
float classicLuxelScale(const std::vector<char>& entityText)
{
  const auto text = std::string_view{entityText.data(), entityText.size()};
  for (const auto* key : {"\"_lightmap_scale\"", "\"lightmap_scale\""})
  {
    if (const auto k = text.find(key); k != std::string_view::npos)
    {
      // value is the next quoted token after the key
      if (const auto vq = text.find('"', k + std::string_view{key}.size());
          vq != std::string_view::npos)
      {
        const auto ve = text.find('"', vq + 1);
        if (ve != std::string_view::npos)
        {
          const auto value = std::atof(std::string{text.substr(vq + 1, ve - vq - 1)}.c_str());
          if (value >= 1.0)
          {
            return float(value);
          }
        }
      }
    }
  }
  return 16.0f;
}

struct BspTexinfo
{
  float vecs[2][4];
  int32_t miptex;
  int32_t flags;
};

struct DecoupledLmFace
{
  uint16_t lmwidth;
  uint16_t lmheight;
  int32_t offset;
  float worldToLm[2][4];
};

/**
 * Packs lightmap blocks into 2048^2 pages using simple shelf allocation. Each block
 * gets a 1px replicated gutter so bilinear sampling can't bleed a neighbour in.
 */
class AtlasPacker
{
private:
  struct Shelf
  {
    size_t y;
    size_t height;
    size_t x;
  };

  std::vector<std::vector<Shelf>> m_pageShelves;
  std::vector<size_t> m_pageNextY;

public:
  std::vector<CompiledBspAtlasPage> pages;

  // returns {page, x, y} for the INNER (gutter-excluded) block or nullopt when full
  std::optional<std::tuple<size_t, size_t, size_t>> allocate(
    const size_t width, const size_t height)
  {
    const auto padded_w = width + 2;
    const auto padded_h = height + 2;
    if (padded_w > AtlasSize || padded_h > AtlasSize)
    {
      return std::nullopt;
    }

    for (size_t page = 0;; ++page)
    {
      if (page == pages.size())
      {
        if (page == MaxAtlasPages)
        {
          return std::nullopt;
        }
        pages.push_back(CompiledBspAtlasPage{
          AtlasSize, AtlasSize, std::vector<unsigned char>(AtlasSize * AtlasSize * 4)});
        m_pageShelves.emplace_back();
        m_pageNextY.push_back(0);
      }

      for (auto& shelf : m_pageShelves[page])
      {
        if (padded_h <= shelf.height && shelf.x + padded_w <= AtlasSize)
        {
          const auto result = std::tuple{page, shelf.x + 1, shelf.y + 1};
          shelf.x += padded_w;
          return result;
        }
      }
      if (m_pageNextY[page] + padded_h <= AtlasSize)
      {
        m_pageShelves[page].push_back(Shelf{m_pageNextY[page], padded_h, padded_w});
        const auto result = std::tuple{page, size_t{1}, m_pageNextY[page] + 1};
        m_pageNextY[page] += padded_h;
        return result;
      }
    }
  }

  void writeBlock(
    const size_t page,
    const size_t x,
    const size_t y,
    const size_t width,
    const size_t height,
    const std::vector<unsigned char>& rgb) // width*height*3
  {
    auto& dst = pages[page].rgba;
    // inner block + edge-replicated 1px gutter
    for (auto gy = ptrdiff_t{-1}; gy <= ptrdiff_t(height); ++gy)
    {
      const auto sy = size_t(std::clamp(gy, ptrdiff_t{0}, ptrdiff_t(height) - 1));
      for (auto gx = ptrdiff_t{-1}; gx <= ptrdiff_t(width); ++gx)
      {
        const auto sx = size_t(std::clamp(gx, ptrdiff_t{0}, ptrdiff_t(width) - 1));
        const auto src = (sy * width + sx) * 3;
        const auto dstOfs =
          ((y + size_t(gy + 1) - 1) * AtlasSize + (x + size_t(gx + 1) - 1)) * 4;
        dst[dstOfs + 0] = rgb[src + 0];
        dst[dstOfs + 1] = rgb[src + 1];
        dst[dstOfs + 2] = rgb[src + 2];
        dst[dstOfs + 3] = 255;
      }
    }
  }
};

} // namespace

Result<CompiledBsp> loadCompiledBsp(const std::filesystem::path& path)
{
  auto file = std::ifstream{path, std::ios::binary};
  if (!file)
  {
    return Error{"Could not open " + path.string()};
  }
  auto data = std::vector<char>{
    std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

  if (data.size() < 4 + LumpCount * 8 || read<int32_t>(data, 0) != 29)
  {
    return Error{"Not a bsp29 file: " + path.string()};
  }

  Lump lumps[LumpCount];
  for (size_t i = 0; i < LumpCount; ++i)
  {
    lumps[i] = Lump{
      size_t(read<int32_t>(data, 4 + i * 8)), size_t(read<int32_t>(data, 8 + i * 8))};
    if (lumps[i].offset + lumps[i].length > data.size())
    {
      return Error{"Corrupt bsp lump directory: " + path.string()};
    }
  }

  // ---- BSPX directory (right after the last lump, 4-aligned; scan a little) ----
  auto bspxLumps = std::map<std::string, Lump>{};
  {
    auto maxEnd = size_t{4 + LumpCount * 8};
    for (const auto& lump : lumps)
    {
      maxEnd = std::max(maxEnd, lump.offset + lump.length);
    }
    for (auto ofs = (maxEnd + 3) & ~size_t{3}; ofs + 8 <= data.size() && ofs < maxEnd + 64;
         ofs += 4)
    {
      if (std::memcmp(data.data() + ofs, "BSPX", 4) == 0)
      {
        const auto count = size_t(read<int32_t>(data, ofs + 4));
        for (size_t i = 0; i < count && ofs + 8 + (i + 1) * 32 <= data.size(); ++i)
        {
          const auto entry = ofs + 8 + i * 32;
          auto name = std::string{data.data() + entry};
          name.resize(std::min(name.size(), size_t{24}));
          const auto lumpOfs = size_t(read<int32_t>(data, entry + 24));
          const auto lumpLen = size_t(read<int32_t>(data, entry + 28));
          if (lumpOfs + lumpLen <= data.size())
          {
            bspxLumps[name] = Lump{lumpOfs, lumpLen};
          }
        }
        break;
      }
    }
  }

  // ---- lighting data: BSPX RGBLIGHTING (3 bytes/sample) or classic mono lump 8 ----
  const unsigned char* lightData = nullptr;
  auto lightSamples = size_t{0}; // count of RGB samples available
  auto lightIsRgb = false;
  if (const auto it = bspxLumps.find("RGBLIGHTING");
      it != bspxLumps.end() && it->second.length > 0)
  {
    lightData = reinterpret_cast<const unsigned char*>(data.data() + it->second.offset);
    lightSamples = it->second.length / 3;
    lightIsRgb = true;
  }
  else if (lumps[LumpLighting].length > 0)
  {
    lightData =
      reinterpret_cast<const unsigned char*>(data.data() + lumps[LumpLighting].offset);
    lightSamples = lumps[LumpLighting].length;
  }
  if (!lightData)
  {
    return Error{"No light data in " + path.string() + " (map not lit?)"};
  }

  // ---- geometry lumps ----
  const auto numVertexes = lumps[LumpVertexes].length / 12;
  const auto numEdges = lumps[LumpEdges].length / 4;
  const auto numSurfedges = lumps[LumpSurfedges].length / 4;
  const auto numTexinfo = lumps[LumpTexinfo].length / 40;
  const auto numFaces = lumps[LumpFaces].length / 20;

  // luxel size for the classic (non-decoupled) lightmap layout
  const auto luxelScale = classicLuxelScale(std::vector<char>(
    data.begin() + ptrdiff_t(lumps[LumpEntities].offset),
    data.begin() + ptrdiff_t(lumps[LumpEntities].offset + lumps[LumpEntities].length)));

  // decoupled lightmap records (40 bytes/face)
  const char* decoupled = nullptr;
  if (const auto it = bspxLumps.find("DECOUPLED_LM");
      it != bspxLumps.end() && it->second.length == numFaces * 40)
  {
    decoupled = data.data() + it->second.offset;
  }

  // miptex names
  auto textureNames = std::vector<std::string>{};
  if (lumps[LumpTextures].length >= 4)
  {
    const auto texBase = lumps[LumpTextures].offset;
    const auto numMiptex = size_t(read<int32_t>(data, texBase));
    for (size_t i = 0; i < numMiptex; ++i)
    {
      const auto dataOfs = read<int32_t>(data, texBase + 4 + i * 4);
      auto name = std::string{};
      if (dataOfs >= 0 && texBase + size_t(dataOfs) + 16 <= data.size())
      {
        name = std::string{data.data() + texBase + size_t(dataOfs)};
        name.resize(std::min(name.size(), size_t{15}));
      }
      textureNames.push_back(std::move(name));
    }
  }

  auto packer = AtlasPacker{};
  // 1x1 white block for faces without a lightmap (sky, water, overflow)
  const auto whiteBlock = packer.allocate(1, 1);
  if (whiteBlock)
  {
    packer.writeBlock(
      std::get<0>(*whiteBlock),
      std::get<1>(*whiteBlock),
      std::get<2>(*whiteBlock),
      1,
      1,
      {128, 128, 128}); // 0.5 * the shader's x2 overbright = fullbright
  }

  auto batches = std::map<std::pair<std::string, size_t>, CompiledBspBatch>{};

  const auto vertexAt = [&](const size_t index) {
    const auto ofs = lumps[LumpVertexes].offset + index * 12;
    return vm::vec3f{
      read<float>(data, ofs), read<float>(data, ofs + 4), read<float>(data, ofs + 8)};
  };

  const auto vec3 = [](const float* v) { return vm::vec3f{v[0], v[1], v[2]}; };

  for (size_t faceIndex = 0; faceIndex < numFaces; ++faceIndex)
  {
    const auto faceOfs = lumps[LumpFaces].offset + faceIndex * 20;
    auto face = BspFace{};
    face.firstEdge = read<int32_t>(data, faceOfs + 4);
    face.numEdges = read<int16_t>(data, faceOfs + 8);
    face.texinfo = read<int16_t>(data, faceOfs + 10);
    std::memcpy(face.styles, data.data() + faceOfs + 12, 4);
    face.lightofs = read<int32_t>(data, faceOfs + 16);

    if (
      face.numEdges < 3 || size_t(face.texinfo) >= numTexinfo || face.firstEdge < 0
      || size_t(face.firstEdge) + size_t(face.numEdges) > numSurfedges)
    {
      continue;
    }

    auto texinfo = BspTexinfo{};
    std::memcpy(
      &texinfo, data.data() + lumps[LumpTexinfo].offset + size_t(face.texinfo) * 40, 40);

    // gather the face polygon
    auto positions = std::vector<vm::vec3f>{};
    positions.reserve(size_t(face.numEdges));
    for (size_t i = 0; i < size_t(face.numEdges); ++i)
    {
      const auto surfedge = read<int32_t>(
        data, lumps[LumpSurfedges].offset + (size_t(face.firstEdge) + i) * 4);
      const auto edgeIndex = size_t(surfedge < 0 ? -surfedge : surfedge);
      if (edgeIndex >= numEdges)
      {
        positions.clear();
        break;
      }
      const auto edgeOfs = lumps[LumpEdges].offset + edgeIndex * 4;
      const auto v = surfedge < 0 ? read<uint16_t>(data, edgeOfs + 2)
                                  : read<uint16_t>(data, edgeOfs);
      if (size_t(v) >= numVertexes)
      {
        positions.clear();
        break;
      }
      positions.push_back(vertexAt(v));
    }
    if (positions.size() < 3)
    {
      continue;
    }

    // lightmap block: decoupled world-unit layout, else the classic 1/16 layout
    auto lmWidth = size_t{0};
    auto lmHeight = size_t{0};
    auto sampleOfs = ptrdiff_t{-1};
    auto lmCoord = std::vector<vm::vec2f>{}; // luxel-space coords per vertex
    lmCoord.reserve(positions.size());

    if (decoupled)
    {
      auto rec = DecoupledLmFace{};
      std::memcpy(&rec, decoupled + faceIndex * 40, 40);
      lmWidth = rec.lmwidth;
      lmHeight = rec.lmheight;
      sampleOfs = rec.offset;
      for (const auto& pos : positions)
      {
        lmCoord.emplace_back(
          rec.worldToLm[0][0] * pos.x() + rec.worldToLm[0][1] * pos.y()
            + rec.worldToLm[0][2] * pos.z() + rec.worldToLm[0][3],
          rec.worldToLm[1][0] * pos.x() + rec.worldToLm[1][1] * pos.y()
            + rec.worldToLm[1][2] * pos.z() + rec.worldToLm[1][3]);
      }
    }
    else
    {
      auto mins =
        vm::vec2f{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
      auto maxs = vm::vec2f{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
      for (const auto& pos : positions)
      {
        const auto s = vm::dot(pos, vec3(texinfo.vecs[0])) + texinfo.vecs[0][3];
        const auto t = vm::dot(pos, vec3(texinfo.vecs[1])) + texinfo.vecs[1][3];
        lmCoord.emplace_back(s, t);
        mins = vm::min(mins, lmCoord.back());
        maxs = vm::max(maxs, lmCoord.back());
      }
      const auto bminX = std::floor(mins.x() / luxelScale);
      const auto bminY = std::floor(mins.y() / luxelScale);
      lmWidth = size_t(std::ceil(maxs.x() / luxelScale) - bminX) + 1;
      lmHeight = size_t(std::ceil(maxs.y() / luxelScale) - bminY) + 1;
      // face.lightofs < 0 = no vanilla table (e.g. -novanilla): leave sampleOfs -1 so the
      // face falls back to the grey block below instead of misreading garbage.
      sampleOfs = face.lightofs;
      for (auto& coord : lmCoord)
      {
        coord = vm::vec2f{
          coord.x() / luxelScale - bminX, coord.y() / luxelScale - bminY};
      }
    }

    // place the block in the atlas (fullbright fallback when unlit or oversized)
    auto page = whiteBlock ? std::get<0>(*whiteBlock) : size_t{0};
    auto atlasX = whiteBlock ? std::get<1>(*whiteBlock) : size_t{0};
    auto atlasY = whiteBlock ? std::get<2>(*whiteBlock) : size_t{0};
    auto lit = false;

    if (
      sampleOfs >= 0 && lmWidth > 0 && lmHeight > 0 && face.styles[0] != 255
      && size_t(sampleOfs) + lmWidth * lmHeight <= lightSamples)
    {
      if (const auto block = packer.allocate(lmWidth, lmHeight))
      {
        auto rgb = std::vector<unsigned char>(lmWidth * lmHeight * 3);
        if (lightIsRgb)
        {
          std::memcpy(
            rgb.data(), lightData + size_t(sampleOfs) * 3, lmWidth * lmHeight * 3);
        }
        else
        {
          for (size_t i = 0; i < lmWidth * lmHeight; ++i)
          {
            const auto v = lightData[size_t(sampleOfs) + i];
            rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = v;
          }
        }
        page = std::get<0>(*block);
        atlasX = std::get<1>(*block);
        atlasY = std::get<2>(*block);
        packer.writeBlock(page, atlasX, atlasY, lmWidth, lmHeight, rgb);
        lit = true;
      }
    }

    // batch key + triangle fan emission
    const auto materialName = size_t(texinfo.miptex) < textureNames.size()
                                ? textureNames[size_t(texinfo.miptex)]
                                : std::string{};
    auto& batch = batches[{materialName, page}];
    batch.materialName = materialName;
    batch.atlasPage = page;

    const auto makeVertex = [&](const size_t i) {
      auto lmUV = vm::vec2f{0.5f, 0.5f}; // centre of the 1x1 white block
      if (lit)
      {
        lmUV = lmCoord[i];
      }
      // half-luxel centring, then into the atlas
      return CompiledBspVertex{
        positions[i],
        vm::vec2f{
          vm::dot(positions[i], vec3(texinfo.vecs[0])) + texinfo.vecs[0][3],
          vm::dot(positions[i], vec3(texinfo.vecs[1])) + texinfo.vecs[1][3]},
        vm::vec2f{
          (float(atlasX) + lmUV.x() + (lit ? 0.5f : 0.0f)) / float(AtlasSize),
          (float(atlasY) + lmUV.y() + (lit ? 0.5f : 0.0f)) / float(AtlasSize)}};
    };

    for (size_t i = 1; i + 1 < positions.size(); ++i)
    {
      batch.vertices.push_back(makeVertex(0));
      batch.vertices.push_back(makeVertex(i));
      batch.vertices.push_back(makeVertex(i + 1));
    }
  }

  auto result = CompiledBsp{};
  result.atlasPages = std::move(packer.pages);
  for (auto& [key, batch] : batches)
  {
    result.batches.push_back(std::move(batch));
  }
  return result;
}

} // namespace tb::mdl
