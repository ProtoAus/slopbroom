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

#include "mdl/VisGroup.h"

#include <fmt/format.h>

#include <charconv>
#include <cmath>

namespace tb::mdl
{
namespace
{
std::optional<int> parseHexByte(const std::string_view s)
{
  auto value = 0;
  const auto* const end = s.data() + s.size();
  const auto result = std::from_chars(s.data(), end, value, 16);
  return (result.ec == std::errc{} && result.ptr == end) ? std::optional{value}
                                                         : std::nullopt;
}
} // namespace

std::string colorToHex(const Color& color)
{
  const auto rgb = color.to<RgbB>();
  return fmt::format(
    "{:02x}{:02x}{:02x}",
    static_cast<int>(rgb.get<ColorChannel::r>()),
    static_cast<int>(rgb.get<ColorChannel::g>()),
    static_cast<int>(rgb.get<ColorChannel::b>()));
}

std::optional<Color> colorFromHex(const std::string_view hex)
{
  if (hex.size() != 6)
  {
    return std::nullopt;
  }
  const auto r = parseHexByte(hex.substr(0, 2));
  const auto g = parseHexByte(hex.substr(2, 2));
  const auto b = parseHexByte(hex.substr(4, 2));
  if (!r || !g || !b)
  {
    return std::nullopt;
  }
  return Color{RgbB{
    static_cast<uint8_t>(*r), static_cast<uint8_t>(*g), static_cast<uint8_t>(*b)}};
}

Color autoGroupColor(const IdType persistentId)
{
  // Deterministic, well-distributed hue: the golden-ratio conjugate spreads consecutive ids far
  // apart on the colour wheel. Fixed saturation/value tuned for readable wireframe lines on the
  // dark viewport.
  constexpr auto saturation = 0.65f;
  constexpr auto value = 1.0f;
  const auto hue = static_cast<float>(
    std::fmod(static_cast<double>(persistentId) * 0.618033988749895, 1.0));

  // HSV -> RGB (standard 6-sector formula; ColorT has no HSV constructor).
  const auto h = hue * 6.0f;
  const auto sector = static_cast<int>(std::floor(h)) % 6;
  const auto f = h - std::floor(h);
  const auto p = value * (1.0f - saturation);
  const auto q = value * (1.0f - saturation * f);
  const auto t = value * (1.0f - saturation * (1.0f - f));

  auto r = value, g = value, b = value;
  switch (sector)
  {
  case 0:
    r = value, g = t, b = p;
    break;
  case 1:
    r = q, g = value, b = p;
    break;
  case 2:
    r = p, g = value, b = t;
    break;
  case 3:
    r = p, g = q, b = value;
    break;
  case 4:
    r = t, g = p, b = value;
    break;
  default: // 5
    r = value, g = p, b = q;
    break;
  }
  return Color{RgbF{r, g, b}};
}

} // namespace tb::mdl
