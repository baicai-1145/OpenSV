#pragma once

#include "PitchAudition.h"
#include "RenderStatistics.h"
#include "RenderVisualization.h"
#include "core/Project.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace sv::audio
{
// All public methods run on the message thread. Singing synthesis and export
// share one worker-owned renderer; the audio callback reads completed buffers.
class PreviewEngine final : private juce::AudioIODeviceCallback,
                            private juce::Thread,
                            private juce::AsyncUpdater
{
public:
    PreviewEngine();
    ~PreviewEngine() override;

    [[nodiscard]] juce::Result initialise();
    void setProject(const Project& project, std::uint64_t documentGeneration);

    void play();
    void pause();
    void stop();
    void seek(Blick position);
    // A negative pitch releases the independent keyboard reference tone.
    void setAuditionPitch(int midiPitch);

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isRendering() const;
    [[nodiscard]] bool isExporting() const;
    [[nodiscard]] double getPositionSeconds() const;
    [[nodiscard]] Blick getPositionBlick() const;
    [[nodiscard]] juce::Result getRenderResult() const;
    [[nodiscard]] RenderStatistics getRenderStatistics() const;
    [[nodiscard]] std::shared_ptr<const RenderVisualization> getVisualization() const;
    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager();

    // Export uses a project snapshot and 48 kHz, 24-bit stereo PCM. The caller
    // confirms replacement; a failed export preserves the old file. Async
    // completion runs on the message thread and is cancelled on destruction.
    void exportWavAsync(const juce::File& file, std::function<void(juce::Result)> completion);

private:
    struct RenderedAudio
    {
        juce::AudioBuffer<float> samples;
        double sampleRate = 48000.0;
        std::uint64_t revision = 0;
    };

    struct ExportRequest
    {
        Project project;
        juce::File file;
        std::function<void(juce::Result)> completion;
    };

    struct ExportCompletion
    {
        std::function<void(juce::Result)> callback;
        juce::Result result;
    };

    void run() override;
    void handleAsyncUpdate() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void renderPlayback(float* const* outputChannelData, int numOutputChannels, int numSamples);

    juce::AudioDeviceManager deviceManager;
    PitchAudition pitchAudition;
    mutable std::mutex projectMutex;
    Project project;
    std::uint64_t documentGeneration = 0;
    juce::Result renderResult = juce::Result::ok();
    RenderStatistics renderStatistics;
    // Last successful display for this document, retained while edits render.
    // Worker/message-thread ownership only; independent of the realtime slots.
    std::shared_ptr<const RenderVisualization> visualization;
    std::atomic<std::uint64_t> requestedRevision{1};
    std::atomic<std::uint64_t> completedRevision{0};
    std::atomic<double> deviceSampleRate{48000.0};
    std::atomic<double> positionSeconds{0.0};
    std::atomic<double> pendingSeekSeconds{-1.0};
    std::atomic<bool> playing{false};
    std::atomic<bool> exporting{false};
    std::optional<ExportRequest> pendingExport;
    std::optional<ExportCompletion> exportCompletion;
    bool initialised = false;

    // The callback owns the front slot, the render thread owns the back slot,
    // and the atomic middle slot transfers ownership between them. Only the
    // render thread allocates or releases sample storage, including old renders.
    static constexpr int updatedFlag = 4;
    static constexpr int slotMask = 3;
    std::array<RenderedAudio, 3> renderedAudio;
    std::atomic<int> middleSlot{1};
    int frontSlot = 0;
    double callbackPositionSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviewEngine)
};
} // namespace sv::audio
