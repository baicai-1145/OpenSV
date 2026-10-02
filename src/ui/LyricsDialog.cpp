#include "LyricsDialog.h"

#include "StudioTheme.h"

#include <memory>
#include <utility>

namespace sv
{
namespace
{
bool isLyricsWhitespace(juce::juce_wchar character)
{
    // The Unicode White_Space property is independent of the process locale.
    return (character >= 0x09 && character <= 0x0d) || character == 0x20 || character == 0x85 || character == 0xa0 || character == 0x1680 || (character >= 0x2000 && character <= 0x200a) || character == 0x2028 || character == 0x2029 || character == 0x202f || character == 0x205f || character == 0x3000;
}

juce::StringArray splitLyrics(const juce::String& text, bool splitCharacters)
{
    juce::StringArray lyrics;
    juce::String word;
    for (const auto character : text)
    {
        if (isLyricsWhitespace(character))
        {
            if (word.isNotEmpty())
            {
                lyrics.add(word);
                word.clear();
            }
        }
        else if (splitCharacters)
        {
            lyrics.add(juce::String::charToString(character));
        }
        else
        {
            word += juce::String::charToString(character);
        }
    }
    if (word.isNotEmpty())
    {
        lyrics.add(word);
    }
    return lyrics;
}

class LyricsContent final : public juce::Component, private juce::KeyListener, private juce::ComponentListener
{
public:
    LyricsContent(juce::Component& owner, const juce::String& initialLyrics, int selectedNoteCount, std::function<void(const juce::StringArray&, bool)> callback)
        : parent(&owner), noteCount(selectedNoteCount), onApply(std::move(callback))
    {
        setLookAndFeel(&theme);
        setOpaque(true);
        owner.addComponentListener(this);

        editor.setMultiLine(true, true);
        editor.setReturnKeyStartsNewLine(true);
        editor.setScrollbarsShown(true);
        editor.setFont(juce::Font(juce::FontOptions(15.0f)));
        editor.setIndents(8, 8);
        editor.setText(initialLyrics, false);
        editor.setTextToShowWhenEmpty(juce::String::fromUTF8("输入歌词，以空格或换行分隔"), colours::subdued);
        editor.addKeyListener(this);
        editor.onTextChange = [this]
        {
            updateLyrics();
        };
        addAndMakeVisible(editor);

        loopButton.setButtonText(juce::String::fromUTF8("循环填充"));
        loopButton.onClick = [this]
        {
            updateLyrics();
        };
        addAndMakeVisible(loopButton);

        charactersButton.setButtonText(juce::String::fromUTF8("按字符隔开"));
        charactersButton.onClick = [this]
        {
            updateLyrics();
        };
        addAndMakeVisible(charactersButton);

        applyButton.setButtonText(juce::String::fromUTF8("确定"));
        applyButton.onClick = [this]
        {
            apply();
        };
        applyButton.setTooltip(juce::String::fromUTF8("Ctrl+Enter 确定"));
        addAndMakeVisible(applyButton);

        cancelButton.setButtonText(juce::String::fromUTF8("取消"));
        cancelButton.onClick = [this]
        {
            close(0);
        };
        addAndMakeVisible(cancelButton);

        setSize(580, 420);
        updateLyrics();
    }

    ~LyricsContent() override
    {
        if (parent != nullptr)
        {
            parent->removeComponentListener(this);
        }
        editor.removeKeyListener(this);
        setLookAndFeel(nullptr);
    }

    void focusLyrics()
    {
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(colours::panel);
        graphics.setColour(colours::accent.darker(0.5f));
        graphics.fillRect(0, 0, 52, getHeight());

        graphics.setColour(colours::accent.brighter(0.35f));
        juce::Path pencil;
        const float centre = static_cast<float>(getHeight()) * 0.5f;
        pencil.startNewSubPath(17.0f, centre + 10.0f);
        pencil.lineTo(20.0f, centre + 1.0f);
        pencil.lineTo(33.0f, centre - 12.0f);
        pencil.lineTo(39.0f, centre - 6.0f);
        pencil.lineTo(26.0f, centre + 7.0f);
        pencil.closeSubPath();
        graphics.strokePath(pencil, juce::PathStrokeType(1.5f));
        graphics.drawLine(20.0f, centre + 1.0f, 26.0f, centre + 7.0f, 1.5f);

        graphics.setColour(colours::text);
        graphics.setFont(juce::Font(juce::FontOptions(20.0f).withStyle("Bold")));
        graphics.drawText(juce::String::fromUTF8("填入歌词"), 82, 28, getWidth() - 112, 32, juce::Justification::centredLeft);

        graphics.setColour(colours::subdued);
        graphics.setFont(juce::Font(juce::FontOptions(14.0f)));
        const auto caption = charactersButton.getToggleState() ? juce::String::fromUTF8("歌词（按字符隔开）") : juce::String::fromUTF8("歌词（按空格隔开）");
        graphics.drawText(caption, 82, 82, getWidth() - 112, 24, juce::Justification::centredLeft);
        graphics.setFont(juce::Font(juce::FontOptions(12.0f)));
        graphics.drawFittedText(statusText, 82, 309, getWidth() - 112, 32, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        const auto contentWidth = getWidth() - 112;
        editor.setBounds(82, 110, contentWidth, 126);
        loopButton.setBounds(78, 250, contentWidth, 26);
        charactersButton.setBounds(78, 280, contentWidth, 26);
        const auto buttonWidth = (contentWidth - 12) / 2;
        applyButton.setBounds(82, 358, buttonWidth, 38);
        cancelButton.setBounds(94 + buttonWidth, 358, contentWidth - buttonWidth - 12, 38);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.isKeyCode(juce::KeyPress::escapeKey))
        {
            close(0);
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::returnKey) && key.getModifiers().isCommandDown())
        {
            apply();
            return true;
        }
        return false;
    }

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        return keyPressed(key);
    }

    void componentBeingDeleted(juce::Component&) override
    {
        parent = nullptr;
        close(0);
    }

    void updateLyrics()
    {
        lyrics = splitLyrics(editor.getText(), charactersButton.getToggleState());
        applyButton.setEnabled(!lyrics.isEmpty() && noteCount > 0);
        editor.setTextToShowWhenEmpty(charactersButton.getToggleState() ? juce::String::fromUTF8("输入歌词，每个字符填入一个音符") : juce::String::fromUTF8("输入歌词，以空格或换行分隔"), colours::subdued);
        statusText = juce::String::fromUTF8("已选 ") + juce::String(noteCount) + juce::String::fromUTF8(" 个音符，输入 ") + juce::String(lyrics.size()) + juce::String::fromUTF8(" 个歌词。");
        if (lyrics.isEmpty())
        {
            statusText += juce::String::fromUTF8("请输入歌词。");
        }
        else if (lyrics.size() > noteCount)
        {
            statusText += juce::String::fromUTF8("超出的歌词将被忽略。");
        }
        else if (lyrics.size() < noteCount)
        {
            statusText += loopButton.getToggleState() ? juce::String::fromUTF8("将循环填满所选音符。") : juce::String::fromUTF8("剩余音符保持不变。");
        }
        repaint();
    }

    void apply()
    {
        updateLyrics();
        if (lyrics.isEmpty() || noteCount <= 0 || !onApply)
        {
            return;
        }
        auto callback = std::move(onApply);
        const auto words = lyrics;
        const auto loop = loopButton.getToggleState();
        const auto recipient = parent;
        applyButton.setEnabled(false);
        close(1);
        if (recipient != nullptr)
        {
            callback(words, loop);
        }
    }

    void close(int result)
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        {
            window->exitModalState(result);
            window->setVisible(false);
        }
    }

    StudioTheme theme;
    juce::Component::SafePointer<juce::Component> parent;
    const int noteCount;
    std::function<void(const juce::StringArray&, bool)> onApply;
    juce::TextEditor editor;
    juce::ToggleButton loopButton;
    juce::ToggleButton charactersButton;
    juce::TextButton applyButton;
    juce::TextButton cancelButton;
    juce::StringArray lyrics;
    juce::String statusText;
};
} // namespace

void LyricsDialog::show(juce::Component& parent, const juce::String& initialLyrics, int noteCount, std::function<void(const juce::StringArray&, bool)> onApply)
{
    if (noteCount <= 0)
    {
        return;
    }
    auto content = std::make_unique<LyricsContent>(parent, initialLyrics, noteCount, std::move(onApply));
    auto* lyricsContent = content.get();
    const auto contentBounds = content->getLocalBounds();
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = juce::String::fromUTF8("填入歌词");
    options.dialogBackgroundColour = colours::panel;
    options.content.setOwned(content.release());
    options.componentToCentreAround = &parent;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    auto* window = options.create();
    window->setTitleBarHeight(0);
    window->setTitleBarButtonsRequired(0, false);
    window->setContentComponentSize(contentBounds.getWidth(), contentBounds.getHeight());
    window->centreAroundComponent(&parent, window->getWidth(), window->getHeight());
    window->enterModalState(true, nullptr, true);
    lyricsContent->focusLyrics();
}
} // namespace sv
