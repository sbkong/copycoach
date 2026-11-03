// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/stretcher.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace cc::audio
{

struct Stretcher::Impl {
    signalsmith::stretch::SignalsmithStretch<float> stretch{1L}; // fixed seed: the same input renders the same
};

Stretcher::Stretcher(double sampleRate)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->stretch.presetDefault(channels, static_cast<float>(sampleRate));
}

Stretcher::~Stretcher() = default;

void Stretcher::setPitchSemitones(double semitones)
{
    m_impl->stretch.setTransposeSemitones(static_cast<float>(semitones));
}

void Stretcher::process(ConstPlanar input, int inFrames, Planar output, int outFrames)
{
    m_impl->stretch.process(input, inFrames, output, outFrames);
}

void Stretcher::reset()
{
    m_impl->stretch.reset();
}

int Stretcher::inputLatency() const
{
    return m_impl->stretch.inputLatency();
}

int Stretcher::outputLatency() const
{
    return m_impl->stretch.outputLatency();
}

} // namespace cc::audio
