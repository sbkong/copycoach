// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/slotbuilder.h"

#include "core/theory/chord.h"
#include "core/theory/scaleform.h"
#include "core/theory/shellvoicing.h"
#include "core/theory/triadcycle.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace cc::ui
{

namespace
{

using theme::Token;
using theory::ChordQuality;
using theory::ChordTone;

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QString option(const routine::RoutineState &state, const char *name, const char *fallback)
{
    const auto it = state.options.find(name);
    return QString::fromStdString(it == state.options.end() ? fallback : it->second);
}

QString qualitySuffix(ChordQuality quality)
{
    switch (quality) {
    case ChordQuality::Maj7:
        return QStringLiteral("maj7");
    case ChordQuality::Min7:
        return QStringLiteral("m7");
    case ChordQuality::Dom7:
        return QStringLiteral("7");
    case ChordQuality::Min7b5:
        return QStringLiteral("m7b5");
    case ChordQuality::Minor:
        return QStringLiteral("m");
    case ChordQuality::Diminished:
        return QStringLiteral("dim");
    case ChordQuality::Major:
        return {};
    }
    return {};
}

QString romanOf(int degree, ChordQuality quality)
{
    constexpr std::array<const char *, theory::degreesPerScale> numerals{"I", "II", "III", "IV", "V", "VI", "VII"};
    return QString::fromLatin1(numerals[theory::degreeIndex(degree)]) + qualitySuffix(quality);
}

QString toneLabel(ChordTone tone)
{
    switch (tone) {
    case ChordTone::Root:
        return QStringLiteral("R");
    case ChordTone::Third:
        return QStringLiteral("3");
    case ChordTone::Fifth:
        return QStringLiteral("5");
    case ChordTone::Seventh:
        return QStringLiteral("7");
    case ChordTone::Ninth:
        return QStringLiteral("9");
    case ChordTone::Thirteenth:
        return QStringLiteral("13");
    case ChordTone::FlatNinth:
        return QStringLiteral("b9");
    case ChordTone::FlatThirteenth:
        return QStringLiteral("b13");
    }
    return {};
}

Token degreeColorFor(ChordTone tone)
{
    switch (tone) {
    case ChordTone::Root:
        return Token::DegreeRootColor;
    case ChordTone::Third:
        return Token::DegreeThirdColor;
    case ChordTone::Fifth:
        return Token::DegreeFifthColor;
    case ChordTone::Seventh:
        return Token::DegreeSeventhColor;
    default:
        return Token::DegreeTensionColor;
    }
}

/// Which chord tone a fretted note is, by interval from the chord root.
ChordTone toneOf(theory::PitchClass root, ChordQuality quality, theory::PitchClass pitch)
{
    const int interval = theory::intervalUp(root, pitch);
    for (const ChordTone tone : {ChordTone::Root,
                                 ChordTone::Third,
                                 ChordTone::Fifth,
                                 ChordTone::Seventh,
                                 ChordTone::Ninth,
                                 ChordTone::FlatNinth,
                                 ChordTone::FlatThirteenth}) {
        if (theory::chordToneInterval(quality, tone) == interval) {
            return tone;
        }
    }
    return ChordTone::Thirteenth;
}

/// "V7/ii": the dominant of the target degree, lower case when the target chord is minor (section 2.5).
QString secondaryRoman(int target)
{
    constexpr std::array<const char *, theory::degreesPerScale> numerals{"I", "II", "III", "IV", "V", "VI", "VII"};
    QString numeral = QString::fromLatin1(numerals[theory::degreeIndex(target)]);
    if (theory::hasMinorThird(theory::diatonicSeventh(target))) {
        numeral = numeral.toLower();
    }
    return QStringLiteral("V7/") + numeral;
}

SlotNote chordNote(theory::StringFret position,
                   theory::PitchClass root,
                   ChordQuality quality,
                   DotKind kind,
                   const theory::Tuning &tuning)
{
    const theory::PitchClass pitch = tuning.pitchClassAt(position);
    const ChordTone tone = toneOf(root, quality, pitch);
    return SlotNote{position,
                    toneLabel(tone),
                    fromView(theory::flatName(pitch)),
                    kind,
                    tone == ChordTone::Root,
                    degreeColorFor(tone)};
}

Slot shellSlot(const theory::ShellSlot &shell, const theory::Tuning &tuning)
{
    Slot slot;
    slot.name = QString::fromStdString(theory::chordName(shell.rootPitchClass, shell.quality));
    slot.roman = shell.target == 0 ? romanOf(shell.degree, shell.quality) : secondaryRoman(shell.target);
    slot.rootString = shell.root.string == 6 ? QStringLiteral("6th") : QStringLiteral("5th");
    slot.chordColor = chordColorForDegree(shell.degree);
    for (const theory::StringFret &position : theory::shellNotes(shell, tuning)) {
        slot.notes.push_back(chordNote(position, shell.rootPitchClass, shell.quality, DotKind::Shell, tuning));
    }
    return slot;
}

void addUpperLayer(Slot &slot,
                   const theory::ShellSlot &shell,
                   const std::array<theory::UpperNote, 2> &upper,
                   const theory::Tuning &tuning)
{
    for (const theory::UpperNote &note : upper) {
        slot.notes.push_back(chordNote(note.position, shell.rootPitchClass, shell.quality, DotKind::Upper, tuning));
    }
}

/// Section 2.7 "upper four": the chord the notes above the root spell on their own (Cmaj9 = C + Em7, Em7 = E + G);
/// the note names when they spell no chord (G13: 3 7 13 9).
QString upperFourOf(const Slot &slot, const theory::Tuning &tuning)
{
    std::vector<theory::PitchClass> pitchClasses;
    QStringList names;
    for (const SlotNote &note : slot.notes) {
        if (!note.isRoot) {
            pitchClasses.push_back(tuning.pitchClassAt(note.position));
            names.append(note.noteName);
        }
    }
    const std::string name = theory::nameChordOf(pitchClasses);
    return name.empty() ? names.join(QStringLiteral("\u00b7")) : QString::fromStdString(name);
}

/// Section 2.5: the shell note a secondary dominant changed is the one outside the key (tensions are not marked).
void markAlteredNotes(Slot &slot, theory::PitchClass key, const theory::Tuning &tuning)
{
    for (SlotNote &note : slot.notes) {
        note.altered = note.kind == DotKind::Shell && !theory::inMajorKey(key, tuning.pitchClassAt(note.position));
    }
}

std::vector<Slot> shellSlots(const routine::RoutineState &state, const theory::Tuning &tuning)
{
    const bool scaleOrder = option(state, "order", "circle5") == QStringLiteral("scale");
    const theory::LoopOrder order = scaleOrder ? theory::LoopOrder::Scale : theory::LoopOrder::Circle5;
    const bool upper = option(state, "upper", "off") == QStringLiteral("on");
    std::vector<Slot> result;
    for (const theory::ShellSlot &shell : theory::shellLoop(state.currentKey, order, tuning)) {
        Slot slot = shellSlot(shell, tuning);
        if (upper) {
            addUpperLayer(slot, shell, theory::upperNotes(shell, tuning), tuning);
            slot.upperFour = upperFourOf(slot, tuning);
        }
        result.push_back(std::move(slot));
    }
    return result;
}

std::vector<Slot> secondaryDominantSlots(const routine::RoutineState &state, const theory::Tuning &tuning)
{
    const QString upper = option(state, "upper", "off");
    const theory::TensionRule rule =
        upper == QStringLiteral("chromatic") ? theory::TensionRule::Chromatic : theory::TensionRule::Diatonic;
    std::vector<Slot> result;
    for (const theory::ShellSlot &shell : theory::secondaryDominantLoop(state.currentKey, tuning)) {
        Slot slot = shellSlot(shell, tuning);
        if (upper != QStringLiteral("off")) {
            addUpperLayer(slot, shell, theory::secondaryUpperNotes(shell, state.currentKey, rule, tuning), tuning);
            slot.upperFour = upperFourOf(slot, tuning);
        }
        markAlteredNotes(slot, state.currentKey, tuning);
        result.push_back(std::move(slot));
    }
    return result;
}

Token scaleDegreeColor(int modeDegree)
{
    switch (modeDegree) {
    case 1:
        return Token::DegreeRootColor;
    case 3:
        return Token::DegreeThirdColor;
    case 5:
        return Token::DegreeFifthColor;
    case 7:
        return Token::DegreeSeventhColor;
    default:
        return Token::DegreeTensionColor;
    }
}

QString modeName(int mode)
{
    constexpr std::array<const char *, theory::modeCount>
        names{"이오니안", "도리안", "프리지안", "리디안", "믹솔리디안", "에올리안", "로크리안"};
    return QString::fromUtf8(names[static_cast<std::size_t>(mode - 1)]);
}

std::vector<Slot> scaleSlots(const routine::RoutineState &state, const theory::Tuning &tuning)
{
    const int mode = std::clamp(option(state, "mode", "1").toInt(), 1, theory::modeCount);
    const theory::RootString rootOn =
        option(state, "root_on", "5A") == QStringLiteral("6E") ? theory::RootString::Six : theory::RootString::Five;
    const theory::PitchClass parent = theory::parentMajorOf(state.currentKey, mode);
    Slot slot;
    slot.name = fromView(theory::flatName(state.currentKey)) + QLatin1Char(' ') + modeName(mode);
    slot.chordColor = Token::ChordCColor;
    slot.isScaleRun = true;
    for (const theory::ScaleNote &note : theory::diagonalMajorForm(parent, rootOn, tuning)) {
        const int degree = theory::modeDegreeOf(note.parentDegree, mode);
        const theory::PitchClass pitch = tuning.pitchClassAt(note.position);
        slot.notes.push_back(SlotNote{note.position,
                                      QString::number(degree),
                                      fromView(theory::flatName(pitch)),
                                      DotKind::Scale,
                                      degree == 1,
                                      scaleDegreeColor(degree)});
    }
    return {slot};
}

template<typename Enum>
Enum lookup(const QString &value, const std::map<QString, Enum> &table, Enum fallback)
{
    const auto it = table.find(value);
    return it == table.end() ? fallback : it->second;
}

theory::ClosedCycleOptions closedOptions(const routine::RoutineState &state)
{
    static const std::map<QString, theory::StringSet> stringSets{
        {QStringLiteral("234"), theory::StringSet::Strings234},
        {QStringLiteral("345"), theory::StringSet::Strings345},
        {QStringLiteral("456"), theory::StringSet::Strings456}};
    static const std::map<QString, theory::Inversion> inversions{{QStringLiteral("root"), theory::Inversion::Root},
                                                                 {QStringLiteral("1st"), theory::Inversion::First},
                                                                 {QStringLiteral("2nd"), theory::Inversion::Second}};
    static const std::map<QString, theory::TriadCycle> cycles{{QStringLiteral("3-6"), theory::TriadCycle::Thirds},
                                                              {QStringLiteral("4-5"), theory::TriadCycle::Fourths}};
    theory::ClosedCycleOptions options;
    options.strings = lookup(option(state, "strings", "123"), stringSets, theory::StringSet::Strings123);
    options.inversion = lookup(option(state, "inversion", "auto"), inversions, theory::Inversion::Auto);
    options.cycle = lookup(option(state, "cycle", "2-7"), cycles, theory::TriadCycle::Seconds);
    options.direction =
        option(state, "direction", "up") == QStringLiteral("down") ? theory::Direction::Down : theory::Direction::Up;
    return options;
}

std::vector<Slot> triadSlots(const routine::RoutineState &state, const theory::Tuning &tuning)
{
    const theory::ClosedCycleOptions options = closedOptions(state);
    const theory::TriadSequence sequence = state.modeId == "open"
        ? theory::openTriadCycle(state.currentKey, options.cycle, options.direction, tuning)
        : theory::closedTriadCycle(state.currentKey, options, tuning);
    std::vector<Slot> slotList;
    for (const theory::TriadVoicing &triad : sequence) {
        Slot slot;
        slot.name = QString::fromStdString(theory::chordName(triad.rootPitchClass, triad.quality));
        slot.roman = romanOf(triad.degree, triad.quality);
        slot.chordColor = chordColorForDegree(triad.degree);
        for (const theory::StringFret &position : triad.notes) {
            slot.notes.push_back(chordNote(position, triad.rootPitchClass, triad.quality, DotKind::Shell, tuning));
        }
        slotList.push_back(std::move(slot));
    }
    return slotList;
}

} // namespace

theme::Token chordColorForDegree(int degree)
{
    constexpr std::array<Token, theory::degreesPerScale> tokens{Token::ChordCColor,
                                                                Token::ChordDmColor,
                                                                Token::ChordEmColor,
                                                                Token::ChordFColor,
                                                                Token::ChordGColor,
                                                                Token::ChordAmColor,
                                                                Token::ChordBdimColor};
    return tokens[theory::degreeIndex(degree)];
}

std::vector<Slot> buildSlots(const routine::RoutineState &state, const theory::Tuning &tuning)
{
    if (state.moduleId == "01") {
        return scaleSlots(state, tuning);
    }
    if (state.moduleId == "02") {
        return state.modeId == "secondary-dominant" ? secondaryDominantSlots(state, tuning) : shellSlots(state, tuning);
    }
    if (state.moduleId == "03") {
        return triadSlots(state, tuning);
    }
    return {};
}

RootDisplay rootDisplayOf(const QString &settingValue)
{
    if (settingValue == QStringLiteral("hide")) {
        return RootDisplay::Hide;
    }
    return settingValue == QStringLiteral("only") ? RootDisplay::Only : RootDisplay::Show;
}

bool noteShown(const SlotNote &note, RootDisplay display)
{
    switch (display) {
    case RootDisplay::Hide:
        return !note.isRoot;
    case RootDisplay::Only:
        return note.isRoot;
    case RootDisplay::Show:
        return true;
    }
    return true;
}

} // namespace cc::ui
