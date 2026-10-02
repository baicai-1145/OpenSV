#pragma once

#include "Project.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace sv
{
// All access is confined to the JUCE message thread. Audio receives project snapshots.
class ProjectDocument final : public juce::ChangeBroadcaster
{
public:
    ProjectDocument();

    [[nodiscard]] const Project& getProject() const;
    [[nodiscard]] int getActiveTrackIndex() const;
    void setActiveTrackIndex(int index);
    [[nodiscard]] const NoteGroup& getActiveGroup() const;
    [[nodiscard]] Blick getActiveGroupOffset() const;
    [[nodiscard]] const std::vector<NoteId>& getSelectedNoteIds() const;
    void setSelectedNoteIds(std::vector<NoteId> ids);

    void performEdit(const juce::String& name, const std::function<void(Project&)>& edit);
    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    void newProject();
    [[nodiscard]] juce::Result load(const juce::File& file);
    [[nodiscard]] juce::Result save(const juce::File& file);
    [[nodiscard]] juce::Result importMidi(const juce::File& file);
    [[nodiscard]] juce::Result exportMidi(const juce::File& file) const;

    void addTrack();
    void removeTrack(int index);
    void setModified(bool modified = true);
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] const juce::File& getFile() const;
    [[nodiscard]] std::uint64_t getRevision() const;
    // Changes only when a document is created or loaded, not on edits or undo.
    [[nodiscard]] std::uint64_t getGeneration() const;

private:
    class EditAction;
    void applyState(const Project& state, std::uint64_t stateId);
    void reset(Project replacement, const juce::File& file);
    void pruneSelection();

    Project project;
    juce::UndoManager undoManager;
    juce::File file;
    std::vector<NoteId> selectedNoteIds;
    int activeTrackIndex = 0;
    std::uint64_t revision = 0;
    std::uint64_t generation = 0;
    std::uint64_t currentStateId = 0;
    std::uint64_t savedStateId = 0;
    std::uint64_t nextStateId = 1;
    bool explicitlyModified = false;
};
} // namespace sv
