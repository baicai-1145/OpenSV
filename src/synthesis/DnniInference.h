#pragma once

#include "DnniReader.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace sv::synthesis
{
struct DnniTensor
{
    std::size_t frames = 0;
    std::size_t channels = 0;
    // Frame-major: values[frame * channels + channel].
    std::vector<float> values;
};

struct DnniRunStatistics
{
    // Accumulated network output frames, not acoustic frames or audio samples.
    std::size_t computedFrames = 0;
    std::size_t reusedFrames = 0;
    std::size_t contextFrames = 0;
};

class DnniInference
{
public:
    // Own one cache per phrase and network on the synthesis worker, separately
    // from shared immutable model parameters. Clearing releases all snapshots.
    class Cache
    {
    public:
        void clear();
        [[nodiscard]] std::size_t getBytes() const noexcept;

    private:
        friend class DnniInference;
        DnniTensor input;
        DnniTensor condition;
        DnniTensor output;
        std::uint64_t modelIdentity = 0;
        bool hasCondition = false;
    };

    // Copies decoded parameters; the reader need not outlive this object.
    [[nodiscard]] juce::Result load(const DnniReader& reader, std::size_t rootNode = 0);
    // Floating-point inference outside the audio callback. A cache permits exact
    // finite-context reuse for frame-preserving networks; other networks run whole.
    // Quantized parameters are decoded to floats, not evaluated with the original integer kernels.
    // Conditional gate blocks require the matching condition tensor; no zero condition is supplied.
    [[nodiscard]] juce::Result run(const DnniTensor& input, DnniTensor& output, const DnniTensor* condition = nullptr, Cache* cache = nullptr, const std::function<bool()>& shouldCancel = {}, DnniRunStatistics* statistics = nullptr) const;

private:
    using Vector = juce::dsp::SIMDRegister<float>;
    static constexpr std::size_t vectorsPerBlock = 4;
    static constexpr std::size_t channelsPerBlock = vectorsPerBlock * Vector::size();
    using WeightBlock = std::array<Vector, vectorsPerBlock>;

    enum class Operation
    {
        sequence,
        dense,
        convolution,
        gatedConvolution,
        residualConvolution,
        gru,
        bidirectionalGru,
        relu,
        tanh,
        sigmoid,
        leakyRelu,
        elu,
        identity,
        silu
    };

    struct Matrix
    {
        std::size_t rows = 0;
        std::size_t columns = 0;
        // Output block, then input column. The SIMD value type guarantees aligned storage.
        std::vector<WeightBlock> values;
    };

    struct Layer
    {
        Operation operation = Operation::sequence;
        std::size_t sourceOffset = 0;
        std::vector<Layer> children;
        std::vector<Matrix> matrices;
        std::vector<float> bias;
        std::size_t stride = 1;
        std::size_t padding = 0;
        std::size_t dilation = 1;
        std::size_t inputChannels = 0;
        std::size_t gateChannels = 0;
        std::size_t conditionChannels = 0;
        std::size_t stageCount = 0;
        float alpha = 0.0f;
    };

    [[nodiscard]] static juce::Result loadLayer(const DnniReader& reader, std::size_t nodeIndex, Layer& layer, std::size_t& parameterCount);
    [[nodiscard]] static juce::Result loadMatrix(const DnniReader& reader, std::size_t nodeIndex, Matrix& packed, std::size_t& parameterCount);
    [[nodiscard]] static std::optional<std::size_t> findContextRadius(const Layer& layer);
    [[nodiscard]] static juce::Result runLayer(const Layer& layer, const DnniTensor& input, DnniTensor& output, const DnniTensor* condition, const std::function<bool()>& shouldCancel);
    [[nodiscard]] static juce::Result runGru(const Layer& layer, const DnniTensor& input, DnniTensor& output, bool reverse, const std::function<bool()>& shouldCancel);
    static void multiplyMatrix(const Matrix& matrix, const float* input, float* output);

    Layer root;
    std::optional<std::size_t> contextRadius;
    std::uint64_t modelIdentity = 0;
    bool loaded = false;
};
} // namespace sv::synthesis
