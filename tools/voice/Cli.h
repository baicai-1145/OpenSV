#pragma once

// Shared helpers for the opensv-voice CLI: JSON parsing helpers, time
// conversion (beats/seconds to blicks), and a uniform command harness.

#include "core/Project.h"
#include "core/ProjectFile.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace cli
{
// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

struct CommandError
{
    juce::String message;
};

// ---------------------------------------------------------------------------
// JSON input: inline file/stdin argument
// ---------------------------------------------------------------------------

[[nodiscard]] inline juce::String readStdinText()
{
    juce::String text;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), stdin) != nullptr)
    {
        text += juce::String::fromUTF8(buffer);
    }
    return text;
}

[[nodiscard]] inline juce::var readJsonInput(const juce::String& pathOrDash)
{
    juce::String text;
    if (pathOrDash == "-")
    {
        text = readStdinText();
    }
    else
    {
        const juce::File file(pathOrDash);
        if (!file.existsAsFile())
        {
            throw CommandError{"JSON input does not exist: " + pathOrDash};
        }
        text = file.loadFileAsString();
    }
    auto parsed = juce::JSON::parse(text);
    if (parsed.isVoid() || !parsed.isObject())
    {
        throw CommandError{"JSON input must be an object: " + pathOrDash};
    }
    return parsed;
}

// ---------------------------------------------------------------------------
// Errors and output helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline juce::String expectField(const juce::var& object, const char* name, const char* type)
{
    return juce::String("field \"") + name + "\" must be " + type + ".";
}

// ---------------------------------------------------------------------------
// JSON input helpers (LLM-friendly: beats/seconds/midi numbers in)
// ---------------------------------------------------------------------------

[[nodiscard]] inline bool parseJsonObject(const juce::String& text, juce::var& output, juce::String& error)
{
    output = juce::JSON::parse(text);
    if (output.isVoid())
    {
        error = "input is not valid JSON.";
        return false;
    }
    if (!output.isObject())
    {
        error = "input must be a JSON object.";
        return false;
    }
    return true;
}

// Reads @p key from @p object as a finite double, reporting @p context on error.
[[nodiscard]] inline bool readNumber(const juce::var& object, const char* key, double& value, const juce::String& context)
{
    const auto& field = object[key];
    if (field.isVoid())
    {
        return true; // optional, untouched
    }
    if (!field.isDouble() && !field.isInt() && !field.isInt64())
    {
        throw CommandError{context + ": " + expectField(object, key, "a number")};
    }
    value = static_cast<double>(field);
    if (!std::isfinite(value))
    {
        throw CommandError{context + ": " + expectField(object, key, "a finite number")};
    }
    return true;
}

[[nodiscard]] inline bool readString(const juce::var& object, const char* key, std::string& value, const juce::String& context)
{
    const auto& field = object[key];
    if (field.isVoid())
    {
        return true;
    }
    if (!field.isString())
    {
        throw CommandError{context + ": " + expectField(object, key, "a string")};
    }
    value = field.toString().toStdString();
    return true;
}

[[nodiscard]] inline bool readBool(const juce::var& object, const char* key, bool& value, const juce::String& context)
{
    const auto& field = object[key];
    if (field.isVoid())
    {
        return true;
    }
    if (!field.isBool())
    {
        throw CommandError{context + ": " + expectField(object, key, "a boolean")};
    }
    value = static_cast<bool>(field);
    return true;
}

[[nodiscard]] inline bool readInteger(const juce::var& object, const char* key, long long& value, const juce::String& context)
{
    const auto& field = object[key];
    if (field.isVoid())
    {
        return true;
    }
    if (!field.isInt() && !field.isInt64())
    {
        throw CommandError{context + ": " + expectField(object, key, "an integer")};
    }
    value = static_cast<long long>(field);
    return true;
}

// ---------------------------------------------------------------------------
// Time conversion. Commands accept musical time as doubles: "beats" (quarter
// notes) or "seconds"; blicks stay an internal detail of the file format.
// ---------------------------------------------------------------------------

inline constexpr double maximumBeats = 1.0e9;

[[nodiscard]] inline sv::Blick beatsToBlick(double beats)
{
    return static_cast<sv::Blick>(std::llround(beats * static_cast<double>(sv::blicksPerQuarter)));
}

[[nodiscard]] inline double blickToBeats(sv::Blick blick)
{
    return static_cast<double>(blick) / static_cast<double>(sv::blicksPerQuarter);
}

// Converts a JSON {beats|seconds} time field into blicks. Exactly one field
// must be present when required; both missing returns "absent".
[[nodiscard]] inline sv::Blick parseTime(const juce::var& object, const char* beatsKey, const char* secondsKey, const sv::TempoMap& tempo, bool required, bool& present, const juce::String& context)
{
    present = false;
    const auto beatsField = object[beatsKey];
    const auto secondsField = object[secondsKey];
    if (beatsField.isVoid() && secondsField.isVoid())
    {
        if (required)
        {
            throw CommandError{context + ": specify \"" + beatsKey + "\" or \"" + secondsKey + "\"."};
        }
        return 0;
    }
    if (!beatsField.isVoid() && !secondsField.isVoid())
    {
        throw CommandError{context + ": specify either \"" + beatsKey + "\" or \"" + secondsKey + "\", not both."};
    }
    double value = 0.0;
    if (!beatsField.isVoid())
    {
        if (!beatsField.isDouble() && !beatsField.isInt() && !beatsField.isInt64())
        {
            throw CommandError{context + ": " + expectField(object, beatsKey, "a number")};
        }
        value = static_cast<double>(beatsField);
        if (!std::isfinite(value) || std::abs(value) > maximumBeats)
        {
            throw CommandError{context + ": \"" + beatsKey + "\" is out of range."};
        }
        present = true;
        return beatsToBlick(value);
    }
    if (!secondsField.isDouble() && !secondsField.isInt() && !secondsField.isInt64())
    {
        throw CommandError{context + ": " + expectField(object, secondsKey, "a number")};
    }
    value = static_cast<double>(secondsField);
    if (!std::isfinite(value) || value < 0.0 || value > 3.6e6)
    {
        throw CommandError{context + ": \"" + secondsKey + "\" is out of range."};
    }
    present = true;
    return tempo.secondsToBlick(value);
}

// ---------------------------------------------------------------------------
// Project loading and saving
// ---------------------------------------------------------------------------

[[nodiscard]] inline sv::Project loadProjectOrThrow(const juce::File& file)
{
    sv::Project project;
    if (const auto result = sv::loadProjectFile(file, project); result.failed())
    {
        throw CommandError{result.getErrorMessage()};
    }
    return project;
}

inline void saveProjectOrThrow(const juce::File& file, const sv::Project& project)
{
    if (const auto result = sv::saveProjectFile(file, project); result.failed())
    {
        throw CommandError{result.getErrorMessage()};
    }
}

// Resolves --track by index (default 0) or name.
[[nodiscard]] inline sv::Track& resolveTrack(sv::Project& project, const juce::var& input)
{
    long long index = 0;
    readInteger(input, "track", index, "input");
    std::string name;
    readString(input, "name", name, "input");
    if (!name.empty())
    {
        for (auto& track : project.tracks)
        {
            if (track.name == name)
            {
                return track;
            }
        }
        throw CommandError{"no track named \"" + juce::String::fromUTF8(name.c_str()) + "\"."};
    }
    if (index < 0 || static_cast<std::size_t>(index) >= project.tracks.size())
    {
        throw CommandError{"track index " + juce::String(index) + " is out of range (project has " + juce::String(static_cast<int>(project.tracks.size())) + " tracks)."};
    }
    return project.tracks[static_cast<std::size_t>(index)];
}

// Locates a note by id across the main groups of all tracks.
[[nodiscard]] inline sv::Note& findNoteOrThrow(sv::Project& project, sv::NoteId id)
{
    for (auto& track : project.tracks)
    {
        for (auto& note : track.mainGroup.notes)
        {
            if (note.id == id)
            {
                return note;
            }
        }
    }
    throw CommandError{"no note with id " + juce::String(static_cast<juce::int64>(id)) + "."};
}
} // namespace cli
