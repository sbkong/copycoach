// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/basicpitch.h"

#include "core/model/duration.h"
#include "core/predict/beatthis.h"
#include "core/predict/ortsession.h"
#include "core/theory/arrange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cc::predict
{

namespace
{

constexpr const char *inputName = "serving_default_input_2:0";
constexpr std::array<const char *, 2> outputNames{"StatefulPartitionedCall:1",
                                                  "StatefulPartitionedCall:2"}; // note, onset
constexpr std::size_t overlapSamples = basicPitchOverlapFrames * basicPitchHop; // 7680
constexpr std::size_t hopSamples = basicPitchWindowSamples - overlapSamples; // 36164
constexpr std::size_t trimFrames = basicPitchOverlapFrames / 2; // 15 off each end of a window
constexpr std::size_t keptFrames = basicPitchFramesPerWindow - basicPitchOverlapFrames; // 142
constexpr double magicAlignmentOffset = 0.0018; // note_creation.MAGIC_ALIGNMENT_OFFSET
constexpr double decodeShare = 0.2;
constexpr model::Tick grid = 240; // the 16th at 960 PPQ: where predicted notes land
constexpr int fingeringMaxFret = 20;

/// Frame index -> seconds (note_creation.model_frames_to_time): plain frame time minus a per-window offset.
double frameSeconds(std::size_t frame)
{
    const double hopSeconds = static_cast<double>(basicPitchHop) / basicPitchSampleRate;
    const double framesShort =
        static_cast<double>(basicPitchFramesPerWindow) - (static_cast<double>(basicPitchWindowSamples) / basicPitchHop);
    const double windowOffset = (hopSeconds * framesShort) + magicAlignmentOffset;
    const double window = std::floor(static_cast<double>(frame) / basicPitchFramesPerWindow);
    return (static_cast<double>(frame) * hopSeconds) - (windowOffset * window);
}

/// One window of the padded signal (zeros past the end), as the [1, 43844, 1] input.
void fillWindow(std::span<const float> padded, std::size_t start, std::span<float> window)
{
    for (std::size_t i = 0; i < window.size(); ++i) {
        window[i] = start + i < padded.size() ? padded[start + i] : 0.0F;
    }
}

/// unwrap_output: the middle 142 frames of each window, in order, cut to the expected total.
void appendKept(const std::vector<float> &windowOut, std::vector<float> &into)
{
    const auto first = windowOut.begin() + static_cast<std::ptrdiff_t>(trimFrames * basicPitchBins);
    into.insert(into.end(), first, first + static_cast<std::ptrdiff_t>(keptFrames * basicPitchBins));
}

std::size_t expectedFrames(std::size_t originalSamples)
{
    const double windows = static_cast<double>(originalSamples) / hopSamples;
    return static_cast<std::size_t>(windows * keptFrames);
}

} // namespace

Result<NoteActivations> runBasicPitch(std::span<const float> mono22k,
                                      const std::filesystem::path &model,
                                      const std::stop_token &stop,
                                      const jobs::Report &report)
{
    OrtSession session;
    if (Result<void> r = session.open(model); !r) {
        return std::unexpected(r.error());
    }
    std::vector<float> padded(overlapSamples / 2, 0.0F); // get_audio_input: half the overlap of silence first
    padded.insert(padded.end(), mono22k.begin(), mono22k.end());
    const std::size_t windows = padded.empty() ? 0 : ((padded.size() - 1) / hopSamples) + 1;
    std::vector<float> window(basicPitchWindowSamples);
    const std::array<std::int64_t, 3> shape{1, static_cast<std::int64_t>(basicPitchWindowSamples), 1};
    std::vector<std::vector<float>> outputs;
    NoteActivations act;
    for (std::size_t w = 0; w < windows; ++w) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, model.string());
        }
        fillWindow(padded, w * hopSamples, window);
        if (Result<void> r = session.run(inputName, shape, window, outputNames, outputs); !r) {
            return std::unexpected(r.error());
        }
        appendKept(outputs[0], act.note);
        appendKept(outputs[1], act.onset);
        report(
            std::min(1.0,
                     decodeShare + ((1.0 - decodeShare) * static_cast<double>(w + 1) / static_cast<double>(windows))));
    }
    act.frames = std::min(expectedFrames(mono22k.size()), act.note.size() / basicPitchBins);
    act.note.resize(act.frames * basicPitchBins);
    act.onset.resize(act.frames * basicPitchBins);
    return act;
}

namespace
{

/// The decoder's working state: note_creation.output_to_notes_polyphonic, step by step.
class NoteDecoder
{
public:
    NoteDecoder(const NoteActivations &act, const BasicPitchOptions &options)
        : m_frames(act.frames)
        , m_options(options)
        , m_note(act.note)
        , m_onset(act.onset)
        , m_remaining(act.note)
    {
    }

    std::vector<NoteEvent> run()
    {
        constrainFrequency();
        if (m_options.inferOnsets) {
            inferOnsets();
        }
        onsetNotes();
        if (m_options.melodiaTrick) {
            melodiaNotes();
        }
        std::ranges::sort(m_events, {}, &NoteEvent::startSeconds);
        return std::move(m_events);
    }

private:
    [[nodiscard]] static float &at(std::vector<float> &m, std::size_t frame, std::size_t bin)
    {
        return m[(frame * basicPitchBins) + bin];
    }

    [[nodiscard]] static float at(const std::vector<float> &m, std::size_t frame, std::size_t bin)
    {
        return m[(frame * basicPitchBins) + bin];
    }

    [[nodiscard]] static std::size_t binOfHz(double hz)
    {
        const double midi = 69.0 + (12.0 * std::log2(hz / 440.0));
        return static_cast<std::size_t>(
            std::clamp(std::lround(midi) - basicPitchMidiOffset, 0L, static_cast<long>(basicPitchBins)));
    }

    /// Zero the bins outside [minHz, maxHz).
    void constrainFrequency()
    {
        const std::size_t from = m_options.minHz ? binOfHz(*m_options.minHz) : 0;
        const std::size_t to = m_options.maxHz ? binOfHz(*m_options.maxHz) : basicPitchBins;
        for (std::size_t f = 0; f < m_frames; ++f) {
            zeroOutside(f, from, to);
        }
    }

    void zeroOutside(std::size_t f, std::size_t from, std::size_t to)
    {
        for (std::size_t b = 0; b < basicPitchBins; ++b) {
            if (b < from || b >= to) {
                at(m_note, f, b) = 0.0F;
                at(m_onset, f, b) = 0.0F;
                at(m_remaining, f, b) = 0.0F;
            }
        }
    }

    /// get_infered_onsets: the smaller of the 1- and 2-frame rises of the note activation, scaled to the
    /// onsets' maximum, merged with the onsets by max.
    void inferOnsets()
    {
        std::vector<float> diff(m_note.size(), 0.0F);
        float maxDiff = 0.0F;
        for (std::size_t f = 2; f < m_frames; ++f) {
            for (std::size_t b = 0; b < basicPitchBins; ++b) {
                const float d1 = at(m_note, f, b) - at(m_note, f - 1, b);
                const float d2 = at(m_note, f, b) - at(m_note, f - 2, b);
                const float d = std::max(0.0F, std::min(d1, d2));
                at(diff, f, b) = d;
                maxDiff = std::max(maxDiff, d);
            }
        }
        const float maxOnset = m_onset.empty() ? 0.0F : *std::ranges::max_element(m_onset);
        if (maxDiff <= 0.0F) {
            return;
        }
        for (std::size_t i = 0; i < m_onset.size(); ++i) {
            m_onset[i] = std::max(m_onset[i], maxOnset * diff[i] / maxDiff);
        }
    }

    /// scipy.signal.argrelmax along time: strictly greater than both neighbours, edges excluded.
    [[nodiscard]] bool isOnsetPeak(std::size_t f, std::size_t b) const
    {
        if (f == 0 || f + 1 >= m_frames) {
            return false;
        }
        const float v = at(m_onset, f, b);
        return v > at(m_onset, f - 1, b) && v > at(m_onset, f + 1, b)
            && v >= static_cast<float>(m_options.onsetThreshold);
    }

    /// Forward from `start + 1` while the energy stays up (tolerance frames allowed); the end frame.
    [[nodiscard]] std::size_t scanForward(std::size_t start, std::size_t b, bool clear)
    {
        std::size_t i = start + 1;
        int k = 0;
        while (i + 1 < m_frames && k < m_options.energyToleranceFrames) {
            k = at(m_remaining, i, b) < static_cast<float>(m_options.frameThreshold) ? k + 1 : 0;
            if (clear) {
                clearAround(i, b);
            }
            ++i;
        }
        return i - static_cast<std::size_t>(k);
    }

    void clearAround(std::size_t f, std::size_t b)
    {
        at(m_remaining, f, b) = 0.0F;
        if (b + 1 < basicPitchBins) {
            at(m_remaining, f, b + 1) = 0.0F;
        }
        if (b > 0) {
            at(m_remaining, f, b - 1) = 0.0F;
        }
    }

    void addEvent(std::size_t from, std::size_t to, std::size_t b)
    {
        double sum = 0.0;
        for (std::size_t f = from; f < to; ++f) {
            sum += at(m_note, f, b);
        }
        m_events.push_back({frameSeconds(from),
                            frameSeconds(to),
                            static_cast<int>(b) + basicPitchMidiOffset,
                            sum / static_cast<double>(to - from)});
    }

    /// Onset peaks, latest first, each followed along its bin until the energy stays below the threshold.
    void onsetNotes()
    {
        for (std::size_t i = m_frames * basicPitchBins; i-- > 0;) { // latest frame first, highest bin first
            const std::size_t f = i / basicPitchBins;
            const std::size_t b = i % basicPitchBins;
            if (isOnsetPeak(f, b) && f + 1 < m_frames) {
                noteFromOnset(f, b);
            }
        }
    }

    void noteFromOnset(std::size_t f, std::size_t b)
    {
        const std::size_t end = scanForward(f, b, false);
        if (end - f <= static_cast<std::size_t>(m_options.minNoteFrames)) {
            return;
        }
        for (std::size_t i = f; i < end; ++i) {
            clearAround(i, b);
        }
        addEvent(f, end, b);
    }

    /// The melodia trick: whatever energy is left above the threshold grows into notes both ways.
    void melodiaNotes()
    {
        while (true) {
            const auto peak = std::ranges::max_element(m_remaining);
            if (peak == m_remaining.end() || *peak <= static_cast<float>(m_options.frameThreshold)) {
                return;
            }
            const auto index = static_cast<std::size_t>(std::distance(m_remaining.begin(), peak));
            const std::size_t mid = index / basicPitchBins;
            const std::size_t b = index % basicPitchBins;
            *peak = 0.0F;
            const std::size_t end = scanForward(mid, b, true) - 1 + 0; // i_end = i - 1 - k
            const std::size_t start = scanBackward(mid, b);
            if (end > start && end - start > static_cast<std::size_t>(m_options.minNoteFrames)) {
                addEvent(start, end, b);
            }
        }
    }

    [[nodiscard]] std::size_t scanBackward(std::size_t mid, std::size_t b)
    {
        std::ptrdiff_t i = static_cast<std::ptrdiff_t>(mid) - 1;
        int k = 0;
        while (i > 0 && k < m_options.energyToleranceFrames) {
            const auto f = static_cast<std::size_t>(i);
            k = at(m_remaining, f, b) < static_cast<float>(m_options.frameThreshold) ? k + 1 : 0;
            clearAround(f, b);
            --i;
        }
        return static_cast<std::size_t>(i + 1 + k);
    }

    std::size_t m_frames;
    BasicPitchOptions m_options;
    std::vector<float> m_note;
    std::vector<float> m_onset;
    std::vector<float> m_remaining;
    std::vector<NoteEvent> m_events;
};

} // namespace

std::vector<NoteEvent> notesFromActivations(const NoteActivations &activations, const BasicPitchOptions &options)
{
    if (activations.frames < 2) {
        return {};
    }
    NoteDecoder decoder(activations, options);
    return decoder.run();
}

Result<std::vector<NoteEvent>> predictNotes(const std::filesystem::path &audio,
                                            const std::filesystem::path &model,
                                            const BasicPitchOptions &options,
                                            const std::stop_token &stop,
                                            const jobs::Report &report)
{
    Result<std::vector<float>> mono = loadMono22k(audio, stop, report);
    if (!mono) {
        return std::unexpected(mono.error());
    }
    Result<NoteActivations> act = runBasicPitch(*mono, model, stop, report);
    if (!act) {
        return std::unexpected(act.error());
    }
    return notesFromActivations(*act, options);
}

namespace
{

/// The string whose fret for `midi` lies nearest to `nearFret`; nullopt when no string reaches it.
} // namespace

/// Positions come from the fingering engine over the whole line in time order (theory/arrange.h).
model::Prediction toPrediction(const std::vector<NoteEvent> &notes,
                               const std::string &sourceId,
                               const model::TimeMap &map,
                               const theory::Tuning &tuning,
                               theory::FingeringPattern pattern)
{
    model::Prediction out;
    out.kind = model::PredictionKind::Notes;
    out.sourceId = sourceId;
    for (const NoteEvent &note : notes) {
        model::PredictedItem item;
        const model::Tick start = map.secondsToTick(note.startSeconds);
        const model::Tick end = map.secondsToTick(note.endSeconds);
        item.tick = ((start + (grid / 2)) / grid) * grid;
        item.dur = std::max<model::Tick>(grid, ((end - item.tick + (grid / 2)) / grid) * grid);
        item.seconds = note.startSeconds;
        item.midi = note.midi;
        item.confidence = note.amplitude;
        out.items.push_back(std::move(item));
    }
    std::ranges::sort(out.items, {}, &model::PredictedItem::tick);
    std::vector<theory::Midi> midis;
    midis.reserve(out.items.size());
    for (const model::PredictedItem &item : out.items) {
        midis.push_back(item.midi.value_or(0));
    }
    const auto positions = theory::arrangeFingering(midis, tuning, {pattern, fingeringMaxFret, true});
    for (std::size_t i = 0; i < out.items.size(); ++i) {
        out.items[i].position = positions[i];
    }
    return out;
}

} // namespace cc::predict
