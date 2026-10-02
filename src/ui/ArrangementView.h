#pragma once

#include "TrackHeader.h"
#include "core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sv
{
class ArrangementView final : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView(ProjectDocument& document);
    ~ArrangementView() override;

    void setPlayhead(Blick position, bool followPlayback);
    void setPixelsPerQuarter(double value);
    void commitPendingEdits();

    std::function<void(Blick)> onSeek;
    std::function<void(int)> onChooseVoice;
    std::function<void(int)> onShowTrackSettings;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    void updateScrollbars();
    void refreshTrackHeaders();
    void layoutTrackHeaders();
    void drawGrid(juce::Graphics& graphics) const;
    void drawTrack(juce::Graphics& graphics, int index) const;
    void drawGroup(juce::Graphics& graphics, const NoteGroup& group, const GroupReference& reference, int trackIndex, bool isReference) const;
    void beginRename(int trackIndex);
    void finishRename(bool commit);
    void seekAt(float x);

    [[nodiscard]] juce::Rectangle<int> getTimelineBounds() const;
    [[nodiscard]] juce::Rectangle<int> getTrackBounds(int index) const;
    [[nodiscard]] int trackAt(float y) const;
    [[nodiscard]] float xAt(Blick position) const;
    [[nodiscard]] double quarterAt(float x) const;

    static constexpr int headerWidth = 300;
    static constexpr int rulerHeight = 30;
    static constexpr int trackHeight = 64;
    static constexpr int scrollbarSize = 12;

    ProjectDocument& document;
    juce::ScrollBar horizontalScrollbar{false};
    juce::ScrollBar verticalScrollbar{true};
    juce::Component headerContents;
    std::vector<std::unique_ptr<TrackHeader>> trackHeaders;
    juce::TextEditor renameEditor;
    Blick playhead = 0;
    double pixelsPerQuarter = 56.0;
    double scrollQuarter = 0.0;
    double scrollTrackY = 0.0;
    int renameTrack = -1;
    int lastActiveTrack = -1;
    std::string renameGroupId;
    std::uint64_t renameGeneration = 0;
    bool isSeeking = false;
};
} // namespace sv
