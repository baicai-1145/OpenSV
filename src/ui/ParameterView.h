#pragma once

#include "core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sv
{
class ParameterView final : public juce::Component,
                            private juce::ChangeListener
{
public:
    explicit ParameterView(ProjectDocument& document);
    ~ParameterView() override;

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void setView(double quarterWidth, Blick leftPosition);
    void setPlayhead(Blick absolutePosition);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    [[nodiscard]] juce::Rectangle<float> getGridBounds() const;
    [[nodiscard]] float xAt(Blick position) const;
    [[nodiscard]] float yAt(double cents) const;
    [[nodiscard]] Blick blickAt(float x) const;
    [[nodiscard]] double centsAt(float y) const;
    [[nodiscard]] const ParameterCurve& getDisplayedCurve() const;
    [[nodiscard]] const std::vector<AutomationPoint>& getDisplayedPoints() const;
    [[nodiscard]] std::optional<std::size_t> pointAt(juce::Point<float> position) const;
    void cancelGesture();
    void commitGesture();
    void deletePoint(std::size_t index);

    ProjectDocument& document;
    double pixelsPerQuarter = 100.0;
    Blick left = 0;
    Blick playhead = 0;
    ParameterCurve gestureCurve;
    std::optional<std::size_t> gesturePoint;
    int gestureTrackIndex = 0;
    std::string gestureGroupId;
    std::uint64_t gestureRevision = 0;
    bool gestureChanged = false;

    static constexpr int rulerWidth = 74;
    static constexpr int scrollBarSize = 12;
    static constexpr double rangeCents = 300.0;
};
} // namespace sv
