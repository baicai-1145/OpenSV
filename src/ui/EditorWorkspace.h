#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace sv
{
class EditorWorkspace final : public juce::Component
{
public:
    // The editor views and their toolbar controls remain owned by the caller.
    EditorWorkspace(juce::Component& arrangement, juce::Component& pianoRoll, juce::Component& parameters);

    [[nodiscard]] juce::Component& getArrangementToolbar();
    [[nodiscard]] juce::Component& getPianoToolbar();
    [[nodiscard]] juce::Component& getParameterToolbar();
    [[nodiscard]] bool isSectionExpanded(int sectionIndex) const;
    void setSectionExpanded(int sectionIndex, bool expanded);
    void resetLayout();

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onLayoutChanged;
    std::function<void()> onToolbarResized;

private:
    class DisclosureButton final : public juce::Button
    {
    public:
        explicit DisclosureButton(const juce::String& title);
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
    };

    class Section final : public juce::Component
    {
    public:
        Section(const juce::String& title, juce::Component& content);
        void setExpanded(bool expanded);
        [[nodiscard]] bool isExpanded() const;
        void paint(juce::Graphics& graphics) override;
        void resized() override;

        DisclosureButton disclosure;
        juce::Component toolbar;
        int headerHeight = 30;

    private:
        juce::Component& content;
    };

    class ResizeBar final : public juce::StretchableLayoutResizerBar
    {
    public:
        ResizeBar(EditorWorkspace& workspace, int itemIndex);
        void paint(juce::Graphics& graphics) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void hasBeenMoved() override;

    private:
        EditorWorkspace& workspace;
        int itemIndex;
    };

    void storeDraggedProportions();

    juce::StretchableLayoutManager layout;
    Section arrangementSection;
    Section pianoSection;
    Section parameterSection;
    std::array<Section*, 3> sections;
    ResizeBar arrangementDivider;
    ResizeBar parameterDivider;
    std::array<double, 3> proportions{0.30, 0.55, 0.15};
};
} // namespace sv
