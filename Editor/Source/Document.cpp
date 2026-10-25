// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Document.hpp"

#include "Common/NodeTexts.hpp"

#include <QCoreApplication>
#include <QFileInfo>

namespace fged {

Document::Document(fg::Runtime& rt, QObject* parent) : QObject(parent), rt_(rt) { newScript(); }

QString Document::displayName() const {
    QString n = !path_.isEmpty() ? QFileInfo(path_).fileName() : !title_.isEmpty() ? title_ : (script_.name == "Untitled" ? QCoreApplication::translate("fged::Document", "Untitled") : QString::fromStdString(script_.name));      // the new script's name, in the editor's language
    return dirty_ ? n + "*" : n;
}

QString Document::graphTitle(const std::string& name) {
    return name == "EventGraph" ? QCoreApplication::translate("fged::Document", "EventGraph") : QString::fromStdString(name);
}

void Document::markDirty(bool d) {
    if (dirty_ == d) return;
    dirty_ = d;
    emit titleChanged();
}

std::shared_ptr<const fg::TypeScope> Document::types() const {
    if (program_ && program_->types) return program_->types;
    return std::make_shared<fg::TypeScope>(rt_.registry());
}

QString Document::structNameProblem(const QString& name, const QString& current) const {
    const std::string n = name.toStdString();
    const fg::TypeExpr e = fg::TypeExpr::parse(n);
    if (n.empty() || !e.valid || !e.isPlain() || e.str() != n) return QCoreApplication::translate("fged::Document", "A struct name is one word, without spaces or < > ,");
    if (name != current && script_.findStruct(n)) return QCoreApplication::translate("fged::Document", "A struct with that name exists.");
    if (rt_.registry().findType(n)) return QCoreApplication::translate("fged::Document", "A type of this application has that name.");
    return QString();
}

void Document::recompile() {
    loadPluginTranslations(rt_);                // plugins loaded since (a launch configuration's) bring their texts
    program_ = fg::compile(script_, rt_.registry());
    emit compiled();
}

void Document::refresh() {
    recompile();
    emit changed(true);
}

void Document::newScript(const std::function<void(fg::Script&)>& init) {
    script_ = fg::Script();
    script_.name = "Untitled";
    if (init) init(script_);
    else script_.addEventGraph("EventGraph");
    fg::stampHostEvents(script_, registry());
    path_.clear();
    title_.clear();
    undo_.clear(); redo_.clear(); undoLabels_.clear(); redoLabels_.clear();
    debugger_.clearBreakpoints();
    emit scriptOpened();
    recompile();
    markDirty(false);
    emit scriptReplaced();
    emit changed(true);
    emit undoStateChanged();
    emit titleChanged();
}

void Document::setScript(const fg::Script& script, const QString& title) {
    script_ = script;
    fg::stampHostEvents(script_, registry());
    path_.clear();
    title_ = title;
    undo_.clear(); redo_.clear(); undoLabels_.clear(); redoLabels_.clear();
    debugger_.clearBreakpoints();
    emit scriptOpened();
    recompile();
    markDirty(false);
    emit scriptReplaced();
    emit changed(true);
    emit undoStateChanged();
    emit titleChanged();
}

bool Document::exportTo(const QString& path, QString* error) const {
    try {
        script_.save(path.toStdString());
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

bool Document::load(const QString& path, QString* error) {
    try {
        script_ = fg::Script::load(path.toStdString());
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    path_ = path;
    title_.clear();
    undo_.clear(); redo_.clear(); undoLabels_.clear(); redoLabels_.clear();
    debugger_.clearBreakpoints();
    emit scriptOpened();
    recompile();
    markDirty(false);
    emit scriptReplaced();
    emit changed(true);
    emit undoStateChanged();
    emit titleChanged();
    return true;
}

bool Document::save(const QString& path, QString* error) {
    QString target = path.isEmpty() ? path_ : path;
    if (target.isEmpty()) {
        if (error) *error = "no file name";
        return false;
    }
    try {
        script_.save(target.toStdString());
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    path_ = target;
    markDirty(false);
    emit titleChanged();
    return true;
}

void Document::edit(const QString& label, const std::function<void(fg::Script&)>& fn, EditKind kind, const QString& coalesceKey) {
    bool coalesce = !coalesceKey.isEmpty() && coalesceKey == lastKey_ && lastEdit_.isValid() && lastEdit_.elapsed() < 1500 && !undo_.isEmpty();
    if (!coalesce) {
        undo_.push_back(QString::fromStdString(script_.toText(0)));
        undoLabels_.push_back(label);
        if (undo_.size() > 200) { undo_.removeFirst(); undoLabels_.removeFirst(); }
    }
    redo_.clear();
    redoLabels_.clear();
    lastKey_ = coalesceKey;
    lastEdit_.restart();
    fn(script_);
    fg::stampHostEvents(script_, registry());       // remember the signature of the host events the script handles
    markDirty(true);
    if (kind == EditKind::Structural) recompile();
    emit changed(kind == EditKind::Structural);
    emit undoStateChanged();
}

void Document::undo() {
    if (undo_.isEmpty()) return;
    redo_.push_back(QString::fromStdString(script_.toText(0)));
    redoLabels_.push_back(undoLabels_.last());
    script_ = fg::Script::parse(undo_.takeLast().toStdString());
    undoLabels_.removeLast();
    lastKey_.clear();
    markDirty(true);
    recompile();
    emit scriptReplaced();
    emit changed(true);
    emit undoStateChanged();
}

void Document::redo() {
    if (redo_.isEmpty()) return;
    undo_.push_back(QString::fromStdString(script_.toText(0)));
    undoLabels_.push_back(redoLabels_.last());
    script_ = fg::Script::parse(redo_.takeLast().toStdString());
    redoLabels_.removeLast();
    lastKey_.clear();
    markDirty(true);
    recompile();
    emit scriptReplaced();
    emit changed(true);
    emit undoStateChanged();
}

const fg::CompiledGraph* Document::compiledGraph(const QString& name) const {
    return program_ ? program_->findGraph(name.toStdString()) : nullptr;
}

std::vector<fg::Diagnostic> Document::diagnosticsFor(const QString& graph, fg::NodeId node) const {
    std::vector<fg::Diagnostic> out;
    if (!program_) return out;
    std::string g = graph.toStdString();
    for (auto& d : program_->diagnostics)
        if (d.graph == g && d.node == node) out.push_back(d);
    return out;
}

} // namespace fged
