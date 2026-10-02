#pragma once

#include "core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sv
{
namespace audio
{
struct RenderVisualization;
struct TrackVisualization;
} // namespace audio

class PianoRoll final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll(ProjectDocument& document);
    ~PianoRoll() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusLost(FocusChangeType cause) override;
    void visibilityChanged() override;
    void enablementChanged() override;

    void setPlayhead(Blick position, bool followPlayback);
    void setPixelsPerQuarter(double pixels);
    [[nodiscard]] double getPixelsPerQuarter() const;
    void setSnap(Blick value);
    void setDrawMode(bool enabled);
    // All visualization access and shared ownership changes stay on the message thread.
    void setVisualization(std::shared_ptr<const audio::RenderVisualization> replacement);
    void setPitchVisible(bool visible);
    void setWaveformVisible(bool visible);
    [[nodiscard]] bool isPitchVisible() const;
    [[nodiscard]] bool isWaveformVisible() const;
    void zoomToFit();
    void scrollTo(Blick position, int pitch = -1);
    void commitPendingEdits();
    void deleteSelectedNotes();
    void selectAllNotes();
    void fillSelectedLyrics();

    std::function<void(Blick)> onSeek;
    std::function<void(int)> onAuditionPitch;
    std::function<void(juce::String)> onStatus;
    std::function<void(double, Blick)> onViewChanged;

private:
    enum class Gesture
    {
        none,
        pan,
        audition,
        move,
        resize,
        select,
        draw,
        seek
    };

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;
    [[nodiscard]] juce::Rectangle<int> getGridBounds() const;
    [[nodiscard]] juce::Rectangle<float> getNoteBounds(const Note& note) const;
    [[nodiscard]] float xAt(Blick position) const;
    [[nodiscard]] float xAtSeconds(double seconds) const;
    [[nodiscard]] double secondsAt(float x) const;
    [[nodiscard]] Blick blickAt(float x) const;
    [[nodiscard]] int pitchAt(float y) const;
    [[nodiscard]] int keyboardPitchAt(juce::Point<float> point) const;
    [[nodiscard]] juce::Rectangle<float> getKeyboardKeyBounds(int pitch) const;
    [[nodiscard]] float yAt(int pitch) const;
    [[nodiscard]] Blick snapped(Blick position) const;
    [[nodiscard]] const Note* noteAt(juce::Point<float> point) const;
    [[nodiscard]] bool isSelected(NoteId id) const;
    [[nodiscard]] const audio::TrackVisualization* getVisualizationTrack() const;
    void updateScrollBars();
    void setScrollPosition(double quarter, double pixels);
    void paintGrid(juce::Graphics& graphics);
    void paintKeyboard(juce::Graphics& graphics);
    void paintRuler(juce::Graphics& graphics);
    void paintNote(juce::Graphics& graphics, const Note& note, bool selected, bool preview);
    void paintWaveforms(juce::Graphics& graphics);
    void paintPitch(juce::Graphics& graphics);
    void beginLyricsEdit(const Note& note);
    void showNoteMenu(juce::Point<float> position);
    void finishLyricsEdit(bool commit);
    void setAuditionPitch(int pitch);
    void cancelGesture();
    void moveSelection(Blick timeDelta, int pitchDelta);
    void reportPosition(Blick position, int pitch);

    ProjectDocument& document;
    std::shared_ptr<const audio::RenderVisualization> visualization;
    juce::ScrollBar horizontalScrollBar{false};
    juce::ScrollBar verticalScrollBar{true};
    juce::TextEditor lyricsEditor;
    std::optional<NoteId> editedNoteId;
    std::string activeGroupId;
    std::vector<Note> gestureOriginals;
    std::vector<Note> gestureNotes;
    std::vector<NoteId> selectionBeforeGesture;
    juce::Point<float> gestureStart;
    juce::Rectangle<float> selectionRectangle;
    Gesture gesture = Gesture::none;
    int gestureTrackIndex = 0;
    Blick gestureStartBlick = 0;
    int gestureStartPitch = 60;
    int auditionPitch = -1;
    Blick snap = blicksPerQuarter / 4;
    Blick playhead = 0;
    double pixelsPerQuarter = 100.0;
    double horizontalQuarter = 0.0;
    double verticalPixels = 50.0 * 22.0;
    bool drawMode = false;
    bool finishEditing = false;
    bool gestureChanged = false;
    bool pitchVisible = true;
    bool waveformVisible = true;

    static constexpr int keyboardWidth = 74;
    static constexpr int rulerHeight = 30;
    static constexpr int scrollBarSize = 12;
    static constexpr float noteHeight = 22.0f;
};
} // namespace sv
