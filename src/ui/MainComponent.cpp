#include "MainComponent.h"

#include "synthesis/VoiceDatabase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace sv
{
namespace
{
constexpr std::array<const char*, 5> voiceLanguages{"japanese", "mandarin", "english", "cantonese", "spanish"};

enum MenuItem
{
    newProject = 100,
    openFile,
    saveFile,
    saveAs,
    importMidi,
    exportMidi,
    exportAudio,
    quit,
    undo = 200,
    redo,
    deleteNotes,
    selectAll,
    fillLyrics,
    fitNotes = 300,
    audioSettings,
    showPitch,
    showWaveform,
    showArrangement,
    showPianoRoll,
    showParameters,
    showInspector,
    resetWorkspace,
    addTrack = 400,
    removeTrack,
    play = 500,
    stop
};

juce::String text(const std::string& value)
{
    return juce::String::fromUTF8(value.c_str());
}

void setEditorText(juce::TextEditor& editor, const juce::String& value)
{
    if (!editor.hasKeyboardFocus(true))
    {
        editor.setText(value, false);
    }
}

juce::File findDictionaryDirectory()
{
    auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int level = 0; level < 8; ++level)
    {
        const auto candidate = directory.getChildFile("references/Synthesizer V Studio Pro.app/Contents/Resources/clf-data");
        if (candidate.isDirectory())
        {
            return candidate;
        }
        const auto parent = directory.getParentDirectory();
        if (parent == directory)
        {
            break;
        }
        directory = parent;
    }
    const auto installed = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory).getChildFile("Synthesizer V Studio Pro/clf-data");
    return installed.isDirectory() ? installed : juce::File();
}
} // namespace

void MainComponent::InspectorTextEditor::focusGained(FocusChangeType cause)
{
    juce::TextEditor::focusGained(cause);
    editing = true;
    if (onEditStarted)
    {
        onEditStarted();
    }
}

void MainComponent::InspectorTextEditor::focusLost(FocusChangeType cause)
{
    finishEdit(true);
    juce::TextEditor::focusLost(cause);
}

void MainComponent::InspectorTextEditor::enablementChanged()
{
    juce::TextEditor::enablementChanged();
    // JUCE chooses the editor accessibility role only when its handler is created.
    invalidateAccessibilityHandler();
}

bool MainComponent::InspectorTextEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'N' || key.getKeyCode() == 'O' || key.getKeyCode() == 'S'))
    {
        return false;
    }
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::escapeKey)
    {
        finishEdit(key == juce::KeyPress::returnKey);
        if (onDismiss)
        {
            onDismiss();
        }
        return true;
    }
    return juce::TextEditor::keyPressed(key);
}

void MainComponent::InspectorTextEditor::finishEdit(bool commit)
{
    if (!editing)
    {
        return;
    }
    editing = false;
    if (onEditFinished)
    {
        onEditFinished(commit);
    }
}

MainComponent::MainComponent()
{
    setLookAndFeel(&theme);
    setWantsKeyboardFocus(true);
    tempoEditor.setComponentID("project-tempo-editor");
    tempoEditor.setName("project-tempo-editor");
    tempoEditor.setTitle(juce::String::fromUTF8("起始速度 BPM"));
    lyricsEditor.setComponentID("inspector-lyrics-editor");
    lyricsEditor.setName("inspector-lyrics-editor");
    lyricsEditor.setTitle(juce::String::fromUTF8("音符歌词"));
    phonemesEditor.setComponentID("inspector-phonemes-editor");
    phonemesEditor.setName("inspector-phonemes-editor");
    phonemesEditor.setTitle(juce::String::fromUTF8("音符音素"));
    pitchEditor.setComponentID("inspector-pitch-editor");
    pitchEditor.setName("inspector-pitch-editor");
    pitchEditor.setTitle(juce::String::fromUTF8("音符音高 MIDI"));
    durationEditor.setComponentID("inspector-duration-editor");
    durationEditor.setName("inspector-duration-editor");
    durationEditor.setTitle(juce::String::fromUTF8("音符时值 四分音符"));
    voiceButton.setComponentID("track-voice-button");
    voiceButton.setName("track-voice-button");
    voiceButton.setTitle(juce::String::fromUTF8("选择当前音轨声库"));
    voiceLanguageBox.setComponentID("track-voice-language");
    voiceLanguageBox.setName("track-voice-language");
    voiceLanguageBox.setTitle(juce::String::fromUTF8("当前音轨歌声语言"));
    dictionaryButton.setComponentID("track-dictionary-button");
    dictionaryButton.setName("track-dictionary-button");
    dictionaryButton.setTitle(juce::String::fromUTF8("选择歌词词典目录"));
    playButton.setComponentID("transport-play-button");
    playButton.setName("transport-play-button");
    stopButton.setComponentID("transport-stop-button");
    stopButton.setName("transport-stop-button");
    exportButton.setComponentID("export-singing-button");
    exportButton.setName("export-singing-button");
    engineLabel.setComponentID("singing-render-status");
    engineLabel.setName("singing-render-status");
    for (auto* component : std::initializer_list<juce::Component*>{&menu, &workspace, &inspectorViewport, &inspectorButton, &deviceButton, &statusLabel})
    {
        addAndMakeVisible(component);
    }
    for (auto* component : std::initializer_list<juce::Component*>{&openButton, &saveButton, &undoButton, &redoButton, &addTrackButton, &titleLabel})
    {
        workspace.getArrangementToolbar().addAndMakeVisible(component);
    }
    for (auto* component : std::initializer_list<juce::Component*>{&selectButton, &drawButton, &snapLabel, &snapBox, &fitButton, &playButton, &stopButton, &tempoLabel, &tempoEditor, &positionLabel})
    {
        workspace.getPianoToolbar().addAndMakeVisible(component);
    }
    workspace.getParameterToolbar().addAndMakeVisible(parameterLabel);
    workspace.getParameterToolbar().addAndMakeVisible(parameterHintLabel);
    parameterLabel.setColour(juce::Label::textColourId, colours::subdued);
    parameterHintLabel.setColour(juce::Label::textColourId, colours::subdued);
    parameterHintLabel.setJustificationType(juce::Justification::centredRight);
    for (auto* component : std::initializer_list<juce::Component*>{&exportButton, &inspectorTitle, &selectionLabel, &lyricsLabel, &lyricsEditor, &phonemesLabel, &phonemesEditor, &pitchLabel, &pitchEditor, &durationLabel, &durationEditor, &trackLabel, &gainLabel, &gainSlider, &engineTitle, &voiceButton, &voiceLanguageBox, &dictionaryButton, &engineLabel, &helpLabel})
    {
        inspectorContents.addAndMakeVisible(component);
    }
    inspectorViewport.setViewedComponent(&inspectorContents, false);
    inspectorViewport.setScrollBarsShown(true, false);
    inspectorViewport.setScrollBarThickness(10);
    inspectorViewport.setVisible(inspectorVisible);
    inspectorButton.setClickingTogglesState(true);
    inspectorButton.setTooltip(juce::String::fromUTF8("显示 / 隐藏音符属性与声库设置"));
    inspectorButton.onClick = [this]
    {
        setInspectorVisible(inspectorButton.getToggleState());
    };
    deviceButton.setButtonText(juce::String::fromUTF8("设备"));
    deviceButton.setTooltip(juce::String::fromUTF8("音频设备设置"));
    workspace.onToolbarResized = [this]
    {
        layoutToolbars();
    };
    workspace.onLayoutChanged = [this]
    {
        menuItemsChanged();
    };

    titleLabel.setColour(juce::Label::textColourId, colours::accent);
    positionLabel.setJustificationType(juce::Justification::centredRight);
    selectionLabel.setColour(juce::Label::textColourId, colours::subdued);
    engineLabel.setColour(juce::Label::textColourId, colours::subdued);
    engineLabel.setJustificationType(juce::Justification::topLeft);
    helpLabel.setText(juce::String::fromUTF8("按住左侧琴键试听音高\n双击空白添加音符\n拖动音符右边缘调整时值\n双击音符编辑歌词\n中键拖动平移视野\nCtrl + 滚轮缩放\nShift + 滚轮横向移动\n空格播放 / 暂停"), juce::dontSendNotification);
    helpLabel.setColour(juce::Label::textColourId, colours::subdued);
    helpLabel.setJustificationType(juce::Justification::topLeft);
    statusLabel.setText(juce::String::fromUTF8("就绪 · 双击钢琴卷帘开始编辑"), juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, colours::subdued);

    openButton.onClick = [this]
    {
        chooseOpen(false);
    };
    saveButton.onClick = [this]
    {
        saveProject(false);
    };
    undoButton.onClick = [this]
    {
        document.undo();
    };
    redoButton.onClick = [this]
    {
        document.redo();
    };
    playButton.onClick = [this]
    {
        togglePlayback();
    };
    stopButton.onClick = [this]
    {
        explicitPlaybackPending = false;
        preview.stop();
    };
    addTrackButton.onClick = [this]
    {
        document.addTrack();
    };
    fitButton.onClick = [this]
    {
        fitPianoRoll();
    };
    deviceButton.onClick = [this]
    {
        showAudioSettings();
    };
    exportButton.onClick = [this]
    {
        chooseExport(true);
    };
    voiceButton.onClick = [this]
    {
        chooseVoiceDatabase();
    };
    dictionaryButton.onClick = [this]
    {
        chooseDictionaryDirectory();
    };
    voiceLanguageBox.addItemList({juce::String::fromUTF8("日语"), juce::String::fromUTF8("普通话"), juce::String::fromUTF8("英语"), juce::String::fromUTF8("粤语"), juce::String::fromUTF8("西班牙语")}, 1);
    voiceLanguageBox.onChange = [this]
    {
        applyVoiceLanguage();
    };
    selectButton.setRadioGroupId(1);
    drawButton.setRadioGroupId(1);
    selectButton.setClickingTogglesState(true);
    drawButton.setClickingTogglesState(true);
    selectButton.setToggleState(true, juce::dontSendNotification);
    selectButton.onClick = [this]
    {
        pianoRoll.setDrawMode(false);
    };
    drawButton.onClick = [this]
    {
        pianoRoll.setDrawMode(true);
    };

    snapBox.addItem("1/4", 1);
    snapBox.addItem("1/8", 2);
    snapBox.addItem("1/16", 3);
    snapBox.addItem("1/32", 4);
    snapBox.addItem(juce::String::fromUTF8("关闭"), 5);
    snapBox.setSelectedId(3, juce::dontSendNotification);
    snapBox.onChange = [this]
    {
        const auto id = snapBox.getSelectedId();
        pianoRoll.setSnap(id == 5 ? 1 : blicksPerQuarter / (1LL << (id - 1)));
    };

    for (auto* editor : {&tempoEditor, &pitchEditor, &durationEditor})
    {
        editor->setInputRestrictions(12, "0123456789.");
    }
    tempoEditor.onEditFinished = [this](bool commit)
    {
        if (commit)
        {
            applyTempo();
        }
        tempoEditor.setText(juce::String(document.getProject().tempoMap.getTempoAt(0), 1), false);
    };
    tempoEditor.onDismiss = [this]
    {
        focusEditor();
    };
    for (auto [editor, field] : {std::pair{&lyricsEditor, 0}, {&phonemesEditor, 1}, {&pitchEditor, 2}, {&durationEditor, 3}})
    {
        editor->onEditStarted = [this, editor]
        {
            noteEditTarget = NoteEditTarget{document.getActiveGroup().id, document.getSelectedNoteIds(), editor->getText(), document.getRevision()};
        };
        editor->onEditFinished = [this, field](bool commit)
        {
            if (commit)
            {
                applyNoteField(field);
            }
            noteEditTarget.reset();
            refreshInspector();
        };
        editor->onDismiss = [this]
        {
            focusEditor();
            refreshInspector();
        };
    }

    gainSlider.setRange(-60.0, 12.0, 0.1);
    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 75, 20);
    gainSlider.setTextValueSuffix(" dB");
    gainSlider.onDragEnd = [this]
    {
        applyGain();
    };
    gainSlider.onValueChange = [this]
    {
        if (!gainSlider.isMouseButtonDown(true))
        {
            applyGain();
        }
    };
    arrangement.onSeek = [this](Blick position)
    {
        preview.seek(position);
    };
    arrangement.onChooseVoice = [this](int trackIndex)
    {
        commitPendingEdits();
        document.setActiveTrackIndex(trackIndex);
        chooseVoiceDatabase();
    };
    arrangement.onShowTrackSettings = [this](int trackIndex)
    {
        commitPendingEdits();
        document.setActiveTrackIndex(trackIndex);
        setInspectorVisible(true);
        refreshInspector();
    };
    pianoRoll.onSeek = [this](Blick position)
    {
        preview.seek(position);
    };
    // PreviewEngine outlives PianoRoll and remains valid during its final note-off.
    pianoRoll.onAuditionPitch = [engine = &preview](int pitch)
    {
        engine->setAuditionPitch(pitch);
    };
    pianoRoll.onStatus = [this](const juce::String& message)
    {
        statusLabel.setText(message, juce::dontSendNotification);
    };
    pianoRoll.onViewChanged = [this](double pixels, Blick left)
    {
        parameters.setView(pixels, left);
    };

    document.addChangeListener(this);
    const auto result = preview.initialise();
    if (result.failed())
    {
        statusLabel.setText(juce::String::fromUTF8("音频设备未就绪：") + result.getErrorMessage(), juce::dontSendNotification);
    }
    refreshDocument();
    setSize(1400, 900);
    fitPianoRoll();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    workspace.onToolbarResized = nullptr;
    workspace.onLayoutChanged = nullptr;
    arrangement.onChooseVoice = nullptr;
    arrangement.onShowTrackSettings = nullptr;
    for (auto* editor : {&tempoEditor, &lyricsEditor, &phonemesEditor, &pitchEditor, &durationEditor})
    {
        editor->onEditStarted = nullptr;
        editor->onEditFinished = nullptr;
        editor->onDismiss = nullptr;
    }
    if (audioSettingsWindow != nullptr)
    {
        delete audioSettingsWindow.getComponent();
    }
    document.removeChangeListener(this);
    preview.stop();
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(colours::background);
    const int railX = getWidth() - sidebarWidth;
    graphics.setColour(colours::panel);
    graphics.fillRect(railX, menuHeight, sidebarWidth, std::max(0, getHeight() - menuHeight - statusHeight));
    if (inspectorVisible)
    {
        graphics.fillRect(inspectorViewport.getBounds());
    }
    graphics.fillRect(0, getHeight() - statusHeight, getWidth(), statusHeight);
    graphics.setColour(colours::border);
    graphics.drawVerticalLine(railX, static_cast<float>(menuHeight), static_cast<float>(getHeight() - statusHeight));
    if (inspectorVisible)
    {
        graphics.drawVerticalLine(inspectorViewport.getX(), static_cast<float>(menuHeight), static_cast<float>(getHeight() - statusHeight));
    }
    graphics.drawHorizontalLine(getHeight() - statusHeight, 0.0f, static_cast<float>(getWidth()));
}

void MainComponent::resized()
{
    auto body = getLocalBounds();
    menu.setBounds(body.removeFromTop(menuHeight));
    statusLabel.setBounds(body.removeFromBottom(statusHeight).reduced(8, 0));
    auto rail = body.removeFromRight(std::min(sidebarWidth, body.getWidth()));
    inspectorButton.setBounds(rail.removeFromTop(48).reduced(3, 5));
    deviceButton.setBounds(rail.removeFromBottom(48).reduced(3, 5));
    if (inspectorVisible)
    {
        inspectorViewport.setBounds(body.removeFromRight(std::min(inspectorWidth, body.getWidth())));
    }
    workspace.setBounds(body);
    layoutToolbars();

    const int sideX = 14;
    const int sideWidth = inspectorWidth - 2 * sideX - inspectorViewport.getScrollBarThickness();
    inspectorContents.setSize(inspectorWidth - inspectorViewport.getScrollBarThickness(), 840);
    inspectorTitle.setBounds(sideX, 10, sideWidth, 24);
    selectionLabel.setBounds(sideX, 38, sideWidth, 22);
    int y = 66;
    for (auto [label, editor] : {std::pair{&lyricsLabel, &lyricsEditor}, {&phonemesLabel, &phonemesEditor}, {&pitchLabel, &pitchEditor}, {&durationLabel, &durationEditor}})
    {
        label->setBounds(sideX, y, sideWidth, 20);
        editor->setBounds(sideX, y + 22, sideWidth, 28);
        y += 58;
    }
    trackLabel.setBounds(sideX, y + 2, sideWidth, 24);
    gainLabel.setBounds(sideX, y + 30, sideWidth, 20);
    gainSlider.setBounds(sideX, y + 54, sideWidth, 40);
    engineTitle.setBounds(sideX, y + 108, sideWidth, 24);
    voiceButton.setBounds(sideX, y + 136, sideWidth, 28);
    voiceLanguageBox.setBounds(sideX, y + 170, sideWidth, 28);
    dictionaryButton.setBounds(sideX, y + 204, sideWidth, 28);
    engineLabel.setBounds(sideX, y + 240, sideWidth, 60);
    exportButton.setBounds(sideX, y + 312, sideWidth, 28);
    helpLabel.setBounds(sideX, y + 356, sideWidth, 172);
}

void MainComponent::layoutToolbars()
{
    auto arrangementBar = workspace.getArrangementToolbar().getLocalBounds().reduced(4, 3);
    for (auto* button : {&openButton, &saveButton, &undoButton, &redoButton})
    {
        button->setBounds(arrangementBar.removeFromLeft(42));
        arrangementBar.removeFromLeft(4);
    }
    arrangementBar.removeFromLeft(8);
    addTrackButton.setBounds(arrangementBar.removeFromLeft(64));
    arrangementBar.removeFromLeft(12);
    titleLabel.setBounds(arrangementBar);
    titleLabel.setJustificationType(juce::Justification::centredRight);

    auto pianoBar = workspace.getPianoToolbar().getLocalBounds().reduced(4, 3);
    selectButton.setBounds(pianoBar.removeFromLeft(38));
    pianoBar.removeFromLeft(4);
    drawButton.setBounds(pianoBar.removeFromLeft(38));
    pianoBar.removeFromLeft(10);
    snapLabel.setBounds(pianoBar.removeFromLeft(30));
    pianoBar.removeFromLeft(4);
    snapBox.setBounds(pianoBar.removeFromLeft(70));
    pianoBar.removeFromLeft(10);
    fitButton.setBounds(pianoBar.removeFromLeft(64));
    pianoBar.removeFromLeft(10);
    playButton.setBounds(pianoBar.removeFromLeft(38));
    pianoBar.removeFromLeft(4);
    stopButton.setBounds(pianoBar.removeFromLeft(38));
    pianoBar.removeFromLeft(10);
    tempoLabel.setBounds(pianoBar.removeFromLeft(30));
    pianoBar.removeFromLeft(2);
    tempoEditor.setBounds(pianoBar.removeFromLeft(56));
    pianoBar.removeFromLeft(6);
    positionLabel.setBounds(pianoBar.removeFromLeft(78));

    auto parameterBar = workspace.getParameterToolbar().getLocalBounds().reduced(4, 3);
    parameterLabel.setBounds(parameterBar.removeFromLeft(156));
    parameterHintLabel.setBounds(parameterBar);
}

void MainComponent::fitPianoRoll()
{
    workspace.setSectionExpanded(1, true);
    pianoRoll.zoomToFit();
}

void MainComponent::focusEditor()
{
    if (pianoRoll.isShowing())
    {
        pianoRoll.grabKeyboardFocus();
    }
    else
    {
        grabKeyboardFocus();
    }
}

void MainComponent::setInspectorVisible(bool visible)
{
    if (inspectorVisible && !visible)
    {
        commitPendingEdits();
    }
    inspectorVisible = visible;
    inspectorViewport.setVisible(visible);
    inspectorButton.setToggleState(visible, juce::dontSendNotification);
    resized();
    repaint();
    menuItemsChanged();
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return {juce::String::fromUTF8("文件"), juce::String::fromUTF8("编辑"), juce::String::fromUTF8("视图"), juce::String::fromUTF8("音轨"), juce::String::fromUTF8("走带控制")};
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu popup;
    if (index == 0)
    {
        popup.addItem(MenuItem::newProject, juce::String::fromUTF8("新建工程          Ctrl+N"));
        popup.addItem(openFile, juce::String::fromUTF8("打开 SVP…         Ctrl+O"));
        popup.addItem(saveFile, juce::String::fromUTF8("保存                 Ctrl+S"));
        popup.addItem(saveAs, juce::String::fromUTF8("另存为…             Ctrl+Shift+S"));
        popup.addSeparator();
        popup.addItem(importMidi, juce::String::fromUTF8("导入 MIDI…"));
        popup.addItem(exportMidi, juce::String::fromUTF8("导出 MIDI…"));
        popup.addItem(exportAudio, juce::String::fromUTF8("导出歌声 WAV…"));
        popup.addSeparator();
        popup.addItem(quit, juce::String::fromUTF8("退出"));
    }
    else if (index == 1)
    {
        popup.addItem(MenuItem::undo, juce::String::fromUTF8("撤销           Ctrl+Z"), document.canUndo());
        popup.addItem(MenuItem::redo, juce::String::fromUTF8("重做           Ctrl+Y"), document.canRedo());
        popup.addSeparator();
        popup.addItem(selectAll, juce::String::fromUTF8("选择全部音符    Ctrl+A"));
        popup.addItem(fillLyrics, juce::String::fromUTF8("填入歌词…       Ctrl+L"), !document.getSelectedNoteIds().empty());
        popup.addItem(deleteNotes, juce::String::fromUTF8("删除音符        Delete"), !document.getSelectedNoteIds().empty());
    }
    else if (index == 2)
    {
        popup.addItem(fitNotes, juce::String::fromUTF8("适配全部音符"));
        popup.addSeparator();
        popup.addItem(showPitch, juce::String::fromUTF8("显示音高线"), true, pianoRoll.isPitchVisible());
        popup.addItem(showWaveform, juce::String::fromUTF8("显示歌声波形"), true, pianoRoll.isWaveformVisible());
        popup.addSeparator();
        popup.addItem(showArrangement, juce::String::fromUTF8("展开编曲区域"), true, workspace.isSectionExpanded(0));
        popup.addItem(showPianoRoll, juce::String::fromUTF8("展开钢琴卷帘"), true, workspace.isSectionExpanded(1));
        popup.addItem(showParameters, juce::String::fromUTF8("展开参数区域"), true, workspace.isSectionExpanded(2));
        popup.addItem(showInspector, juce::String::fromUTF8("显示属性面板"), true, inspectorVisible);
        popup.addItem(resetWorkspace, juce::String::fromUTF8("恢复默认布局"));
        popup.addSeparator();
        popup.addItem(audioSettings, juce::String::fromUTF8("音频设备设置…"));
    }
    else if (index == 3)
    {
        popup.addItem(addTrack, juce::String::fromUTF8("添加人声音轨"));
        popup.addItem(removeTrack, juce::String::fromUTF8("删除当前音轨"), document.getProject().tracks.size() > 1);
    }
    else if (index == 4)
    {
        popup.addItem(play, juce::String::fromUTF8("播放 / 暂停     Space"));
        popup.addItem(stop, juce::String::fromUTF8("停止并回到开头"));
    }
    return popup;
}

void MainComponent::menuItemSelected(int item, int)
{
    switch (item)
    {
        case MenuItem::newProject:
            checkUnsaved([this]
                         { explicitPlaybackPending = false; preview.stop(); document.newProject(); });
            break;
        case openFile:
            chooseOpen(false);
            break;
        case saveFile:
            saveProject(false);
            break;
        case saveAs:
            saveProject(true);
            break;
        case importMidi:
            chooseOpen(true);
            break;
        case exportMidi:
            chooseExport(false);
            break;
        case exportAudio:
            chooseExport(true);
            break;
        case quit:
            requestClose();
            break;
        case MenuItem::undo:
            document.undo();
            break;
        case MenuItem::redo:
            document.redo();
            break;
        case deleteNotes:
            pianoRoll.deleteSelectedNotes();
            break;
        case selectAll:
            pianoRoll.selectAllNotes();
            break;
        case fillLyrics:
            commitPendingEdits();
            pianoRoll.fillSelectedLyrics();
            break;
        case fitNotes:
            fitPianoRoll();
            break;
        case showPitch:
            pianoRoll.setPitchVisible(!pianoRoll.isPitchVisible());
            menuItemsChanged();
            break;
        case showWaveform:
            pianoRoll.setWaveformVisible(!pianoRoll.isWaveformVisible());
            menuItemsChanged();
            break;
        case showArrangement:
            workspace.setSectionExpanded(0, !workspace.isSectionExpanded(0));
            break;
        case showPianoRoll:
            workspace.setSectionExpanded(1, !workspace.isSectionExpanded(1));
            break;
        case showParameters:
            workspace.setSectionExpanded(2, !workspace.isSectionExpanded(2));
            break;
        case showInspector:
            setInspectorVisible(!inspectorVisible);
            break;
        case resetWorkspace:
            workspace.resetLayout();
            setInspectorVisible(false);
            break;
        case audioSettings:
            showAudioSettings();
            break;
        case addTrack:
            document.addTrack();
            break;
        case removeTrack:
            document.removeTrack(document.getActiveTrackIndex());
            break;
        case play:
            togglePlayback();
            break;
        case stop:
            explicitPlaybackPending = false;
            preview.stop();
            break;
        default:
            break;
    }
}

bool MainComponent::isTextInputActive() const
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return dynamic_cast<juce::TextEditor*>(focused) != nullptr || (focused != nullptr && focused->findParentComponentOfClass<juce::TextEditor>() != nullptr);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        switch (key.getKeyCode())
        {
            case 'N':
                commitPendingEdits();
                menuItemSelected(MenuItem::newProject, 0);
                return true;
            case 'O':
                commitPendingEdits();
                chooseOpen(false);
                return true;
            case 'S':
                commitPendingEdits();
                saveProject(key.getModifiers().isShiftDown());
                return true;
            default:
                break;
        }
    }
    if (isTextInputActive())
    {
        return false;
    }
    if (key.getModifiers().isCommandDown())
    {
        switch (key.getKeyCode())
        {
            case 'Z':
                key.getModifiers().isShiftDown() ? document.redo() : document.undo();
                return true;
            case 'Y':
                document.redo();
                return true;
            case 'L':
                if (!key.getModifiers().isShiftDown() && !key.getModifiers().isAltDown())
                {
                    menuItemSelected(fillLyrics, 1);
                    return true;
                }
                break;
            default:
                break;
        }
    }
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }
    return false;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (noteEditTarget.has_value() && (noteEditTarget->groupId != document.getActiveGroup().id || noteEditTarget->noteIds != document.getSelectedNoteIds()))
    {
        commitPendingEdits();
    }
    refreshDocument();
}

void MainComponent::commitPendingEdits()
{
    for (auto* editor : {&tempoEditor, &lyricsEditor, &phonemesEditor, &pitchEditor, &durationEditor})
    {
        editor->finishEdit(true);
    }
    arrangement.commitPendingEdits();
    pianoRoll.commitPendingEdits();
    if (isTextInputActive())
    {
        focusEditor();
    }
    refreshInspector();
}

void MainComponent::refreshDocument()
{
    refreshing = true;
    if (audioRevision != document.getRevision())
    {
        audioRevision = document.getRevision();
        preview.setProject(document.getProject(), document.getGeneration());
        pianoRoll.setVisualization(preview.getVisualization());
        renderResultPending = true;
    }
    undoButton.setEnabled(document.canUndo());
    redoButton.setEnabled(document.canRedo());
    const auto name = document.getFile().existsAsFile() ? document.getFile().getFileNameWithoutExtension() : juce::String::fromUTF8("未命名");
    titleLabel.setText(name + (document.isModified() ? " *" : ""), juce::dontSendNotification);
    if (onTitleChanged)
    {
        onTitleChanged(juce::String::fromUTF8("OpenSV — ") + name + (document.isModified() ? " *" : ""));
    }
    setEditorText(tempoEditor, juce::String(document.getProject().tempoMap.getTempoAt(0), 1));
    refreshInspector();
    refreshing = false;
    menuItemsChanged();
}

const Note* MainComponent::getFirstSelectedNote() const
{
    const auto& ids = document.getSelectedNoteIds();
    for (const auto& note : document.getActiveGroup().notes)
    {
        if (std::find(ids.begin(), ids.end(), note.id) != ids.end())
        {
            return &note;
        }
    }
    return nullptr;
}

void MainComponent::refreshInspector()
{
    const auto* note = getFirstSelectedNote();
    selectionLabel.setText(note != nullptr ? juce::String::fromUTF8("已选择 ") + juce::String(static_cast<int>(document.getSelectedNoteIds().size())) + juce::String::fromUTF8(" 个音符") : juce::String::fromUTF8("选择音符以编辑属性"), juce::dontSendNotification);
    for (auto* editor : {&lyricsEditor, &phonemesEditor, &pitchEditor, &durationEditor})
    {
        editor->setEnabled(note != nullptr);
    }
    setEditorText(lyricsEditor, note != nullptr ? text(note->lyrics) : juce::String());
    setEditorText(phonemesEditor, note != nullptr ? text(note->phonemes) : juce::String());
    setEditorText(pitchEditor, note != nullptr ? juce::String(note->pitch) : juce::String());
    setEditorText(durationEditor, note != nullptr ? juce::String(static_cast<double>(note->duration) / blicksPerQuarter, 3) : juce::String());
    const auto& tracks = document.getProject().tracks;
    const auto index = document.getActiveTrackIndex();
    if (juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())))
    {
        const auto& track = tracks[static_cast<std::size_t>(index)];
        trackLabel.setText(juce::String::fromUTF8("音轨 · ") + text(track.name), juce::dontSendNotification);
        const auto& voice = track.voice;
        voiceButton.setButtonText(voice.databasePath.empty() ? juce::String::fromUTF8("选择声库…") : juce::File(text(voice.databasePath)).getParentDirectory().getFileName());
        voiceButton.setTooltip(voice.databasePath.empty() ? juce::String::fromUTF8("选择当前音轨使用的 voice.nofs 声库文件") : text(voice.databasePath));
        const auto language = std::find(voiceLanguages.begin(), voiceLanguages.end(), voice.language);
        voiceLanguageBox.setSelectedId(language == voiceLanguages.end() ? 0 : static_cast<int>(std::distance(voiceLanguages.begin(), language)) + 1, juce::dontSendNotification);
        dictionaryButton.setButtonText(voice.dictionaryDirectory.empty() ? juce::String::fromUTF8("选择歌词词典…") : juce::String::fromUTF8("词典：") + juce::File(text(voice.dictionaryDirectory)).getFileName());
        dictionaryButton.setTooltip(voice.dictionaryDirectory.empty() ? juce::String::fromUTF8("选择 clf-data 目录；填写音素的音符无需词典") : text(voice.dictionaryDirectory));
        if (!gainSlider.isMouseButtonDown(true))
        {
            gainSlider.setValue(juce::Decibels::gainToDecibels(track.gain, -60.0), juce::dontSendNotification);
        }
    }
    refreshEngineStatus();
}

void MainComponent::chooseVoiceDatabase()
{
    commitPendingEdits();
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(document.getActiveTrackIndex())];
    const auto groupId = track.mainGroup.id;
    const auto generation = document.getGeneration();
    const auto initial = track.voice.databasePath.empty() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Dreamtonics/Synthesizer V Studio/databases") : juce::File(text(track.voice.databasePath));
    fileChooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("选择歌声声库 voice.nofs"), initial, "*.nofs");
    const juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, groupId, generation](const juce::FileChooser& chooser)
                             {
        const auto selected = chooser.getResult();
        if (safe == nullptr || safe->document.getGeneration() != generation || !selected.existsAsFile())
        {
            return;
        }
        const auto& tracks = safe->document.getProject().tracks;
        const auto track = std::find_if(tracks.begin(), tracks.end(), [&groupId](const Track& candidate) { return candidate.mainGroup.id == groupId; });
        if (track == tracks.end() || track->voice.databasePath == selected.getFullPathName().toStdString())
        {
            return;
        }
        synthesis::VoiceDatabase database;
        const auto result = database.open(selected);
        if (result.failed())
        {
            safe->showError(juce::String::fromUTF8("读取声库"), result);
            return;
        }
        const auto dictionary = track->voice.dictionaryDirectory.empty() ? findDictionaryDirectory().getFullPathName().toStdString() : track->voice.dictionaryDirectory;
        safe->document.performEdit(juce::String::fromUTF8("选择音轨声库"), [groupId, selected, dictionary](Project& project)
        {
            const auto target = std::find_if(project.tracks.begin(), project.tracks.end(), [&groupId](const Track& candidate) { return candidate.mainGroup.id == groupId; });
            if (target != project.tracks.end())
            {
                target->voice.databasePath = selected.getFullPathName().toStdString();
                target->voice.dictionaryDirectory = dictionary;
            }
        });
        safe->refreshDocument(); });
}

void MainComponent::chooseDictionaryDirectory()
{
    commitPendingEdits();
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(document.getActiveTrackIndex())];
    const auto groupId = track.mainGroup.id;
    const auto initial = track.voice.dictionaryDirectory.empty() ? findDictionaryDirectory() : juce::File(text(track.voice.dictionaryDirectory));
    fileChooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("选择歌词词典 clf-data 目录"), initial);
    const juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe, groupId](const juce::FileChooser& chooser)
                             {
        const auto selected = chooser.getResult();
        if (safe == nullptr || !selected.isDirectory())
        {
            return;
        }
        const auto& tracks = safe->document.getProject().tracks;
        const auto track = std::find_if(tracks.begin(), tracks.end(), [&groupId](const Track& candidate) { return candidate.mainGroup.id == groupId; });
        if (track == tracks.end() || track->voice.dictionaryDirectory == selected.getFullPathName().toStdString())
        {
            return;
        }
        safe->document.performEdit(juce::String::fromUTF8("选择歌词词典"), [groupId, selected](Project& project)
        {
            const auto target = std::find_if(project.tracks.begin(), project.tracks.end(), [&groupId](const Track& candidate) { return candidate.mainGroup.id == groupId; });
            if (target != project.tracks.end())
            {
                target->voice.dictionaryDirectory = selected.getFullPathName().toStdString();
            }
        });
        safe->refreshDocument(); });
}

void MainComponent::applyVoiceLanguage()
{
    if (refreshing || !juce::isPositiveAndBelow(voiceLanguageBox.getSelectedId() - 1, static_cast<int>(voiceLanguages.size())))
    {
        return;
    }
    const std::string language = voiceLanguages[static_cast<std::size_t>(voiceLanguageBox.getSelectedId() - 1)];
    commitPendingEdits();
    const auto index = document.getActiveTrackIndex();
    if (document.getProject().tracks[static_cast<std::size_t>(index)].voice.language == language)
    {
        return;
    }
    document.performEdit(juce::String::fromUTF8("修改歌声语言"), [index, language](Project& project)
                         { project.tracks[static_cast<std::size_t>(index)].voice.language = language; });
    refreshDocument();
}

void MainComponent::refreshEngineStatus()
{
    const auto& track = document.getProject().tracks[static_cast<std::size_t>(document.getActiveTrackIndex())];
    juce::String message;
    juce::String detail;
    if (track.voice.databasePath.empty())
    {
        message = juce::String::fromUTF8("请选择当前音轨声库。\n音符支持歌词或手动音素。");
        if (!preview.isRendering())
        {
            if (const auto result = preview.getRenderResult(); result.failed())
            {
                message = juce::String::fromUTF8("请选择当前音轨声库。\n工程合成失败，悬停查看原因。");
                detail = result.getErrorMessage();
            }
        }
    }
    else if (preview.isRendering())
    {
        message = preview.isPlaying() ? juce::String::fromUTF8("正在合成歌声…\n完成后自动播放，可暂停或停止。") : juce::String::fromUTF8("正在合成歌声…\n点击播放可等待合成完成。");
    }
    else if (const auto result = preview.getRenderResult(); result.failed())
    {
        message = juce::String::fromUTF8("歌声合成失败。\n请检查声库、语言、歌词或音素。");
        detail = result.getErrorMessage();
    }
    else
    {
        const auto statistics = preview.getRenderStatistics();
        message = juce::String::fromUTF8("歌声已就绪 · 独立合成\n本次合成 ") + juce::String(static_cast<juce::int64>(statistics.renderedPhrases)) + juce::String::fromUTF8(" 句 · 复用 ") + juce::String(static_cast<juce::int64>(statistics.reusedPhrases)) + juce::String::fromUTF8(" 句");
        detail = message + juce::String::fromUTF8("\n本次处理耗时 ") + juce::String(statistics.elapsedMilliseconds, 1) + juce::String::fromUTF8(" ms，可播放或导出 WAV。");
    }
    engineLabel.setText(message, juce::dontSendNotification);
    engineLabel.setTooltip(detail.isNotEmpty() ? detail : message);
    exportButton.setEnabled(!preview.isExporting());
}

void MainComponent::applyNoteField(int field)
{
    if (!noteEditTarget.has_value() || noteEditTarget->noteIds.empty())
    {
        return;
    }
    const auto target = *noteEditTarget;
    if (target.revision != document.getRevision())
    {
        statusLabel.setText(juce::String::fromUTF8("工程已变化，未应用过期的属性编辑。"), juce::dontSendNotification);
        return;
    }
    const auto* fieldEditor = field == 0 ? &lyricsEditor : field == 1 ? &phonemesEditor
                                                       : field == 2   ? &pitchEditor
                                                                      : &durationEditor;
    if (fieldEditor->getText() == target.originalText)
    {
        return;
    }
    const auto lyrics = lyricsEditor.getText().toStdString();
    const auto phonemes = phonemesEditor.getText().toStdString();
    const auto pitch = pitchEditor.getText().getIntValue();
    const auto length = durationEditor.getText().getDoubleValue();
    if ((field == 2 && (pitch < 0 || pitch > 127 || pitchEditor.getText().isEmpty())) || (field == 3 && (!std::isfinite(length) || length <= 0.0 || length > 4096.0)))
    {
        statusLabel.setText(juce::String::fromUTF8("音高范围为 0–127；时值必须为正数。"), juce::dontSendNotification);
        return;
    }
    document.performEdit(juce::String::fromUTF8("修改音符属性"), [target, field, lyrics, phonemes, pitch, length](Project& project)
                         {
        const auto track = std::find_if(project.tracks.begin(), project.tracks.end(), [&target](const Track& candidate) { return candidate.mainGroup.id == target.groupId; });
        if (track == project.tracks.end())
        {
            return;
        }
        for (auto& current : track->mainGroup.notes)
        {
            if (std::find(target.noteIds.begin(), target.noteIds.end(), current.id) == target.noteIds.end())
            {
                continue;
            }
            if (field == 0) { current.lyrics = lyrics; }
            if (field == 1) { current.phonemes = phonemes; }
            if (field == 2) { current.pitch = pitch; }
            if (field == 3) { current.duration = static_cast<Blick>(std::llround(length * blicksPerQuarter)); }
        } });
}

void MainComponent::applyTempo()
{
    if (refreshing)
    {
        return;
    }
    const auto bpm = tempoEditor.getText().getDoubleValue();
    if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0)
    {
        tempoEditor.setText(juce::String(document.getProject().tempoMap.getTempoAt(0), 1), false);
        statusLabel.setText(juce::String::fromUTF8("速度范围为 20–400 BPM。"), juce::dontSendNotification);
        return;
    }
    if (std::abs(bpm - document.getProject().tempoMap.getTempoAt(0)) > 0.0001)
    {
        document.performEdit(juce::String::fromUTF8("修改起始速度"), [bpm](Project& project)
                             { project.tempoMap.tempos.front().bpm = bpm; });
    }
}

void MainComponent::applyGain()
{
    if (refreshing)
    {
        return;
    }
    const auto index = document.getActiveTrackIndex();
    const auto gain = juce::Decibels::decibelsToGain(gainSlider.getValue(), -60.0);
    if (std::abs(document.getProject().tracks[static_cast<std::size_t>(index)].gain - gain) > 0.000001)
    {
        document.performEdit(juce::String::fromUTF8("修改轨道音量"), [index, gain](Project& project)
                             { project.tracks[static_cast<std::size_t>(index)].gain = gain; });
    }
}

void MainComponent::timerCallback()
{
    if (renderResultPending && !preview.isRendering())
    {
        renderResultPending = false;
        const auto result = preview.getRenderResult();
        if (explicitPlaybackPending)
        {
            showError(juce::String::fromUTF8("歌声合成失败"), result);
        }
        else if (result.failed())
        {
            statusLabel.setText(juce::String::fromUTF8("歌声合成：") + result.getErrorMessage(), juce::dontSendNotification);
        }
        explicitPlaybackPending = false;
    }
    refreshEngineStatus();
    pianoRoll.setVisualization(preview.getVisualization());
    const auto position = preview.getPositionBlick();
    const bool playing = preview.isPlaying();
    const bool followPlayback = playing && !preview.isRendering() && !pianoRoll.isMouseButtonDown(true) && !parameters.isMouseButtonDown(true) && !arrangement.isMouseButtonDown(true);
    pianoRoll.setPlayhead(position, followPlayback);
    arrangement.setPlayhead(position, followPlayback);
    parameters.setPlayhead(position);
    playButton.setButtonText(playing ? juce::String::fromUTF8("暂停") : juce::String::fromUTF8("播放"));
    playButton.setToggleState(playing, juce::dontSendNotification);
    const auto seconds = preview.getPositionSeconds();
    positionLabel.setText(juce::String(static_cast<int>(seconds) / 60).paddedLeft('0', 2) + ":" + juce::String(std::fmod(seconds, 60.0), 2).paddedLeft('0', 5), juce::dontSendNotification);
}

void MainComponent::togglePlayback()
{
    commitPendingEdits();
    refreshDocument();
    if (preview.isPlaying())
    {
        explicitPlaybackPending = false;
        preview.pause();
    }
    else
    {
        auto* device = preview.getDeviceManager().getCurrentAudioDevice();
        if (device == nullptr || !device->isOpen())
        {
            showError(juce::String::fromUTF8("无法播放"), juce::Result::fail(juce::String::fromUTF8("请在音频设备中选择可用的输出设备。")));
            return;
        }
        const auto result = preview.getRenderResult();
        if (!preview.isRendering() && result.failed())
        {
            showError(juce::String::fromUTF8("歌声合成失败"), result);
            return;
        }
        explicitPlaybackPending = preview.isRendering();
        preview.play();
        if (!preview.isPlaying())
        {
            explicitPlaybackPending = false;
            showError(juce::String::fromUTF8("歌声合成失败"), preview.getRenderResult());
        }
    }
}

void MainComponent::showError(const juce::String& operation, const juce::Result& result)
{
    if (result.failed())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, operation, result.getErrorMessage());
        statusLabel.setText(operation + juce::String::fromUTF8("：") + result.getErrorMessage(), juce::dontSendNotification);
    }
}

void MainComponent::checkUnsaved(std::function<void()> continuation)
{
    commitPendingEdits();
    if (!document.isModified())
    {
        continuation();
        return;
    }
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8("保存工程"), juce::String::fromUTF8("当前工程有未保存的更改。"), juce::String::fromUTF8("保存"), juce::String::fromUTF8("放弃更改"), juce::String::fromUTF8("取消"), this, juce::ModalCallbackFunction::create([safe, continuation = std::move(continuation)](int result) mutable
                                                                                                                                                                                                                                                                                                                        {
        if (safe == nullptr) { return; }
        if (result == 1) { safe->saveProject(false, std::move(continuation)); }
        if (result == 2) { continuation(); } }));
}

void MainComponent::requestClose()
{
    checkUnsaved([this]
                 { if (onClose) { onClose(); } });
}

void MainComponent::openProject(const juce::File& file)
{
    checkUnsaved([this, file]
                 {
        explicitPlaybackPending = false;
        preview.stop();
        const auto result = document.load(file);
        showError(juce::String::fromUTF8("打开工程失败"), result);
        if (result.wasOk())
        {
            refreshDocument();
            fitPianoRoll();
            statusLabel.setText(juce::String::fromUTF8("已打开 ") + file.getFileName(), juce::dontSendNotification);
        } });
}

void MainComponent::chooseOpen(bool midi)
{
    commitPendingEdits();
    fileChooser = std::make_unique<juce::FileChooser>(midi ? juce::String::fromUTF8("导入 MIDI") : juce::String::fromUTF8("打开 Synthesizer V 工程"), document.getFile().getParentDirectory(), midi ? "*.mid;*.midi" : "*.svp");
    const juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, midi](const juce::FileChooser& chooser)
                             {
        const auto file = chooser.getResult();
        if (safe == nullptr || !file.existsAsFile()) { return; }
        if (!midi) { safe->openProject(file); return; }
        safe->checkUnsaved([safe, file]
        {
            if (safe == nullptr) { return; }
            safe->explicitPlaybackPending = false;
            safe->preview.stop();
            const auto result = safe->document.importMidi(file);
            safe->showError(juce::String::fromUTF8("MIDI 导入失败"), result);
            if (result.wasOk()) { safe->refreshDocument(); safe->fitPianoRoll(); }
        }); });
}

void MainComponent::saveProject(bool choosePath, std::function<void()> continuation)
{
    commitPendingEdits();
    if (!choosePath && document.getFile() != juce::File())
    {
        const auto result = document.save(document.getFile());
        showError(juce::String::fromUTF8("保存工程失败"), result);
        if (result.wasOk())
        {
            statusLabel.setText(juce::String::fromUTF8("已保存 ") + document.getFile().getFileName(), juce::dontSendNotification);
            if (continuation)
            {
                continuation();
            }
        }
        return;
    }
    const auto initialFile = document.getFile() != juce::File() ? document.getFile() : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(juce::String::fromUTF8("未命名.svp"));
    fileChooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("保存工程"), initialFile, "*.svp");
    const juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe, continuation = std::move(continuation)](const juce::FileChooser& chooser)
                             {
        const auto selected = chooser.getResult();
        if (safe == nullptr || selected == juce::File()) { return; }
        safe->confirmDestination(selected, "svp", [safe, continuation = std::move(continuation)](const juce::File& file)
        {
            if (safe == nullptr) { return; }
            const auto result = safe->document.save(file);
            safe->showError(juce::String::fromUTF8("保存工程失败"), result);
            if (result.wasOk())
            {
                safe->statusLabel.setText(juce::String::fromUTF8("已保存 ") + safe->document.getFile().getFileName(), juce::dontSendNotification);
                if (continuation) { continuation(); }
            }
        }); });
}

void MainComponent::chooseExport(bool audioFile)
{
    commitPendingEdits();
    refreshDocument();
    if (audioFile && !preview.isRendering() && preview.getRenderResult().failed())
    {
        showError(juce::String::fromUTF8("无法导出歌声"), preview.getRenderResult());
        return;
    }
    const auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(audioFile ? juce::String::fromUTF8("歌声.wav") : juce::String::fromUTF8("旋律.mid"));
    fileChooser = std::make_unique<juce::FileChooser>(audioFile ? juce::String::fromUTF8("导出歌声 WAV") : juce::String::fromUTF8("导出 MIDI"), initial, audioFile ? "*.wav" : "*.mid");
    const juce::Component::SafePointer<MainComponent> safe(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe, audioFile](const juce::FileChooser& chooser)
                             {
        const auto selected = chooser.getResult();
        if (safe == nullptr || selected == juce::File()) { return; }
        safe->confirmDestination(selected, audioFile ? "wav" : "mid", [safe, audioFile](const juce::File& file)
        {
            if (safe == nullptr) { return; }
            if (audioFile)
            {
                safe->statusLabel.setText(juce::String::fromUTF8("正在导出歌声…"), juce::dontSendNotification);
                safe->preview.exportWavAsync(file, [safe, file](juce::Result result)
                {
                    if (safe == nullptr) { return; }
                    safe->showError(juce::String::fromUTF8("导出失败"), result);
                    if (result.wasOk()) { safe->statusLabel.setText(juce::String::fromUTF8("已导出 ") + file.getFullPathName(), juce::dontSendNotification); }
                });
            }
            else
            {
                const auto result = safe->document.exportMidi(file);
                safe->showError(juce::String::fromUTF8("导出失败"), result);
                if (result.wasOk()) { safe->statusLabel.setText(juce::String::fromUTF8("已导出 ") + file.getFullPathName(), juce::dontSendNotification); }
            }
        }); });
}

void MainComponent::confirmDestination(const juce::File& selected, const juce::String& extension, std::function<void(const juce::File&)> continuation)
{
    const auto destination = selected.withFileExtension(extension);
    if (destination == selected || !destination.exists())
    {
        continuation(destination);
        return;
    }
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8("替换已有文件"), juce::String::fromUTF8("实际保存路径已存在：\n") + destination.getFullPathName(), juce::String::fromUTF8("替换"), juce::String::fromUTF8("取消"), this, juce::ModalCallbackFunction::create([safe, destination, continuation = std::move(continuation)](int result)
                                                                                                                                                                                                                                                                                                                   {
        if (safe != nullptr && result == 1)
        {
            continuation(destination);
        } }));
}

void MainComponent::showAudioSettings()
{
    if (audioSettingsWindow != nullptr)
    {
        audioSettingsWindow->toFront(true);
        return;
    }
    auto settings = std::make_unique<juce::AudioDeviceSelectorComponent>(preview.getDeviceManager(), 0, 0, 2, 2, false, false, true, false);
    settings->setSize(460, 360);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(settings.release());
    options.dialogTitle = juce::String::fromUTF8("音频设备");
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    audioSettingsWindow = options.launchAsync();
}
} // namespace sv
