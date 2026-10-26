// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/scaleform.h"

#include "core/theory/chord.h"

#include <array>
#include <cassert>
#include <span>

namespace cc::theory
{

namespace
{

constexpr std::array<int, 3> sevenOneTwo{7, 1, 2};
constexpr std::array<int, 4> threeToSix{3, 4, 5, 6};

ScaleGroup groupOf(int parentDegree, bool threeNoteGroup)
{
    if (parentDegree == 1) {
        return ScaleGroup::Root;
    }
    return threeNoteGroup ? ScaleGroup::SevenOneTwo : ScaleGroup::ThreeToSix;
}

/// Builds the form string by string, keeping the pitch strictly ascending across the whole form.
class FormBuilder
{
public:
    FormBuilder(const Tuning &tuning, PitchClass parentMajor)
        : m_tuning(&tuning)
        , m_parentMajor(parentMajor)
    {
    }

    void appendString(int string, bool threeNoteGroup)
    {
        const std::span<const int> degrees =
            threeNoteGroup ? std::span<const int>(sevenOneTwo) : std::span<const int>(threeToSix);
        for (const int degree : degrees) {
            const StringFret position{string, nextFret(string, degreePitchClass(m_parentMajor, degree))};
            m_previousPitch = m_tuning->pitchAt(position);
            m_notes.push_back(ScaleNote{position, degree, groupOf(degree, threeNoteGroup)});
        }
    }

    std::vector<ScaleNote> take()
    {
        return std::move(m_notes);
    }

private:
    /// Lowest fret on `string` sounding `pitchClass` above the previous note (first note: lowest fret at all).
    [[nodiscard]] int nextFret(int string, PitchClass pitchClass) const
    {
        int fret = intervalUp(pitchClassOf(m_tuning->openPitch(string) + m_tuning->capo), pitchClass);
        while (m_tuning->pitchAt(StringFret{string, fret}) <= m_previousPitch) {
            fret += semitonesPerOctave;
        }
        return fret;
    }

    const Tuning *m_tuning; // not owned; a pointer rather than a reference so the builder stays assignable (CG C.12)
    PitchClass m_parentMajor;
    Midi m_previousPitch = -1;
    std::vector<ScaleNote> m_notes;
};

} // namespace

PitchClass parentMajorOf(PitchClass tonic, int mode)
{
    assert(mode >= 1 && mode <= modeCount);
    return transpose(tonic, -majorScaleStep(mode));
}

int modeDegreeOf(int parentDegree, int mode)
{
    assert(mode >= 1 && mode <= modeCount);
    return ((parentDegree - mode + degreesPerScale) % degreesPerScale) + 1;
}

std::vector<ScaleNote> diagonalMajorForm(PitchClass parentMajor, RootString rootOn, const Tuning &tuning)
{
    assert(tuning.stringCount() == 6); // the diagonal form is defined for standard six strings (routine.md section 4)
    FormBuilder builder(tuning, parentMajor);
    // Root on 5A: string 6 starts with the 4-note group; root on 6E: string 6 starts with the 3-note group.
    bool threeNoteGroup = rootOn == RootString::Six;
    for (int string = 6; string >= 1; --string) {
        builder.appendString(string, threeNoteGroup);
        threeNoteGroup = !threeNoteGroup;
    }
    return builder.take();
}

} // namespace cc::theory
