#include "ui/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace sv
{
class StudioApplication final : public juce::JUCEApplication
{
public:
    [[nodiscard]] const juce::String getApplicationName() override
    {
        return "OpenSV";
    }
    [[nodiscard]] const juce::String getApplicationVersion() override
    {
        return "0.1.0";
    }
    [[nodiscard]] bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String& commandLine) override
    {
        window = std::make_unique<MainWindow>();
        openFromCommandLine(commandLine);
    }

    void shutdown() override
    {
        window.reset();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
        {
            window->editor().requestClose();
        }
        else
        {
            quit();
        }
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        openFromCommandLine(commandLine);
        if (window != nullptr)
        {
            window->toFront(true);
        }
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow() : DocumentWindow("OpenSV", colours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            auto content = std::make_unique<MainComponent>();
            content->onTitleChanged = [this](const juce::String& title)
            {
                setName(title);
            };
            content->onClose = []
            {
                juce::JUCEApplication::quit();
            };
            setContentOwned(content.release(), true);
            setResizable(true, false);
            setResizeLimits(1000, 800, 4000, 2400);
            centreWithSize(1400, 900);
            setVisible(true);
        }

        MainComponent& editor()
        {
            return *static_cast<MainComponent*>(getContentComponent());
        }
        void closeButtonPressed() override
        {
            editor().requestClose();
        }
    };

    void openFromCommandLine(const juce::String& commandLine)
    {
        auto arguments = juce::StringArray::fromTokens(commandLine, true);
        if (window != nullptr && !arguments.isEmpty())
        {
            const juce::File file(arguments[0].unquoted());
            if (file.existsAsFile() && file.hasFileExtension("svp"))
            {
                window->editor().openProject(file);
            }
        }
    }

    std::unique_ptr<MainWindow> window;
};
} // namespace sv

START_JUCE_APPLICATION(sv::StudioApplication)
