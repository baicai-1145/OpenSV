#pragma once

#include <cstddef>

namespace sv::synthesis
{
// Accumulated synchronously by the worker; the caller owns and resets this data.
struct SynthesisStatistics
{
    std::size_t pitchFrames = 0;
    std::size_t acousticFrames = 0;
    std::size_t vocoderFrames = 0;
    // Network windows, reused outputs and context overlap, summed over networks.
    // Networks can use phoneme, acoustic or subband frames, so these are not audio duration.
    std::size_t dnniComputedFrames = 0;
    std::size_t dnniReusedFrames = 0;
    std::size_t dnniContextFrames = 0;
    double pitchMilliseconds = 0.0;
    double acousticMilliseconds = 0.0;
    double vocoderMilliseconds = 0.0;
    double vocoderNetworkMilliseconds = 0.0;
    double vocoderSourceMilliseconds = 0.0;
    double vocoderResidualMilliseconds = 0.0;
    double vocoderFilterMilliseconds = 0.0;
};
} // namespace sv::synthesis
