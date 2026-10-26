// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstdint>
#include <random>
#include <string>

// Stable element ids (score-data-path.md section 2): 8 base32 characters, unique within one project, never reused.
namespace cc::model
{

using Id = std::string;

class IdGenerator
{
public:
    IdGenerator(); // random seed
    static IdGenerator seeded(std::uint64_t seed); // deterministic ids for tests and golden files

    Id next();

private:
    explicit IdGenerator(std::uint64_t seed);
    std::mt19937_64 m_engine;
};

/// True when `id` has the generated shape (8 lowercase base32 characters).
bool isWellFormedId(const Id &id);

} // namespace cc::model
