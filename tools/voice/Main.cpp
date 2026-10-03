#include "Cli.h"
#include "InspectionCommands.h"
#include "ProjectCommands.h"

#include "audio/ProjectRenderer.h"
#include "audio/WaveFile.h"
#include "core/Project.h"
#include "core/ProjectFile.h"
#include "synthesis/VoiceDatabase.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
void printUsage()
{
    std::printf(
        "opensv-voice - headless OpenSV engine\n"
        "\n"
        "Project editing (all times in beats unless suffixed *Seconds):\n"
        "  new-project <out.svp> [--voice <voice.nofs>] [--bpm N] [--language L] [--name S] [--dictionary DIR]\n"
        "  edit-notes <svp> <json|->           add/update/remove notes {add:[{onset,duration,pitch,lyrics,phonemes,musicalType,detune,instantMode,attributes}],update:[{id,...}],remove:[ids]}\n"
        "  set-pitch-attrs <svp> <json|->      {notes:[ids], attributes:{tF0Offset,tF0Left,tF0Right,dF0Left,dF0Right,tF0VbrStart,tF0VbrLeft,tF0VbrRight,dF0Vbr,fF0Vbr,pF0Vbr,dF0VbrMod}}\n"
        "  set-pitch-curve <svp> <json|->      {points:[{beats|seconds,v}], mode:linear|cubic|cosine}\n"
        "  set-vibrato-env <svp> <json|->      same point format (v is a vibrato depth multiplier)\n"
        "  set-tempo <svp> <bpm>\n"
        "  set-track <svp> <json|->            {track?,name?,gainDb?,pan?,mute?,solo?,rename?}\n"
        "  set-voice <svp> <json|->            {track?,name?,databasePath?,language?,dictionaryDirectory?}\n"
        "\n"
        "Inspection and validation:\n"
        "  inspect-project <svp> [--json]      note ids, attributes and curves as JSON\n"
        "  inspect-voice <voice.nofs>\n"
        "  dump-phonemes <voice.nofs> [--json] phoneme table incl. cl/br specials\n"
        "  validate <svp>                      phoneme legality and renderability checks\n"
        "\n"
        "Rendering:\n"
        "  render-project <svp> <wav> [--rate 48000] [--range startS:endS] [--f0 out.csv] [--json]\n"
        "\n"
        "Notes:\n"
        "  - Time fields accept beats (onset/duration/beats) or seconds (onsetSeconds/durationSeconds/seconds).\n"
        "  - Pass \"-\" to read JSON from stdin.\n"
        "  - All edit commands print a JSON summary; exit code is non-zero on error.\n");
}

int inspectVoice(const juce::File& file)
{
    sv::synthesis::VoiceDatabase database;
    if (const auto result = database.open(file); result.failed())
    {
        throw cli::CommandError{result.getErrorMessage()};
    }
    const auto& metadata = database.getMetadata();
    std::printf("name:     %s\n", metadata.name.toRawUTF8());
    std::printf("vendor:   %s\n", metadata.vendor.toRawUTF8());
    std::printf("language: %s\n", metadata.language.toRawUTF8());
    std::printf("phoneset: %s\n", metadata.phoneset.toRawUTF8());
    std::printf("multi:    %s\n", metadata.languages.joinIntoString(" ").toRawUTF8());
    std::printf("timbre:   %s\n", metadata.timbreStyles.joinIntoString(" ").toRawUTF8());
    std::printf("entries:  %zu\n", database.getEntries().size());
    return 0;
}

juce::StringArray restArguments(const juce::StringArray& arguments, int from)
{
    juce::StringArray rest;
    for (int index = from; index < arguments.size(); ++index)
    {
        rest.add(arguments[index]);
    }
    return rest;
}
} // namespace

int main(int argc, char** argv)
{
    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index)
    {
        arguments.add(juce::String::fromUTF8(argv[index]));
    }
    if (arguments.size() < 1)
    {
        printUsage();
        return 2;
    }
    const auto command = arguments[0];
    const auto rest = restArguments(arguments, 1);
    try
    {
        if (command == "render-project")
        {
            if (rest.size() < 2)
            {
                throw cli::CommandError{"usage: render-project <project.svp> <output.wav> [options]"};
            }
            return cli::renderProject(rest);
        }
        if (command == "new-project")
        {
            return cli::commandNewProject(rest);
        }
        if (command == "edit-notes")
        {
            return cli::commandEditNotes(rest);
        }
        if (command == "set-pitch-attrs")
        {
            return cli::commandSetPitchAttrs(rest);
        }
        if (command == "set-pitch-curve")
        {
            return cli::commandSetCurve(rest, false);
        }
        if (command == "set-vibrato-env")
        {
            return cli::commandSetCurve(rest, true);
        }
        if (command == "set-tempo")
        {
            return cli::commandSetTempo(rest);
        }
        if (command == "set-track")
        {
            return cli::commandSetTrack(rest);
        }
        if (command == "set-voice")
        {
            return cli::commandSetVoice(rest);
        }
        if (command == "inspect-project")
        {
            return cli::inspectProject(rest);
        }
        if (command == "inspect-voice")
        {
            if (rest.isEmpty())
            {
                throw cli::CommandError{"usage: inspect-voice <voice.nofs>"};
            }
            return inspectVoice(juce::File(rest[0]));
        }
        if (command == "dump-phonemes")
        {
            return cli::dumpPhonemes(rest);
        }
        if (command == "validate")
        {
            return cli::validateProject(rest);
        }
        printUsage();
        return 2;
    }
    catch (const cli::CommandError& error)
    {
        std::fprintf(stderr, "error: %s\n", error.message.toRawUTF8());
        return 1;
    }
}
