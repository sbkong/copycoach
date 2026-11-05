// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/score/musicxmlcontext.h"

#include <array>
#include <string_view>
#include <utility>

// <harmony> -> chord symbol text in the spelling theory::parseChordSymbol reads ("Dm7", "G7b9", "C/E"). The
// kind's text attribute wins when the exporter wrote one (Guitar Pro does), the kind name maps otherwise.
namespace cc::score::detail
{

namespace
{

constexpr std::array<std::pair<std::string_view, std::string_view>, 24> kindNames{{
    {"major", ""},
    {"minor", "m"},
    {"augmented", "aug"},
    {"diminished", "dim"},
    {"dominant", "7"},
    {"major-seventh", "maj7"},
    {"minor-seventh", "m7"},
    {"diminished-seventh", "dim7"},
    {"augmented-seventh", "aug7"},
    {"half-diminished", "m7b5"},
    {"major-minor", "mMaj7"},
    {"major-sixth", "6"},
    {"minor-sixth", "m6"},
    {"dominant-ninth", "9"},
    {"major-ninth", "maj9"},
    {"minor-ninth", "m9"},
    {"dominant-11th", "11"},
    {"major-11th", "maj11"},
    {"minor-11th", "m11"},
    {"dominant-13th", "13"},
    {"major-13th", "maj13"},
    {"minor-13th", "m13"},
    {"suspended-second", "sus2"},
    {"suspended-fourth", "sus4"},
}};

std::string accidental(int alter)
{
    std::string out;
    for (; alter < 0; ++alter) {
        out += 'b';
    }
    for (; alter > 0; --alter) {
        out += '#';
    }
    return out;
}

/// "<step><alter>" of a root or bass node, with the child names given ("root-step", "root-alter").
std::string noteName(const pugi::xml_node &node, const char *stepName, const char *alterName)
{
    const std::string step = node.child(stepName).text().as_string();
    if (step.empty()) {
        return {};
    }
    return step + accidental(node.child(alterName).text().as_int(0));
}

std::string kindText(const pugi::xml_node &kind)
{
    std::string text = kind.attribute("text").as_string();
    if (!text.empty() || !kind.attribute("text").empty()) {
        return text; // an explicit empty text means a plain major chord
    }
    const std::string_view name = kind.text().as_string();
    for (const auto &[xmlName, symbol] : kindNames) {
        if (xmlName == name) {
            return std::string(symbol);
        }
    }
    return name == "power" ? std::string("5") : std::string();
}

std::string degreeText(const pugi::xml_node &degree)
{
    const int value = degree.child("degree-value").text().as_int(0);
    const int alter = degree.child("degree-alter").text().as_int(0);
    const std::string_view type = degree.child("degree-type").text().as_string();
    if (value == 0) {
        return {};
    }
    if (type == "subtract") {
        return "no" + std::to_string(value);
    }
    const std::string prefix = (type == "add" && alter == 0) ? "add" : accidental(alter);
    return prefix + std::to_string(value);
}

} // namespace

std::string harmonyText(const pugi::xml_node &harmony)
{
    std::string text = noteName(harmony.child("root"), "root-step", "root-alter");
    if (text.empty()) {
        return {};
    }
    text += kindText(harmony.child("kind"));
    for (const pugi::xml_node &degree : harmony.children("degree")) {
        text += degreeText(degree);
    }
    const std::string bass = noteName(harmony.child("bass"), "bass-step", "bass-alter");
    if (!bass.empty()) {
        text += "/" + bass;
    }
    return text;
}

} // namespace cc::score::detail
