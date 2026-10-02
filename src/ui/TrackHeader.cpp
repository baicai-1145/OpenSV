#include "TrackHeader.h"

#include "StudioTheme.h"
#include "synthesis/VoiceDatabase.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>

namespace sv
{
namespace
{
juce::String fromUtf8(const std::string& text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::String panText(double pan)
{
    if (std::abs(pan) < 0.005)
    {
        return juce::String::fromUTF8("居中");
    }
    return juce::String::fromUTF8(pan < 0.0 ? "左 " : "右 ") + juce::String(std::round(std::abs(pan) * 100.0), 0) + "%";
}

juce::DrawablePath makeVoiceIcon()
{
    juce::Path path;
    path.addEllipse(3.5f, 1.0f, 5.0f, 5.0f);
    path.addRoundedRectangle(0.5f, 7.0f, 11.0f, 5.0f, 2.0f);
    juce::Path waves;
    waves.startNewSubPath(11.0f, 2.0f);
    waves.quadraticTo(14.0f, 4.5f, 11.0f, 7.0f);
    waves.startNewSubPath(13.5f, 0.0f);
    waves.quadraticTo(18.0f, 4.5f, 13.5f, 9.0f);
    juce::Path stroke;
    juce::PathStrokeType{1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded}.createStrokedPath(stroke, waves);
    path.addPath(stroke);
    juce::DrawablePath icon;
    icon.setPath(path);
    icon.setFill(colours::border);
    return icon;
}

juce::DrawablePath makeSettingsIcon()
{
    juce::Path path;
    for (int row = 0; row < 3; ++row)
    {
        path.addRectangle(0.0f, static_cast<float>(row * 4), 14.0f, 1.25f);
    }
    juce::DrawablePath icon;
    icon.setPath(path);
    icon.setFill(colours::border.withAlpha(0.7f));
    return icon;
}
} // namespace

TrackHeader::MixerButton::MixerButton(const juce::String& label)
    : juce::TextButton(label)
{
}

void TrackHeader::MixerButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    auto& buttonTheme = getLookAndFeel();
    buttonTheme.drawButtonBackground(graphics, *this, findColour(getToggleState() ? buttonOnColourId : buttonColourId), highlighted, down);
    graphics.setColour(findColour(getToggleState() ? textColourOnId : textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
    graphics.setFont(buttonTheme.getTextButtonFont(*this, getHeight()));
    // Single-letter mixer controls do not need the standard button's wide inset.
    graphics.drawText(getButtonText(), getLocalBounds().reduced(1, 0), juce::Justification::centred, false);
}

TrackHeader::VoiceButton::VoiceButton()
    : juce::Button("track-voice")
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TrackHeader::VoiceButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    if (highlighted || down)
    {
        graphics.setColour(colours::text.withAlpha(down ? 0.12f : 0.06f));
        graphics.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    }
    graphics.setColour((highlighted ? colours::text : colours::subdued).withMultipliedAlpha(isEnabled() ? 1.0f : 0.6f));
    graphics.setFont(juce::FontOptions{13.0f});
    graphics.drawText(getButtonText(), getLocalBounds(), juce::Justification::centredLeft, true);
}

void TrackHeader::KnobLookAndFeel::drawRotarySlider(juce::Graphics& graphics, int x, int y, int width, int height, float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto diameter = static_cast<float>(std::min(width, height)) - 2.0f;
    const auto centre = juce::Point<float>{static_cast<float>(x) + static_cast<float>(width) * 0.5f, static_cast<float>(y) + static_cast<float>(height) * 0.5f};
    const auto bounds = juce::Rectangle<float>{diameter, diameter}.withCentre(centre);
    graphics.setColour(slider.isMouseOverOrDragging() ? colours::border.brighter(0.12f) : colours::border);
    graphics.fillEllipse(bounds);
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto radius = diameter * 0.5f;
    const auto outer = centre.getPointOnCircumference(radius * 0.82f, angle);
    const auto inner = centre.getPointOnCircumference(radius * 0.18f, angle);
    graphics.setColour(colours::text.withMultipliedAlpha(slider.isEnabled() ? 1.0f : 0.35f));
    graphics.drawLine({inner, outer}, 1.5f);
}

TrackHeader::TrackHeader(ProjectDocument& documentToUse, int trackIndex)
    : document(documentToUse), generation(document.getGeneration()), groupId(document.getProject().tracks[static_cast<std::size_t>(trackIndex)].mainGroup.id)
{
    setOpaque(true);
    setComponentID("track-header-" + juce::String(trackIndex + 1));
    for (auto* component : std::initializer_list<juce::Component*>{&gainSlider, &panSlider, &muteButton, &soloButton, &voiceIcon, &settingsButton, &voiceButton})
    {
        addAndMakeVisible(*component);
    }
    for (auto* slider : {&gainSlider, &panSlider})
    {
        slider->setLookAndFeel(&knobLookAndFeel);
        slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider->setPopupDisplayEnabled(true, false, nullptr);
        slider->setScrollWheelEnabled(false);
        slider->setMouseDragSensitivity(180);
        slider->setDoubleClickReturnValue(true, 0.0);
    }
    gainSlider.setName(juce::String::fromUTF8("音轨音量"));
    gainSlider.setRange(-60.0, 12.0, 0.1);
    gainSlider.setSkewFactorFromMidPoint(0.0);
    gainSlider.setTextValueSuffix(" dB");
    panSlider.setName(juce::String::fromUTF8("音轨声像"));
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.textFromValueFunction = [](double value)
    {
        return panText(value);
    };
    gainSlider.onDragStart = [this]
    {
        beginMixDrag(true);
    };
    panSlider.onDragStart = [this]
    {
        beginMixDrag(false);
    };
    gainSlider.onDragEnd = [this]
    {
        finishMixDrag(true);
    };
    panSlider.onDragEnd = [this]
    {
        finishMixDrag(false);
    };
    gainSlider.onValueChange = [this]
    {
        updateMixTooltips();
        if (!gainDragOrigin.has_value())
        {
            applyMix(true, {});
        }
    };
    panSlider.onValueChange = [this]
    {
        updateMixTooltips();
        if (!panDragOrigin.has_value())
        {
            applyMix(false, {});
        }
    };
    muteButton.setTooltip(juce::String::fromUTF8("静音"));
    soloButton.setTooltip(juce::String::fromUTF8("独奏"));
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour{0xffe1ac67});
    for (auto* button : {&muteButton, &soloButton})
    {
        button->setColour(juce::TextButton::buttonColourId, colours::background);
        button->setColour(juce::TextButton::textColourOffId, colours::subdued);
        button->onClick = [this, mute = button == &muteButton]
        {
            const auto index = selectTrack();
            if (index < 0)
            {
                return;
            }
            document.performEdit(mute ? "Toggle track mute" : "Toggle track solo", [index, mute](Project& project)
                                 {
                auto& track = project.tracks[static_cast<std::size_t>(index)];
                auto& value = mute ? track.mute : track.solo;
                value = !value; });
            refresh();
        };
    }
    auto singer = makeVoiceIcon();
    voiceIcon.setImages(&singer);
    auto settings = makeSettingsIcon();
    settingsButton.setImages(&settings);
    for (auto* button : {&voiceIcon, &settingsButton})
    {
        button->setEdgeIndent(4);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    voiceIcon.setTooltip(juce::String::fromUTF8("选择音轨声库"));
    settingsButton.setTooltip(juce::String::fromUTF8("打开音轨属性"));
    voiceButton.onClick = [this]
    {
        const auto index = selectTrack();
        if (index >= 0 && onChooseVoice)
        {
            onChooseVoice(index);
        }
    };
    voiceIcon.onClick = voiceButton.onClick;
    settingsButton.onClick = [this]
    {
        const auto index = selectTrack();
        if (index >= 0 && onShowTrackSettings)
        {
            onShowTrackSettings(index);
        }
    };
    refresh();
}

TrackHeader::~TrackHeader()
{
    for (auto* slider : {&gainSlider, &panSlider})
    {
        slider->onValueChange = {};
        slider->onDragStart = {};
        slider->onDragEnd = {};
    }
    gainSlider.setLookAndFeel(nullptr);
    panSlider.setLookAndFeel(nullptr);
}

bool TrackHeader::matches(std::uint64_t expectedGeneration, const std::string& expectedGroupId) const
{
    return generation == expectedGeneration && groupId == expectedGroupId;
}

void TrackHeader::refresh()
{
    const auto index = findTrackIndex();
    setEnabled(index >= 0);
    if (index < 0)
    {
        return;
    }
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(index)];
    selected = index == document.getActiveTrackIndex();
    trackName = fromUtf8(track.name);
    setName(trackName);
    if (!gainDragOrigin.has_value())
    {
        gainSlider.setValue(juce::Decibels::gainToDecibels(track.gain, -60.0), juce::dontSendNotification);
    }
    if (!panDragOrigin.has_value())
    {
        panSlider.setValue(track.pan, juce::dontSendNotification);
    }
    muteButton.setToggleState(track.mute, juce::dontSendNotification);
    soloButton.setToggleState(track.solo, juce::dontSendNotification);
    updateMixTooltips();
    refreshVoice(track);
    repaint();
}

void TrackHeader::paint(juce::Graphics& graphics)
{
    graphics.fillAll(selected ? colours::raised.brighter(0.12f) : colours::panel);
    graphics.setColour(selected ? colours::accent : colours::accent.darker(0.3f));
    graphics.fillRect(0, 0, 30, getHeight());
    graphics.setColour(colours::text);
    graphics.setFont(juce::FontOptions{14.0f});
    graphics.drawText(trackName, 46, 8, std::max(0, getWidth() - 158), 23, juce::Justification::centredLeft, true);
    graphics.setColour(colours::subdued);
    const auto gainCentre = gainSlider.getBounds().getCentreX();
    for (int bar = 0; bar < 5; ++bar)
    {
        const auto height = static_cast<float>(bar + 2);
        graphics.fillRect(static_cast<float>(gainCentre - 7 + bar * 3), 53.0f - height, 1.25f, height);
    }
    const auto panCentre = panSlider.getBounds().getCentreX();
    for (int bar = -3; bar <= 3; ++bar)
    {
        const auto height = static_cast<float>(bar == 0 ? 7 : std::abs(bar) + 2);
        graphics.fillRect(static_cast<float>(panCentre + bar * 2), 49.0f - height * 0.5f, 1.0f, height);
    }
    graphics.setColour(colours::border);
    graphics.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void TrackHeader::resized()
{
    voiceIcon.setBounds(6, 9, 19, 20);
    settingsButton.setBounds(6, 36, 18, 18);
    voiceButton.setBounds(46, 33, std::max(0, getWidth() - 158), 23);
    gainSlider.setBounds(getWidth() - 99, 12, 28, 28);
    panSlider.setBounds(getWidth() - 65, 12, 28, 28);
    muteButton.setBounds(getWidth() - 25, 10, 18, 19);
    soloButton.setBounds(getWidth() - 25, 33, 18, 19);
}

void TrackHeader::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown())
    {
        static_cast<void>(selectTrack());
    }
}

void TrackHeader::visibilityChanged()
{
    if (!isShowing())
    {
        gainDragOrigin.reset();
        panDragOrigin.reset();
    }
}

void TrackHeader::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto index = selectTrack();
    if (index >= 0 && event.position.x >= 30.0f && event.position.x < static_cast<float>(getWidth() - 110) && event.position.y < 33.0f && onRename)
    {
        onRename(index);
    }
}

int TrackHeader::findTrackIndex() const
{
    if (generation != document.getGeneration())
    {
        return -1;
    }
    const auto& tracks = document.getProject().tracks;
    const auto found = std::find_if(tracks.begin(), tracks.end(), [this](const Track& track)
                                    { return track.mainGroup.id == groupId; });
    return found == tracks.end() ? -1 : static_cast<int>(std::distance(tracks.begin(), found));
}

int TrackHeader::selectTrack()
{
    const auto index = findTrackIndex();
    if (index >= 0)
    {
        document.setActiveTrackIndex(index);
    }
    return index;
}

void TrackHeader::refreshVoice(const Track& track)
{
    voiceButton.setEnabled(!track.mainRef.isInstrumental);
    voiceIcon.setEnabled(!track.mainRef.isInstrumental);
    if (track.mainRef.isInstrumental)
    {
        voiceButton.setButtonText(juce::String::fromUTF8("音频轨道"));
        voiceButton.setTooltip(juce::String::fromUTF8("音频轨道不使用歌声声库"));
        voicePath.reset();
        return;
    }
    if (voicePath.has_value() && *voicePath == track.voice.databasePath)
    {
        return;
    }
    voicePath = track.voice.databasePath;
    if (voicePath->empty())
    {
        voiceButton.setButtonText(juce::String::fromUTF8("选择声库…"));
        voiceButton.setTooltip(juce::String::fromUTF8("点击设置此音轨的歌声声库"));
        return;
    }
    const juce::File file{fromUtf8(*voicePath)};
    synthesis::VoiceDatabase database;
    const auto result = database.open(file);
    if (result.failed())
    {
        voiceButton.setButtonText(juce::String::fromUTF8("声库不可用"));
        voiceButton.setTooltip(file.getFullPathName() + "\n" + result.getErrorMessage());
        return;
    }
    const auto& metadata = database.getMetadata();
    const auto name = metadata.name.isNotEmpty() ? metadata.name : file.getFileNameWithoutExtension();
    voiceButton.setButtonText(name);
    voiceButton.setTooltip(name + "\n" + file.getFullPathName() + juce::String::fromUTF8("\n点击更换声库"));
}

void TrackHeader::beginMixDrag(bool gain)
{
    const auto index = selectTrack();
    if (index < 0)
    {
        return;
    }
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(index)];
    (gain ? gainDragOrigin : panDragOrigin) = gain ? track.gain : track.pan;
}

void TrackHeader::finishMixDrag(bool gain)
{
    auto& origin = gain ? gainDragOrigin : panDragOrigin;
    const auto value = origin;
    origin.reset();
    if (value.has_value())
    {
        applyMix(gain, value);
    }
}

void TrackHeader::applyMix(bool gain, std::optional<double> originalValue)
{
    if (!isShowing())
    {
        return;
    }
    const auto index = findTrackIndex();
    if (index < 0)
    {
        return;
    }
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(index)];
    const auto previous = gain ? track.gain : track.pan;
    // An external edit to this control during the drag must not be overwritten.
    if (originalValue.has_value() && std::abs(previous - *originalValue) > 0.000001)
    {
        refresh();
        return;
    }
    const auto value = gain ? juce::Decibels::decibelsToGain(gainSlider.getValue(), -60.0) : panSlider.getValue();
    if (std::abs(previous - value) <= 0.000001)
    {
        return;
    }
    document.setActiveTrackIndex(index);
    document.performEdit(juce::String::fromUTF8(gain ? "修改轨道音量" : "修改轨道声像"), [index, gain, value](Project& project)
                         {
        auto& target = project.tracks[static_cast<std::size_t>(index)];
        (gain ? target.gain : target.pan) = value; });
}

void TrackHeader::updateMixTooltips()
{
    gainSlider.setTooltip(juce::String::fromUTF8("音量：") + juce::String(gainSlider.getValue(), 1) + juce::String::fromUTF8(" dB\n拖动调节，双击恢复 0 dB"));
    panSlider.setTooltip(juce::String::fromUTF8("声像：") + panText(panSlider.getValue()) + juce::String::fromUTF8("\n拖动调节，双击居中"));
}
} // namespace sv
