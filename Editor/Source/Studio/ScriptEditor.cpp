// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include <FlowGraphEditor/ScriptEditor.hpp>

#include "../Common/CustomViewFactory/CustomViewFactory.hpp"
#include "ScriptEditorWindow.hpp"

#include "../Graph/Style.hpp"
#include "RunController.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QStyleFactory>

#include <algorithm>
#include <stdexcept>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>

static void initResources() { Q_INIT_RESOURCE(Resources); }

namespace fged {

void initEditor() {
    initResources();
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    auto flags = KDDockWidgets::Config::self().flags();
    flags |= KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;
    flags |= KDDockWidgets::Config::Flag_AllowReorderTabs;
    flags |= KDDockWidgets::Config::Flag_AlwaysShowTabs;
    flags |= KDDockWidgets::Config::Flag_DoubleClickMaximizes;
    flags |= KDDockWidgets::Config::Flag_AutoHideSupport;
    flags |= KDDockWidgets::Config::Flag_CloseOnlyCurrentTab;
    flags |= KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden;
    flags |= KDDockWidgets::Config::Flag_KeepAboveIfNotUtilityWindow;
    KDDockWidgets::Config::self().setFlags(flags);
    KDDockWidgets::Config::self().setViewFactory(new CCustomViewFactory());
}

// ------------------------------------------------------------------ EditorHost defaults
std::vector<LaunchEntry> EditorHost::launchEntries(const fg::Program& program) {
    // events first, then functions, each by name (not by their titles: those are translated)
    std::vector<LaunchEntry> out, functions;
    for (auto& [name, loc] : program.events) {
        LaunchEntry e;
        e.id = e.entry = qs(name);
        const fg::CompiledNode& n = program.graphs[size_t(loc.first)].nodes[size_t(loc.second)];
        QStringList sig;
        for (int pi : n.dataOut) {
            const fg::CompiledPin& p = n.pins[size_t(pi)];
            e.params.push_back({p.name, p.type ? p.type->name : "any", false, false, fg::Value()});
            sig << qs(p.name) + ": " + (p.type ? qs(p.type->name) : "any");
        }
        e.title = QCoreApplication::translate("fged::EditorHost", "Event  %1(%2)").arg(e.entry, sig.join(", "));
        out.push_back(std::move(e));
    }
    for (auto& [name, gi] : program.functions) {
        LaunchEntry e;
        e.id = e.entry = qs(name);
        QStringList sig;
        for (auto& p : program.graphs[size_t(gi)].inputs) {
            e.params.push_back(p.decl);
            sig << qs(p.decl.name) + ": " + qs(p.decl.type);
        }
        e.title = QCoreApplication::translate("fged::EditorHost", "Function  %1(%2)").arg(e.entry, sig.join(", "));
        functions.push_back(std::move(e));
    }
    auto byName = [](const LaunchEntry& a, const LaunchEntry& b) { return a.entry < b.entry; };
    std::sort(out.begin(), out.end(), byName);
    std::sort(functions.begin(), functions.end(), byName);
    for (auto& f : functions) out.push_back(std::move(f));
    return out;
}

bool EditorHost::prepareLaunch(const LaunchEntry& entry, LaunchSpec& spec, QWidget* parent) {
    spec.entry = entry.entry;
    spec.args.clear();
    if (entry.params.empty()) return true;
    QDialog dlg(parent);
    dlg.setWindowTitle(QCoreApplication::translate("fged::EditorHost", "Arguments for %1").arg(entry.entry));
    auto* form = new QFormLayout(&dlg);
    std::vector<QLineEdit*> edits;
    for (auto& p : entry.params) {
        auto* le = new QLineEdit(&dlg);
        edits.push_back(le);
        form->addRow(qs(p.name) + " (" + qs(p.type) + ")", le);
    }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    for (size_t i = 0; i < entry.params.size(); ++i)
        spec.args.push_back(RunController::parseArgument(runtime().registry(), runtime().registry().findType(entry.params[i].type), edits[i]->text()));
    return true;
}

// ------------------------------------------------------------------ ScriptEditor
namespace {
ScriptEditor* g_instance = nullptr;
}

ScriptEditor* ScriptEditor::instance() { return g_instance; }

ScriptEditor::ScriptEditor(EditorHost& host) : host_(host) {
    if (g_instance)
        throw std::logic_error("There is already a ScriptEditor: a program has only one script editor. Let it edit the other script with ScriptEditor::edit().");
    g_instance = this;
    try {
        window_ = new ScriptEditorWindow(host, *this);
        host_.attached(*this);
    } catch (...) {
        delete window_;
        window_ = nullptr;
        g_instance = nullptr;
        throw;
    }
}

ScriptEditor::~ScriptEditor() {
    delete window_;
    if (g_instance == this) g_instance = nullptr;
}

QWidget* ScriptEditor::window() const { return window_; }
void ScriptEditor::show() {
    window_->restoreSavedLayout();
    window_->show();
}
bool ScriptEditor::open(const QString& path) { return window_->openFile(path); }
bool ScriptEditor::edit(const fg::Script& script, const QString& title) { return window_->editScript(script, title); }
bool ScriptEditor::attach(std::shared_ptr<fg::AsyncExecutor> executor) { return window_->attachExecutor(std::move(executor)); }
void ScriptEditor::detach() { window_->detachExecutor(); }
bool ScriptEditor::isAttached() const { return window_->isAttached(); }
void ScriptEditor::newScript() { window_->newScript(); }
bool ScriptEditor::save() { return window_->fileSave(); }
bool ScriptEditor::saveAs() { return window_->fileSaveAs(); }
bool ScriptEditor::maybeSave() { return window_->maybeSave(); }
QString ScriptEditor::path() const { return window_->document()->path(); }
bool ScriptEditor::isModified() const { return window_->document()->isDirty(); }
QMenu* ScriptEditor::menu(const QString& title) { return window_->menuNamed(title); }
QToolBar* ScriptEditor::toolBar() { return window_->mainToolBar(); }
void ScriptEditor::addToLaunchSelector(QAction* a) { window_->addToLaunchSelector(a); }
void ScriptEditor::refreshLaunchEntries() { window_->refreshEntries(); }
void ScriptEditor::refreshScript() { window_->document()->refresh(); }
void ScriptEditor::log(int level, const QString& text) { window_->log(level, text); }
void ScriptEditor::devRun(const QString& entry, const QString& breakpoint) {
    if (!breakpoint.isEmpty()) {
        QStringList parts = breakpoint.split(':');
        if (parts.size() == 2) window_->document()->debugger().setBreakpoint(parts[0].toStdString(), parts[1].toInt(), true);
    }
    window_->runEntry(entry);
}
void ScriptEditor::devSelect(const QString& spec) { window_->devSelect(spec); }
void ScriptEditor::devProbe(const QString& file, bool coordinates) { window_->devProbe(file, coordinates); }
void ScriptEditor::devSceneShot(const QString& file) { window_->devSceneShot(file); }

} // namespace fged
