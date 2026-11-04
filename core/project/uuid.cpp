// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/project/uuid.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <random>

namespace cc::project
{

namespace
{

constexpr std::size_t uuidLength = 36;
constexpr std::array<std::size_t, 4> hyphenAt{8, 13, 18, 23};

} // namespace

std::string newUuidV4()
{
    std::random_device device;
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const std::uint32_t word = device();
        for (std::size_t k = 0; k < 4; ++k) {
            bytes[i + k] = static_cast<std::uint8_t>(word >> (8 * k));
        }
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U); // version 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U); // RFC 4122 variant
    std::string out;
    out.reserve(uuidLength);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out += '-';
        }
        out += std::format("{:02x}", bytes[i]);
    }
    return out;
}

bool isUuid(std::string_view text)
{
    if (text.size() != uuidLength) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool hyphen = i == hyphenAt[0] || i == hyphenAt[1] || i == hyphenAt[2] || i == hyphenAt[3];
        const auto c = static_cast<unsigned char>(text[i]);
        if (hyphen ? c != '-' : std::isxdigit(c) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace cc::project
