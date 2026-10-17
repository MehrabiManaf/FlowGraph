#pragma once
// The one script editor of the application, and how the application uses it. fged::ScriptEditor is one per program; this application has many
// tables, so "Edit Script" of a table makes the editor edit the script of that table (ScriptEditor::edit).
//
// The editor is an *embedded* one (fged::EditorMode::Embedded) because the application owns the scripts and runs them: there are no files in the
// editor and no Run button. Instead
//   - Save in the editor hands the edited script to the application (EditorHost::applyScript): the table that is being edited gets that script
//     and runs it again over all the messages. Save As writes a copy to a file.
//   - the debugger attaches to what the application runs (EditorHost::debugTarget): the executor of the table that is being edited. When the
//     table runs its script again while the editor is attached, the editor is attached to the new executor before the first message runs.
#include "MessageStore.hpp"

#include <FlowGraphEditor/ScriptEditor.hpp>

#include <QObject>
#include <QPointer>

#include <memory>

namespace msgapp {

class TableDock;

class ScriptEditorHub : public QObject {
    Q_OBJECT
public:
    ScriptEditorHub(fg::Runtime& rt, MessageStore& store, QObject* parent = nullptr);
    ~ScriptEditorHub() override;

    // The tables the application has.
    void addTable(TableDock* table);
    void removeTable(TableDock* table);

    // The editor edits the script of `table`: created on the first call, shown when `show` is set. The changes of the script that was edited
    // before are asked for first; false when the user cancels.
    bool edit(TableDock* table, bool show = true);
    // Saves in the editor (the table that is being edited gets the script and runs it); edits `table` first when it is not the one.
    bool save(TableDock* table);
    // The table goes away or gets another script: the editor no longer edits it. False when the user cancelled (unsaved changes).
    bool release(TableDock* table, bool askToSave = true);
    // A table made a new executor for its script. An editor that is attached to that table's script attaches to the new one.
    void executorCreated(TableDock* table, std::shared_ptr<fg::AsyncExecutor> executor);
    // The application closes: closes the editor window (asks about unsaved changes). False when the user cancelled.
    bool closeEditor();

    bool created() const { return editor_ != nullptr; }
    fged::ScriptEditor* editor() const { return editor_.get(); }       // null until the first edit()
    TableDock* current() const { return current_; }                      // the table the editor edits

private:
    class Host;
    friend class Host;

    fg::Runtime& rt_;
    MessageStore& store_;
    std::unique_ptr<Host> host_;                         // before editor_: the editor uses it until it is destroyed
    std::unique_ptr<fged::ScriptEditor> editor_;
    QList<QPointer<TableDock>> tables_;
    QPointer<TableDock> current_;
};

} // namespace msgapp
