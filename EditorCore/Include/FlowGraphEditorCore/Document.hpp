// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// The script being edited, its compiled form, its undo history and its debugger: what every editor of FlowGraph (Qt's,
// GLWidgets') shows and changes. No user interface in it: texts an editor shows are made by the editor, in its language.
#include <FlowGraphEditorCore/Signal.hpp>

#include <FlowGraph/Runtime.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fged::core {

enum class EditKind {
    Structural,     // changes what the script does: recompiles, stops a running instance
    Layout,         // positions, comments, colors: only the views are refreshed
};

// Why a name cannot be the name of a struct (Document::structNameProblem).
enum class StructNameProblem {
    None,
    NotAName,       // empty, or not one word: spaces, < > ,
    StructExists,   // another struct of the script has it
    TypeExists,     // a type of the application has it
};

class Document {
public:
    explicit Document(fg::Runtime& rt);
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    fg::Runtime& runtime() const { return rt_; }
    const fg::Registry& registry() const { return rt_.registry(); }
    const fg::Script& script() const { return script_; }
    // Read access for the views; every modification goes through edit().
    std::shared_ptr<const fg::Program> program() const { return program_; }
    // The types the script can use: the registry's and its structs (as of the last compile). Never null.
    std::shared_ptr<const fg::TypeScope> types() const;
    // `current`: the struct being renamed (its own name is no problem).
    StructNameProblem structNameProblem(const std::string& name, const std::string& current = {}) const;
    fg::Debugger& debugger() { return debugger_; }

    // The file of the document (UTF-8; empty: none yet), and the title of a script that has no file (setScript).
    const std::string& path() const { return path_; }
    const std::string& title() const { return title_; }
    bool isDirty() const { return dirty_; }

    // Called before every compile: an editor loads the translations of plugins loaded since, say.
    void setBeforeCompile(std::function<void()> fn) { beforeCompile_ = std::move(fn); }
    // Compiles again (the application added plugins or events after the script was opened).
    void refresh();
    // An empty script with one event graph, or what `init` builds (a template of the application).
    void newScript(const std::function<void(fg::Script&)>& init = {});
    bool load(const std::string& path, std::string* error = nullptr);
    // Into `path`, or the document's file when it is empty.
    bool save(const std::string& path = {}, std::string* error = nullptr);
    // A script that does not come from a file: the application owns it (an embedded editor). `title` is what the window shows.
    void setScript(const fg::Script& script, const std::string& title);
    // The script was handed over to whoever owns it: there is nothing left to save.
    void markSaved() { markDirty(false); }
    // Writes the script to a file without making that file the document's.
    bool exportTo(const std::string& path, std::string* error = nullptr) const;

    // Runs `fn` (which changes the script it is given) as one undoable step called `label`. Edits with the same non-empty
    // `coalesceKey` that follow each other quickly share one step (typing in a field).
    void edit(const std::string& label, const std::function<void(fg::Script&)>& fn, EditKind kind = EditKind::Structural,
              const std::string& coalesceKey = {});

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::string undoLabel() const { return undoLabels_.empty() ? std::string() : undoLabels_.back(); }
    std::string redoLabel() const { return redoLabels_.empty() ? std::string() : redoLabels_.back(); }
    void undo();
    void redo();

    const fg::Graph* graph(const std::string& name) const { return script_.findGraph(name); }
    const fg::CompiledGraph* compiledGraph(const std::string& name) const;
    std::vector<fg::Diagnostic> diagnosticsFor(const std::string& graph, fg::NodeId node) const;
    bool hasErrors() const { return program_ && program_->hasErrors; }

    Signal<bool> changed;                   // after every edit / undo / redo / load (structural or not)
    Signal<> compiled;                      // a new Program is there
    Signal<> scriptReplaced;                // load / new / undo / redo: graph objects may have changed identity
    Signal<> scriptOpened;                  // load / new: a different script, not an edit of the same (before it is compiled)
    Signal<> titleChanged;                  // the file, the title or whether there is something to save
    Signal<> undoStateChanged;

private:
    void recompile();
    void markDirty(bool d);
    void replaced(const std::string& path, const std::string& title);     // a new script: history, breakpoints, signals

    fg::Runtime& rt_;
    fg::Script script_;
    std::shared_ptr<fg::Program> program_;
    fg::Debugger debugger_;
    std::string path_;
    std::string title_;
    bool dirty_ = false;
    std::vector<std::string> undo_, redo_, undoLabels_, redoLabels_;
    std::string lastKey_;
    std::chrono::steady_clock::time_point lastEdit_{};
    std::function<void()> beforeCompile_;
};

} // namespace fged::core
