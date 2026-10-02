#pragma once

#include "ArrangementView.h"
#include "EditorWorkspace.h"
#include "ParameterView.h"
#include "PianoRoll.h"
#include "StudioTheme.h"
#include "audio/PreviewEngine.h"
#include "core/ProjectDocument.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sv
{
class MainComponent final : public juce::Component,
                            private juce::MenuBarModel,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void openProject(const juce::File& file);
    void requestClose();

    std::function<void()> onClose;
    std::function<void(const juce::String&)> onTitleChanged;

private:
    class InspectorTextEditor final : public juce::TextEditor
    {
    public:
        void focusGained(FocusChangeType cause) override;
        void focusLost(FocusChangeType cause) override;
        void enablementChanged() override;
        bool keyPressed(const juce::KeyPress& key) override;
        void finishEdit(bool commit);

        std::function<void()> onEditStarted;
        std::function<void(bool)> onEditFinished;
        std::function<void()> onDismiss;

    private:
        bool editing = false;
    };

    struct NoteEditTarget
    {
        std::string groupId;
        std::vector<NoteId> noteIds;
        juce::String originalText;
        std::uint64_t revision = 0;
    };

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int item, int index) override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void refreshDocument();
    void refreshInspector();
    void commitPendingEdits();
    void checkUnsaved(std::function<void()> continuation);
    void chooseOpen(bool midi);
    void saveProject(bool choosePath, std::function<void()> continuation = {});
    void chooseExport(bool audioFile);
    void chooseVoiceDatabase();
    void chooseDictionaryDirectory();
    void applyVoiceLanguage();
    void refreshEngineStatus();
    void confirmDestination(const juce::File& selected, const juce::String& extension, std::function<void(const juce::File&)> continuation);
    void showError(const juce::String& operation, const juce::Result& result);
    void showAudioSettings();
    void togglePlayback();
    void applyNoteField(int field);
    void applyTempo();
    void applyGain();
    void layoutToolbars();
    void setInspectorVisible(bool visible);
    void fitPianoRoll();
    void focusEditor();
    [[nodiscard]] const Note* getFirstSelectedNote() const;
    [[nodiscard]] bool isTextInputActive() const;

    StudioTheme theme;
    ProjectDocument document;
    audio::PreviewEngine preview;
    juce::MenuBarComponent menu{this};
    ArrangementView arrangement{document};
    PianoRoll pianoRoll{document};
    ParameterView parameters{document};
    EditorWorkspace workspace{arrangement, pianoRoll, parameters};
    juce::Component inspectorContents;
    juce::Viewport inspectorViewport;
    juce::TooltipWindow tooltips{this, 650};
    juce::TextButton inspectorButton{juce::String::fromUTF8("属性")};
    juce::TextButton openButton{juce::String::fromUTF8("打开")};
    juce::TextButton saveButton{juce::String::fromUTF8("保存")};
    juce::TextButton undoButton{juce::String::fromUTF8("撤销")};
    juce::TextButton redoButton{juce::String::fromUTF8("重做")};
    juce::TextButton playButton{juce::String::fromUTF8("播放")};
    juce::TextButton stopButton{juce::String::fromUTF8("停止")};
    juce::TextButton addTrackButton{juce::String::fromUTF8("+ 音轨")};
    juce::TextButton selectButton{juce::String::fromUTF8("选择")};
    juce::TextButton drawButton{juce::String::fromUTF8("绘制")};
    juce::TextButton fitButton{juce::String::fromUTF8("适配音符")};
    juce::TextButton deviceButton{juce::String::fromUTF8("音频设备")};
    juce::TextButton exportButton{juce::String::fromUTF8("导出歌声 WAV")};
    juce::Label titleLabel;
    juce::Label positionLabel;
    juce::Label tempoLabel{"", "BPM"};
    InspectorTextEditor tempoEditor;
    juce::Label snapLabel{"", juce::String::fromUTF8("吸附")};
    juce::ComboBox snapBox;
    juce::Label inspectorTitle{"", juce::String::fromUTF8("音符属性")};
    juce::Label selectionLabel;
    juce::Label lyricsLabel{"", juce::String::fromUTF8("歌词")};
    InspectorTextEditor lyricsEditor;
    juce::Label phonemesLabel{"", juce::String::fromUTF8("音素")};
    InspectorTextEditor phonemesEditor;
    juce::Label pitchLabel{"", juce::String::fromUTF8("音高 · MIDI")};
    InspectorTextEditor pitchEditor;
    juce::Label durationLabel{"", juce::String::fromUTF8("时值 · 四分音符")};
    InspectorTextEditor durationEditor;
    juce::Label trackLabel;
    juce::Label gainLabel{"", juce::String::fromUTF8("轨道音量")};
    juce::Slider gainSlider;
    juce::Label engineTitle{"", juce::String::fromUTF8("歌声合成")};
    juce::TextButton voiceButton{juce::String::fromUTF8("选择声库…")};
    juce::ComboBox voiceLanguageBox;
    juce::TextButton dictionaryButton{juce::String::fromUTF8("选择歌词词典…")};
    juce::Label engineLabel;
    juce::Label helpLabel;
    juce::Label parameterLabel{"", juce::String::fromUTF8("EDIT: 音高偏移")};
    juce::Label parameterHintLabel{"", juce::String::fromUTF8("点击添加 / 拖动调整 / 右键删除")};
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
    std::optional<NoteEditTarget> noteEditTarget;
    std::uint64_t audioRevision = std::numeric_limits<std::uint64_t>::max();
    bool refreshing = false;
    bool renderResultPending = false;
    bool explicitPlaybackPending = false;
    bool inspectorVisible = false;

    static constexpr int menuHeight = 28;
    static constexpr int statusHeight = 24;
    static constexpr int sidebarWidth = 42;
    static constexpr int inspectorWidth = 270;
};
} // namespace sv
