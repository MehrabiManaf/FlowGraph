#include <FlowGraphEditor/ScriptEditor.hpp>

#include "../Common/CustomViewFactory/CustomViewFactory.hpp"
#include "ScriptEditorWindow.hpp"

#include "../Graph/Style.hpp"
#include "RunController.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QStyleFactory>

#include <algorithm>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>

static void initResources() { Q_INIT_RESOURCE(Resources); }

namespace fged {

void applyDarkTheme(QApplication& a) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    const QColor darkBg(45, 45, 45), darkerBg(35, 35, 35), lightBg(53, 53, 53), textColor(240, 240, 240), disabledText(120, 120, 120);
    const QColor highlight(255, 127, 0), highlightedText(Qt::black);
    QPalette p;
    p.setColor(QPalette::Window, darkBg);
    p.setColor(QPalette::WindowText, textColor);
    p.setColor(QPalette::Base, darkerBg);
    p.setColor(QPalette::AlternateBase, lightBg);
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::ButtonText, textColor);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::ToolTipText, textColor);
    p.setColor(QPalette::ToolTipBase, darkBg);
    p.setColor(QPalette::Button, lightBg);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Base, darkBg);
    p.setColor(QPalette::Disabled, QPalette::Window, darkBg);
    p.setColor(QPalette::Disabled, QPalette::Button, darkBg);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, highlight.darker(150));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    p.setColor(QPalette::Link, QColor(42, 130, 218));
    p.setColor(QPalette::LinkVisited, QColor(80, 80, 200));
    p.setColor(QPalette::PlaceholderText, QColor(160, 160, 160));
    p.setColor(QPalette::Light, lightBg.lighter(120));
    p.setColor(QPalette::Midlight, lightBg);
    p.setColor(QPalette::Dark, darkerBg);
    p.setColor(QPalette::Mid, darkBg);
    p.setColor(QPalette::Shadow, Qt::black);
    a.setPalette(p);
}

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
    std::vector<LaunchEntry> out;
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
        e.title = "Event  " + e.entry + "(" + sig.join(", ") + ")";
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
        e.title = "Function  " + e.entry + "(" + sig.join(", ") + ")";
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const LaunchEntry& a, const LaunchEntry& b) {
        bool ea = a.title.startsWith("Event"), eb = b.title.startsWith("Event");
        if (ea != eb) return ea;
        return a.entry < b.entry;
    });
    return out;
}

bool EditorHost::prepareLaunch(const LaunchEntry& entry, LaunchSpec& spec, QWidget* parent) {
    spec.entry = entry.entry;
    spec.args.clear();
    if (entry.params.empty()) return true;
    QDialog dlg(parent);
    dlg.setWindowTitle("Arguments for " + entry.entry);
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
ScriptEditor::ScriptEditor(EditorHost& host) : host_(host) {
    window_ = new ScriptEditorWindow(host, *this);
    host_.attached(*this);
}

ScriptEditor::~ScriptEditor() { delete window_; }

QWidget* ScriptEditor::window() const { return window_; }
void ScriptEditor::show() { window_->show(); }
bool ScriptEditor::open(const QString& path) { return window_->openFile(path); }
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
