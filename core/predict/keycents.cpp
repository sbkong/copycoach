// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/keycents.h"

#include "core/dsp/fft.h"
#include "core/predict/beatthis.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

namespace cc::predict
{

namespace
{

constexpr double a4Hz = 440.0;
constexpr int a4Midi = 69;
constexpr double minHz = 55.0; // A1: below it guitar and bass fundamentals are rare and bins are coarse
constexpr double maxHz = 2000.0;
constexpr int centsBins = 100; // one bin per cent, -50..49
constexpr int centsWindow = 10; // the mean is taken this far around the histogram peak
constexpr double peakFloor = 1e-4; // relative to the frame's loudest bin: quieter peaks are noise
constexpr std::size_t stopCheckEvery = 16;

// Krumhansl-Kessler probe-tone profiles (Krumhansl 1990, table 2.1), index 0 = the tonic.
constexpr std::array<double, 12> majorProfile{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> minorProfile{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

std::array<float, keyCentsFftSize> hann()
{
    std::array<float, keyCentsFftSize> window{};
    for (std::size_t i = 0; i < keyCentsFftSize; ++i) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(keyCentsFftSize);
        window[i] = static_cast<float>(0.5 - (0.5 * std::cos(phase)));
    }
    return window;
}

double binHz(double bin)
{
    return bin * keyCentsSampleRate / static_cast<double>(keyCentsFftSize);
}

/// Fractional midi number of a frequency against A4 = 440 Hz.
double midiOf(double hz)
{
    return a4Midi + (12.0 * std::log2(hz / a4Hz));
}

/// Peaks of one magnitude spectrum, each refined by a parabola through its neighbours.
struct Peak {
    double hz;
    double magnitude;
};

std::vector<Peak> peaksOf(std::span<const float> magnitude)
{
    std::vector<Peak> peaks;
    const auto from = static_cast<std::size_t>(std::ceil(minHz * keyCentsFftSize / keyCentsSampleRate));
    const auto to = static_cast<std::size_t>(std::floor(maxHz * keyCentsFftSize / keyCentsSampleRate));
    const float loudest = *std::ranges::max_element(magnitude.subspan(from, to - from));
    const float floor = loudest * static_cast<float>(peakFloor);
    for (std::size_t k = from + 1; k < to; ++k) {
        const float m = magnitude[k];
        if (m <= floor || m <= magnitude[k - 1] || m < magnitude[k + 1]) {
            continue;
        }
        const double a = magnitude[k - 1];
        const double c = magnitude[k + 1];
        const double denominator = a - (2.0 * m) + c;
        const double offset = denominator != 0.0 ? 0.5 * (a - c) / denominator : 0.0;
        peaks.push_back({binHz(static_cast<double>(k) + offset), m});
    }
    return peaks;
}

/// Accumulates the two things the analysis needs: a cents histogram of all peaks, and the peaks themselves
/// for the chroma once the tuning offset is known.
struct Accumulator {
    std::array<double, centsBins> centsHistogram{};
    std::vector<Peak> peaks;

    void add(const std::vector<Peak> &framePeaks)
    {
        for (const Peak &peak : framePeaks) {
            const double midi = midiOf(peak.hz);
            const double cents = (midi - std::round(midi)) * 100.0; // -50..50
            const int bin = std::clamp(static_cast<int>(std::floor(cents)) + (centsBins / 2), 0, centsBins - 1);
            centsHistogram[static_cast<std::size_t>(bin)] += peak.magnitude;
            peaks.push_back(peak);
        }
    }
};

/// The histogram's peak, then the weighted mean within +-10 cents of it (wrapping at +-50).
std::pair<double, double> centsEstimate(const std::array<double, centsBins> &histogram)
{
    const double total = std::accumulate(histogram.begin(), histogram.end(), 0.0);
    if (total <= 0.0) {
        return {0.0, 0.0};
    }
    const auto peak = static_cast<int>(std::distance(histogram.begin(), std::ranges::max_element(histogram)));
    double weighted = 0.0;
    double mass = 0.0;
    for (int d = -centsWindow; d <= centsWindow; ++d) {
        const int bin = (((peak + d) % centsBins) + centsBins) % centsBins;
        const double w = histogram[static_cast<std::size_t>(bin)];
        weighted += w * static_cast<double>(peak + d);
        mass += w;
    }
    const double cents = (weighted / mass) + 0.5 - (centsBins / 2.0); // bin centre, back to -50..50
    return {cents, mass / total};
}

std::array<double, 12> chromaOf(const std::vector<Peak> &peaks, double cents)
{
    std::array<double, 12> chroma{};
    for (const Peak &peak : peaks) {
        const double midi = midiOf(peak.hz) - (cents / 100.0);
        const int pitchClass = theory::wrapPitchClass(static_cast<int>(std::lround(midi)));
        chroma[static_cast<std::size_t>(pitchClass)] += peak.magnitude;
    }
    const double sum = std::accumulate(chroma.begin(), chroma.end(), 0.0);
    if (sum > 0.0) {
        for (double &c : chroma) {
            c /= sum;
        }
    }
    return chroma;
}

double correlation(const std::array<double, 12> &chroma, const std::array<double, 12> &profile, int tonic)
{
    const double meanC = std::accumulate(chroma.begin(), chroma.end(), 0.0) / 12.0;
    const double meanP = std::accumulate(profile.begin(), profile.end(), 0.0) / 12.0;
    double num = 0.0;
    double dc = 0.0;
    double dp = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double c = chroma[static_cast<std::size_t>(theory::wrapPitchClass(tonic + i))] - meanC;
        const double p = profile[static_cast<std::size_t>(i)] - meanP;
        num += c * p;
        dc += c * c;
        dp += p * p;
    }
    return dc > 0.0 && dp > 0.0 ? num / std::sqrt(dc * dp) : 0.0;
}

/// The best and the runner-up so far among the 24 keys.
struct KeyRace {
    double best = -2.0;
    double second = -2.0;
    int tonic = 0;
    bool minor = false;

    void offer(double r, int candidateTonic, bool candidateMinor)
    {
        if (r > best) {
            second = best;
            best = r;
            tonic = candidateTonic;
            minor = candidateMinor;
        } else if (r > second) {
            second = r;
        }
    }
};

/// Best of the 24 keys by profile correlation; the margin tells how far the runner-up was.
void chooseKey(KeyCentsPrediction &out)
{
    KeyRace race;
    for (int tonic = 0; tonic < 12; ++tonic) {
        race.offer(correlation(out.chroma, majorProfile, tonic), tonic, false);
        race.offer(correlation(out.chroma, minorProfile, tonic), tonic, true);
    }
    out.tonic = race.tonic;
    out.minor = race.minor;
    out.keyConfidence = std::max(0.0, race.best);
    out.keyMargin = std::max(0.0, race.best - race.second);
}

Result<void>
accumulateFrames(std::span<const float> mono, Accumulator &acc, const std::stop_token &stop, const jobs::Report &report)
{
    static const std::array<float, keyCentsFftSize> window = hann();
    const dsp::Fft fft(keyCentsFftSize);
    std::vector<float> frame(keyCentsFftSize);
    std::vector<float> magnitude((keyCentsFftSize / 2) + 1);
    const std::size_t frames = mono.size() >= keyCentsFftSize ? 1 + ((mono.size() - keyCentsFftSize) / keyCentsHop) : 1;
    for (std::size_t f = 0; f < frames; ++f) {
        if (f % stopCheckEvery == 0 && stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, "key");
        }
        const std::size_t start = f * keyCentsHop;
        for (std::size_t i = 0; i < keyCentsFftSize; ++i) {
            frame[i] = start + i < mono.size() ? mono[start + i] * window[i] : 0.0F;
        }
        fft.magnitudes(frame, magnitude);
        acc.add(peaksOf(magnitude));
        report(0.2 + (0.8 * static_cast<double>(f + 1) / static_cast<double>(frames)));
    }
    return {};
}

} // namespace

KeyCentsPrediction estimateKeyCents(std::span<const float> mono22k)
{
    Accumulator acc;
    const std::stop_token never;
    static_cast<void>(accumulateFrames(mono22k, acc, never, [](double) { }));
    KeyCentsPrediction out;
    const auto [cents, confidence] = centsEstimate(acc.centsHistogram);
    out.cents = cents;
    out.centsConfidence = confidence;
    out.chroma = chromaOf(acc.peaks, cents);
    chooseKey(out);
    return out;
}

Result<KeyCentsPrediction>
predictKeyCents(const std::filesystem::path &audio, const std::stop_token &stop, const jobs::Report &report)
{
    Result<std::vector<float>> mono = loadMono22k(audio, stop, report); // reports 0..0.2
    if (!mono) {
        return std::unexpected(mono.error());
    }
    Accumulator acc;
    if (Result<void> r = accumulateFrames(*mono, acc, stop, report); !r) {
        return std::unexpected(r.error());
    }
    KeyCentsPrediction out;
    const auto [cents, confidence] = centsEstimate(acc.centsHistogram);
    out.cents = cents;
    out.centsConfidence = confidence;
    out.chroma = chromaOf(acc.peaks, cents);
    chooseKey(out);
    return out;
}

std::string keyName(const KeyCentsPrediction &prediction)
{
    return std::string(theory::flatName(prediction.tonic)) + (prediction.minor ? " minor" : " major");
}

/// Fifths of a major tonic on the circle (C 0, G 1 ... F -1, Bb -2), taking the side with fewer accidentals.
model::KeySignature keySignatureOf(const KeyCentsPrediction &prediction)
{
    const theory::PitchClass major = prediction.minor ? theory::transpose(prediction.tonic, 3) : prediction.tonic;
    int fifths = 0;
    theory::PitchClass walk = 0;
    while (walk != major) { // up the circle of fifths from C
        walk = theory::transpose(walk, 7);
        ++fifths;
    }
    if (fifths > 6) {
        fifths -= 12; // the flat side is shorter
    }
    return {fifths, prediction.minor};
}

model::Prediction toPrediction(const KeyCentsPrediction &prediction, const std::string &sourceId)
{
    model::Prediction out;
    out.kind = model::PredictionKind::Key;
    out.sourceId = sourceId;
    model::PredictedItem item;
    item.text = keyName(prediction);
    item.confidence = prediction.keyConfidence;
    out.items.push_back(std::move(item));
    return out;
}

} // namespace cc::predict
