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

} // namespace tb::mdl
