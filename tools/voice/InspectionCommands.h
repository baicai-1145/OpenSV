#pragma once

// Inspection and voice-database commands: inspect-project (--json),
// dump-phonemes, validate, and the enhanced render-project.

#include "Cli.h"
#include "audio/ProjectRenderer.h"
#include "audio/RenderVisualization.h"
#include "audio/WaveFile.h"
#include "core/ProjectFile.h"
#include "synthesis/DnniReader.h"
#include "synthesis/PhoneSet.h"
#include "synthesis/VoiceDatabase.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace cli
{
// ---------------------------------------------------------------------------
// inspect-project --json: machine-readable dump used by LLM tool loops
// ---------------------------------------------------------------------------

inline void printJsonString(const char* key, const std::string& value, bool last = false)
{
    juce::String escaped(value.c_str(), static_cast<int>(value.size()));
    std::printf("  \"%s\": \"%s\"%s\n", key, escaped.replace("\\", "\\\\").replace("\"", "\\\"").toRawUTF8(), last ? "" : ",");
}

inline int inspectProject(const juce::StringArray& arguments)
{
    if (arguments.isEmpty())
    {
        throw CommandError{"usage: inspect-project <project.svp> [--json]"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const bool json = arguments.contains("--json");
    if (!json)
    {
        std::printf("name: %s\ntracks: %zu\n", project.name.c_str(), project.tracks.size());
    }
    else
    {
        std::printf("{\n\"name\": \"%s\",\n\"bpm\": %s,\n\"tracks\": [\n",
                    project.name.c_str(),
                    juce::String(project.tempoMap.tempos.empty() ? 120.0 : project.tempoMap.tempos.front().bpm).toRawUTF8());
    }
    for (std::size_t trackIndex = 0; trackIndex < project.tracks.size(); ++trackIndex)
    {
        const auto& track = project.tracks[trackIndex];
        if (!json)
        {
            std::printf("  track '%s' notes=%zu voice=%s lang=%s\n",
                        track.name.c_str(), track.mainGroup.notes.size(),
                        track.voice.databasePath.c_str(), track.voice.language.c_str());
            continue;
        }
        std::printf("  {\n  \"index\": %d,\n", static_cast<int>(trackIndex));
        printJsonString("name", track.name);
        printJsonString("voice", track.voice.databasePath);
        printJsonString("language", track.voice.language);
        printJsonString("dictionary", track.voice.dictionaryDirectory);
        std::printf("  \"gain\": %s,\n  \"pan\": %s,\n  \"mute\": %s,\n  \"solo\": %s,\n  \"notes\": [\n",
                    juce::String(track.gain, 6).toRawUTF8(),
                    juce::String(track.pan, 4).toRawUTF8(),
                    track.mute ? "true" : "false",
                    track.solo ? "true" : "false");
        const auto& notes = track.mainGroup.notes;
        for (std::size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex)
        {
            const auto& note = notes[noteIndex];
            const double beats = blickToBeats(note.onset);
            const double durationBeats = blickToBeats(note.duration);
            std::printf("    {\"id\": %lld, \"onset\": %.6f, \"duration\": %.6f, \"pitch\": %d, \"lyrics\": \"%s\", \"phonemes\": \"%s\", \"detune\": %s, \"instantMode\": %s, \"musicalType\": \"%s\"",
                        static_cast<long long>(note.id),
                        beats,
                        durationBeats,
                        note.pitch,
                        note.lyrics.c_str(),
                        note.phonemes.c_str(),
                        juce::String(note.detune, 4).toRawUTF8(),
                        note.instantMode ? "true" : "false",
                        note.musicalType.c_str());
            const auto attribute = [](const std::optional<double>& value)
            {
                return value.has_value() ? juce::String(*value, 6) : juce::String("null");
            };
            std::printf(", \"attributes\": {\"tF0Offset\": %s, \"tF0Left\": %s, \"tF0Right\": %s, \"dF0Left\": %s, \"dF0Right\": %s, \"tF0VbrStart\": %s, \"tF0VbrLeft\": %s, \"tF0VbrRight\": %s, \"dF0Vbr\": %s, \"fF0Vbr\": %s, \"pF0Vbr\": %s, \"dF0VbrMod\": %s}",
                        attribute(note.attributes.tF0Offset).toRawUTF8(),
                        attribute(note.attributes.tF0Left).toRawUTF8(),
                        attribute(note.attributes.tF0Right).toRawUTF8(),
                        attribute(note.attributes.dF0Left).toRawUTF8(),
                        attribute(note.attributes.dF0Right).toRawUTF8(),
                        attribute(note.attributes.tF0VbrStart).toRawUTF8(),
                        attribute(note.attributes.tF0VbrLeft).toRawUTF8(),
                        attribute(note.attributes.tF0VbrRight).toRawUTF8(),
                        attribute(note.attributes.dF0Vbr).toRawUTF8(),
                        attribute(note.attributes.fF0Vbr).toRawUTF8(),
                        attribute(note.attributes.pF0Vbr).toRawUTF8(),
                        attribute(note.attributes.dF0VbrMod).toRawUTF8());
            const auto printCurve = [](const char* key, const sv::ParameterCurve& curve, bool last)
            {
                std::printf("%s\"%s\": {\"mode\": \"%s\", \"points\": [", last ? "" : ", ", key, curve.mode.c_str());
                for (std::size_t point = 0; point < curve.points.size(); ++point)
                {
                    std::printf("%s{\"beats\": %.6f, \"v\": %.6f}",
                                point == 0 ? "" : ", ",
                                blickToBeats(curve.points[point].position),
                                curve.points[point].value);
                }
                std::printf("]}%s", last ? "" : ",");
            };
            // Curves are group-level; emit them on every note entry.
            printCurve("pitchDelta", track.mainGroup.pitchDelta, false);
            printCurve("vibratoEnv", track.mainGroup.vibratoEnv, true);
            std::printf("}%s\n", noteIndex + 1 == notes.size() ? "" : ",");
        }
        std::printf("  ]}%s\n", trackIndex + 1 == project.tracks.size() ? "" : ",");
    }
    if (json)
    {
        std::printf("]}\n");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// dump-phonemes: walk the acoustic model dnni for _psv2 phoneme tables
// ---------------------------------------------------------------------------

inline void collectPhoneSets(const sv::synthesis::DnniReader& reader, std::size_t nodeIndex, std::vector<sv::synthesis::PhoneSet>& output)
{
    const auto& nodes = reader.getNodes();
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (nodes[index].type != "_psv2")
        {
            continue;
        }
        sv::synthesis::PhoneSet phoneSet;
        if (readPhoneSet(reader, index, phoneSet).wasOk())
        {
            output.push_back(std::move(phoneSet));
        }
    }
}

inline int dumpPhonemes(const juce::StringArray& arguments)
{
    if (arguments.isEmpty())
    {
        throw CommandError{"usage: dump-phonemes <voice.nofs> [--json]"};
    }
    const bool json = arguments.contains("--json");
    sv::synthesis::VoiceDatabase database;
    if (const auto result = database.open(juce::File(arguments[0])); result.failed())
    {
        throw CommandError{result.getErrorMessage()};
    }
    // The acoustic model (model_timbre_pred) holds the phoneme tables.
    // Open every large binary entry and harvest _psv2 nodes.
    std::vector<sv::synthesis::PhoneSet> phoneSets;
    for (const auto& entry : database.getEntries())
    {
        if (entry.valueSize < 1000)
        {
            continue; // metadata and small blobs
        }
        juce::MemoryBlock bytes;
        if (database.readEntry(entry, bytes).failed())
        {
            continue;
        }
        sv::synthesis::DnniReader reader;
        if (reader.load(std::move(bytes)).failed())
        {
            continue;
        }
        collectPhoneSets(reader, 0, phoneSets);
    }
    // The unified table (largest symbol coverage) is what rendering indexes.
    const sv::synthesis::PhoneSet* unified = nullptr;
    for (const auto& phoneSet : phoneSets)
    {
        if (unified == nullptr || phoneSet.symbols.size() > unified->symbols.size())
        {
            unified = &phoneSet;
        }
    }
    if (unified == nullptr)
    {
        throw CommandError{"no phoneme tables found in the voice database."};
    }
    const auto& metadata = database.getMetadata();
    if (!json)
    {
        std::printf("voice:     %s\n", metadata.name.toRawUTF8());
        std::printf("phoneset:  %s\n", metadata.phoneset.toRawUTF8());
        std::printf("languages: %s\n", metadata.languages.joinIntoString(" ").toRawUTF8());
        std::printf("symbols:   %zu\n", unified->symbols.size());
        for (std::size_t index = 0; index < unified->symbols.size(); ++index)
        {
            std::printf("  %-6s %-10s -> %s\n",
                        unified->symbols[index].c_str(),
                        unified->categories[index].c_str(),
                        unified->unifiedSymbols[index].c_str());
        }
        return 0;
    }
    std::printf("{\n  \"voice\": \"%s\",\n  \"phoneset\": \"%s\",\n  \"languages\": \"%s\",\n  \"symbols\": [\n",
                metadata.name.toRawUTF8(),
                metadata.phoneset.toRawUTF8(),
                metadata.languages.joinIntoString(" ").toRawUTF8());
    // JSON-escape helper shared by symbol output.
    const auto escapeSymbol = [](const std::string& text)
    {
        juce::String escaped(text.c_str(), static_cast<int>(text.size()));
        return escaped.replace("\\", "\\\\").replace("\"", "\\\"").toStdString();
    };
    for (std::size_t index = 0; index < unified->symbols.size(); ++index)
    {
        std::printf("    {\"symbol\": \"%s\", \"category\": \"%s\", \"unified\": \"%s\"}%s\n",
                    escapeSymbol(unified->symbols[index]).c_str(),
                    escapeSymbol(unified->categories[index]).c_str(),
                    escapeSymbol(unified->unifiedSymbols[index]).c_str(),
                    index + 1 == unified->symbols.size() ? "" : ",");
    }
    std::printf("  ]\n}\n");
    return 0;
}

// ---------------------------------------------------------------------------
// validate: phoneme legality, timing sanity, continuation markers
// ---------------------------------------------------------------------------

inline int validateProject(const juce::StringArray& arguments)
{
    if (arguments.isEmpty())
    {
        throw CommandError{"usage: validate <project.svp>"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    juce::StringArray problems;
    // Load the referenced voice phoneme tables when available.
    std::set<std::string> validSymbols;
    bool haveSymbols = false;
    if (!project.tracks.empty() && !project.tracks.front().voice.databasePath.empty())
    {
        sv::synthesis::VoiceDatabase database;
        if (const auto result = database.open(juce::File(juce::String::fromUTF8(project.tracks.front().voice.databasePath.c_str()))); result.wasOk())
        {
            for (const auto& entry : database.getEntries())
            {
                if (entry.valueSize < 1000)
                {
                    continue;
                }
                juce::MemoryBlock bytes;
                if (database.readEntry(entry, bytes).failed())
                {
                    continue;
                }
                sv::synthesis::DnniReader reader;
                if (reader.load(std::move(bytes)).failed())
                {
                    continue;
                }
                std::vector<sv::synthesis::PhoneSet> phoneSets;
                collectPhoneSets(reader, 0, phoneSets);
                const sv::synthesis::PhoneSet* unified = nullptr;
                for (const auto& phoneSet : phoneSets)
                {
                    if (unified == nullptr || phoneSet.symbols.size() > unified->symbols.size())
                    {
                        unified = &phoneSet;
                    }
                }
                if (unified != nullptr)
                {
                    for (const auto& symbol : unified->symbols)
                    {
                        validSymbols.insert(symbol);
                    }
                    haveSymbols = true;
                }
                break; // the acoustic model is the first large entry
            }
        }
        else
        {
            problems.add("voice database failed to open: " + result.getErrorMessage());
        }
    }
    for (const auto& track : project.tracks)
    {
        for (const auto& note : track.mainGroup.notes)
        {
            const juce::String context = "track '" + juce::String::fromUTF8(track.name.c_str()) + "' note " + juce::String(static_cast<juce::int64>(note.id)) + " (" + juce::String::fromUTF8(note.lyrics.c_str()) + ")";
            if (note.lyrics == "+" || note.lyrics == "-")
            {
                problems.add(context + ": continuation lyrics +/- are not implemented; merge into one note.");
            }
            if (note.duration <= 0)
            {
                problems.add(context + ": non-positive duration.");
            }
            if (note.phonemes.empty() && track.voice.dictionaryDirectory.empty())
            {
                problems.add(context + ": no phonemes and no dictionary directory; rendering will fail. Set phonemes explicitly.");
            }
            if (haveSymbols && !note.phonemes.empty())
            {
                juce::StringArray fields = juce::StringArray::fromTokens(juce::String::fromUTF8(note.phonemes.c_str()), " \t\r\n", "");
                fields.removeEmptyStrings();
                for (const auto& field : fields)
                {
                    if (!validSymbols.contains(field.toStdString()))
                    {
                        problems.add(context + ": phoneme \"" + field + "\" is not in the voice phoneme table.");
                    }
                }
            }
        }
    }
    const bool ok = problems.isEmpty();
    std::printf("{\n  \"valid\": %s,\n  \"problems\": [\n", ok ? "true" : "false");
    for (int index = 0; index < problems.size(); ++index)
    {
        std::printf("    \"%s\"%s\n", problems[index].replace("\\", "\\\\").replace("\"", "\\\"").toRawUTF8(), index + 1 == problems.size() ? "" : ",");
    }
    std::printf("  ]\n}\n");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// render-project with --range, --f0 and --json
// ---------------------------------------------------------------------------

inline int renderProject(const juce::StringArray& arguments)
{
    if (arguments.size() < 2)
    {
        throw CommandError{"usage: render-project <project.svp> <output.wav> [--rate 48000] [--range startSeconds:endSeconds] [--f0 <f0.csv>] [--json]"};
    }
    const juce::File projectFile(arguments[0]);
    const juce::File outputFile(arguments[1]);
    double sampleRate = 48000.0;
    double rangeStart = -1.0;
    double rangeEnd = -1.0;
    juce::File f0File;
    bool json = false;
    for (int index = 2; index < arguments.size(); ++index)
    {
        if (arguments[index] == "--rate" && index + 1 < arguments.size())
        {
            sampleRate = arguments[++index].getDoubleValue();
        }
        else if (arguments[index] == "--range" && index + 1 < arguments.size())
        {
            const auto range = arguments[++index];
            const auto separator = range.indexOfChar(':');
            if (separator <= 0 || separator == range.length() - 1)
            {
                throw CommandError{"--range must be startSeconds:endSeconds."};
            }
            rangeStart = range.substring(0, separator).getDoubleValue();
            rangeEnd = range.substring(separator + 1).getDoubleValue();
            if (rangeStart < 0.0 || rangeEnd <= rangeStart)
            {
                throw CommandError{"--range must satisfy 0 <= start < end."};
            }
        }
        else if (arguments[index] == "--f0" && index + 1 < arguments.size())
        {
            f0File = juce::File(arguments[++index]);
        }
        else if (arguments[index] == "--json")
        {
            json = true;
        }
        else
        {
            throw CommandError{"unknown option: " + arguments[index]};
        }
    }
    sv::Project project = loadProjectOrThrow(projectFile);
    // Range rendering: crop the score to notes overlapping the window, with a
    // small context margin so attack consonants keep their preceding rest.
    const bool useRange = rangeStart >= 0.0;
    if (useRange)
    {
        const auto startBlick = project.tempoMap.secondsToBlick(std::max(0.0, rangeStart - 0.5));
        const auto endBlick = project.tempoMap.secondsToBlick(rangeEnd + 1.0);
        const auto shiftBlick = startBlick;
        for (auto& track : project.tracks)
        {
            auto& notes = track.mainGroup.notes;
            const auto kept = std::remove_if(notes.begin(), notes.end(), [&](const sv::Note& note)
                                             { return note.onset + note.duration < startBlick || note.onset > endBlick; });
            notes.erase(kept, notes.end());
            for (auto& note : notes)
            {
                note.onset = std::max<sv::Blick>(0, note.onset - shiftBlick);
            }
            // Keep curves inside the shifted timeline.
            for (auto* curve : {&track.mainGroup.pitchDelta, &track.mainGroup.vibratoEnv})
            {
                std::vector<sv::AutomationPoint> shifted;
                shifted.reserve(curve->points.size());
                for (auto point : curve->points)
                {
                    const auto position = point.position - shiftBlick;
                    if (position >= 0)
                    {
                        shifted.push_back({position, point.value});
                    }
                }
                curve->points = std::move(shifted);
            }
        }
        sv::normaliseProject(project);
    }
    sv::audio::ProjectRenderer renderer;
    sv::audio::RenderVisualization visualization;
    juce::AudioBuffer<float> buffer;
    if (const auto result = renderer.render(project, sampleRate, buffer, {}, &visualization); result.failed())
    {
        throw CommandError{result.getErrorMessage()};
    }
    if (useRange)
    {
        // Trim silence outside the requested window.
        const int startSample = static_cast<int>(std::floor(rangeStart * sampleRate));
        const int endSample = std::min(static_cast<int>(std::floor(rangeEnd * sampleRate)), buffer.getNumSamples());
        // Export the requested window only.
        const int length = std::max(0, endSample - startSample);
        juce::AudioBuffer<float> window(2, length);
        for (int channel = 0; channel < 2; ++channel)
        {
            window.copyFrom(channel, 0, buffer, channel, startSample, length);
        }
        if (const auto result = sv::audio::writeWaveFile(outputFile, window, sampleRate); result.failed())
        {
            throw CommandError{result.getErrorMessage()};
        }
    }
    else if (const auto result = sv::audio::writeWaveFile(outputFile, buffer, sampleRate); result.failed())
    {
        throw CommandError{result.getErrorMessage()};
    }
    if (f0File != juce::File())
    {
        // seconds,midi_pitch per frame across all phrases of all tracks.
        if (!f0File.getParentDirectory().createDirectory())
        {
            // Existing directory is fine; only hard failure surfaces.
        }
        juce::FileOutputStream stream(f0File);
        if (stream.openedOk())
        {
            stream.writeText("seconds,midi_pitch\n", false, false, nullptr);
            for (const auto& trackVisualization : visualization.tracks)
            {
                for (const auto& phrase : trackVisualization.phrases)
                {
                    if (phrase.data == nullptr)
                    {
                        continue;
                    }
                    const auto& pitch = phrase.data->midiPitch;
                    const double interval = phrase.data->frameIntervalSeconds;
                    for (std::size_t frame = 0; frame < pitch.size(); ++frame)
                    {
                        stream.writeText(juce::String(phrase.startSeconds + static_cast<double>(frame) * interval, 6) + "," + juce::String(pitch[frame], 4) + "\n", false, false, nullptr);
                    }
                }
            }
            stream.flush();
        }
        else
        {
            throw CommandError{"cannot write F0 output: " + f0File.getFullPathName()};
        }
    }
    const auto& statistics = renderer.getStatistics();
    if (json)
    {
        std::printf("{\n  \"output\": \"%s\",\n  \"samples\": %d,\n  \"seconds\": %.3f,\n  \"phrases\": %zu,\n  \"rendered\": %zu,\n  \"reused\": %zu,\n  \"elapsed_ms\": %.1f\n}\n",
                    outputFile.getFullPathName().toRawUTF8(),
                    buffer.getNumSamples(),
                    buffer.getNumSamples() / sampleRate,
                    statistics.totalPhrases,
                    statistics.renderedPhrases,
                    statistics.reusedPhrases,
                    statistics.elapsedMilliseconds);
    }
    else
    {
        std::printf("wrote %s\n", outputFile.getFullPathName().toRawUTF8());
        std::printf("samples: %d  seconds: %.3f  phrases: %zu (rendered %zu, reused %zu)\n", buffer.getNumSamples(), buffer.getNumSamples() / sampleRate, statistics.totalPhrases, statistics.renderedPhrases, statistics.reusedPhrases);
        std::printf("elapsed: %.1f ms (model load %.1f ms)\n", statistics.elapsedMilliseconds, statistics.modelLoadMilliseconds);
    }
    return 0;
}
} // namespace cli
