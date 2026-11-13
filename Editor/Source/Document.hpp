// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document: the script being edited, its compiled form, undo history and the debugger - the Qt face of the editors' shared
// fged::core::Document (EditorCore): QString in and out, Qt signals, texts in the editor's language.
#include <FlowGraphEditorCore/Document.hpp>

#include <QObject>
#include <QString>
#include <functional>

namespace fged {

using EditKind = core::EditKind;

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(fg::Runtime& rt, QObject* parent = nullptr);

    core::Document& core() { return core_; }
    const core::Document& core() const { return core_; }

    fg::Runtime& runtime() const { return core_.runtime(); }
    const fg::Registry& registry() const { return core_.registry(); }
    const fg::Script& script() const { return core_.script(); }
    // Read access for the views; every modification must go through edit().
    std::shared_ptr<const fg::Program> program() const { return core_.program(); }
    // The types the script can use: the registry's and its structs (as of the last compile). Never null.
    std::shared_ptr<const fg::TypeScope> types() const { return core_.types(); }
    // Why `name` cannot be the name of a struct (another struct or a type of the application has it, it is not a name...); empty
    // when it can. `current` is the struct being renamed.
    QString structNameProblem(const QString& name, const QString& current = QString()) const;
    fg::Debugger& debugger() { return core_.debugger(); }

    QString path() const { return QString::fromStdString(core_.path()); }
    bool isDirty() const { return core_.isDirty(); }
    QString displayName() const;
    // A graph's name as the editor shows it: the name a new script's event graph gets ("EventGraph") in the editor's language,
    // every other name as it is. The script keeps the name itself.
    static QString graphTitle(const std::string& name);
    static QString graphTitle(const QString& name) { return graphTitle(name.toStdString()); }

    // Compiles again (the application added plugins or events after the script was opened).
    void refresh() { core_.refresh(); }
    // An empty script with one event graph, or what `init` builds (a template of the application).
    void newScript(const std::function<void(fg::Script&)>& init = {}) { core_.newScript(init); }
    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path = QString(), QString* error = nullptr);
    // A script that does not come from a file: the application owns it (an embedded editor). `title` is the name the window shows.
    void setScript(const fg::Script& script, const QString& title) { core_.setScript(script, title.toStdString()); }
    // The script was handed over to whoever owns it: there is nothing left to save.
    void markSaved() { core_.markSaved(); }
    // Writes the script to a file without making that file the file of the document.
    bool exportTo(const QString& path, QString* error = nullptr) const;

    // Runs `fn` (which mutates the script given as argument) as one undoable step. Edits with the same
    // non-empty `coalesceKey` that follow each other quickly share one undo step (typing in a field).
    void edit(const QString& label, const std::function<void(fg::Script&)>& fn, EditKind kind = EditKind::Structural,
              const QString& coalesceKey = QString()) {
        core_.edit(label.toStdString(), fn, kind, coalesceKey.toStdString());
    }

    bool canUndo() const { return core_.canUndo(); }
    bool canRedo() const { return core_.canRedo(); }
    QString undoLabel() const { return QString::fromStdString(core_.undoLabel()); }
    QString redoLabel() const { return QString::fromStdString(core_.redoLabel()); }
    void undo() { core_.undo(); }
    void redo() { core_.redo(); }

    // Graph helpers
    const fg::Graph* graph(const QString& name) const { return core_.graph(name.toStdString()); }
    const fg::CompiledGraph* compiledGraph(const QString& name) const { return core_.compiledGraph(name.toStdString()); }
    std::vector<fg::Diagnostic> diagnosticsFor(const QString& graph, fg::NodeId node) const {
        return core_.diagnosticsFor(graph.toStdString(), node);
    }

    // Called by the run controller so edits can stop a running instance.
    bool hasErrors() const { return core_.hasErrors(); }

signals:
    void changed(bool structural);          // after every edit / undo / redo / load
    void compiled();                        // a new Program is available
    void scriptReplaced();                  // load / new / undo / redo: graph objects may have changed identity
    void scriptOpened();                    // load / new: a different script, not an edit of the same one (emitted before it is compiled)
    void titleChanged();
    void undoStateChanged();

private:
    core::Document core_;
};

} // namespace fged
