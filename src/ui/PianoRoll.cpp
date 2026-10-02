#include "PianoRoll.h"

#include "LyricsDialog.h"
#include "PlaybackPaging.h"
#include "StudioTheme.h"
#include "audio/RenderVisualization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sv
{
namespace
{
const juce::Colour gridBackground = colours::background;
const juce::Colour darkRow = colours::background.darker(0.13f);
const juce::Colour selectedNoteColour = colours::accent.interpolatedWith(juce::Colours::white, 0.32f);
const juce::Colour waveformColour = juce::Colour(0xffa3a7a2).withAlpha(0.34f);
const juce::Colour pitchColour{0xff8d948c};

audio::WaveformPeak getWaveformPeak(const audio::PhraseVisualization& data, double firstSample, double lastSample)
{
    const double sampleCount = static_cast<double>(data.sampleCount);
    firstSample = std::clamp(firstSample, 0.0, sampleCount);
    lastSample = std::clamp(lastSample, firstSample, sampleCount);
    if (lastSample <= firstSample || data.waveform.empty())
    {
        return {};
    }
    const double width = lastSample - firstSample;
    const audio::WaveformLevel* level = &data.waveform.front();
    for (const auto& candidate : data.waveform)
    {
        if (static_cast<double>(candidate.samplesPerPeak) > width)
        {
            break;
        }
        level = &candidate;
    }
    if (level->samplesPerPeak == 0 || level->peaks.empty())
    {
        return {};
    }
    const double blockSize = static_cast<double>(level->samplesPerPeak);
    const auto first = std::min(level->peaks.size(), static_cast<std::size_t>(std::floor(firstSample / blockSize)));
    const auto end = std::min(level->peaks.size(), static_cast<std::size_t>(std::ceil(lastSample / blockSize)));
    audio::WaveformPeak peak;
    for (auto index = first; index < end; ++index)
    {
        peak.minimum = std::min(peak.minimum, level->peaks[index].minimum);
        peak.maximum = std::max(peak.maximum, level->peaks[index].maximum);
    }
    return peak;
}

bool isBlackKey(int pitch)
{
    const auto key = pitch % 12;
    return key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
}

juce::String pitchName(int pitch)
{
    static const std::array<const char*, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[static_cast<std::size_t>(pitch % 12)]) + juce::String(pitch / 12 - 1);
}

struct BarMark
{
    Blick position;
    int number;
};

std::vector<BarMark> getBarMarks(const TempoMap& map, Blick first, Blick last)
{
    std::vector<BarMark> marks;
    Blick segmentStart = 0;
    for (std::size_t index = 0; index < map.timeSignatures.size(); ++index)
    {
        const auto& signature = map.timeSignatures[index];
        const auto duration = blicksPerQuarter * 4 * signature.numerator / std::max(1, signature.denominator);
        if (duration <= 0)
        {
            continue;
        }
        const auto endBar = index + 1 < map.timeSignatures.size() ? map.timeSignatures[index + 1].bar : std::numeric_limits<int>::max();
        const auto relativeFirst = std::max<Blick>(0, (first - segmentStart) / duration);
        const auto relativeLast = std::min<Blick>((last - segmentStart) / duration + 1, static_cast<Blick>(endBar) - signature.bar - 1);
        for (Blick bar = relativeFirst; bar <= relativeLast; ++bar)
        {
            marks.push_back({segmentStart + bar * duration, signature.bar + static_cast<int>(bar) + 1});
        }
        if (endBar == std::numeric_limits<int>::max())
        {
            break;
        }
        segmentStart += (static_cast<Blick>(endBar) - signature.bar) * duration;
        if (segmentStart > last)
        {
            break;
        }
    }
    return marks;
}
} // namespace

PianoRoll::PianoRoll(ProjectDocument& documentToUse)
    : document(documentToUse), activeGroupId(document.getActiveGroup().id)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    document.addChangeListener(this);

    addAndMakeVisible(horizontalScrollBar);
    addAndMakeVisible(verticalScrollBar);
    for (auto* bar : {&horizontalScrollBar, &verticalScrollBar})
    {
        bar->addListener(this);
        bar->setAutoHide(false);
        bar->setColour(juce::ScrollBar::backgroundColourId, colours::background);
        bar->setColour(juce::ScrollBar::thumbColourId, colours::raised.brighter(0.1f));
    }
    horizontalScrollBar.setSingleStepSize(1.0);
    verticalScrollBar.setSingleStepSize(noteHeight * 3.0);

    addChildComponent(lyricsEditor);
    lyricsEditor.setComponentID("piano-roll-lyrics-editor");
    lyricsEditor.setName("piano-roll-lyrics-editor");
    lyricsEditor.setTitle(juce::String::fromUTF8("钢琴卷帘歌词"));
    lyricsEditor.setMultiLine(false);
    lyricsEditor.setSelectAllWhenFocused(true);
    lyricsEditor.setJustification(juce::Justification::centredLeft);
    lyricsEditor.setColour(juce::TextEditor::backgroundColourId, colours::accent.interpolatedWith(juce::Colours::white, 0.8f));
    lyricsEditor.setColour(juce::TextEditor::textColourId, colours::border);
    lyricsEditor.setColour(juce::TextEditor::outlineColourId, selectedNoteColour);
    lyricsEditor.setColour(juce::TextEditor::focusedOutlineColourId, selectedNoteColour);
    lyricsEditor.onReturnKey = [this]
    {
        finishLyricsEdit(true);
        grabKeyboardFocus();
    };
    lyricsEditor.onEscapeKey = [this]
    {
        finishLyricsEdit(false);
        grabKeyboardFocus();
    };
    lyricsEditor.onFocusLost = [this]
    {
        finishLyricsEdit(true);
    };
}

PianoRoll::~PianoRoll()
{
    setAuditionPitch(-1);
    lyricsEditor.onFocusLost = nullptr;
    document.removeChangeListener(this);
    horizontalScrollBar.removeListener(this);
    verticalScrollBar.removeListener(this);
}

juce::Rectangle<int> PianoRoll::getGridBounds() const
{
    return {keyboardWidth, rulerHeight, std::max(0, getWidth() - keyboardWidth - scrollBarSize), std::max(0, getHeight() - rulerHeight - scrollBarSize)};
}

float PianoRoll::xAt(Blick position) const
{
    return static_cast<float>(keyboardWidth + (static_cast<double>(position) / blicksPerQuarter - horizontalQuarter) * pixelsPerQuarter);
}

float PianoRoll::xAtSeconds(double seconds) const
{
    const double position = static_cast<double>(document.getProject().tempoMap.secondsToBlick(seconds)) - static_cast<double>(document.getActiveGroupOffset());
    return static_cast<float>(keyboardWidth + (position / blicksPerQuarter - horizontalQuarter) * pixelsPerQuarter);
}

double PianoRoll::secondsAt(float x) const
{
    const auto position = blickAt(x);
    const auto offset = document.getActiveGroupOffset();
    auto absolute = position;
    if (offset > 0 && position > std::numeric_limits<Blick>::max() - offset)
    {
        absolute = std::numeric_limits<Blick>::max();
    }
    else if (offset < 0 && position < std::numeric_limits<Blick>::lowest() - offset)
    {
        absolute = std::numeric_limits<Blick>::lowest();
    }
    else
    {
        absolute += offset;
    }
    return document.getProject().tempoMap.blickToSeconds(absolute);
}

Blick PianoRoll::blickAt(float x) const
{
    return static_cast<Blick>(std::llround(((static_cast<double>(x) - keyboardWidth) / pixelsPerQuarter + horizontalQuarter) * blicksPerQuarter));
}

int PianoRoll::pitchAt(float y) const
{
    return std::clamp(127 - static_cast<int>(std::floor((y - rulerHeight + verticalPixels) / noteHeight)), 0, 127);
}

int PianoRoll::keyboardPitchAt(juce::Point<float> point) const
{
    const juce::Rectangle<float> bounds{0.0f, static_cast<float>(rulerHeight), static_cast<float>(std::min(keyboardWidth, getWidth())), static_cast<float>(getGridBounds().getHeight())};
    if (!bounds.contains(point))
    {
        return -1;
    }
    // Black keys cover part of the white keys, so they take precedence.
    for (const bool black : {true, false})
    {
        for (int pitch = 0; pitch <= 127; ++pitch)
        {
            if (isBlackKey(pitch) == black && getKeyboardKeyBounds(pitch).contains(point))
            {
                return pitch;
            }
        }
    }
    return -1;
}

juce::Rectangle<float> PianoRoll::getKeyboardKeyBounds(int pitch) const
{
    if (isBlackKey(pitch))
    {
        return {0.0f, yAt(pitch), keyboardWidth * 0.5f, noteHeight};
    }

    // Seven equal white keys share the same octave span as twelve grid rows.
    // The C/B boundary anchors each octave; black keys keep their grid position.
    static constexpr std::array<int, 12> whiteKeyIndices{0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    const auto key = pitch % 12;
    const auto whiteIndex = whiteKeyIndices[static_cast<std::size_t>(key)];
    const auto whiteHeight = noteHeight * 12.0f / 7.0f;
    const auto octaveBottom = yAt(pitch - key) + noteHeight;
    const auto top = std::max(yAt(127), octaveBottom - (whiteIndex + 1) * whiteHeight);
    const auto bottom = std::min(yAt(0) + noteHeight, octaveBottom - whiteIndex * whiteHeight);
    return {0.0f, top, static_cast<float>(keyboardWidth), bottom - top};
}

float PianoRoll::yAt(int pitch) const
{
    return static_cast<float>(rulerHeight + (127 - pitch) * noteHeight - verticalPixels);
}

juce::Rectangle<float> PianoRoll::getNoteBounds(const Note& note) const
{
    return {xAt(note.onset) + 1.0f, yAt(note.pitch) + 1.5f, std::max(3.0f, xAt(note.onset + note.duration) - xAt(note.onset) - 2.0f), noteHeight - 3.0f};
}

Blick PianoRoll::snapped(Blick position) const
{
    return static_cast<Blick>(std::llround(static_cast<double>(position) / static_cast<double>(snap))) * snap;
}

bool PianoRoll::isSelected(NoteId id) const
{
    const auto& selection = document.getSelectedNoteIds();
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

const audio::TrackVisualization* PianoRoll::getVisualizationTrack() const
{
    // Keep the last successful display through gestures and committed edits,
    // but never reuse it for a newly created or loaded document.
    if (visualization == nullptr || visualization->documentGeneration != document.getGeneration())
    {
        return nullptr;
    }
    const auto trackIndex = static_cast<std::size_t>(document.getActiveTrackIndex());
    if (trackIndex >= visualization->tracks.size() || visualization->tracks[trackIndex].mainGroupId != document.getActiveGroup().id)
    {
        return nullptr;
    }
    return &visualization->tracks[trackIndex];
}

const Note* PianoRoll::noteAt(juce::Point<float> point) const
{
    const auto& notes = document.getActiveGroup().notes;
    for (auto note = notes.rbegin(); note != notes.rend(); ++note)
    {
        if (getNoteBounds(*note).contains(point))
        {
            return &*note;
        }
    }
    return nullptr;
}

void PianoRoll::paint(juce::Graphics& graphics)
{
    graphics.fillAll(gridBackground);
    paintGrid(graphics);
    paintKeyboard(graphics);
    paintRuler(graphics);
    graphics.setColour(colours::background);
    graphics.fillRect(0, getHeight() - scrollBarSize, keyboardWidth, scrollBarSize);
}

void PianoRoll::paintGrid(juce::Graphics& graphics)
{
    const juce::Graphics::ScopedSaveState state(graphics);
    const auto grid = getGridBounds();
    graphics.reduceClipRegion(grid);
    const auto firstPitch = pitchAt(static_cast<float>(grid.getBottom()));
    const auto lastPitch = pitchAt(static_cast<float>(grid.getY()));
    for (int pitch = firstPitch; pitch <= lastPitch; ++pitch)
    {
        const auto y = yAt(pitch);
        graphics.setColour(isBlackKey(pitch) ? darkRow : gridBackground);
        graphics.fillRect(static_cast<float>(grid.getX()), y, static_cast<float>(grid.getWidth()), noteHeight);
        graphics.setColour(juce::Colour(pitch % 12 == 0 ? 0xff464c50 : 0xff343a3e));
        graphics.drawHorizontalLine(static_cast<int>(y + noteHeight), static_cast<float>(grid.getX()), static_cast<float>(grid.getRight()));
    }

    const auto gridStep = pixelsPerQuarter * static_cast<double>(snap) / blicksPerQuarter >= 8.0 ? snap : blicksPerQuarter;
    const auto first = std::max<Blick>(0, blickAt(static_cast<float>(grid.getX())) / gridStep * gridStep);
    const auto last = blickAt(static_cast<float>(grid.getRight()));
    for (auto position = first; position <= last; position += gridStep)
    {
        graphics.setColour(juce::Colour(position % blicksPerQuarter == 0 ? 0xff41474b : 0xff33383c));
        graphics.drawVerticalLine(static_cast<int>(xAt(position)), static_cast<float>(grid.getY()), static_cast<float>(grid.getBottom()));
    }
    const auto offset = document.getActiveGroupOffset();
    for (const auto& mark : getBarMarks(document.getProject().tempoMap, first + offset, last + offset))
    {
        graphics.setColour(juce::Colour(0xff555c60));
        graphics.drawVerticalLine(static_cast<int>(xAt(mark.position - offset)), static_cast<float>(grid.getY()), static_cast<float>(grid.getBottom()));
    }

    paintWaveforms(graphics);
    for (const auto& note : document.getActiveGroup().notes)
    {
        if ((gesture == Gesture::move || gesture == Gesture::resize) && isSelected(note.id))
        {
            continue;
        }
        paintNote(graphics, note, isSelected(note.id), false);
    }
    for (const auto& note : gestureNotes)
    {
        paintNote(graphics, note, true, true);
    }
    paintPitch(graphics);

    if (gesture == Gesture::select)
    {
        graphics.setColour(selectedNoteColour.withAlpha(0.12f));
        graphics.fillRect(selectionRectangle);
        graphics.setColour(selectedNoteColour.withAlpha(0.7f));
        graphics.drawRect(selectionRectangle, 1.0f);
    }
    graphics.setColour(juce::Colour(0xfff4bc64));
    graphics.drawVerticalLine(static_cast<int>(xAt(playhead - offset)), static_cast<float>(grid.getY()), static_cast<float>(grid.getBottom()));

    if (document.getActiveGroup().notes.empty() && gesture != Gesture::draw)
    {
        graphics.setFont(juce::FontOptions(14.0f));
        graphics.setColour(juce::Colour(0xff879097));
        graphics.drawText(juce::String::fromUTF8("双击空白处添加音符 · 双击音符编辑歌词"), grid.reduced(24).withHeight(25), juce::Justification::topLeft);
    }
}

void PianoRoll::paintWaveforms(juce::Graphics& graphics)
{
    const auto* track = getVisualizationTrack();
    const auto clip = graphics.getClipBounds().getIntersection(getGridBounds());
    if (!waveformVisible || track == nullptr || clip.isEmpty())
    {
        return;
    }
    const auto& reference = document.getProject().tracks[static_cast<std::size_t>(document.getActiveTrackIndex())].mainRef;
    const auto& tempo = document.getProject().tempoMap;
    const double cropStartSeconds = tempo.blickToSeconds(reference.absoluteBegin);
    const double cropEndSeconds = reference.absoluteEnd >= 0 ? tempo.blickToSeconds(reference.absoluteEnd) : std::numeric_limits<double>::infinity();
    const double firstSeconds = std::max(secondsAt(static_cast<float>(clip.getX())), cropStartSeconds);
    const double lastSeconds = std::min(secondsAt(static_cast<float>(clip.getRight())), cropEndSeconds);
    if (!std::isfinite(firstSeconds) || !std::isfinite(lastSeconds) || lastSeconds <= firstSeconds)
    {
        return;
    }
    const auto firstPhrase = std::lower_bound(track->phrases.begin(), track->phrases.end(), firstSeconds, [](const audio::VisualPhrase& phrase, double seconds)
                                              { return phrase.endSeconds <= seconds; });
    graphics.setColour(waveformColour);
    for (auto phrase = firstPhrase; phrase != track->phrases.end() && phrase->startSeconds < lastSeconds; ++phrase)
    {
        if (phrase->data == nullptr || phrase->notes.empty())
        {
            continue;
        }
        const auto& data = *phrase->data;
        if (data.waveform.empty() || data.sampleCount == 0 || !std::isfinite(data.sampleRate) || data.sampleRate <= 0.0 || !std::isfinite(data.peakMagnitude) || data.peakMagnitude <= 0.0f)
        {
            continue;
        }
        // One phrase-wide gain preserves dynamics. The floor keeps quiet noise
        // from being expanded to the height of a sung vowel.
        const float amplitude = noteHeight * 0.72f / std::max(0.1f, data.peakMagnitude);
        const auto firstNote = std::upper_bound(phrase->notes.begin(), phrase->notes.end(), firstSeconds, [](double seconds, const audio::VisualNote& note)
                                                { return seconds < note.startSeconds; });
        auto note = firstNote == phrase->notes.begin() ? firstNote : std::prev(firstNote);
        for (; note != phrase->notes.end() && (note == phrase->notes.begin() ? phrase->startSeconds : note->startSeconds) < lastSeconds; ++note)
        {
            if (!note->mainReference)
            {
                continue;
            }
            const auto next = std::next(note);
            // Score onsets select the display row. The first and last rows also
            // contain the phrase's real anticipation and release audio.
            const double beginSeconds = std::max(firstSeconds, note == phrase->notes.begin() ? phrase->startSeconds : note->startSeconds);
            const double endSeconds = std::min({lastSeconds, phrase->endSeconds, next == phrase->notes.end() ? phrase->endSeconds : next->startSeconds});
            if (endSeconds <= beginSeconds)
            {
                continue;
            }
            const float baseline = yAt(note->pitch) + noteHeight * 3.0f;
            if (baseline + noteHeight * 0.72f < static_cast<float>(clip.getY()) || baseline - noteHeight * 0.72f > static_cast<float>(clip.getBottom()))
            {
                continue;
            }
            const float firstX = std::max(static_cast<float>(clip.getX()), xAtSeconds(beginSeconds));
            const float lastX = std::min(static_cast<float>(clip.getRight()), xAtSeconds(endSeconds));
            if (lastX <= firstX)
            {
                continue;
            }
            juce::Path envelope;
            std::vector<juce::Point<float>> lowerEdge;
            lowerEdge.reserve(static_cast<std::size_t>(std::ceil(lastX - firstX)) + 2);
            for (int pixel = static_cast<int>(std::floor(firstX)); static_cast<float>(pixel) < lastX; ++pixel)
            {
                const float left = std::max(firstX, static_cast<float>(pixel));
                const float right = std::min(lastX, static_cast<float>(pixel + 1));
                const double beginSample = (std::max(beginSeconds, secondsAt(left)) - phrase->startSeconds) * data.sampleRate;
                const double endSample = (std::min(endSeconds, secondsAt(right)) - phrase->startSeconds) * data.sampleRate;
                const auto peak = getWaveformPeak(data, beginSample, endSample);
                const float top = baseline - peak.maximum * amplitude;
                const float bottom = baseline - peak.minimum * amplitude;
                const float x = (left + right) * 0.5f;
                if (lowerEdge.empty())
                {
                    envelope.startNewSubPath(left, top);
                    lowerEdge.emplace_back(left, bottom);
                }
                envelope.lineTo(x, top);
                lowerEdge.emplace_back(x, bottom);
                if (right == lastX)
                {
                    envelope.lineTo(right, top);
                    lowerEdge.emplace_back(right, bottom);
                }
            }
            for (auto point = lowerEdge.rbegin(); point != lowerEdge.rend(); ++point)
            {
                envelope.lineTo(*point);
            }
            envelope.closeSubPath();
            graphics.fillPath(envelope);
        }
    }
}

void PianoRoll::paintPitch(juce::Graphics& graphics)
{
    const auto* track = getVisualizationTrack();
    const auto clip = graphics.getClipBounds().getIntersection(getGridBounds());
    if (!pitchVisible || track == nullptr || clip.isEmpty())
    {
        return;
    }
    const auto& reference = document.getProject().tracks[static_cast<std::size_t>(document.getActiveTrackIndex())].mainRef;
    const auto& tempo = document.getProject().tempoMap;
    const double cropStartSeconds = tempo.blickToSeconds(reference.absoluteBegin);
    const double cropEndSeconds = reference.absoluteEnd >= 0 ? tempo.blickToSeconds(reference.absoluteEnd) : std::numeric_limits<double>::infinity();
    const double firstSeconds = std::max(secondsAt(static_cast<float>(clip.getX())), cropStartSeconds);
    const double lastSeconds = std::min(secondsAt(static_cast<float>(clip.getRight())), cropEndSeconds);
    if (!std::isfinite(firstSeconds) || !std::isfinite(lastSeconds) || lastSeconds <= firstSeconds)
    {
        return;
    }
    const auto firstPhrase = std::lower_bound(track->phrases.begin(), track->phrases.end(), firstSeconds, [](const audio::VisualPhrase& phrase, double seconds)
                                              { return phrase.endSeconds <= seconds; });
    graphics.setColour(pitchColour);
    for (auto phrase = firstPhrase; phrase != track->phrases.end() && phrase->startSeconds < lastSeconds; ++phrase)
    {
        if (phrase->data == nullptr || phrase->notes.empty())
        {
            continue;
        }
        const auto& data = *phrase->data;
        if (data.midiPitch.empty() || !std::isfinite(data.frameIntervalSeconds) || data.frameIntervalSeconds <= 0.0)
        {
            continue;
        }
        // Keep the continuous control curve, but only expose the score's note
        // spans. Cached bounds keep the last completed overlay intact during edits.
        juce::Path noteClip;
        const auto firstVisibleNote = std::lower_bound(phrase->notes.begin(), phrase->notes.end(), firstSeconds, [](const audio::VisualNote& note, double seconds)
                                                       { return note.endSeconds <= seconds; });
        for (auto note = firstVisibleNote; note != phrase->notes.end() && note->startSeconds < lastSeconds; ++note)
        {
            if (!note->mainReference)
            {
                continue;
            }
            const double beginSeconds = std::max({firstSeconds, phrase->startSeconds, note->startSeconds});
            const double endSeconds = std::min({lastSeconds, phrase->endSeconds, note->endSeconds});
            if (endSeconds > beginSeconds)
            {
                const float left = xAtSeconds(beginSeconds);
                noteClip.addRectangle(left, static_cast<float>(clip.getY()), xAtSeconds(endSeconds) - left, static_cast<float>(clip.getHeight()));
            }
        }
        const juce::Graphics::ScopedSaveState state(graphics);
        if (!graphics.reduceClipRegion(noteClip))
        {
            continue;
        }
        const double frameCount = static_cast<double>(data.midiPitch.size());
        const auto firstFrame = static_cast<std::size_t>(std::clamp(std::floor((firstSeconds - phrase->startSeconds) / data.frameIntervalSeconds - 0.5) - 1.0, 0.0, frameCount));
        const auto endFrame = static_cast<std::size_t>(std::clamp(std::ceil((lastSeconds - phrase->startSeconds) / data.frameIntervalSeconds - 0.5) + 2.0, 0.0, frameCount));
        const double firstFrameSeconds = phrase->startSeconds + (static_cast<double>(firstFrame) + 0.5) * data.frameIntervalSeconds;
        const auto firstNote = std::upper_bound(phrase->notes.begin(), phrase->notes.end(), firstFrameSeconds, [](double seconds, const audio::VisualNote& note)
                                                { return seconds < note.startSeconds; });
        auto note = firstNote == phrase->notes.begin() ? firstNote : std::prev(firstNote);
        juce::Path path;
        bool connected = false;
        for (auto frame = firstFrame; frame < endFrame; ++frame)
        {
            const double seconds = phrase->startSeconds + (static_cast<double>(frame) + 0.5) * data.frameIntervalSeconds;
            while (std::next(note) != phrase->notes.end() && std::next(note)->startSeconds <= seconds)
            {
                ++note;
            }
            const float pitch = data.midiPitch[frame];
            if (!note->mainReference || !std::isfinite(pitch) || seconds > phrase->endSeconds || seconds < cropStartSeconds || seconds > cropEndSeconds)
            {
                connected = false;
                continue;
            }
            const float x = xAtSeconds(seconds);
            const float y = static_cast<float>(rulerHeight + (127.5 - static_cast<double>(pitch) + static_cast<double>(note->pitchOffset)) * noteHeight - verticalPixels);
            if (connected)
            {
                path.lineTo(x, y);
            }
            else
            {
                path.startNewSubPath(x, y);
                connected = true;
            }
        }
        graphics.strokePath(path, juce::PathStrokeType(1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void PianoRoll::paintNote(juce::Graphics& graphics, const Note& note, bool selected, bool preview)
{
    const auto bounds = getNoteBounds(note);
    if (!bounds.intersects(getGridBounds().toFloat()))
    {
        return;
    }
    const auto colour = selected ? selectedNoteColour : colours::accent;
    graphics.setColour(colour.withAlpha(preview ? 0.88f : 1.0f));
    graphics.fillRoundedRectangle(bounds, 2.0f);
    graphics.setColour(colour.brighter(0.25f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);
    if (bounds.getWidth() > 15.0f)
    {
        graphics.setFont(juce::FontOptions(13.0f));
        graphics.setColour(colours::border);
        graphics.drawText(juce::String::fromUTF8(note.lyrics.c_str()), bounds.reduced(6.0f, 0.0f), juce::Justification::centredLeft, true);
    }
    if (selected && bounds.getWidth() > 24.0f)
    {
        graphics.setColour(colours::accent.darker(0.5f));
        graphics.drawVerticalLine(static_cast<int>(bounds.getRight() - 5.0f), bounds.getY() + 5.0f, bounds.getBottom() - 5.0f);
    }
}

void PianoRoll::paintKeyboard(juce::Graphics& graphics)
{
    const juce::Graphics::ScopedSaveState state(graphics);
    const juce::Rectangle<int> keyboardBounds{0, rulerHeight, keyboardWidth, getGridBounds().getHeight()};
    graphics.reduceClipRegion(keyboardBounds);
    graphics.fillAll(colours::panel);
    graphics.setFont(juce::FontOptions(11.0f));
    for (const bool black : {false, true})
    {
        for (int pitch = 0; pitch <= 127; ++pitch)
        {
            if (isBlackKey(pitch) != black)
            {
                continue;
            }
            const auto bounds = getKeyboardKeyBounds(pitch);
            if (!bounds.intersects(keyboardBounds.toFloat()))
            {
                continue;
            }
            const auto colour = juce::Colour(black ? 0xff202020 : 0xffb7b7b7);
            graphics.setColour(pitch == auditionPitch ? colour.interpolatedWith(colours::accent, black ? 0.85f : 0.7f) : colour);
            // Extend the left corners beyond the clip to round only the key fronts.
            graphics.fillRoundedRectangle(bounds.reduced(0.0f, black ? 0.0f : 0.35f).withLeft(-3.0f), 3.0f);
            if (pitch % 12 == 0)
            {
                graphics.setColour(juce::Colour(0xff737373));
                graphics.drawText(pitchName(pitch), 6, static_cast<int>(yAt(pitch)), keyboardWidth - 13, static_cast<int>(noteHeight), juce::Justification::centredRight);
            }
        }
    }
    graphics.setColour(colours::border);
    graphics.fillRect(keyboardWidth - 1, rulerHeight, 1, getGridBounds().getHeight());
}

void PianoRoll::paintRuler(juce::Graphics& graphics)
{
    graphics.setColour(colours::panel);
    graphics.fillRect(0, 0, getWidth(), rulerHeight);
    graphics.setFont(juce::FontOptions(11.5f));
    graphics.setColour(colours::subdued);
    graphics.drawText("KEY", 9, 0, keyboardWidth - 15, rulerHeight, juce::Justification::centredLeft);

    const juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion({keyboardWidth, 0, getGridBounds().getWidth(), rulerHeight});
    const auto first = blickAt(static_cast<float>(keyboardWidth));
    const auto last = blickAt(static_cast<float>(getGridBounds().getRight()));
    const auto offset = document.getActiveGroupOffset();
    for (const auto& mark : getBarMarks(document.getProject().tempoMap, first + offset, last + offset))
    {
        const auto x = static_cast<int>(xAt(mark.position - offset));
        graphics.setColour(colours::text);
        graphics.drawText(juce::String(mark.number), x + 7, 0, 70, rulerHeight - 3, juce::Justification::centredLeft);
        graphics.setColour(colours::subdued.withAlpha(0.6f));
        graphics.drawVerticalLine(x, 18.0f, static_cast<float>(rulerHeight));
    }
    const auto x = xAt(playhead - offset);
    juce::Path marker;
    marker.addTriangle(x - 5.0f, 2.0f, x + 5.0f, 2.0f, x, 10.0f);
    graphics.setColour(juce::Colour(0xfff4bc64));
    graphics.fillPath(marker);
    graphics.drawVerticalLine(static_cast<int>(x), 8.0f, static_cast<float>(rulerHeight));
}

void PianoRoll::resized()
{
    horizontalScrollBar.setBounds(keyboardWidth, getHeight() - scrollBarSize, getGridBounds().getWidth(), scrollBarSize);
    verticalScrollBar.setBounds(getWidth() - scrollBarSize, rulerHeight, scrollBarSize, getGridBounds().getHeight());
    finishLyricsEdit(true);
    updateScrollBars();
}

void PianoRoll::updateScrollBars()
{
    const auto grid = getGridBounds();
    const auto visibleQuarters = grid.getWidth() / pixelsPerQuarter;
    double endQuarter = 128.0;
    for (const auto& note : document.getActiveGroup().notes)
    {
        endQuarter = std::max(endQuarter, static_cast<double>(note.onset + note.duration) / blicksPerQuarter + 16.0);
    }
    horizontalScrollBar.setRangeLimits(0.0, std::max(endQuarter, horizontalQuarter + visibleQuarters), juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange(horizontalQuarter, visibleQuarters, juce::dontSendNotification);
    verticalScrollBar.setRangeLimits(0.0, (128.0 + (waveformVisible ? 4.0 : 0.0)) * noteHeight, juce::dontSendNotification);
    verticalScrollBar.setCurrentRange(verticalPixels, grid.getHeight(), juce::dontSendNotification);
    horizontalQuarter = horizontalScrollBar.getCurrentRangeStart();
    verticalPixels = verticalScrollBar.getCurrentRangeStart();
    if (onViewChanged)
    {
        onViewChanged(pixelsPerQuarter, static_cast<Blick>(std::llround(horizontalQuarter * blicksPerQuarter)));
    }
}

void PianoRoll::setScrollPosition(double quarter, double pixels)
{
    horizontalQuarter = std::max(0.0, quarter);
    const double contentHeight = (128.0 + (waveformVisible ? 4.0 : 0.0)) * noteHeight;
    verticalPixels = std::clamp(pixels, 0.0, std::max(0.0, contentHeight - getGridBounds().getHeight()));
    updateScrollBars();
    repaint();
}

void PianoRoll::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    finishLyricsEdit(true);
    if (scrollBar == &horizontalScrollBar)
    {
        horizontalQuarter = newRangeStart;
    }
    else
    {
        verticalPixels = newRangeStart;
    }
    if (onViewChanged)
    {
        onViewChanged(pixelsPerQuarter, static_cast<Blick>(std::llround(horizontalQuarter * blicksPerQuarter)));
    }
    repaint();
}

void PianoRoll::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        finishLyricsEdit(true);
        grabKeyboardFocus();
        cancelGesture();
        showNoteMenu(event.position);
        return;
    }
    if (!event.mods.isLeftButtonDown() && !event.mods.isMiddleButtonDown())
    {
        return;
    }
    finishLyricsEdit(true);
    grabKeyboardFocus();
    cancelGesture();
    gestureStart = event.position;
    if (event.mods.isMiddleButtonDown())
    {
        gesture = Gesture::pan;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (const auto pitch = keyboardPitchAt(event.position); pitch >= 0)
    {
        gesture = Gesture::audition;
        setAuditionPitch(pitch);
        return;
    }
    gestureStartBlick = blickAt(event.position.x);
    gestureStartPitch = pitchAt(event.position.y);
    gestureTrackIndex = document.getActiveTrackIndex();
    selectionBeforeGesture = document.getSelectedNoteIds();
    if (event.position.y < rulerHeight && event.position.x >= keyboardWidth)
    {
        gesture = Gesture::seek;
        if (onSeek)
        {
            onSeek(std::max<Blick>(0, snapped(gestureStartBlick) + document.getActiveGroupOffset()));
        }
        return;
    }
    if (!getGridBounds().toFloat().contains(event.position))
    {
        return;
    }
    if (const auto* note = noteAt(event.position))
    {
        const auto clickedId = note->id;
        const auto rightEdge = getNoteBounds(*note).getRight();
        auto selection = document.getSelectedNoteIds();
        const auto selected = std::find(selection.begin(), selection.end(), clickedId);
        if (event.mods.isCommandDown() && selected != selection.end())
        {
            selection.erase(selected);
            document.setSelectedNoteIds(std::move(selection));
            repaint();
            return;
        }
        if (selected == selection.end())
        {
            if (!event.mods.isCommandDown() && !event.mods.isShiftDown())
            {
                selection.clear();
            }
            selection.push_back(clickedId);
            document.setSelectedNoteIds(std::move(selection));
        }
        for (const auto& current : document.getActiveGroup().notes)
        {
            if (isSelected(current.id))
            {
                gestureOriginals.push_back(current);
            }
        }
        gestureNotes = gestureOriginals;
        gesture = event.position.x >= rightEdge - 7.0f ? Gesture::resize : Gesture::move;
    }
    else if (drawMode && event.getNumberOfClicks() == 1)
    {
        Note drawnNote;
        drawnNote.id = createNoteId();
        drawnNote.onset = std::max<Blick>(0, snapped(gestureStartBlick));
        drawnNote.duration = snap;
        drawnNote.pitch = gestureStartPitch;
        gestureNotes.push_back(std::move(drawnNote));
        gesture = Gesture::draw;
    }
    else
    {
        gesture = Gesture::select;
        if (!event.mods.isShiftDown() && !event.mods.isCommandDown())
        {
            selectionBeforeGesture.clear();
            document.setSelectedNoteIds({});
        }
        selectionRectangle = {event.position.x, event.position.y, 0.0f, 0.0f};
    }
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& event)
{
    if (gesture == Gesture::none)
    {
        return;
    }
    if (gesture == Gesture::audition)
    {
        if (!event.mods.isLeftButtonDown())
        {
            cancelGesture();
            return;
        }
        setAuditionPitch(keyboardPitchAt(event.position));
        return;
    }
    if (gesture == Gesture::pan)
    {
        const auto delta = event.position - gestureStart;
        gestureStart = event.position;
        setScrollPosition(horizontalQuarter - delta.x / pixelsPerQuarter, verticalPixels - delta.y);
        return;
    }
    if (gesture == Gesture::seek)
    {
        if (onSeek)
        {
            onSeek(std::max<Blick>(0, snapped(blickAt(event.position.x)) + document.getActiveGroupOffset()));
        }
        return;
    }
    if (gesture == Gesture::select)
    {
        selectionRectangle = juce::Rectangle<float>(gestureStart, event.position).getIntersection(getGridBounds().toFloat());
        auto selection = selectionBeforeGesture;
        for (const auto& note : document.getActiveGroup().notes)
        {
            if (getNoteBounds(note).intersects(selectionRectangle) && std::find(selection.begin(), selection.end(), note.id) == selection.end())
            {
                selection.push_back(note.id);
            }
        }
        document.setSelectedNoteIds(std::move(selection));
        repaint();
        return;
    }
    if (gesture == Gesture::draw)
    {
        auto& note = gestureNotes.front();
        note.duration = std::max(snap, snapped(blickAt(event.position.x)) - note.onset);
        reportPosition(note.onset, note.pitch);
        repaint();
        return;
    }
    if (event.getDistanceFromDragStart() < 3)
    {
        return;
    }
    auto timeDelta = snapped(blickAt(event.position.x) - gestureStartBlick);
    int pitchDelta = pitchAt(event.position.y) - gestureStartPitch;
    for (const auto& note : gestureOriginals)
    {
        if (gesture == Gesture::move)
        {
            timeDelta = std::max(timeDelta, -note.onset);
            pitchDelta = std::clamp(pitchDelta, -note.pitch, 127 - note.pitch);
        }
        else
        {
            timeDelta = std::max(timeDelta, snap - note.duration);
        }
    }
    gestureNotes = gestureOriginals;
    for (auto& note : gestureNotes)
    {
        if (gesture == Gesture::move)
        {
            note.onset += timeDelta;
            note.pitch += pitchDelta;
        }
        else
        {
            note.duration += timeDelta;
        }
    }
    gestureChanged = timeDelta != 0 || (gesture == Gesture::move && pitchDelta != 0);
    if (!gestureNotes.empty())
    {
        reportPosition(gestureNotes.front().onset, gestureNotes.front().pitch);
    }
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent& event)
{
    if (gesture == Gesture::pan || gesture == Gesture::audition)
    {
        cancelGesture();
        mouseMove(event);
        return;
    }
    if (gesture == Gesture::draw || ((gesture == Gesture::move || gesture == Gesture::resize) && gestureChanged))
    {
        const auto notes = gestureNotes;
        const auto trackIndex = gestureTrackIndex;
        const bool inserting = gesture == Gesture::draw;
        const auto label = inserting ? "Add note" : gesture == Gesture::resize ? "Resize notes"
                                                                               : "Move notes";
        document.performEdit(label, [notes, trackIndex, inserting](Project& project)
                             {
            auto& target = project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup.notes;
            if (inserting)
            {
                target.insert(target.end(), notes.begin(), notes.end());
                return;
            }
            for (const auto& replacement : notes)
            {
                const auto found = std::find_if(target.begin(), target.end(), [&replacement](const Note& note) { return note.id == replacement.id; });
                if (found != target.end())
                {
                    *found = replacement;
                }
            } });
        if (inserting && !notes.empty())
        {
            document.setSelectedNoteIds({notes.front().id});
        }
    }
    cancelGesture();
    updateScrollBars();
    repaint();
}

void PianoRoll::mouseMove(const juce::MouseEvent& event)
{
    if (!getGridBounds().toFloat().contains(event.position))
    {
        setMouseCursor(keyboardPitchAt(event.position) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        return;
    }
    if (const auto* note = noteAt(event.position))
    {
        setMouseCursor(event.position.x >= getNoteBounds(*note).getRight() - 7.0f ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    }
    else
    {
        setMouseCursor(drawMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }
    reportPosition(std::max<Blick>(0, snapped(blickAt(event.position.x))), pitchAt(event.position.y));
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || event.mods.isMiddleButtonDown() || !getGridBounds().toFloat().contains(event.position))
    {
        return;
    }
    cancelGesture();
    if (const auto* note = noteAt(event.position))
    {
        document.setSelectedNoteIds({note->id});
        beginLyricsEdit(*note);
        return;
    }
    Note note;
    note.id = createNoteId();
    note.onset = std::max<Blick>(0, snapped(blickAt(event.position.x)));
    note.duration = blicksPerQuarter;
    note.pitch = pitchAt(event.position.y);
    const auto trackIndex = document.getActiveTrackIndex();
    document.performEdit("Add note", [note, trackIndex](Project& project)
                         { project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup.notes.push_back(note); });
    document.setSelectedNoteIds({note.id});
    updateScrollBars();
    repaint();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (gesture == Gesture::pan || gesture == Gesture::audition)
    {
        return;
    }
    finishLyricsEdit(true);
    if (event.mods.isCommandDown())
    {
        const auto anchorX = std::max(static_cast<float>(keyboardWidth), event.position.x);
        const double anchorQuarter = static_cast<double>(blickAt(anchorX)) / blicksPerQuarter;
        pixelsPerQuarter = std::clamp(pixelsPerQuarter * std::pow(1.25, static_cast<double>(wheel.deltaY) * 3.0), 24.0, 800.0);
        setScrollPosition(anchorQuarter - (anchorX - keyboardWidth) / pixelsPerQuarter, verticalPixels);
    }
    else if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        setScrollPosition(horizontalQuarter - delta * 480.0 / pixelsPerQuarter, verticalPixels);
    }
    else
    {
        setScrollPosition(horizontalQuarter, verticalPixels - wheel.deltaY * noteHeight * 12.0);
    }
}

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        cancelGesture();
        document.setSelectedNoteIds({});
        repaint();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelectedNotes();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        selectAllNotes();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
        {
            document.redo();
        }
        else
        {
            document.undo();
        }
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        document.redo();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        const int direction = key.getKeyCode() == juce::KeyPress::upKey ? 1 : -1;
        moveSelection(0, direction * (key.getModifiers().isShiftDown() ? 12 : 1));
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        const int direction = key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1;
        moveSelection(direction * (key.getModifiers().isShiftDown() ? blicksPerQuarter : snap), 0);
        return true;
    }
    if (key == juce::KeyPress::returnKey && document.getSelectedNoteIds().size() == 1)
    {
        const auto selectedId = document.getSelectedNoteIds().front();
        for (const auto& note : document.getActiveGroup().notes)
        {
            if (note.id == selectedId)
            {
                beginLyricsEdit(note);
                return true;
            }
        }
    }
    return false;
}

void PianoRoll::showNoteMenu(juce::Point<float> position)
{
    if (!getGridBounds().toFloat().contains(position))
    {
        return;
    }
    if (const auto* note = noteAt(position); note != nullptr && !isSelected(note->id))
    {
        document.setSelectedNoteIds({note->id});
        repaint();
    }

    juce::PopupMenu menu;
    juce::PopupMenu::Item item(juce::String::fromUTF8("填入歌词…"));
    item.itemID = 1;
    item.shortcutKeyDescription = "Ctrl+L";
    item.isEnabled = !document.getSelectedNoteIds().empty();
    menu.addItem(std::move(item));
    const juce::Component::SafePointer<PianoRoll> safe(this);
    const auto generation = document.getGeneration();
    const auto groupId = document.getActiveGroup().id;
    const auto selection = document.getSelectedNoteIds();
    const auto screenPosition = localPointToGlobal(position.toInt());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}).withDeletionCheck(*this), [safe, generation, groupId, selection](int result)
                       {
        if (result == 1 && safe != nullptr && safe->document.getGeneration() == generation && safe->document.getActiveGroup().id == groupId && safe->document.getSelectedNoteIds() == selection)
        {
            safe->fillSelectedLyrics();
        } });
}

void PianoRoll::fillSelectedLyrics()
{
    finishLyricsEdit(true);
    cancelGesture();
    const auto& selection = document.getSelectedNoteIds();
    const std::unordered_set<NoteId> selectedIds(selection.begin(), selection.end());
    std::vector<NoteId> orderedIds;
    juce::StringArray initialLyrics;
    // ProjectDocument keeps notes stably sorted by onset, regardless of selection order.
    for (const auto& note : document.getActiveGroup().notes)
    {
        if (selectedIds.contains(note.id))
        {
            orderedIds.push_back(note.id);
            initialLyrics.add(juce::String::fromUTF8(note.lyrics.c_str()));
        }
    }
    if (orderedIds.empty())
    {
        return;
    }

    const juce::Component::SafePointer<PianoRoll> safe(this);
    const auto generation = document.getGeneration();
    const auto groupId = document.getActiveGroup().id;
    LyricsDialog::show(*this, initialLyrics.joinIntoString(" "), static_cast<int>(orderedIds.size()), [safe, generation, groupId, orderedIds](const juce::StringArray& lyrics, bool loop)
                       {
        if (safe == nullptr || safe->document.getGeneration() != generation || lyrics.isEmpty())
        {
            return;
        }
        const auto& tracks = safe->document.getProject().tracks;
        const auto track = std::find_if(tracks.begin(), tracks.end(), [&groupId](const Track& candidate) { return candidate.mainGroup.id == groupId; });
        if (track == tracks.end())
        {
            return;
        }

        const auto tokenCount = static_cast<std::size_t>(lyrics.size());
        const auto fillCount = loop ? orderedIds.size() : std::min(orderedIds.size(), tokenCount);
        std::unordered_map<NoteId, std::string> replacements;
        replacements.reserve(fillCount);
        for (std::size_t index = 0; index < fillCount; ++index)
        {
            replacements.emplace(orderedIds[index], lyrics[static_cast<int>(index % tokenCount)].toStdString());
        }
        const bool changed = std::any_of(track->mainGroup.notes.begin(), track->mainGroup.notes.end(), [&replacements](const Note& note)
        {
            const auto replacement = replacements.find(note.id);
            return replacement != replacements.end() && note.lyrics != replacement->second;
        });
        if (!changed)
        {
            return;
        }

        safe->document.performEdit(juce::String::fromUTF8("填入歌词"), [&groupId, &replacements](Project& project)
        {
            const auto target = std::find_if(project.tracks.begin(), project.tracks.end(), [&groupId](const Track& candidate) { return candidate.mainGroup.id == groupId; });
            if (target == project.tracks.end())
            {
                return;
            }
            for (auto& note : target->mainGroup.notes)
            {
                const auto replacement = replacements.find(note.id);
                if (replacement != replacements.end() && note.lyrics != replacement->second)
                {
                    note.lyrics = replacement->second;
                    note.phonemes.clear();
                }
            }
        }); });
}

void PianoRoll::beginLyricsEdit(const Note& note)
{
    finishLyricsEdit(true);
    editedNoteId = note.id;
    lyricsEditor.setText(juce::String::fromUTF8(note.lyrics.c_str()), false);
    auto bounds = getNoteBounds(note).toNearestInt().expanded(1);
    bounds.setWidth(std::max(150, bounds.getWidth()));
    bounds = bounds.constrainedWithin(getGridBounds());
    lyricsEditor.setBounds(bounds);
    lyricsEditor.setVisible(true);
    lyricsEditor.toFront(true);
    lyricsEditor.grabKeyboardFocus();
    lyricsEditor.selectAll();
}

void PianoRoll::finishLyricsEdit(bool commit)
{
    if (!editedNoteId.has_value() || finishEditing)
    {
        return;
    }
    finishEditing = true;
    const auto noteId = *editedNoteId;
    const auto lyrics = lyricsEditor.getText().toStdString();
    editedNoteId.reset();
    lyricsEditor.setVisible(false);
    if (commit)
    {
        const auto& notes = document.getActiveGroup().notes;
        const auto found = std::find_if(notes.begin(), notes.end(), [noteId](const Note& note)
                                        { return note.id == noteId; });
        if (found != notes.end() && found->lyrics != lyrics)
        {
            const auto trackIndex = document.getActiveTrackIndex();
            document.performEdit("Edit lyrics", [noteId, lyrics, trackIndex](Project& project)
                                 {
                auto& target = project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup.notes;
                const auto note = std::find_if(target.begin(), target.end(), [noteId](const Note& current) { return current.id == noteId; });
                if (note != target.end())
                {
                    note->lyrics = lyrics;
                    note->phonemes.clear();
                } });
        }
    }
    finishEditing = false;
    repaint();
}

void PianoRoll::setAuditionPitch(int pitch)
{
    if (auditionPitch == pitch)
    {
        return;
    }
    auditionPitch = pitch;
    if (onAuditionPitch)
    {
        onAuditionPitch(pitch);
    }
    if (pitch >= 0 && onStatus)
    {
        onStatus(juce::String::fromUTF8("键盘试听 · ") + pitchName(pitch) + " · MIDI " + juce::String(pitch));
    }
    repaint(0, rulerHeight, keyboardWidth, getGridBounds().getHeight());
}

void PianoRoll::focusLost(FocusChangeType)
{
    if (gesture == Gesture::audition)
    {
        cancelGesture();
    }
}

void PianoRoll::visibilityChanged()
{
    if (!isShowing() && gesture == Gesture::audition)
    {
        cancelGesture();
    }
}

void PianoRoll::enablementChanged()
{
    if (!isEnabled() && gesture == Gesture::audition)
    {
        cancelGesture();
    }
}

void PianoRoll::cancelGesture()
{
    setAuditionPitch(-1);
    if (gesture == Gesture::pan)
    {
        setMouseCursor(drawMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }
    gesture = Gesture::none;
    gestureOriginals.clear();
    gestureNotes.clear();
    gestureChanged = false;
    selectionRectangle = {};
}

void PianoRoll::commitPendingEdits()
{
    finishLyricsEdit(true);
}

void PianoRoll::deleteSelectedNotes()
{
    finishLyricsEdit(true);
    cancelGesture();
    const auto selection = document.getSelectedNoteIds();
    if (selection.empty())
    {
        return;
    }
    const auto trackIndex = document.getActiveTrackIndex();
    document.performEdit("Delete notes", [selection, trackIndex](Project& project)
                         {
        auto& notes = project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup.notes;
        std::erase_if(notes, [&selection](const Note& note)
        {
            return std::find(selection.begin(), selection.end(), note.id) != selection.end();
        }); });
    document.setSelectedNoteIds({});
    repaint();
}

void PianoRoll::selectAllNotes()
{
    std::vector<NoteId> ids;
    ids.reserve(document.getActiveGroup().notes.size());
    for (const auto& note : document.getActiveGroup().notes)
    {
        ids.push_back(note.id);
    }
    document.setSelectedNoteIds(std::move(ids));
    repaint();
}

void PianoRoll::moveSelection(Blick timeDelta, int pitchDelta)
{
    cancelGesture();
    const auto selection = document.getSelectedNoteIds();
    if (selection.empty())
    {
        return;
    }
    for (const auto& note : document.getActiveGroup().notes)
    {
        if (isSelected(note.id))
        {
            timeDelta = std::max(timeDelta, -note.onset);
            pitchDelta = std::clamp(pitchDelta, -note.pitch, 127 - note.pitch);
        }
    }
    if (timeDelta == 0 && pitchDelta == 0)
    {
        return;
    }
    const auto trackIndex = document.getActiveTrackIndex();
    document.performEdit("Move notes", [selection, trackIndex, timeDelta, pitchDelta](Project& project)
                         {
        for (auto& note : project.tracks[static_cast<std::size_t>(trackIndex)].mainGroup.notes)
        {
            if (std::find(selection.begin(), selection.end(), note.id) != selection.end())
            {
                note.onset += timeDelta;
                note.pitch += pitchDelta;
            }
        } });
    repaint();
}

void PianoRoll::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (activeGroupId != document.getActiveGroup().id)
    {
        finishLyricsEdit(false);
        cancelGesture();
        activeGroupId = document.getActiveGroup().id;
    }
    if (editedNoteId.has_value())
    {
        const auto& notes = document.getActiveGroup().notes;
        if (std::none_of(notes.begin(), notes.end(), [this](const Note& note)
                         { return note.id == *editedNoteId; }))
        {
            finishLyricsEdit(false);
        }
    }
    updateScrollBars();
    repaint();
}

void PianoRoll::setPlayhead(Blick position, bool followPlayback)
{
    const auto offset = document.getActiveGroupOffset();
    if (followPlayback && gesture == Gesture::none && !lyricsEditor.isVisible())
    {
        const auto quarter = static_cast<double>(position - offset) / blicksPerQuarter;
        const auto pageStart = playbackPageStart(quarter, horizontalQuarter, getGridBounds().getWidth() / pixelsPerQuarter);
        if (pageStart != horizontalQuarter)
        {
            playhead = position;
            setScrollPosition(pageStart, verticalPixels);
            return;
        }
    }
    if (playhead == position)
    {
        return;
    }
    const auto oldX = static_cast<int>(xAt(playhead - offset));
    playhead = position;
    const auto newX = static_cast<int>(xAt(playhead - offset));
    repaint(oldX - 7, 0, 15, getHeight() - scrollBarSize);
    repaint(newX - 7, 0, 15, getHeight() - scrollBarSize);
}

void PianoRoll::setPixelsPerQuarter(double pixels)
{
    finishLyricsEdit(true);
    pixelsPerQuarter = std::clamp(pixels, 24.0, 800.0);
    updateScrollBars();
    repaint();
}

double PianoRoll::getPixelsPerQuarter() const
{
    return pixelsPerQuarter;
}

void PianoRoll::setSnap(Blick value)
{
    snap = std::max<Blick>(1, value);
    repaint();
}

void PianoRoll::setDrawMode(bool enabled)
{
    drawMode = enabled;
    setMouseCursor(drawMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::setVisualization(std::shared_ptr<const audio::RenderVisualization> replacement)
{
    if (visualization == replacement)
    {
        return;
    }
    visualization = std::move(replacement);
    repaint();
}

void PianoRoll::setPitchVisible(bool visible)
{
    if (pitchVisible != visible)
    {
        pitchVisible = visible;
        repaint();
    }
}

void PianoRoll::setWaveformVisible(bool visible)
{
    if (waveformVisible != visible)
    {
        waveformVisible = visible;
        updateScrollBars();
        repaint();
    }
}

bool PianoRoll::isPitchVisible() const
{
    return pitchVisible;
}

bool PianoRoll::isWaveformVisible() const
{
    return waveformVisible;
}

void PianoRoll::zoomToFit()
{
    const auto& notes = document.getActiveGroup().notes;
    if (notes.empty())
    {
        setPixelsPerQuarter(100.0);
        scrollTo(0, 67);
        return;
    }
    Blick first = notes.front().onset;
    Blick last = first;
    int lowest = 127;
    int highest = 0;
    for (const auto& note : notes)
    {
        first = std::min(first, note.onset);
        last = std::max(last, note.onset + note.duration);
        lowest = std::min(lowest, note.pitch);
        highest = std::max(highest, note.pitch);
    }
    const double length = std::max(4.0, static_cast<double>(last - first) / blicksPerQuarter + 2.0);
    setPixelsPerQuarter(getGridBounds().getWidth() / length);
    const double centrePitch = (lowest + highest) * 0.5 - (waveformVisible ? 2.0 : 0.0);
    const double vertical = (127.0 - centrePitch) * noteHeight - getGridBounds().getHeight() * 0.5 + noteHeight * 0.5;
    setScrollPosition(static_cast<double>(std::max<Blick>(0, first - blicksPerQuarter)) / blicksPerQuarter, vertical);
}

void PianoRoll::scrollTo(Blick position, int pitch)
{
    const auto newVertical = pitch >= 0 ? (127.0 - std::clamp(pitch, 0, 127)) * noteHeight - getGridBounds().getHeight() * 0.5 + noteHeight * 0.5 : verticalPixels;
    setScrollPosition(static_cast<double>(position) / blicksPerQuarter, newVertical);
}

void PianoRoll::reportPosition(Blick position, int pitch)
{
    if (onStatus)
    {
        onStatus(pitchName(pitch) + "  ·  " + juce::String(static_cast<double>(position) / blicksPerQuarter + 1.0, 2) + " beats");
    }
}
} // namespace sv
