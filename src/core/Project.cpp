#include "Project.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace sv
{
namespace
{
double secondsPerBlick(double bpm)
{
    return 60.0 / (bpm * static_cast<double>(blicksPerQuarter));
}

Blick roundedBlick(double value)
{
    constexpr auto lower = std::numeric_limits<Blick>::lowest();
    constexpr auto upper = std::numeric_limits<Blick>::max();
    if (value <= static_cast<double>(lower))
    {
        return lower;
    }
    if (value >= static_cast<double>(upper))
    {
        return upper;
    }
    return static_cast<Blick>(std::llround(value));
}

Blick addBlick(Blick left, Blick right)
{
    if (right > 0 && left > std::numeric_limits<Blick>::max() - right)
    {
        return std::numeric_limits<Blick>::max();
    }
    if (right < 0 && left < std::numeric_limits<Blick>::min() - right)
    {
        return std::numeric_limits<Blick>::min();
    }
    return left + right;
}

Blick referencedGroupEnd(const NoteGroup& group, const GroupReference& reference)
{
    Blick end = 0;
    for (const auto& note : group.notes)
    {
        auto noteEnd = addBlick(addBlick(note.onset, note.duration), reference.timeOffset);
        if (reference.absoluteEnd >= 0)
        {
            noteEnd = std::min(noteEnd, reference.absoluteEnd);
        }
        if (noteEnd > reference.absoluteBegin)
        {
            end = std::max(end, noteEnd);
        }
    }
    return end;
}

void normaliseCurve(ParameterCurve& curve)
{
    auto& points = curve.points;
    std::erase_if(points, [](const AutomationPoint& point)
                  { return !std::isfinite(point.value); });
    std::stable_sort(points.begin(), points.end(), [](const AutomationPoint& left, const AutomationPoint& right)
                     { return left.position < right.position; });
    points.erase(points.begin(), std::unique(points.rbegin(), points.rend(), [](const AutomationPoint& left, const AutomationPoint& right)
                                             { return left.position == right.position; })
                                     .base());
}
} // namespace

double TempoMap::blickToSeconds(Blick position) const
{
    if (tempos.empty())
    {
        return static_cast<double>(position) * secondsPerBlick(120.0);
    }
    double seconds = 0.0;
    Blick cursor = 0;
    double bpm = tempos.front().bpm;
    for (const auto& tempo : tempos)
    {
        if (tempo.position > position)
        {
            break;
        }
        seconds += static_cast<double>(tempo.position - cursor) * secondsPerBlick(bpm);
        cursor = tempo.position;
        bpm = tempo.bpm;
    }
    return seconds + static_cast<double>(position - cursor) * secondsPerBlick(bpm);
}

Blick TempoMap::secondsToBlick(double seconds) const
{
    if (!std::isfinite(seconds))
    {
        return 0;
    }
    if (tempos.empty())
    {
        return roundedBlick(seconds / secondsPerBlick(120.0));
    }
    double elapsed = 0.0;
    Blick cursor = 0;
    double bpm = tempos.front().bpm;
    for (const auto& tempo : tempos)
    {
        const double segment = static_cast<double>(tempo.position - cursor) * secondsPerBlick(bpm);
        if (elapsed + segment > seconds)
        {
            break;
        }
        elapsed += segment;
        cursor = tempo.position;
        bpm = tempo.bpm;
    }
    return roundedBlick(static_cast<double>(cursor) + (seconds - elapsed) / secondsPerBlick(bpm));
}

double TempoMap::getTempoAt(Blick position) const
{
    double bpm = tempos.empty() ? 120.0 : tempos.front().bpm;
    for (const auto& tempo : tempos)
    {
        if (tempo.position > position)
        {
            break;
        }
        bpm = tempo.bpm;
    }
    return bpm;
}

NoteId createNoteId()
{
    static std::atomic<NoteId> nextId{1};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

NoteGroup createNoteGroup(const std::string& name)
{
    NoteGroup group;
    group.id = juce::Uuid().toDashedString().toStdString();
    group.name = name;
    return group;
}

Project createEmptyProject()
{
    Project project;
    Track track;
    track.mainGroup = createNoteGroup();
    track.mainRef.groupId = track.mainGroup.id;
    project.tracks.push_back(std::move(track));
    return project;
}

const NoteGroup* findNoteGroup(const Project& project, const std::string& groupId)
{
    for (const auto& group : project.library)
    {
        if (group.id == groupId)
        {
            return &group;
        }
    }
    for (const auto& track : project.tracks)
    {
        if (track.mainGroup.id == groupId)
        {
            return &track.mainGroup;
        }
    }
    return nullptr;
}

Blick getProjectEnd(const Project& project)
{
    Blick end = 0;
    for (const auto& track : project.tracks)
    {
        end = std::max(end, referencedGroupEnd(track.mainGroup, track.mainRef));
        for (const auto& reference : track.groups)
        {
            if (const auto* group = findNoteGroup(project, reference.groupId))
            {
                end = std::max(end, referencedGroupEnd(*group, reference));
            }
        }
    }
    return end;
}

void normaliseProject(Project& project)
{
    if (project.tracks.empty())
    {
        project.tracks = createEmptyProject().tracks;
    }
    std::unordered_set<NoteId> noteIds;
    const auto normaliseGroup = [&noteIds](NoteGroup& group)
    {
        if (group.id.empty())
        {
            group.id = juce::Uuid().toDashedString().toStdString();
        }
        for (auto& note : group.notes)
        {
            while (note.id == 0 || noteIds.contains(note.id))
            {
                note.id = createNoteId();
            }
            noteIds.insert(note.id);
            note.duration = std::max<Blick>(1, note.duration);
            note.pitch = std::clamp(note.pitch, 0, 127);
            if (!std::isfinite(note.detune))
            {
                note.detune = 0.0;
            }
        }
        std::stable_sort(group.notes.begin(), group.notes.end(), [](const Note& left, const Note& right)
                         { return left.onset < right.onset; });
        normaliseCurve(group.pitchDelta);
        normaliseCurve(group.vibratoEnv);
    };
    for (auto& group : project.library)
    {
        normaliseGroup(group);
    }
    for (auto& track : project.tracks)
    {
        normaliseGroup(track.mainGroup);
        track.mainRef.groupId = track.mainGroup.id;
        normaliseCurve(track.mainRef.systemPitchDelta);
        for (auto& reference : track.groups)
        {
            normaliseCurve(reference.systemPitchDelta);
        }
    }
    auto& tempos = project.tempoMap.tempos;
    if (tempos.empty())
    {
        tempos.push_back({0, 120.0, {}});
    }
    std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& left, const Tempo& right)
                     { return left.position < right.position; });
    if (tempos.front().position > 0)
    {
        tempos.insert(tempos.begin(), {0, 120.0, {}});
    }
    auto& signatures = project.tempoMap.timeSignatures;
    if (signatures.empty())
    {
        signatures.push_back({0, 4, 4, {}});
    }
    std::stable_sort(signatures.begin(), signatures.end(), [](const TimeSignature& left, const TimeSignature& right)
                     { return left.bar < right.bar; });
    if (signatures.front().bar > 0)
    {
        signatures.insert(signatures.begin(), {0, 4, 4, {}});
    }
}
} // namespace sv
