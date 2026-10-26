// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/id.h"

#include <algorithm>
#include <string_view>

namespace cc::model
{

namespace
{

constexpr std::string_view alphabet = "0123456789abcdefghjkmnpqrstvwxyz"; // Crockford base32, lowercase
constexpr std::size_t idLength = 8;
constexpr unsigned bitsPerChar = 5;
constexpr std::uint64_t charMask = 0x1FU;

} // namespace

IdGenerator::IdGenerator()
    : IdGenerator(std::random_device{}())
{
}

IdGenerator::IdGenerator(std::uint64_t seed)
    : m_engine(seed)
{
}

IdGenerator IdGenerator::seeded(std::uint64_t seed)
{
    return IdGenerator(seed);
}

Id IdGenerator::next()
{
    std::uint64_t bits = m_engine();
    Id id(idLength, '0');
    for (char &c : id) {
        c = alphabet[static_cast<std::size_t>(bits & charMask)];
        bits >>= bitsPerChar;
    }
    return id;
}

bool isWellFormedId(const Id &id)
{
    return id.size() == idLength && std::ranges::all_of(id, [](char c) {
               return alphabet.contains(c);
           });
}

} // namespace cc::model
