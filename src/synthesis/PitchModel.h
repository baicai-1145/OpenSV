#pragma once

#include "DnniInference.h"
#include "DnniReader.h"
#include "PhonemeTiming.h"
#include "PitchContext.h"
#include "PitchDecoder.h"
#include "PitchFeatures.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sv::synthesis
{
class PitchModel
{
public:
    struct State
    {
        PitchContext::State context;
        bool reusedPrediction = false;

        [[nodiscard]] std::size_t getBytes() const noexcept;

    private:
        friend class PitchModel;
        std::vector<PitchNote> notes;
        std::vector<PhonemeDuration> phonemes;
        std::vector<float> midiPitch;
        std::vector<float> vibratoEnvelope;
        std::uint64_t modelIdentity = 0;
        std::uint32_t noiseSeed = 0;
    };

    [[nodiscard]] juce::Result load(const DnniReader& reader, std::size_t rootNode = 0);
    [[nodiscard]] float getFrameIntervalSeconds() const noexcept;
    // Predicts absolute MIDI pitch on the model's 5 ms grid, starting at time zero.
    // Notes and phonemes describe the same complete prediction interval, including
    // any silence context prepared by the caller. All frames are returned.
    // State and output belong to the synthesis worker, never the audio callback.
    // An optional vibrato envelope is sampled at frame * frameIntervalSeconds;
    // it must cover the prediction frames. An empty envelope means unity.
    // noiseSeed is the global suffix of phoneme-context seed events, not the
    // initial MT19937 state. The editor's default suffix is zero.
    [[nodiscard]] juce::Result run(std::span<const PitchNote> notes, std::span<const PhonemeDuration> phonemes, std::vector<float>& midiPitch, std::uint32_t noiseSeed = 0, State* state = nullptr, const std::function<bool()>& shouldCancel = {}, DnniRunStatistics* statistics = nullptr, std::span<const float> vibratoEnvelope = {}) const;

private:
    PitchFeatures features;
    PitchContext context;
    PitchDecoder decoder;
    DnniInference enhancementProjection;
    std::vector<float> speaker;
    float expression = 0.8f;
    float expressionStrength = 1.0f;
    float rapExpression = 0.7f;
    std::uint64_t modelIdentity = 0;
    bool loaded = false;
};
} // namespace sv::synthesis
