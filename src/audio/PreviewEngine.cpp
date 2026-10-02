#include "PreviewEngine.h"

#include "ProjectRenderer.h"
#include "WaveFile.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace sv::audio
{
namespace
{
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

constexpr double exportSampleRate = 48000.0;

juce::Result writeWav(ProjectRenderer& renderer, const Project& project, const juce::File& file, const std::function<bool()>& shouldCancel)
{
    try
    {
        juce::AudioBuffer<float> samples;
        const auto result = renderer.render(project, exportSampleRate, samples, shouldCancel);
        if (result.failed())
        {
            return result;
        }

        return writeWaveFile(file, samples, exportSampleRate, shouldCancel);
    }
    catch (const std::exception& exception)
    {
        return juce::Result::fail("WAV export failed: " + juce::String(exception.what()));
    }
    catch (...)
    {
        return juce::Result::fail("WAV export failed with an unexpected error.");
    }
}
} // namespace

PreviewEngine::PreviewEngine()
    : juce::Thread("Singing renderer")
{
    startThread();
}

PreviewEngine::~PreviewEngine()
{
    playing.store(false, std::memory_order_release);
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    signalThreadShouldExit();
    notify();
    waitForThreadToExit(-1);
    cancelPendingUpdate();
}

juce::Result PreviewEngine::initialise()
{
    if (initialised)
    {
        return juce::Result::ok();
    }

    const auto error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
    {
        return juce::Result::fail("Cannot open the audio output device: " + error);
    }

    deviceManager.addAudioCallback(this);
    initialised = true;
    return juce::Result::ok();
}

void PreviewEngine::setProject(const Project& newProject, std::uint64_t newDocumentGeneration)
{
    {
        const std::scoped_lock lock(projectMutex);
        project = newProject;
        if (documentGeneration != newDocumentGeneration)
        {
            visualization.reset();
        }
        documentGeneration = newDocumentGeneration;
        renderResult = juce::Result::ok();
        requestedRevision.fetch_add(1, std::memory_order_release);
    }
    notify();
}

void PreviewEngine::play()
{
    const std::scoped_lock lock(projectMutex);
    if (completedRevision.load(std::memory_order_acquire) == requestedRevision.load(std::memory_order_acquire) && renderResult.failed())
    {
        return;
    }

    const double endSeconds = project.tempoMap.blickToSeconds(getProjectEnd(project));
    if (getPositionSeconds() >= endSeconds)
    {
        seek(0);
    }
    playing.store(true, std::memory_order_release);
}

void PreviewEngine::pause()
{
    playing.store(false, std::memory_order_release);
}

void PreviewEngine::stop()
{
    pause();
    seek(0);
}

void PreviewEngine::seek(Blick position)
{
    const double seconds = std::max(0.0, project.tempoMap.blickToSeconds(position));
    pendingSeekSeconds.store(seconds, std::memory_order_release);
}

void PreviewEngine::setAuditionPitch(int midiPitch)
{
    pitchAudition.setPitch(midiPitch);
}

bool PreviewEngine::isPlaying() const
{
    return playing.load(std::memory_order_acquire);
}

bool PreviewEngine::isRendering() const
{
    return completedRevision.load(std::memory_order_acquire) != requestedRevision.load(std::memory_order_acquire);
}

bool PreviewEngine::isExporting() const
{
    return exporting.load(std::memory_order_acquire);
}

double PreviewEngine::getPositionSeconds() const
{
    const double requestedSeconds = pendingSeekSeconds.load(std::memory_order_acquire);
    return requestedSeconds >= 0.0 ? requestedSeconds : positionSeconds.load(std::memory_order_acquire);
}

Blick PreviewEngine::getPositionBlick() const
{
    return project.tempoMap.secondsToBlick(getPositionSeconds());
}

juce::Result PreviewEngine::getRenderResult() const
{
    const std::scoped_lock lock(projectMutex);
    return renderResult;
}

RenderStatistics PreviewEngine::getRenderStatistics() const
{
    const std::scoped_lock lock(projectMutex);
    return renderStatistics;
}

std::shared_ptr<const RenderVisualization> PreviewEngine::getVisualization() const
{
    const std::scoped_lock lock(projectMutex);
    return visualization;
}

juce::AudioDeviceManager& PreviewEngine::getDeviceManager()
{
    return deviceManager;
}

void PreviewEngine::exportWavAsync(const juce::File& file, std::function<void(juce::Result)> completion)
{
    if (isExporting())
    {
        if (completion)
        {
            completion(juce::Result::fail("A WAV export is already running."));
        }
        return;
    }

    {
        const std::scoped_lock lock(projectMutex);
        pendingExport.emplace(ExportRequest{project, file, std::move(completion)});
        exporting.store(true, std::memory_order_release);
    }
    notify();
}

void PreviewEngine::handleAsyncUpdate()
{
    std::optional<ExportCompletion> completion;
    {
        const std::scoped_lock lock(projectMutex);
        completion = std::move(exportCompletion);
        exportCompletion.reset();
    }
    exporting.store(false, std::memory_order_release);
    if (completion.has_value() && completion->callback)
    {
        completion->callback(completion->result);
    }
}

void PreviewEngine::run()
{
    // Cached voice models are created, used, and released only on this worker.
    ProjectRenderer renderer;
    int backSlot = 2;
    std::uint64_t handledRevision = 0;
    while (!threadShouldExit())
    {
        std::optional<ExportRequest> exportRequest;
        {
            const std::scoped_lock lock(projectMutex);
            exportRequest = std::move(pendingExport);
            pendingExport.reset();
        }
        if (exportRequest.has_value())
        {
            const auto result = writeWav(renderer, exportRequest->project, exportRequest->file, [this]
                                         { return threadShouldExit(); });
            if (!threadShouldExit())
            {
                {
                    const std::scoped_lock lock(projectMutex);
                    exportCompletion.emplace(ExportCompletion{std::move(exportRequest->completion), result});
                }
                triggerAsyncUpdate();
            }
            continue;
        }

        if (requestedRevision.load(std::memory_order_acquire) == handledRevision)
        {
            wait(100);
            continue;
        }

        std::uint64_t revision = 0;
        auto result = juce::Result::ok();
        RenderStatistics statistics;
        std::shared_ptr<RenderVisualization> renderedVisualization;
        try
        {
            Project snapshot;
            std::uint64_t snapshotDocumentGeneration = 0;
            {
                const std::scoped_lock lock(projectMutex);
                revision = requestedRevision.load(std::memory_order_acquire);
                snapshot = project;
                snapshotDocumentGeneration = documentGeneration;
            }
            renderedVisualization = std::make_shared<RenderVisualization>();
            auto& audio = renderedAudio[static_cast<std::size_t>(backSlot)];
            audio.sampleRate = deviceSampleRate.load(std::memory_order_acquire);
            result = renderer.render(snapshot, audio.sampleRate, audio.samples, [this, revision]
                                     { return threadShouldExit() || requestedRevision.load(std::memory_order_acquire) != revision; },
                                     renderedVisualization.get());
            renderedVisualization->documentGeneration = snapshotDocumentGeneration;
            statistics = renderer.getStatistics();
            audio.revision = revision;
        }
        catch (const std::exception& exception)
        {
            result = juce::Result::fail("Preview rendering failed: " + juce::String(exception.what()));
        }
        catch (...)
        {
            result = juce::Result::fail("Preview rendering failed with an unexpected error.");
        }

        handledRevision = revision;
        {
            const std::scoped_lock lock(projectMutex);
            if (threadShouldExit() || requestedRevision.load(std::memory_order_acquire) != revision)
            {
                continue;
            }

            renderResult = result;
            renderStatistics = statistics;
            if (result.wasOk())
            {
                visualization = std::move(renderedVisualization);
                backSlot = middleSlot.exchange(backSlot | updatedFlag, std::memory_order_acq_rel) & slotMask;
            }
            else
            {
                playing.store(false, std::memory_order_release);
            }
            completedRevision.store(revision, std::memory_order_release);
        }
    }
}

void PreviewEngine::audioDeviceIOCallbackWithContext(const float* const*,
                                                     int,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
        {
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        }
    }

    if ((middleSlot.load(std::memory_order_acquire) & updatedFlag) != 0)
    {
        frontSlot = middleSlot.exchange(frontSlot, std::memory_order_acq_rel) & slotMask;
    }
    const double seekSeconds = pendingSeekSeconds.exchange(-1.0, std::memory_order_acq_rel);
    if (seekSeconds >= 0.0)
    {
        callbackPositionSeconds = seekSeconds;
        positionSeconds.store(seekSeconds, std::memory_order_release);
    }

    renderPlayback(outputChannelData, numOutputChannels, numSamples);
    pitchAudition.addToOutput(outputChannelData, numOutputChannels, numSamples);
}

void PreviewEngine::renderPlayback(float* const* outputChannelData, int numOutputChannels, int numSamples)
{
    const auto& audio = renderedAudio[static_cast<std::size_t>(frontSlot)];
    if (!playing.load(std::memory_order_acquire) || audio.revision == 0 || audio.revision != requestedRevision.load(std::memory_order_acquire) || audio.sampleRate != deviceSampleRate.load(std::memory_order_acquire))
    {
        return;
    }

    const double startSample = std::round(callbackPositionSeconds * audio.sampleRate);
    if (startSample >= audio.samples.getNumSamples())
    {
        playing.store(false, std::memory_order_release);
        return;
    }

    const int offset = static_cast<int>(startSample);
    const int samplesToCopy = std::min(numSamples, audio.samples.getNumSamples() - offset);
    if (numOutputChannels == 1 && outputChannelData[0] != nullptr)
    {
        juce::FloatVectorOperations::copy(outputChannelData[0], audio.samples.getReadPointer(0, offset), samplesToCopy);
        juce::FloatVectorOperations::add(outputChannelData[0], audio.samples.getReadPointer(1, offset), samplesToCopy);
        juce::FloatVectorOperations::multiply(outputChannelData[0], 0.5f, samplesToCopy);
    }
    else
    {
        for (int channel = 0; channel < std::min(numOutputChannels, 2); ++channel)
        {
            if (outputChannelData[channel] != nullptr)
            {
                juce::FloatVectorOperations::copy(outputChannelData[channel], audio.samples.getReadPointer(channel, offset), samplesToCopy);
            }
        }
    }

    callbackPositionSeconds = static_cast<double>(offset + samplesToCopy) / audio.sampleRate;
    positionSeconds.store(callbackPositionSeconds, std::memory_order_release);
    if (samplesToCopy < numSamples)
    {
        playing.store(false, std::memory_order_release);
    }
}

void PreviewEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    pitchAudition.prepare(device->getCurrentSampleRate());
    deviceSampleRate.store(device->getCurrentSampleRate(), std::memory_order_release);
    requestedRevision.fetch_add(1, std::memory_order_release);
    notify();
}

void PreviewEngine::audioDeviceStopped()
{
    pitchAudition.reset();
    playing.store(false, std::memory_order_release);
}
} // namespace sv::audio
