#include "ArrangementView.h"

#include "PlaybackPaging.h"
#include "StudioTheme.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sv
{
namespace
{
juce::String fromUtf8(const std::string& text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

} // namespace

ArrangementView::ArrangementView(ProjectDocument& documentToUse)
    : document(documentToUse)
{
    setOpaque(true);
    document.addChangeListener(this);
    for (auto* scrollbar : {&horizontalScrollbar, &verticalScrollbar})
    {
        addAndMakeVisible(*scrollbar);
        scrollbar->addListener(this);
        scrollbar->setAutoHide(false);
        scrollbar->setColour(juce::ScrollBar::backgroundColourId, colours::background);
        scrollbar->setColour(juce::ScrollBar::thumbColourId, colours::raised.brighter(0.1f));
    }
    horizontalScrollbar.setSingleStepSize(1.0);
    verticalScrollbar.setSingleStepSize(trackHeight);
    addAndMakeVisible(headerContents);
    headerContents.setInterceptsMouseClicks(false, true);
    headerContents.addChildComponent(renameEditor);
    renameEditor.setComponentID("arrangement-track-name-editor");
    renameEditor.setName("arrangement-track-name-editor");
    renameEditor.setTitle(juce::String::fromUTF8("音轨名称"));
    renameEditor.setFont(juce::FontOptions{14.0f});
    renameEditor.setSelectAllWhenFocused(true);
    renameEditor.setColour(juce::TextEditor::backgroundColourId, colours::panel);
    renameEditor.setColour(juce::TextEditor::textColourId, colours::text);
    renameEditor.setColour(juce::TextEditor::focusedOutlineColourId, colours::accent);
    renameEditor.onReturnKey = [this]
    {
        finishRename(true);
    };
    renameEditor.onEscapeKey = [this]
    {
        finishRename(false);
    };
    renameEditor.onFocusLost = [this]
    {
        finishRename(true);
    };
    refreshTrackHeaders();
}

ArrangementView::~ArrangementView()
{
    document.removeChangeListener(this);
    horizontalScrollbar.removeListener(this);
    verticalScrollbar.removeListener(this);
}

void ArrangementView::setPlayhead(Blick position, bool followPlayback)
{
    if (followPlayback && !renameEditor.isVisible())
    {
        const auto quarter = static_cast<double>(position) / blicksPerQuarter;
        const auto pageStart = playbackPageStart(quarter, scrollQuarter, getTimelineBounds().getWidth() / pixelsPerQuarter);
        if (pageStart != scrollQuarter)
        {
            playhead = position;
            scrollQuarter = pageStart;
            updateScrollbars();
            repaint();
            return;
        }
    }
    if (playhead == position)
    {
        return;
    }
    const auto previousX = xAt(playhead);
    playhead = position;
    const auto nextX = xAt(playhead);
    const auto invalidate = [this](float x)
    {
        if (x >= static_cast<float>(headerWidth) && x < static_cast<float>(getWidth()))
        {
            repaint(static_cast<int>(x) - 6, 0, 13, std::max(0, getHeight() - scrollbarSize));
        }
    };
    invalidate(previousX);
    invalidate(nextX);
}

void ArrangementView::setPixelsPerQuarter(double value)
{
    if (!std::isfinite(value))
    {
        return;
    }
    pixelsPerQuarter = juce::jlimit(16.0, 240.0, value);
    updateScrollbars();
    repaint();
}

void ArrangementView::commitPendingEdits()
{
    finishRename(true);
}

void ArrangementView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(colours::background);
    graphics.setColour(colours::panel);
    graphics.fillRect(0, 0, getWidth(), std::min(getHeight(), rulerHeight));
    graphics.fillRect(0, rulerHeight, std::min(getWidth(), headerWidth), std::max(0, getHeight() - rulerHeight));
    graphics.setColour(colours::subdued);
    graphics.setFont(juce::FontOptions{11.0f});
    graphics.drawText(juce::String::fromUTF8("音轨 · ") + juce::String(static_cast<int>(document.getProject().tracks.size())), 16, 0, headerWidth - 32, rulerHeight, juce::Justification::centredLeft);

    const auto timeline = getTimelineBounds();
    {
        juce::Graphics::ScopedSaveState state(graphics);
        graphics.reduceClipRegion(0, rulerHeight, std::max(0, getWidth() - scrollbarSize), timeline.getHeight());
        for (int index = 0; index < static_cast<int>(document.getProject().tracks.size()); ++index)
        {
            if (getTrackBounds(index).intersects(graphics.getClipBounds()))
            {
                drawTrack(graphics, index);
            }
        }
    }
    drawGrid(graphics);
    {
        juce::Graphics::ScopedSaveState state(graphics);
        graphics.reduceClipRegion(timeline);
        const auto& project = document.getProject();
        for (int index = 0; index < static_cast<int>(project.tracks.size()); ++index)
        {
            if (!getTrackBounds(index).intersects(timeline))
            {
                continue;
            }
            const auto& track = project.tracks[static_cast<std::size_t>(index)];
            drawGroup(graphics, track.mainGroup, track.mainRef, index, false);
            for (const auto& reference : track.groups)
            {
                if (const auto* group = findNoteGroup(project, reference.groupId))
                {
                    drawGroup(graphics, *group, reference, index, true);
                }
            }
        }
    }
    {
        juce::Graphics::ScopedSaveState state(graphics);
        graphics.reduceClipRegion(headerWidth, 0, timeline.getWidth(), std::max(0, getHeight() - scrollbarSize));
        const auto x = xAt(playhead);
        graphics.setColour(juce::Colour{0xffe6b976});
        graphics.drawVerticalLine(static_cast<int>(std::round(x)), 0.0f, static_cast<float>(timeline.getBottom()));
        juce::Path marker;
        marker.addTriangle(x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
        graphics.fillPath(marker);
    }
    graphics.setColour(colours::border);
    graphics.drawVerticalLine(headerWidth - 1, 0.0f, static_cast<float>(getHeight()));
    graphics.drawHorizontalLine(rulerHeight - 1, 0.0f, static_cast<float>(getWidth()));
}

void ArrangementView::resized()
{
    const auto timeline = getTimelineBounds();
    horizontalScrollbar.setBounds(headerWidth, std::max(0, getHeight() - scrollbarSize), timeline.getWidth(), std::min(getHeight(), scrollbarSize));
    verticalScrollbar.setBounds(std::max(0, getWidth() - scrollbarSize), rulerHeight, std::min(getWidth(), scrollbarSize), timeline.getHeight());
    headerContents.setBounds(0, rulerHeight, std::min(getWidth(), headerWidth), timeline.getHeight());
    updateScrollbars();
}

void ArrangementView::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
    {
        return;
    }
    isSeeking = event.position.x >= static_cast<float>(headerWidth) && event.position.y < static_cast<float>(rulerHeight);
    if (isSeeking)
    {
        seekAt(event.position.x);
        return;
    }
    const auto index = trackAt(event.position.y);
    if (index < 0)
    {
        return;
    }
    document.setActiveTrackIndex(index);
}

void ArrangementView::mouseDrag(const juce::MouseEvent& event)
{
    if (isSeeking)
    {
        seekAt(event.position.x);
    }
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        const auto anchorQuarter = quarterAt(event.position.x);
        pixelsPerQuarter = juce::jlimit(16.0, 240.0, pixelsPerQuarter * std::exp(static_cast<double>(wheel.deltaY) * 1.5));
        scrollQuarter = anchorQuarter - (static_cast<double>(event.position.x) - headerWidth) / pixelsPerQuarter;
    }
    else if (event.position.x < static_cast<float>(headerWidth))
    {
        scrollTrackY -= static_cast<double>(wheel.deltaY) * trackHeight * 3.0;
    }
    else
    {
        const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollQuarter -= static_cast<double>(delta) * 480.0 / pixelsPerQuarter;
    }
    updateScrollbars();
    if (renameTrack >= 0)
    {
        finishRename(true);
    }
    repaint();
}

void ArrangementView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (renameTrack >= 0 && (renameGeneration != document.getGeneration() || renameTrack >= static_cast<int>(document.getProject().tracks.size()) || document.getProject().tracks[static_cast<std::size_t>(renameTrack)].mainGroup.id != renameGroupId))
    {
        finishRename(false);
    }
    const auto activeTrack = document.getActiveTrackIndex();
    if (activeTrack != lastActiveTrack && activeTrack >= 0)
    {
        lastActiveTrack = activeTrack;
        const auto bounds = getTrackBounds(activeTrack);
        const auto timeline = getTimelineBounds();
        if (bounds.getY() < timeline.getY())
        {
            scrollTrackY = static_cast<double>(activeTrack * trackHeight);
        }
        else if (bounds.getBottom() > timeline.getBottom())
        {
            scrollTrackY = static_cast<double>((activeTrack + 1) * trackHeight - timeline.getHeight());
        }
    }
    refreshTrackHeaders();
    updateScrollbars();
    repaint();
}

void ArrangementView::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &horizontalScrollbar)
    {
        scrollQuarter = newRangeStart;
    }
    else
    {
        scrollTrackY = newRangeStart;
        finishRename(true);
        layoutTrackHeaders();
    }
    repaint();
}

void ArrangementView::updateScrollbars()
{
    const auto timeline = getTimelineBounds();
    const auto visibleQuarters = static_cast<double>(timeline.getWidth()) / pixelsPerQuarter;
    const auto projectQuarters = static_cast<double>(getProjectEnd(document.getProject())) / static_cast<double>(blicksPerQuarter);
    const auto totalQuarters = std::max({64.0, projectQuarters + 16.0, visibleQuarters});
    scrollQuarter = juce::jlimit(0.0, std::max(0.0, totalQuarters - visibleQuarters), scrollQuarter);
    horizontalScrollbar.setRangeLimits(0.0, totalQuarters, juce::dontSendNotification);
    horizontalScrollbar.setCurrentRange(scrollQuarter, visibleQuarters, juce::dontSendNotification);

    const auto totalTrackHeight = static_cast<double>(document.getProject().tracks.size()) * trackHeight;
    const auto visibleTrackHeight = static_cast<double>(timeline.getHeight());
    scrollTrackY = juce::jlimit(0.0, std::max(0.0, totalTrackHeight - visibleTrackHeight), scrollTrackY);
    verticalScrollbar.setRangeLimits(0.0, std::max(totalTrackHeight, visibleTrackHeight), juce::dontSendNotification);
    verticalScrollbar.setCurrentRange(scrollTrackY, visibleTrackHeight, juce::dontSendNotification);
    layoutTrackHeaders();
}

void ArrangementView::refreshTrackHeaders()
{
    std::vector<std::unique_ptr<TrackHeader>> refreshed;
    const auto& tracks = document.getProject().tracks;
    refreshed.reserve(tracks.size());
    for (std::size_t index = 0; index < tracks.size(); ++index)
    {
        const auto found = std::find_if(trackHeaders.begin(), trackHeaders.end(), [this, &tracks, index](const auto& header)
                                        { return header != nullptr && header->matches(document.getGeneration(), tracks[index].mainGroup.id); });
        if (found != trackHeaders.end())
        {
            refreshed.push_back(std::move(*found));
        }
        else
        {
            auto header = std::make_unique<TrackHeader>(document, static_cast<int>(index));
            header->onChooseVoice = [this](int trackIndex)
            {
                if (onChooseVoice)
                {
                    onChooseVoice(trackIndex);
                }
            };
            header->onShowTrackSettings = [this](int trackIndex)
            {
                if (onShowTrackSettings)
                {
                    onShowTrackSettings(trackIndex);
                }
            };
            header->onRename = [this](int trackIndex)
            {
                beginRename(trackIndex);
            };
            headerContents.addAndMakeVisible(*header);
            refreshed.push_back(std::move(header));
        }
        refreshed.back()->refresh();
    }
    trackHeaders = std::move(refreshed);
    renameEditor.toFront(false);
}

void ArrangementView::layoutTrackHeaders()
{
    for (std::size_t index = 0; index < trackHeaders.size(); ++index)
    {
        trackHeaders[index]->setBounds(0, static_cast<int>(index) * trackHeight - static_cast<int>(scrollTrackY), headerWidth, trackHeight);
    }
    if (renameTrack >= 0)
    {
        renameEditor.setBounds(44, getTrackBounds(renameTrack).getY() - rulerHeight + 7, headerWidth - 154, 24);
    }
}

void ArrangementView::drawGrid(juce::Graphics& graphics) const
{
    juce::Graphics::ScopedSaveState state(graphics);
    const auto timeline = getTimelineBounds();
    graphics.reduceClipRegion(headerWidth, 0, timeline.getWidth(), timeline.getBottom());
    const auto visibleEndQuarter = scrollQuarter + static_cast<double>(timeline.getWidth()) / pixelsPerQuarter;
    const auto& signatures = document.getProject().tempoMap.timeSignatures;
    const TimeSignature defaultSignature;
    double segmentQuarter = 0.0;
    const auto segmentCount = std::max<std::size_t>(1, signatures.size());
    for (std::size_t segment = 0; segment < segmentCount; ++segment)
    {
        const auto& signature = signatures.empty() ? defaultSignature : signatures[segment];
        const auto beatsPerBar = std::max(1, signature.numerator);
        const auto quartersPerBeat = 4.0 / static_cast<double>(std::max(1, signature.denominator));
        const auto quartersPerBar = static_cast<double>(beatsPerBar) * quartersPerBeat;
        const auto nextBar = segment + 1 < signatures.size() ? signatures[segment + 1].bar : std::numeric_limits<int>::max();
        const auto firstBar = std::max(signature.bar, signature.bar + static_cast<int>(std::floor((scrollQuarter - segmentQuarter) / quartersPerBar)));
        for (auto bar = firstBar; bar < nextBar; ++bar)
        {
            const auto barQuarter = segmentQuarter + static_cast<double>(bar - signature.bar) * quartersPerBar;
            if (barQuarter > visibleEndQuarter)
            {
                break;
            }
            const auto x = static_cast<float>(headerWidth + (barQuarter - scrollQuarter) * pixelsPerQuarter);
            graphics.setColour(juce::Colours::white.withAlpha(0.13f));
            graphics.drawVerticalLine(static_cast<int>(x), 12.0f, static_cast<float>(timeline.getBottom()));
            graphics.setColour(colours::text);
            graphics.setFont(juce::FontOptions{12.0f});
            graphics.drawText(juce::String(bar + 1), static_cast<int>(x) + 5, 3, 48, 18, juce::Justification::centredLeft);
            for (int beat = 1; beat < beatsPerBar; ++beat)
            {
                const auto beatX = x + static_cast<float>(static_cast<double>(beat) * quartersPerBeat * pixelsPerQuarter);
                graphics.setColour(juce::Colours::white.withAlpha(0.045f));
                graphics.drawVerticalLine(static_cast<int>(beatX), static_cast<float>(rulerHeight), static_cast<float>(timeline.getBottom()));
                graphics.setColour(colours::subdued.withAlpha(0.45f));
                graphics.drawVerticalLine(static_cast<int>(beatX), static_cast<float>(rulerHeight - 6), static_cast<float>(rulerHeight - 1));
            }
        }
        if (nextBar == std::numeric_limits<int>::max())
        {
            break;
        }
        segmentQuarter += static_cast<double>(nextBar - signature.bar) * quartersPerBar;
    }
    graphics.setFont(juce::FontOptions{10.5f});
    graphics.setColour(colours::subdued);
    for (const auto& tempo : document.getProject().tempoMap.tempos)
    {
        const auto x = xAt(tempo.position);
        if (x >= static_cast<float>(headerWidth) - 80.0f && x < static_cast<float>(timeline.getRight()))
        {
            graphics.drawText(juce::String(tempo.bpm, 1) + " BPM", static_cast<int>(x) + 38, 3, 90, 18, juce::Justification::centredLeft);
        }
    }
}

void ArrangementView::drawTrack(juce::Graphics& graphics, int index) const
{
    const auto bounds = getTrackBounds(index);
    graphics.setColour(colours::background);
    graphics.fillRect(bounds.withTrimmedLeft(headerWidth));
    graphics.setColour(colours::border);
    graphics.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getRight()));
}

void ArrangementView::drawGroup(juce::Graphics& graphics, const NoteGroup& group, const GroupReference& reference, int trackIndex, bool isReference) const
{
    if (group.notes.empty() && !reference.isInstrumental)
    {
        return;
    }
    if (reference.isInstrumental && reference.absoluteEnd < 0)
    {
        const auto bounds = getTrackBounds(trackIndex).withTrimmedLeft(headerWidth + 12).reduced(0, 12);
        graphics.setColour(colours::subdued);
        graphics.setFont(juce::FontOptions{12.0f});
        graphics.drawText(juce::String::fromUTF8("Audio source · duration unavailable · playback unavailable"), bounds, juce::Justification::centredLeft, true);
        return;
    }
    Blick start = std::numeric_limits<Blick>::max();
    Blick end = 0;
    int lowestPitch = 127;
    int highestPitch = 0;
    for (const auto& note : group.notes)
    {
        start = std::min(start, note.onset);
        end = std::max(end, note.onset + note.duration);
        lowestPitch = std::min(lowestPitch, note.pitch + reference.pitchOffset);
        highestPitch = std::max(highestPitch, note.pitch + reference.pitchOffset);
    }
    if (reference.isInstrumental)
    {
        start = reference.absoluteBegin;
        end = reference.absoluteEnd;
    }
    else
    {
        start += reference.timeOffset;
        end += reference.timeOffset;
    }
    if (end <= start)
    {
        return;
    }
    const auto trackBounds = getTrackBounds(trackIndex);
    const auto left = xAt(start);
    const auto right = xAt(end);
    const juce::Rectangle<float> bounds{left, static_cast<float>(trackBounds.getY() + 7), std::max(5.0f, right - left), static_cast<float>(trackHeight - 14)};
    if (!bounds.intersects(getTimelineBounds().toFloat()))
    {
        return;
    }
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(trackIndex)];
    const bool selected = document.getActiveTrackIndex() == trackIndex;
    const auto clipColour = reference.isInstrumental ? juce::Colour{0xff455764} : (track.mute ? juce::Colour{0xff475143} : (isReference ? juce::Colour{0xff426f53} : juce::Colour{0xff587944}));
    graphics.setColour(clipColour);
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(selected ? colours::accent.withAlpha(0.85f) : colours::accent.withAlpha(0.3f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion(bounds.reduced(2.0f).toNearestInt());
    graphics.setColour(juce::Colour{0xffe1efd7});
    graphics.setFont(juce::FontOptions{11.0f});
    const auto label = isReference ? fromUtf8(group.name) + "  [group]" : fromUtf8(track.name);
    graphics.drawText(label, bounds.reduced(7.0f, 3.0f).withHeight(16.0f), juce::Justification::centredLeft, true);
    if (reference.isInstrumental)
    {
        graphics.setColour(juce::Colour{0xffbfced8});
        graphics.drawText(juce::String::fromUTF8("Audio · playback unavailable"), bounds.reduced(7.0f, 3.0f).withTrimmedTop(20.0f), juce::Justification::centredLeft, true);
        return;
    }
    const auto pitchRange = static_cast<float>(std::max(12, highestPitch - lowestPitch + 1));
    const auto pitchCentre = static_cast<float>(highestPitch + lowestPitch) * 0.5f;
    const auto noteArea = bounds.withTrimmedTop(20.0f).reduced(0.0f, 4.0f);
    graphics.setColour(track.mute ? juce::Colour{0xff8a9783} : juce::Colour{0xffcae9af});
    for (const auto& note : group.notes)
    {
        const auto noteLeft = xAt(note.onset + reference.timeOffset);
        const auto noteRight = xAt(note.onset + note.duration + reference.timeOffset);
        const auto noteY = noteArea.getCentreY() - (static_cast<float>(note.pitch + reference.pitchOffset) - pitchCentre) * noteArea.getHeight() / pitchRange;
        graphics.fillRect(noteLeft, noteY, std::max(2.0f, noteRight - noteLeft - 1.0f), 2.0f);
    }
}

void ArrangementView::beginRename(int trackIndex)
{
    finishRename(true);
    renameTrack = trackIndex;
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(trackIndex)];
    renameGroupId = track.mainGroup.id;
    renameGeneration = document.getGeneration();
    renameEditor.setText(fromUtf8(track.name), false);
    layoutTrackHeaders();
    renameEditor.setVisible(true);
    renameEditor.toFront(false);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
}

void ArrangementView::finishRename(bool commit)
{
    if (renameTrack < 0)
    {
        return;
    }
    const auto index = renameTrack;
    const auto text = renameEditor.getText().trim();
    renameTrack = -1;
    renameEditor.setVisible(false);
    if (!commit || text.isEmpty() || renameGeneration != document.getGeneration() || index >= static_cast<int>(document.getProject().tracks.size()))
    {
        return;
    }
    if (document.getProject().tracks[static_cast<std::size_t>(index)].mainGroup.id != renameGroupId)
    {
        return;
    }
    const auto newName = text.toStdString();
    if (document.getProject().tracks[static_cast<std::size_t>(index)].name != newName)
    {
        document.performEdit("Rename track", [index, newName](Project& project)
                             { project.tracks[static_cast<std::size_t>(index)].name = newName; });
    }
}

void ArrangementView::seekAt(float x)
{
    const auto quarter = std::max(0.0, quarterAt(x));
    if (onSeek)
    {
        onSeek(static_cast<Blick>(std::llround(quarter * static_cast<double>(blicksPerQuarter))));
    }
}

juce::Rectangle<int> ArrangementView::getTimelineBounds() const
{
    return {headerWidth, rulerHeight, std::max(0, getWidth() - headerWidth - scrollbarSize), std::max(0, getHeight() - rulerHeight - scrollbarSize)};
}

juce::Rectangle<int> ArrangementView::getTrackBounds(int index) const
{
    return {0, rulerHeight + index * trackHeight - static_cast<int>(scrollTrackY), std::max(0, getWidth() - scrollbarSize), trackHeight};
}

int ArrangementView::trackAt(float y) const
{
    if (y < static_cast<float>(rulerHeight) || y >= static_cast<float>(getHeight() - scrollbarSize))
    {
        return -1;
    }
    const auto index = static_cast<int>((static_cast<double>(y) - rulerHeight + scrollTrackY) / trackHeight);
    return index >= 0 && index < static_cast<int>(document.getProject().tracks.size()) ? index : -1;
}

float ArrangementView::xAt(Blick position) const
{
    return static_cast<float>(headerWidth + (static_cast<double>(position) / static_cast<double>(blicksPerQuarter) - scrollQuarter) * pixelsPerQuarter);
}

double ArrangementView::quarterAt(float x) const
{
    return scrollQuarter + (static_cast<double>(x) - headerWidth) / pixelsPerQuarter;
}
} // namespace sv
