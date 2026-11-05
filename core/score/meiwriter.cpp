// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/score/meiwriter.h"

#include "core/score/meicontext.h"
#include "core/theory/pitch.h"

#include <cmath>
#include <optional>
#include <sstream>
#include <string>

namespace cc::score
{

namespace
{

using detail::MeasureContext;

constexpr const char *meiVersion = "5.0";
constexpr int ppq = 960;

std::string keySigValue(const model::KeySignature &key)
{
    if (key.fifths == 0) {
        return "0";
    }
    return std::to_string(std::abs(key.fifths)) + (key.fifths > 0 ? "s" : "f");
}

/// MEI tstamp: 1 at the measure start, in units of the meter's beat (fractions allowed).
std::string tstampOf(model::Tick tick, const model::TimeSignature &meter)
{
    const auto beatTicks = static_cast<double>(model::baseTicks(model::beatDur(meter)));
    std::ostringstream text;
    text << 1.0 + (static_cast<double>(tick) / beatTicks);
    return text.str();
}

void appendMeterAndKey(pugi::xml_node parent, const model::TimeSignature &meter, const model::KeySignature &key)
{
    pugi::xml_node meterSig = parent.append_child("meterSig");
    meterSig.append_attribute("count") = meter.count;
    meterSig.append_attribute("unit") = meter.unit;
    parent.append_child("keySig").append_attribute("sig") = keySigValue(key).c_str();
}

void appendTuning(pugi::xml_node staffDef, const theory::Tuning &tuning, const model::KeySignature &key)
{
    pugi::xml_node tuningNode = staffDef.append_child("tuning");
    for (int string = tuning.stringCount(); string >= 1; --string) { // low course first, as the spike MEI
        const detail::Spelling s = detail::spell(tuning.openPitch(string) + tuning.capo, key);
        pugi::xml_node course = tuningNode.append_child("course");
        course.append_attribute("n") = string;
        course.append_attribute("pname") = std::string(1, s.pname).c_str();
        course.append_attribute("oct") = s.octave;
        if (!s.accid.empty() && s.accid != "n") {
            course.append_attribute("accid") = s.accid.c_str();
        }
    }
}

void appendStaffDefs(pugi::xml_node group, const model::Score &score, std::size_t track, const MeiOptions &options)
{
    const model::Track &t = score.tracks[track];
    if (!options.tabOnly) {
        pugi::xml_node notation = group.append_child("staffDef");
        notation.append_attribute("n") = notationStaffOf(track, options);
        notation.append_attribute("lines") = 5;
        notation.append_attribute("ppq") = ppq;
        pugi::xml_node clef = notation.append_child("clef");
        clef.append_attribute("shape") = "G";
        clef.append_attribute("line") = 2;
        clef.append_attribute("dis") = 8;
        clef.append_attribute("dis.place") = "below";
        appendMeterAndKey(notation, score.initialTimeSig, score.initialKeySig);
    }
    pugi::xml_node tab = group.append_child("staffDef");
    tab.append_attribute("n") = tabStaffOf(track, options);
    tab.append_attribute("notationtype") = "tab.guitar";
    tab.append_attribute("lines") = t.tuning.stringCount();
    tab.append_attribute("ppq") = ppq;
    pugi::xml_node clef = tab.append_child("clef");
    clef.append_attribute("shape") = "TAB";
    clef.append_attribute("line") = 5;
    appendTuning(tab, t.tuning, score.initialKeySig);
    appendMeterAndKey(tab, score.initialTimeSig, score.initialKeySig);
}

void appendScoreDef(pugi::xml_node scoreNode, const model::Score &score, const MeiOptions &options)
{
    pugi::xml_node scoreDef = scoreNode.append_child("scoreDef");
    pugi::xml_node outer = scoreDef.append_child("staffGrp");
    for (std::size_t track = 0; track < score.tracks.size(); ++track) {
        pugi::xml_node group = outer.append_child("staffGrp");
        group.append_attribute("xml:id") = ("tr-" + score.tracks[track].id).c_str();
        group.append_attribute("bar.thru") = "true";
        if (!options.tabOnly) {
            group.append_child("grpSym").append_attribute("symbol") = "brace";
        }
        group.append_child("label").text().set(score.tracks[track].name.c_str());
        appendStaffDefs(group, score, track, options);
    }
}

pugi::xml_node appendSkeleton(pugi::xml_document &doc)
{
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    pugi::xml_node mei = doc.append_child("mei");
    mei.append_attribute("xmlns") = "http://www.music-encoding.org/ns/mei";
    mei.append_attribute("meiversion") = meiVersion;
    pugi::xml_node head = mei.append_child("meiHead");
    head.append_child("fileDesc").append_child("titleStmt").append_child("title");
    return mei.append_child("music").append_child("body").append_child("mdiv").append_child("score");
}

// --- measures -----------------------------------------------------------------------------------------------

/// A scoreDef before the measure when the meter or key changes there.
void appendChanges(pugi::xml_node section, const model::Measure &measure)
{
    if (!measure.timeSig && !measure.keySig) {
        return;
    }
    pugi::xml_node scoreDef = section.append_child("scoreDef");
    if (measure.timeSig) {
        pugi::xml_node meterSig = scoreDef.append_child("meterSig");
        meterSig.append_attribute("count") = measure.timeSig->count;
        meterSig.append_attribute("unit") = measure.timeSig->unit;
    }
    if (measure.keySig) {
        scoreDef.append_child("keySig").append_attribute("sig") = keySigValue(*measure.keySig).c_str();
    }
}

void appendTempo(const MeasureContext &ctx, double bpm)
{
    pugi::xml_node tempo = pugi::xml_node(ctx.measureNode).append_child("tempo");
    tempo.append_attribute("staff") = 1;
    tempo.append_attribute("tstamp") = "1";
    tempo.append_attribute("mm") = std::lround(bpm);
    tempo.append_attribute("mm.unit") = 4;
    tempo.append_attribute("place") = "above";
    std::ostringstream text;
    text << "♩ = " << std::lround(bpm);
    tempo.text().set(text.str().c_str());
}

void appendSectionLabel(const MeasureContext &ctx, const model::Section &section)
{
    pugi::xml_node dir = pugi::xml_node(ctx.measureNode).append_child("dir");
    dir.append_attribute("xml:id") = ("sd-" + section.id).c_str();
    dir.append_attribute("staff") = 1;
    dir.append_attribute("tstamp") = "1";
    dir.append_attribute("place") = "above";
    if (!section.letter.empty()) {
        pugi::xml_node rend = dir.append_child("rend");
        rend.append_attribute("fontweight") = "bold";
        rend.text().set(section.letter.c_str());
    }
    if (!section.name.empty()) {
        dir.append_child(pugi::node_pcdata)
            .set_value((section.letter.empty() ? section.name : " " + section.name).c_str());
    }
}

void appendControlEvents(const MeasureContext &ctx, const model::Measure &measure)
{
    for (const model::ChordSymbol &chord : measure.chords) {
        pugi::xml_node harm = pugi::xml_node(ctx.measureNode).append_child("harm");
        harm.append_attribute("xml:id") = ("h-" + chord.id).c_str();
        harm.append_attribute("staff") = 1;
        harm.append_attribute("tstamp") = tstampOf(chord.tick, ctx.meter).c_str();
        harm.append_attribute("place") = "above";
        harm.text().set(chord.text.c_str());
    }
    for (const auto &[start, end] : ctx.ties) {
        pugi::xml_node tie = pugi::xml_node(ctx.measureNode).append_child("tie");
        tie.append_attribute("startid") = ("#" + start).c_str();
        tie.append_attribute("endid") = ("#" + end).c_str();
    }
}

void appendMeasure(pugi::xml_node section, MeasureContext &ctx, const model::Section *startingSection)
{
    const model::Score &score = *ctx.score;
    const model::Measure &measure = score.measures[ctx.measureIndex];
    appendChanges(section, measure);
    ctx.measureNode = section.append_child("measure");
    ctx.measureNode.append_attribute("xml:id") = ("m-" + measure.id).c_str();
    ctx.measureNode.append_attribute("n") = static_cast<unsigned>(ctx.measureIndex + 1);
    ctx.meter = score.timeSigAt(ctx.measureIndex);
    ctx.key = score.keySigAt(ctx.measureIndex);
    ctx.ties.clear();
    for (std::size_t track = 0; track < score.tracks.size(); ++track) {
        if (!ctx.options->tabOnly) {
            detail::writeStaffLayers(ctx, track, notationStaffOf(track, *ctx.options), false);
        }
        detail::writeStaffLayers(ctx, track, tabStaffOf(track, *ctx.options), true);
    }
    if (ctx.measureIndex == 0) {
        appendTempo(ctx, score.initialTempoBpm);
    } else if (measure.tempoBpm) {
        appendTempo(ctx, *measure.tempoBpm);
    }
    if (startingSection != nullptr) {
        appendSectionLabel(ctx, *startingSection);
    }
    appendControlEvents(ctx, measure);
}

/// Measures nest under their section (score-data-path.md section 4); measures before the first section sit in
/// an unlabeled one.
void appendSections(pugi::xml_node scoreNode, const model::Score &score, const MeiOptions &options)
{
    MeasureContext ctx;
    ctx.score = &score;
    ctx.options = &options;
    const pugi::xml_node lead = scoreNode.append_child("section");
    pugi::xml_node section = lead;
    std::size_t nextSection = 0;
    for (ctx.measureIndex = 0; ctx.measureIndex < score.measures.size(); ++ctx.measureIndex) {
        const model::Section *starting = nullptr;
        if (nextSection < score.sections.size() && score.sections[nextSection].startMeasure == ctx.measureIndex) {
            starting = &score.sections[nextSection];
            section = scoreNode.append_child("section");
            section.append_attribute("xml:id") = ("s-" + starting->id).c_str();
            const std::string label =
                starting->letter + (starting->letter.empty() || starting->name.empty() ? "" : " ") + starting->name;
            section.append_attribute("label") = label.c_str();
            ++nextSection;
        }
        appendMeasure(section, ctx, starting);
    }
    if (lead.first_child().empty()) {
        scoreNode.remove_child(lead); // every measure belongs to a labeled section
    }
}

} // namespace

int notationStaffOf(std::size_t track, const MeiOptions &options)
{
    return options.tabOnly ? tabStaffOf(track, options) : static_cast<int>(track * 2) + 1;
}

int tabStaffOf(std::size_t track, const MeiOptions &options)
{
    return options.tabOnly ? static_cast<int>(track) + 1 : static_cast<int>(track * 2) + 2;
}

std::string writeMei(const model::Score &score, const MeiOptions &options)
{
    pugi::xml_document doc;
    const pugi::xml_node scoreNode = appendSkeleton(doc);
    appendScoreDef(scoreNode, score, options);
    appendSections(scoreNode, score, options);
    std::ostringstream out;
    doc.save(out, "  ");
    return out.str();
}

} // namespace cc::score
