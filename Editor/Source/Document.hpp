// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document: the script being edited, its compiled form, undo history and the debugger.
#include <FlowGraph/Runtime.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace fged {

enum class EditKind {
    Structural,     // changes what the script does: recompiles, stops a running instance
    Layout,         // positions, comments, colors: only the views are refreshed
};

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(fg::Runtime& rt, QObject* parent = nullptr);

    fg::Runtime& runtime() const { return rt_; }
    const fg::Registry& registry() const { return rt_.registry(); }
    const fg::Script& script() const { return script_; }
    // Read access for the views; every modification must go through edit().
    std::shared_ptr<const fg::Program> program() const { return program_; }
    fg::Debugger& debugger() { return debugger_; }

    const QString& path() const { return path_; }
    bool isDirty() const { return dirty_; }
    QString displayName() const;

    // Compiles again (the application added plugins or events after the script was opened).
    void refresh();
    // An empty script with one event graph, or what `init` builds (a template of the application).
    void newScript(const std::function<void(fg::Script&)>& init = {});
    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path = QString(), QString* error = nullptr);
    // A script that does not come from a file: the application owns it (an embedded editor). `title` is the name the window shows.
    void setScript(const fg::Script& script, const QString& title);
    // The script was handed over to whoever owns it: there is nothing left to save.
    void markSaved() { markDirty(false); }
    // Writes the script to a file without making that file the file of the document.
    bool exportTo(const QString& path, QString* error = nullptr) const;

    // Runs `fn` (which mutates the script given as argument) as one undoable step. Edits with the same
    // non-empty `coalesceKey` that follow each other quickly share one undo step (typing in a field).
    void edit(const QString& label, const std::function<void(fg::Script&)>& fn, EditKind kind = EditKind::Structural,
              const QString& coalesceKey = QString());

    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    QString undoLabel() const { return undo_.isEmpty() ? QString() : undoLabels_.last(); }
    QString redoLabel() const { return redo_.isEmpty() ? QString() : redoLabels_.last(); }
    void undo();
    void redo();

    // Graph helpers
    const fg::Graph* graph(const QString& name) const { return script_.findGraph(name.toStdString()); }
    const fg::CompiledGraph* compiledGraph(const QString& name) const;
    std::vector<fg::Diagnostic> diagnosticsFor(const QString& graph, fg::NodeId node) const;

    // Called by the run controller so edits can stop a running instance.
    bool hasErrors() const { return program_ && program_->hasErrors; }

signals:
    void changed(bool structural);          // after every edit / undo / redo / load
    void compiled();                        // a new Program is available
    void scriptReplaced();                  // load / new / undo / redo: graph objects may have changed identity
    void scriptOpened();                    // load / new: a different script, not an edit of the same one (emitted before it is compiled)
    void titleChanged();
    void undoStateChanged();

private:
    void recompile();
    void markDirty(bool d);

    fg::Runtime& rt_;
    fg::Script script_;
    std::shared_ptr<fg::Program> program_;
    fg::Debugger debugger_;
    QString path_;
    QString title_;                         // shown instead of the script's name when the script has no file (setScript)
    bool dirty_ = false;
    QStringList undo_, redo_, undoLabels_, redoLabels_;
    QString lastKey_;
    QElapsedTimer lastEdit_;
};

} // namespace fged
