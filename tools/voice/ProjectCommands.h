#pragma once

// Project-level commands: new-project, edit-notes, set-pitch-attrs,
// set-pitch-curve, set-vibrato-env, set-tempo, set-track, set-voice.

#include "Cli.h"
#include "core/ProjectFile.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <string>

namespace cli
{
namespace
{
constexpr double maximumDetuneCents = 2400.0;

// Values used by the renderer when an attribute is absent on every level;
// mirrored here so validate can warn about hopeless values.
constexpr double maximumAttributeSeconds = 10.0;

void applyPitchAttributes(const juce::var& input, sv::PitchAttributes& attributes, const juce::String& context)
{
    static const struct
    {
        const char* name;
        std::optional<double> sv::PitchAttributes::*member;
        double limit;
    } fields[] = {
        {"tF0Offset", &sv::PitchAttributes::tF0Offset, maximumAttributeSeconds},
        {"tF0Left", &sv::PitchAttributes::tF0Left, maximumAttributeSeconds},
        {"tF0Right", &sv::PitchAttributes::tF0Right, maximumAttributeSeconds},
        {"tF0VbrStart", &sv::PitchAttributes::tF0VbrStart, maximumAttributeSeconds},
        {"tF0VbrLeft", &sv::PitchAttributes::tF0VbrLeft, maximumAttributeSeconds},
        {"tF0VbrRight", &sv::PitchAttributes::tF0VbrRight, maximumAttributeSeconds},
        {"dF0Left", &sv::PitchAttributes::dF0Left, 24.0},
        {"dF0Right", &sv::PitchAttributes::dF0Right, 24.0},
        {"dF0Vbr", &sv::PitchAttributes::dF0Vbr, 12.0},
        {"fF0Vbr", &sv::PitchAttributes::fF0Vbr, 16.0},
        {"pF0Vbr", &sv::PitchAttributes::pF0Vbr, 6.28318530718},
        {"dF0VbrMod", &sv::PitchAttributes::dF0VbrMod, 1.0},
    };
    for (const auto& field : fields)
    {
        double value = 0.0;
        const auto& raw = input[field.name];
        if (raw.isVoid())
        {
            continue;
        }
        if (raw.isString() && raw.toString().isEmpty())
        {
            // Explicit null clears an attribute back to inheritance.
            attributes.*field.member = std::nullopt;
            continue;
        }
        if (!raw.isDouble() && !raw.isInt() && !raw.isInt64())
        {
            throw CommandError{context + ": \"" + field.name + "\" must be a number or null."};
        }
        value = static_cast<double>(raw);
        if (!std::isfinite(value) || std::abs(value) > field.limit)
        {
            throw CommandError{context + ": \"" + field.name + "\" is out of range (|x| <= " + juce::String(field.limit) + ")."};
        }
        attributes.*field.member = value;
    }
}

sv::ParameterCurve parseCurve(const juce::var& input, const sv::TempoMap& tempo, const juce::String& context)
{
    sv::ParameterCurve curve;
    std::string mode;
    readString(input, "mode", mode, context);
    if (!mode.empty())
    {
        if (mode != "linear" && mode != "cubic" && mode != "cosine")
        {
            throw CommandError{context + ": mode must be linear, cubic or cosine."};
        }
        curve.mode = mode;
    }
    const auto& pointsField = input["points"];
    if (!pointsField.isVoid() && !pointsField.isArray())
    {
        throw CommandError{context + ": points must be an array of {t,v} or {beats,v}/{seconds,v} objects."};
    }
    if (pointsField.isArray())
    {
        for (const auto& item : *pointsField.getArray())
        {
            if (!item.isObject())
            {
                throw CommandError{context + ": each point must be an object."};
            }
            bool present = false;
            const auto position = parseTime(item, "beats", "seconds", tempo, false, present, context + " point");
            double value = 0.0;
            bool valuePresent = false;
            const auto& rawValue = item["v"];
            if (!rawValue.isVoid())
            {
                if (!rawValue.isDouble() && !rawValue.isInt() && !rawValue.isInt64())
                {
                    throw CommandError{context + ": point field \"v\" must be a number."};
                }
                value = static_cast<double>(rawValue);
                valuePresent = true;
            }
            if (!present || !valuePresent)
            {
                throw CommandError{context + ": each point needs a time (beats or seconds) and a value (v)."};
            }
            if (!std::isfinite(value))
            {
                throw CommandError{context + ": point value must be finite."};
            }
            curve.points.push_back({position, value});
        }
        std::stable_sort(curve.points.begin(), curve.points.end(), [](const sv::AutomationPoint& a, const sv::AutomationPoint& b)
                         { return a.position < b.position; });
    }
    return curve;
}
} // namespace

// ---------------------------------------------------------------------------
// new-project
// ---------------------------------------------------------------------------

inline int commandNewProject(const juce::StringArray& arguments)
{
    if (arguments.size() < 1)
    {
        throw CommandError{"usage: new-project <output.svp> [--voice <voice.nofs>] [--bpm 120] [--language japanese] [--name <name>] [--dictionary <dir>]"};
    }
    const juce::File output(arguments[0]);
    juce::String voicePath;
    double bpm = 120.0;
    juce::String language = "japanese";
    juce::String name = "Untitled";
    juce::String dictionary;
    for (int index = 1; index < arguments.size(); ++index)
    {
        if (arguments[index] == "--voice" && index + 1 < arguments.size())
        {
            voicePath = arguments[++index];
        }
        else if (arguments[index] == "--bpm" && index + 1 < arguments.size())
        {
            bpm = arguments[++index].getDoubleValue();
        }
        else if (arguments[index] == "--language" && index + 1 < arguments.size())
        {
            language = arguments[++index];
        }
        else if (arguments[index] == "--name" && index + 1 < arguments.size())
        {
            name = arguments[++index];
        }
        else if (arguments[index] == "--dictionary" && index + 1 < arguments.size())
        {
            dictionary = arguments[++index];
        }
        else
        {
            throw CommandError{"unknown option: " + arguments[index]};
        }
    }
    if (bpm <= 0.0 || !std::isfinite(bpm) || bpm > 1000.0)
    {
        throw CommandError{"--bpm must be between 0 and 1000."};
    }
    sv::Project project = sv::createEmptyProject();
    project.name = name.toStdString();
    project.tempoMap.tempos.front().bpm = bpm;
    auto& track = project.tracks.front();
    if (voicePath.isNotEmpty())
    {
        const juce::File voice(voicePath);
        if (!voice.existsAsFile())
        {
            throw CommandError{"voice database does not exist: " + voicePath};
        }
        track.voice.databasePath = voice.getFullPathName().toStdString();
        track.voice.language = language.toStdString();
        track.voice.dictionaryDirectory = dictionary.isNotEmpty() ? juce::File(dictionary).getFullPathName().toStdString() : std::string{};
    }
    sv::normaliseProject(project);
    saveProjectOrThrow(output, project);
    std::printf("{\n  \"project\": \"%s\",\n  \"bpm\": %s,\n  \"voice\": \"%s\",\n  \"language\": \"%s\"\n}\n",
                output.getFullPathName().toRawUTF8(),
                juce::String(bpm).toRawUTF8(),
                voicePath.toRawUTF8(),
                language.toRawUTF8());
    return 0;
}

// ---------------------------------------------------------------------------
// edit-notes: add / update / remove notes via a JSON patch object
// ---------------------------------------------------------------------------

inline int commandEditNotes(const juce::StringArray& arguments)
{
    if (arguments.size() < 2)
    {
        throw CommandError{"usage: edit-notes <project.svp> <edit.json|->  (JSON: {add:[...], update:[...], remove:[ids], track|name})"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const auto& input = readJsonInput(arguments[1]);
    sv::Track& track = resolveTrack(project, input);
    const auto& tempo = project.tempoMap;

    int added = 0;
    int updated = 0;
    int removed = 0;

    const auto& addField = input["add"];
    if (!addField.isVoid() && !addField.isArray())
    {
        throw CommandError{"add must be an array."};
    }
    if (addField.isArray())
    {
        for (const auto& item : *addField.getArray())
        {
            if (!item.isObject())
            {
                throw CommandError{"each added note must be an object."};
            }
            sv::Note note;
            bool present = false;
            note.onset = parseTime(item, "onset", "onsetSeconds", tempo, true, present, "add note");
            note.duration = std::max<sv::Blick>(1, parseTime(item, "duration", "durationSeconds", tempo, false, present, "add note"));
            if (!present)
            {
                note.duration = sv::blicksPerQuarter;
            }
            double pitch = 60.0;
            readNumber(item, "pitch", pitch, "add note");
            if (pitch < 0.0 || pitch > 127.0 || std::trunc(pitch) != pitch)
            {
                throw CommandError{"add note: pitch must be an integer MIDI number 0-127 (use detune for cents)."};
            }
            note.pitch = static_cast<int>(pitch);
            note.lyrics = "la";
            readString(item, "lyrics", note.lyrics, "add note");
            if (note.lyrics.empty() || note.lyrics == "+" || note.lyrics == "-")
            {
                throw CommandError{"add note: lyrics must not be empty or the unsupported continuation markers +/-."};
            }
            readString(item, "phonemes", note.phonemes, "add note");
            std::string musicalType;
            readString(item, "musicalType", musicalType, "add note");
            if (!musicalType.empty())
            {
                if (musicalType != "singing" && musicalType != "rap")
                {
                    throw CommandError{"add note: musicalType must be singing or rap."};
                }
                note.musicalType = musicalType;
            }
            double detune = 0.0;
            readNumber(item, "detune", detune, "add note");
            note.detune = detune;
            bool instantMode = note.instantMode;
            readBool(item, "instantMode", instantMode, "add note");
            note.instantMode = instantMode;
            if (item["attributes"].isObject())
            {
                applyPitchAttributes(item["attributes"], note.attributes, "add note attributes");
            }
            track.mainGroup.notes.push_back(std::move(note));
            ++added;
        }
    }

    const auto& updateField = input["update"];
    if (!updateField.isVoid() && !updateField.isArray())
    {
        throw CommandError{"update must be an array."};
    }
    if (updateField.isArray())
    {
        for (const auto& item : *updateField.getArray())
        {
            if (!item.isObject())
            {
                throw CommandError{"each updated note must be an object with an id."};
            }
            long long id = 0;
            readInteger(item, "id", id, "update note");
            if (id <= 0)
            {
                throw CommandError{"update note: id must be a positive note id."};
            }
            sv::Note& note = findNoteOrThrow(project, static_cast<sv::NoteId>(id));
            const juce::String context = "update note " + juce::String(id);
            bool present = false;
            const auto onset = parseTime(item, "onset", "onsetSeconds", tempo, false, present, context);
            if (present)
            {
                note.onset = onset;
            }
            const auto duration = std::max<sv::Blick>(1, parseTime(item, "duration", "durationSeconds", tempo, false, present, context));
            if (present)
            {
                note.duration = duration;
            }
            double pitch = 0.0;
            const auto& pitchField = item["pitch"];
            if (!pitchField.isVoid())
            {
                if (!pitchField.isDouble() && !pitchField.isInt() && !pitchField.isInt64())
                {
                    throw CommandError{context + ": pitch must be a number."};
                }
                pitch = static_cast<double>(pitchField);
                if (pitch < 0.0 || pitch > 127.0 || std::trunc(pitch) != pitch)
                {
                    throw CommandError{context + ": pitch must be an integer MIDI number 0-127."};
                }
                note.pitch = static_cast<int>(pitch);
            }
            std::string lyrics;
            readString(item, "lyrics", lyrics, context);
            if (!lyrics.empty())
            {
                if (lyrics == "+" || lyrics == "-")
                {
                    throw CommandError{context + ": continuation lyrics +/- are not implemented."};
                }
                note.lyrics = lyrics;
            }
            readString(item, "phonemes", note.phonemes, context);
            std::string musicalType;
            readString(item, "musicalType", musicalType, context);
            if (!musicalType.empty())
            {
                if (musicalType != "singing" && musicalType != "rap")
                {
                    throw CommandError{context + ": musicalType must be singing or rap."};
                }
                note.musicalType = musicalType;
            }
            double detune = 0.0;
            const auto& detuneField = item["detune"];
            if (!detuneField.isVoid())
            {
                if (!detuneField.isDouble() && !detuneField.isInt() && !detuneField.isInt64())
                {
                    throw CommandError{context + ": detune must be a number."};
                }
                detune = static_cast<double>(detuneField);
                if (!std::isfinite(detune) || std::abs(detune) > maximumDetuneCents)
                {
                    throw CommandError{context + ": detune is out of range (|x| <= 2400 cents)."};
                }
                note.detune = detune;
            }
            bool instantMode = note.instantMode;
            readBool(item, "instantMode", instantMode, context);
            note.instantMode = instantMode;
            if (item["attributes"].isObject())
            {
                applyPitchAttributes(item["attributes"], note.attributes, context + " attributes");
            }
            ++updated;
        }
    }

    const auto& removeField = input["remove"];
    if (!removeField.isVoid() && !removeField.isArray())
    {
        throw CommandError{"remove must be an array of note ids."};
    }
    if (removeField.isArray())
    {
        for (const auto& idValue : *removeField.getArray())
        {
            if (!idValue.isInt() && !idValue.isInt64())
            {
                throw CommandError{"remove: each entry must be an integer note id."};
            }
            const auto id = static_cast<sv::NoteId>(static_cast<long long>(idValue));
            bool found = false;
            for (auto& trackItem : project.tracks)
            {
                auto& notes = trackItem.mainGroup.notes;
                if (const auto position = std::remove_if(notes.begin(), notes.end(), [id](const sv::Note& note)
                                                         { return note.id == id; });
                    position != notes.end())
                {
                    notes.erase(position, notes.end());
                    found = true;
                    ++removed;
                }
            }
            if (!found)
            {
                throw CommandError{"remove: no note with id " + juce::String(static_cast<juce::int64>(id)) + "."};
            }
        }
    }

    sv::normaliseProject(project);
    saveProjectOrThrow(juce::File(arguments[0]), project);
    std::printf("{\n  \"added\": %d,\n  \"updated\": %d,\n  \"removed\": %d\n}\n", added, updated, removed);
    return 0;
}

// ---------------------------------------------------------------------------
// set-pitch-attrs
// ---------------------------------------------------------------------------

inline int commandSetPitchAttrs(const juce::StringArray& arguments)
{
    if (arguments.size() < 2)
    {
        throw CommandError{"usage: set-pitch-attrs <project.svp> <attrs.json|->  (JSON: {notes:[ids], attributes:{...}})"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const auto& input = readJsonInput(arguments[1]);
    const auto& notesField = input["notes"];
    if (!notesField.isArray() || notesField.getArray()->isEmpty())
    {
        throw CommandError{"notes must be a non-empty array of note ids."};
    }
    const auto& attributesField = input["attributes"];
    if (!attributesField.isObject())
    {
        throw CommandError{"attributes must be an object (use null values to clear)."};
    }
    int changed = 0;
    for (const auto& idValue : *notesField.getArray())
    {
        if (!idValue.isInt() && !idValue.isInt64())
        {
            throw CommandError{"notes: each entry must be an integer note id."};
        }
        sv::Note& note = findNoteOrThrow(project, static_cast<sv::NoteId>(static_cast<long long>(idValue)));
        applyPitchAttributes(attributesField, note.attributes, "attributes");
        ++changed;
    }
    saveProjectOrThrow(juce::File(arguments[0]), project);
    std::printf("{\n  \"changed\": %d\n}\n", changed);
    return 0;
}

// ---------------------------------------------------------------------------
// set-pitch-curve / set-vibrato-env
// ---------------------------------------------------------------------------

inline int commandSetCurve(const juce::StringArray& arguments, bool vibrato)
{
    const char* command = vibrato ? "set-vibrato-env" : "set-pitch-curve";
    if (arguments.size() < 2)
    {
        throw CommandError{juce::String("usage: ") + command + " <project.svp> <curve.json|->  (JSON: {points:[{beats|seconds,v},...], mode?, track?, name?})"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const auto& input = readJsonInput(arguments[1]);
    sv::Track& track = resolveTrack(project, input);
    const auto curve = parseCurve(input, project.tempoMap, command);
    if (vibrato)
    {
        track.mainGroup.vibratoEnv = curve;
    }
    else
    {
        track.mainGroup.pitchDelta = curve;
    }
    sv::normaliseProject(project);
    saveProjectOrThrow(juce::File(arguments[0]), project);
    std::printf("{\n  \"points\": %d\n}\n", static_cast<int>(curve.points.size()));
    return 0;
}

// ---------------------------------------------------------------------------
// set-tempo / set-track / set-voice
// ---------------------------------------------------------------------------

inline int commandSetTempo(const juce::StringArray& arguments)
{
    if (arguments.size() < 2)
    {
        throw CommandError{"usage: set-tempo <project.svp> <bpm>"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const double bpm = arguments[1].getDoubleValue();
    if (bpm <= 0.0 || !std::isfinite(bpm) || bpm > 1000.0)
    {
        throw CommandError{"bpm must be between 0 and 1000."};
    }
    project.tempoMap.tempos.front().bpm = bpm;
    saveProjectOrThrow(juce::File(arguments[0]), project);
    std::printf("{\n  \"bpm\": %s\n}\n", juce::String(bpm).toRawUTF8());
    return 0;
}

inline int commandSetTrack(const juce::StringArray& arguments)
{
    if (arguments.size() < 2)
    {
        throw CommandError{"usage: set-track <project.svp> <settings.json|->  (JSON: {track?, name?, gainDb?, pan?, mute?, solo?, rename?})"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const auto& input = readJsonInput(arguments[1]);
    sv::Track& track = resolveTrack(project, input);
    double gainDb = 0.0;
    const auto& gainField = input["gainDb"];
    if (!gainField.isVoid())
    {
        if (!gainField.isDouble() && !gainField.isInt() && !gainField.isInt64())
        {
            throw CommandError{"gainDb must be a number."};
        }
        gainDb = static_cast<double>(gainField);
        if (!std::isfinite(gainDb) || gainDb < -100.0 || gainDb > 24.0)
        {
            throw CommandError{"gainDb must be between -100 and 24."};
        }
        track.gain = gainDb <= -100.0 ? 0.0 : std::pow(10.0, gainDb / 20.0);
    }
    double pan = 0.0;
    const auto& panField = input["pan"];
    if (!panField.isVoid())
    {
        if (!panField.isDouble() && !panField.isInt() && !panField.isInt64())
        {
            throw CommandError{"pan must be a number."};
        }
        pan = static_cast<double>(panField);
        if (!std::isfinite(pan) || pan < -1.0 || pan > 1.0)
        {
            throw CommandError{"pan must be between -1 and 1."};
        }
        track.pan = pan;
    }
    bool mute = track.mute;
    readBool(input, "mute", mute, "input");
    track.mute = mute;
    bool solo = track.solo;
    readBool(input, "solo", solo, "input");
    track.solo = solo;
    std::string rename;
    readString(input, "rename", rename, "input");
    if (!rename.empty())
    {
        track.name = rename;
    }
    saveProjectOrThrow(juce::File(arguments[0]), project);
    std::printf("{\n  \"track\": \"%s\"\n}\n", juce::String::fromUTF8(track.name.c_str()).toRawUTF8());
    return 0;
}

inline int commandSetVoice(const juce::StringArray& arguments)
{
    if (arguments.size() < 2)
    {
        throw CommandError{"usage: set-voice <project.svp> <voice.json|->  (JSON: {track?, name?, databasePath?, language?, dictionaryDirectory?})"};
    }
    sv::Project project = loadProjectOrThrow(juce::File(arguments[0]));
    const auto& input = readJsonInput(arguments[1]);
    sv::Track& track = resolveTrack(project, input);
    std::string databasePath;
    readString(input, "databasePath", databasePath, "input");
    if (!databasePath.empty())
    {
        const juce::File database(juce::String::fromUTF8(databasePath.c_str()));
        if (!database.existsAsFile())
        {
            throw CommandError{"voice database does not exist: " + juce::String::fromUTF8(databasePath.c_str())};
        }
        track.voice.databasePath = database.getFullPathName().toStdString();
    }
    std::string language;
    readString(input, "language", language, "input");
    if (!language.empty())
    {
        static const char* supported[] = {"japanese", "mandarin", "cantonese", "spanish", "english"};
        if (std::find(supported, supported + 5, language) == supported + 5)
        {
            throw CommandError{"language must be one of japanese, mandarin, cantonese, spanish, english."};
        }
        track.voice.language = language;
    }
    std::string dictionary;
    readString(input, "dictionaryDirectory", dictionary, "input");
    if (!dictionary.empty())
    {
        const juce::File directory(juce::String::fromUTF8(dictionary.c_str()));
        if (!directory.isDirectory())
        {
            throw CommandError{"dictionary directory does not exist: " + juce::String::fromUTF8(dictionary.c_str())};
        }
        track.voice.dictionaryDirectory = directory.getFullPathName().toStdString();
    }
    saveProjectOrThrow(juce::File(arguments[0]), project);
    std::printf("{\n  \"voice\": \"%s\",\n  \"language\": \"%s\"\n}\n",
                juce::String::fromUTF8(track.voice.databasePath.c_str()).toRawUTF8(),
                juce::String::fromUTF8(track.voice.language.c_str()).toRawUTF8());
    return 0;
}
} // namespace cli
