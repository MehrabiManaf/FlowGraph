// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "ScriptEditorHub.hpp"
#include "TableDock.hpp"

#include <QMessageBox>

namespace msgapp {

// ------------------------------------------------------------------ the host of the one editor
// This is what an application tells the editor: which runtime to use, that the editor is an embedded one, what a new script looks like, what
// to do with a script the user saved, and what the debugger can attach to.
class ScriptEditorHub::Host : public fged::EditorHost {
public:
    Host(ScriptEditorHub& hub, fg::Runtime& rt) : hub_(hub), rt_(rt) {}

    fg::Runtime& runtime() override { return rt_; }
    QString applicationName() const override { return kApplicationName; }

    // The application owns the scripts and runs them: the editor has no files and no Run button.
    fged::EditorMode mode() const override { return fged::EditorMode::Embedded; }
    // The theme of the editor is the theme of the whole application (its tables too): the user may choose it in the editor.
    // main() applies the saved choice before the first window is shown.
    bool offersThemeChoice() const override { return true; }
    // So is its language: the editor has View > Language too (the same choice as the main window's), applied by main() at the start.
    bool offersLanguageChoice() const override { return true; }

    // A script is only taken (edit(), File > Open) when it handles the event of this application. The editor has already checked that the
    // application provides everything the script uses: a script made for another application (Main, Tick...) never gets here.
    bool acceptsScript(const fg::Script& script, QString* reason) override {
        if (handlesNewMessage(script)) return true;
        if (reason) *reason = ScriptEditorHub::tr("The script does not handle the event %1: nothing would run.").arg(kNewMessageEvent);
        return false;
    }

    // Save in the editor: the table that is being edited gets the script and runs it. The script of the table is only replaced by one that
    // is all right: it fits the application and compiles; otherwise the table keeps (and keeps running) the script it has.
    bool applyScript(fged::ScriptEditor& editor, const fg::Script& script) override {
        if (!hub_.current_) return false;
        std::string why = whyNotUsable(script, rt_.registry());
        if (why.empty()) {
            auto program = rt_.compile(script);
            if (!program->ok())
                for (auto& d : program->diagnostics)
                    if (d.severity == fg::Diagnostic::Severity::Error) {
                        why = ScriptEditorHub::tr("The script has a compile error: %1").arg(QString::fromStdString(d.message)).toStdString();
                        break;
                    }
        }
        if (!why.empty()) {
            auto* box = new QMessageBox(QMessageBox::Warning, ScriptEditorHub::tr("Save"),
                                        ScriptEditorHub::tr("The table keeps its script, this one cannot be used:") + "\n\n" + QString::fromStdString(why),
                                        QMessageBox::Ok, editor.window());
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
            return false;
        }
        hub_.current_->setScript(script);
        return true;
    }

    // Attach in the editor: the executor that runs the script of the table that is being edited (null: it has not run yet).
    std::shared_ptr<fg::AsyncExecutor> debugTarget(fged::ScriptEditor&) override { return hub_.current_ ? hub_.current_->executor() : nullptr; }

private:
    ScriptEditorHub& hub_;
    fg::Runtime& rt_;
};

// ------------------------------------------------------------------ the hub
ScriptEditorHub::ScriptEditorHub(fg::Runtime& rt, MessageStore& store, QObject* parent)
    : QObject(parent), rt_(rt), store_(store), host_(std::make_unique<Host>(*this, rt)) {}

ScriptEditorHub::~ScriptEditorHub() = default;

void ScriptEditorHub::addTable(TableDock* table) { tables_.push_back(table); }
void ScriptEditorHub::removeTable(TableDock* table) {
    tables_.removeAll(QPointer<TableDock>(table));
    if (current_ == table) current_ = nullptr;
}

bool ScriptEditorHub::edit(TableDock* table, bool show) {
    if (!editor_) editor_ = std::make_unique<fged::ScriptEditor>(*host_);      // the one editor: made on the first use, never again
    if (current_ != table) {
        // The editor shows the script of the table. When it has unsaved changes of the previous one it asks first (Save hands them to that table).
        if (!editor_->edit(table->script(), table->scriptTitle())) return false;
        current_ = table;
    }
    if (show) {
        QWidget* w = editor_->window();
        if (!w->isVisible()) editor_->show();
        w->raise();
        w->activateWindow();
    }
    return true;
}

bool ScriptEditorHub::save(TableDock* table) {
    if (!edit(table, false)) return false;
    return editor_->save();                                                   // success runs EditorHost::applyScript
}

bool ScriptEditorHub::release(TableDock* table, bool askToSave) {
    if (current_ != table) return true;
    if (askToSave && editor_ && !editor_->maybeSave()) return false;
    current_ = nullptr;
    return true;
}

void ScriptEditorHub::executorCreated(TableDock* table, std::shared_ptr<fg::AsyncExecutor> executor) {
    if (editor_ && editor_->isAttached() && current_ == table) editor_->attach(std::move(executor));
}

bool ScriptEditorHub::closeEditor() { return !editor_ || editor_->window()->close(); }

} // namespace msgapp
