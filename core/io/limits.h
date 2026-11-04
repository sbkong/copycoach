// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstddef>

// Upper bounds for untrusted input (coding-standards.md section 19): one place, so a parser that hits one
// reports FileCorrupt instead of allocating without end. Generous for real songs, tight for junk.
namespace cc::io
{

constexpr std::size_t maxTracks = 32;
constexpr std::size_t maxMeasures = 4096;
constexpr std::size_t maxBeatsPerMeasure = 256;
constexpr std::size_t maxNotesPerBeat = 12;
constexpr std::size_t maxChordsPerMeasure = 32;
constexpr std::size_t maxListItems = 4096; // sources, sync points, sections, ranges, phrases, keys
constexpr std::size_t maxNameBytes = 1024; // names, labels, paths, chord text
constexpr std::size_t maxTextBytes = std::size_t{1024} * 1024; // memos, lyrics

} // namespace cc::io
