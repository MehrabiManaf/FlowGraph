// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraphEditorCore/Document.hpp>

#include <FlowGraph/TypeExpr.hpp>

namespace fged::core {

namespace {
constexpr size_t kMaxUndo = 200;
constexpr auto kCoalesceTime = std::chrono::milliseconds(1500);
} // namespace

Document::Document(fg::Runtime& rt) : rt_(rt) { newScript(); }

void Document::markDirty(bool d) {
    if (dirty_ == d) return;
    dirty_ = d;
    titleChanged();
}

std::shared_ptr<const fg::TypeScope> Document::types() const {
    if (program_ && program_->types) return program_->types;
    return std::make_shared<fg::TypeScope>(rt_.registry());
}

StructNameProblem Document::structNameProblem(const std::string& name, const std::string& current) const {
    const fg::TypeExpr e = fg::TypeExpr::parse(name);
    if (name.empty() || !e.valid || !e.isPlain() || e.str() != name) return StructNameProblem::NotAName;
    if (name != current && script_.findStruct(name)) return StructNameProblem::StructExists;
    if (rt_.registry().findType(name)) return StructNameProblem::TypeExists;
    return StructNameProblem::None;
}

void Document::recompile() {
    if (beforeCompile_) beforeCompile_();
    program_ = fg::compile(script_, rt_.registry());
    compiled();
}

void Document::refresh() {
    recompile();
    changed(true);
}

// script_ holds a different script now: whatever belonged to the one before goes, and everybody hears of it.
void Document::replaced(const std::string& path, const std::string& title) {
    path_ = path;
    title_ = title;
    undo_.clear();
    redo_.clear();
    undoLabels_.clear();
    redoLabels_.clear();
    debugger_.clearBreakpoints();
    scriptOpened();
    recompile();
    markDirty(false);
    scriptReplaced();
    changed(true);
    undoStateChanged();
    titleChanged();
}

void Document::newScript(const std::function<void(fg::Script&)>& init) {
    script_ = fg::Script();
    script_.name = "Untitled";
    if (init) init(script_);
    else script_.addEventGraph("EventGraph");
    fg::stampHostEvents(script_, registry());
    replaced({}, {});
}

void Document::setScript(const fg::Script& script, const std::string& title) {
    script_ = script;
    fg::stampHostEvents(script_, registry());
    replaced({}, title);
}

bool Document::exportTo(const std::string& path, std::string* error) const {
    try {
        script_.save(path);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

bool Document::load(const std::string& path, std::string* error) {
    try {
        script_ = fg::Script::load(path);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    replaced(path, {});
    return true;
}

bool Document::save(const std::string& path, std::string* error) {
    const std::string target = path.empty() ? path_ : path;
    if (target.empty()) {
        if (error) *error = "no file name";
        return false;
    }
    try {
        script_.save(target);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    path_ = target;
    markDirty(false);
    titleChanged();
    return true;
}

void Document::edit(const std::string& label, const std::function<void(fg::Script&)>& fn, EditKind kind, const std::string& coalesceKey) {
    const auto now = std::chrono::steady_clock::now();
    const bool coalesce = !coalesceKey.empty() && coalesceKey == lastKey_ && now - lastEdit_ < kCoalesceTime && !undo_.empty();
    if (!coalesce) {
        undo_.push_back(script_.toText(0));
        undoLabels_.push_back(label);
        if (undo_.size() > kMaxUndo) {
            undo_.erase(undo_.begin());
            undoLabels_.erase(undoLabels_.begin());
        }
    }
    redo_.clear();
    redoLabels_.clear();
    lastKey_ = coalesceKey;
    lastEdit_ = now;
    fn(script_);
    fg::stampHostEvents(script_, registry());       // remember the signature of the host events the script handles
    markDirty(true);
    if (kind == EditKind::Structural) recompile();
    changed(kind == EditKind::Structural);
    undoStateChanged();
}

void Document::undo() {
    if (undo_.empty()) return;
    redo_.push_back(script_.toText(0));
    redoLabels_.push_back(undoLabels_.back());
    script_ = fg::Script::parse(undo_.back());
    undo_.pop_back();
    undoLabels_.pop_back();
    lastKey_.clear();
    markDirty(true);
    recompile();
    scriptReplaced();
    changed(true);
    undoStateChanged();
}

void Document::redo() {
    if (redo_.empty()) return;
    undo_.push_back(script_.toText(0));
    undoLabels_.push_back(redoLabels_.back());
    script_ = fg::Script::parse(redo_.back());
    redo_.pop_back();
    redoLabels_.pop_back();
    lastKey_.clear();
    markDirty(true);
    recompile();
    scriptReplaced();
    changed(true);
    undoStateChanged();
}

const fg::CompiledGraph* Document::compiledGraph(const std::string& name) const {
    return program_ ? program_->findGraph(name) : nullptr;
}

std::vector<fg::Diagnostic> Document::diagnosticsFor(const std::string& graph, fg::NodeId node) const {
    std::vector<fg::Diagnostic> out;
    if (!program_) return out;
    for (auto& d : program_->diagnostics)
        if (d.graph == graph && d.node == node) out.push_back(d);
    return out;
}

} // namespace fged::core
