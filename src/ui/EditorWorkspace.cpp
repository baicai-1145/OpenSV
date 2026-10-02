#include "EditorWorkspace.h"

#include "StudioTheme.h"

#include <algorithm>
#include <cmath>

namespace sv
{
namespace
{
constexpr int sectionHeaderHeight = 30;
constexpr int sectionTitleWidth = 112;
constexpr int dividerHeight = 6;
constexpr std::array<int, 3> minimumContentHeights{96, 160, 72};
constexpr std::array<double, 3> defaultProportions{0.30, 0.55, 0.15};
} // namespace

EditorWorkspace::DisclosureButton::DisclosureButton(const juce::String& title)
    : juce::Button(title)
{
    setClickingTogglesState(true);
    setToggleState(true, juce::dontSendNotification);
    setWantsKeyboardFocus(true);
    setTitle(title);
    setTooltip(juce::String::fromUTF8("折叠") + title);
}

void EditorWorkspace::DisclosureButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    if (highlighted || down)
    {
        graphics.setColour(colours::raised.withAlpha(down ? 1.0f : 0.55f));
        graphics.fillRect(getLocalBounds());
    }

    const auto centerY = static_cast<float>(getHeight()) * 0.5f;
    juce::Path arrow;
    if (getToggleState())
    {
        arrow.startNewSubPath(11.0f, centerY - 3.0f);
        arrow.lineTo(16.0f, centerY + 2.0f);
        arrow.lineTo(21.0f, centerY - 3.0f);
    }
    else
    {
        arrow.startNewSubPath(13.0f, centerY - 5.0f);
        arrow.lineTo(18.0f, centerY);
        arrow.lineTo(13.0f, centerY + 5.0f);
    }
    graphics.setColour(colours::text);
    graphics.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    graphics.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    graphics.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(31), juce::Justification::centredLeft, true);

    if (hasKeyboardFocus(false))
    {
        graphics.setColour(colours::accent);
        graphics.drawRect(getLocalBounds().reduced(2), 1);
    }
}

EditorWorkspace::Section::Section(const juce::String& title, juce::Component& contentView)
    : disclosure(title), content(contentView)
{
    setName(title);
    addAndMakeVisible(disclosure);
    addAndMakeVisible(toolbar);
    addAndMakeVisible(content);
    toolbar.setName(title + juce::String::fromUTF8("工具栏"));
}

void EditorWorkspace::Section::setExpanded(bool expanded)
{
    if (!expanded && content.hasKeyboardFocus(true))
    {
        disclosure.grabKeyboardFocus();
    }
    disclosure.setToggleState(expanded, juce::dontSendNotification);
    disclosure.setTooltip(juce::String::fromUTF8(expanded ? "折叠" : "展开") + disclosure.getButtonText());
    content.setVisible(expanded);
}

bool EditorWorkspace::Section::isExpanded() const
{
    return disclosure.getToggleState();
}

void EditorWorkspace::Section::paint(juce::Graphics& graphics)
{
    graphics.fillAll(colours::background);
    graphics.setColour(colours::panel);
    graphics.fillRect(getLocalBounds().removeFromTop(headerHeight));
    graphics.setColour(colours::border);
    graphics.drawHorizontalLine(std::max(0, headerHeight - 1), 0.0f, static_cast<float>(getWidth()));
}

void EditorWorkspace::Section::resized()
{
    auto body = getLocalBounds();
    auto header = body.removeFromTop(std::min(headerHeight, body.getHeight()));
    disclosure.setBounds(header.removeFromLeft(std::min(sectionTitleWidth, header.getWidth())));
    toolbar.setBounds(header);
    if (isExpanded())
    {
        content.setBounds(body);
    }
    else
    {
        // Keep shared horizontal views aligned while preserving the hidden view's height.
        content.setSize(body.getWidth(), content.getHeight());
    }
}

EditorWorkspace::ResizeBar::ResizeBar(EditorWorkspace& owner, int index)
    : juce::StretchableLayoutResizerBar(&owner.layout, index, false), workspace(owner), itemIndex(index)
{
    setWantsKeyboardFocus(true);
    setTitle(juce::String::fromUTF8("调整区域高度"));
    setDescription(juce::String::fromUTF8("上下拖动，或使用上、下方向键调整区域高度"));
}

void EditorWorkspace::ResizeBar::paint(juce::Graphics& graphics)
{
    graphics.fillAll(colours::panel);
    graphics.setColour(isEnabled() && (isMouseOverOrDragging() || hasKeyboardFocus(false)) ? colours::accent.withAlpha(0.7f) : colours::border);
    graphics.fillRect(0, getHeight() / 2, getWidth(), 1);
}

void EditorWorkspace::ResizeBar::mouseDown(const juce::MouseEvent& event)
{
    if (isEnabled() && event.mods.isLeftButtonDown())
    {
        juce::StretchableLayoutResizerBar::mouseDown(event);
    }
}

void EditorWorkspace::ResizeBar::mouseDrag(const juce::MouseEvent& event)
{
    if (isEnabled() && event.mods.isLeftButtonDown())
    {
        juce::StretchableLayoutResizerBar::mouseDrag(event);
    }
}

bool EditorWorkspace::ResizeBar::keyPressed(const juce::KeyPress& key)
{
    if (!isEnabled() || (key.getKeyCode() != juce::KeyPress::upKey && key.getKeyCode() != juce::KeyPress::downKey))
    {
        return false;
    }
    const auto distance = key.getModifiers().isShiftDown() ? 32 : 8;
    const auto direction = key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1;
    workspace.layout.setItemPosition(itemIndex, workspace.layout.getItemCurrentPosition(itemIndex) + distance * direction);
    hasBeenMoved();
    return true;
}

void EditorWorkspace::ResizeBar::hasBeenMoved()
{
    workspace.storeDraggedProportions();
    workspace.resized();
}

EditorWorkspace::EditorWorkspace(juce::Component& arrangement, juce::Component& pianoRoll, juce::Component& parameters)
    : arrangementSection(juce::String::fromUTF8("编曲"), arrangement), pianoSection(juce::String::fromUTF8("钢琴卷帘"), pianoRoll), parameterSection(juce::String::fromUTF8("参数"), parameters), sections{&arrangementSection, &pianoSection, &parameterSection}, arrangementDivider(*this, 1), parameterDivider(*this, 3)
{
    for (int sectionIndex = 0; sectionIndex < static_cast<int>(sections.size()); ++sectionIndex)
    {
        auto& section = *sections[static_cast<std::size_t>(sectionIndex)];
        addAndMakeVisible(section);
        section.disclosure.onClick = [this, sectionIndex]
        {
            auto& target = *sections[static_cast<std::size_t>(sectionIndex)];
            target.setExpanded(target.isExpanded());
            resized();
            if (onLayoutChanged)
            {
                onLayoutChanged();
            }
        };
    }
    addAndMakeVisible(arrangementDivider);
    addAndMakeVisible(parameterDivider);
}

juce::Component& EditorWorkspace::getArrangementToolbar()
{
    return arrangementSection.toolbar;
}

juce::Component& EditorWorkspace::getPianoToolbar()
{
    return pianoSection.toolbar;
}

juce::Component& EditorWorkspace::getParameterToolbar()
{
    return parameterSection.toolbar;
}

bool EditorWorkspace::isSectionExpanded(int sectionIndex) const
{
    return juce::isPositiveAndBelow(sectionIndex, static_cast<int>(sections.size())) && sections[static_cast<std::size_t>(sectionIndex)]->isExpanded();
}

void EditorWorkspace::setSectionExpanded(int sectionIndex, bool expanded)
{
    if (!juce::isPositiveAndBelow(sectionIndex, static_cast<int>(sections.size())) || isSectionExpanded(sectionIndex) == expanded)
    {
        return;
    }
    sections[static_cast<std::size_t>(sectionIndex)]->setExpanded(expanded);
    resized();
    if (onLayoutChanged)
    {
        onLayoutChanged();
    }
}

void EditorWorkspace::resetLayout()
{
    proportions = defaultProportions;
    for (auto* section : sections)
    {
        section->setExpanded(true);
    }
    resized();
    if (onLayoutChanged)
    {
        onLayoutChanged();
    }
}

void EditorWorkspace::paint(juce::Graphics& graphics)
{
    graphics.fillAll(colours::background);
}

void EditorWorkspace::resized()
{
    const auto height = getHeight();
    const auto headerHeight = std::min(sectionHeaderHeight, height / 3);
    const auto barHeight = std::min(dividerHeight, (height - headerHeight * 3) / 2);
    const auto contentHeight = std::max(0, height - headerHeight * 3 - barHeight * 2);
    auto activeWeight = 0.0;
    auto totalMinimum = 0;
    for (std::size_t index = 0; index < sections.size(); ++index)
    {
        sections[index]->headerHeight = headerHeight;
        if (sections[index]->isExpanded())
        {
            activeWeight += proportions[index];
            totalMinimum += minimumContentHeights[index];
        }
    }

    // JUCE's layout manager uses a minimum of one pixel per item. For an empty
    // or very small workspace, place headers directly without overflowing it.
    if (activeWeight <= 0.0 || contentHeight == 0)
    {
        auto y = 0;
        for (std::size_t index = 0; index < sections.size(); ++index)
        {
            sections[index]->setBounds(0, y, getWidth(), headerHeight);
            sections[index]->resized();
            y += headerHeight;
            if (index < 2)
            {
                auto& divider = index == 0 ? arrangementDivider : parameterDivider;
                divider.setBounds(0, y, getWidth(), barHeight);
                divider.setEnabled(false);
                divider.setMouseCursor(juce::MouseCursor::NormalCursor);
                y += barHeight;
            }
        }
    }
    else
    {
        for (std::size_t index = 0; index < sections.size(); ++index)
        {
            const auto expanded = sections[index]->isExpanded();
            const auto minimumContent = expanded ? static_cast<int>(std::floor(minimumContentHeights[index] * std::min(1.0, static_cast<double>(contentHeight) / totalMinimum))) : 0;
            const auto preferred = headerHeight + (expanded ? contentHeight * proportions[index] / activeWeight : 0.0);
            layout.setItemLayout(static_cast<int>(index) * 2, headerHeight + minimumContent, expanded ? height : headerHeight, preferred);
        }
        layout.setItemLayout(1, barHeight, barHeight, barHeight);
        layout.setItemLayout(3, barHeight, barHeight, barHeight);
        std::array<juce::Component*, 5> components{&arrangementSection, &arrangementDivider, &pianoSection, &parameterDivider, &parameterSection};
        layout.layOutComponents(components.data(), static_cast<int>(components.size()), 0, 0, getWidth(), height, true, true);

        const auto hasSpareSpace = contentHeight > totalMinimum;
        arrangementDivider.setEnabled(hasSpareSpace && arrangementSection.isExpanded() && (pianoSection.isExpanded() || parameterSection.isExpanded()));
        parameterDivider.setEnabled(hasSpareSpace && parameterSection.isExpanded() && (arrangementSection.isExpanded() || pianoSection.isExpanded()));
        for (auto* divider : {&arrangementDivider, &parameterDivider})
        {
            divider->setMouseCursor(divider->isEnabled() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        }
    }
    if (onToolbarResized)
    {
        onToolbarResized();
    }
}

void EditorWorkspace::storeDraggedProportions()
{
    auto activeWeight = 0.0;
    auto totalContentHeight = 0;
    std::array<int, 3> contentHeights{};
    for (std::size_t index = 0; index < sections.size(); ++index)
    {
        if (sections[index]->isExpanded())
        {
            contentHeights[index] = std::max(0, layout.getItemCurrentAbsoluteSize(static_cast<int>(index) * 2) - sections[index]->headerHeight);
            totalContentHeight += contentHeights[index];
            activeWeight += proportions[index];
        }
    }
    if (totalContentHeight <= 0)
    {
        return;
    }
    for (std::size_t index = 0; index < sections.size(); ++index)
    {
        if (sections[index]->isExpanded())
        {
            proportions[index] = activeWeight * contentHeights[index] / totalContentHeight;
        }
    }
}
} // namespace sv
