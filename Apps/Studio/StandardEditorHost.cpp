#include "StandardEditorHost.hpp"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace flowstudio {

using fg::standard::LaunchConfig;
using fg::standard::LaunchFile;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& q) { return q.toStdString(); }

QStringList toLines(const std::vector<std::string>& v) {
    QStringList out;
    for (auto& x : v) out << q(x);
    return out;
}
std::vector<std::string> fromLines(const QString& text) {
    std::vector<std::string> out;
    for (auto& line : text.split('\n'))
        if (!line.trimmed().isEmpty()) out.push_back(s(line.trimmed().isEmpty() ? line : QString(line).remove('\r')));
    return out;
}

} // namespace

// ------------------------------------------------------------------ what can be run
std::vector<fged::LaunchEntry> StandardEditorHost::launchEntries(const fg::Program& program) {
    std::vector<fged::LaunchEntry> out;
    if (!program.events.count(fg::standard::kMainEvent)) return out;            // nothing to run without Main
    launch_.current();                                                          // makes sure there is a configuration
    for (auto& c : launch_.configs) {
        fged::LaunchEntry e;
        e.id = q(c.name);
        e.entry = fg::standard::kMainEvent;
        QString args = toLines(c.args).join(' ');
        e.title = q(c.name) + (args.isEmpty() ? QString() : "  (" + args + ")");
        out.push_back(std::move(e));
    }
    return out;
}

void StandardEditorHost::launchEntrySelected(const QString& id) {
    if (launch_.selected == s(id) || !launch_.find(s(id))) return;
    launch_.selected = s(id);
    saveLaunchFile();                                      // remembered next to the script
}

bool StandardEditorHost::prepareLaunch(const fged::LaunchEntry& entry, fged::LaunchSpec& spec, QWidget*) {
    LaunchConfig* cfg = launch_.find(s(entry.id));
    if (!cfg) return false;
    if (launch_.selected != cfg->name) {                  // remember the choice next to the script
        launch_.selected = cfg->name;
        saveLaunchFile();
    }
    active_ = *cfg;
    spec.entry = entry.entry;
    spec.args = {fg::standard::argsValue(rt_.registry(), cfg->args)};
    spec.stepDelayMs = cfg->stepDelayMs;
    return true;
}

QString StandardEditorHost::scriptDir() const {
    return scriptPath_.isEmpty() ? QDir::currentPath() : QFileInfo(scriptPath_).absolutePath();
}

void StandardEditorHost::aboutToRun(const fged::LaunchSpec&) {
    envApplied_ = true;
    savedEnv_.clear();
    for (auto& [k, v] : active_.env) {
        QByteArray old = qgetenv(k.c_str());
        savedEnv_[k] = qEnvironmentVariableIsSet(k.c_str()) ? std::optional<std::string>(old.toStdString()) : std::nullopt;
        qputenv(k.c_str(), QByteArray::fromStdString(v));
    }
    savedCwd_ = QDir::currentPath();
    if (!active_.cwd.empty()) QDir::setCurrent(QDir(scriptDir()).filePath(q(active_.cwd)));
}

void StandardEditorHost::runFinished() {
    if (!envApplied_) return;
    envApplied_ = false;
    for (auto& [k, old] : savedEnv_) {
        if (old) qputenv(k.c_str(), QByteArray::fromStdString(*old));
        else qunsetenv(k.c_str());
    }
    savedEnv_.clear();
    if (!savedCwd_.isEmpty()) QDir::setCurrent(savedCwd_);
}

// ------------------------------------------------------------------ launch file of the script
void StandardEditorHost::attached(fged::ScriptEditor& editor) {
    editor_ = &editor;
    QMenu* run = editor.menu("&Run");
    run->addSeparator();
    QAction* a = run->addAction("Launch &Configurations...");
    QObject::connect(a, &QAction::triggered, &editor, [this] { editConfigurations(); });
    // a button next to the selector of the toolbar
    QAction* configure = new QAction("Configure...", &editor);
    configure->setToolTip("Edit the launch configurations of this script");
    QObject::connect(configure, &QAction::triggered, &editor, [this] { editConfigurations(); });
    editor.addToLaunchSelector(configure);
}

void StandardEditorHost::loadPluginPaths(const LaunchConfig& cfg) {
    bool any = false;
    for (auto& p : cfg.pluginPaths) {
        QString dir = QDir(scriptDir()).filePath(q(p));
        if (!QFileInfo::exists(dir)) continue;
        std::vector<std::string> errors;
        int before = int(rt_.loadedPlugins().size());
        rt_.loadPluginsFrom(s(dir), &errors);
        any |= int(rt_.loadedPlugins().size()) != before;
        for (auto& e : errors) editor_->log(1, q(e));
    }
    if (any) editor_->refreshScript();                      // the plugins may provide nodes the script needs
}

void StandardEditorHost::scriptChanged(fged::ScriptEditor& editor, fged::ScriptChange why) {
    QString path = editor.path();
    switch (why) {
    case fged::ScriptChange::Created:
        launch_ = LaunchFile();
        launch_.current();
        scriptPath_.clear();
        break;
    case fged::ScriptChange::Opened:
        scriptPath_ = path;
        try {
            launch_ = LaunchFile::load(LaunchFile::pathFor(s(path)));
        } catch (const std::exception& e) {
            editor.log(1, "Cannot read the launch file of this script: " + q(e.what()));
            launch_ = LaunchFile();
            launch_.current();
        }
        for (auto& c : launch_.configs) loadPluginPaths(c);
        break;
    case fged::ScriptChange::Saved:
        if (path != scriptPath_) {                           // saved under a (new) name: the launch file moves along
            scriptPath_ = path;
            saveLaunchFile();
        }
        break;
    }
    editor.refreshLaunchEntries();
}

void StandardEditorHost::saveLaunchFile() {
    if (scriptPath_.isEmpty()) return;                      // untitled script: kept in memory until it is saved
    try {
        launch_.save(LaunchFile::pathFor(s(scriptPath_)));
    } catch (const std::exception& e) {
        if (editor_) editor_->log(2, "Cannot save the launch file: " + q(e.what()));
    }
}

// ------------------------------------------------------------------ the configurations dialog
void StandardEditorHost::editConfigurations() {
    LaunchFile work = launch_;
    work.current();
    QDialog dlg(editor_->window());
    dlg.setWindowTitle("Launch Configurations");
    dlg.resize(760, 460);
    auto* outer = new QVBoxLayout(&dlg);
    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);

    auto* left = new QVBoxLayout;
    auto* list = new QListWidget;
    auto* add = new QPushButton("Add");
    auto* dup = new QPushButton("Duplicate");
    auto* del = new QPushButton("Remove");
    left->addWidget(list, 1);
    auto* lb = new QHBoxLayout;
    lb->addWidget(add);
    lb->addWidget(dup);
    lb->addWidget(del);
    left->addLayout(lb);
    body->addLayout(left, 1);

    auto* form = new QFormLayout;
    auto* name = new QLineEdit;
    auto* args = new QPlainTextEdit;
    args->setPlaceholderText("one argument per line");
    auto* cwd = new QLineEdit;
    cwd->setPlaceholderText("relative to the script's folder; empty = unchanged");
    auto* env = new QPlainTextEdit;
    env->setPlaceholderText("NAME=value, one per line");
    auto* plugins = new QPlainTextEdit;
    plugins->setPlaceholderText("plugin directories (relative to the script), one per line");
    auto* delay = new QSpinBox;
    delay->setRange(0, 10000);
    delay->setSuffix(" ms");
    delay->setToolTip("Delay between two nodes while running: slow motion for watching the links");
    form->addRow("Name", name);
    form->addRow("Arguments", args);
    form->addRow("Working directory", cwd);
    form->addRow("Environment", env);
    form->addRow("Plugin directories", plugins);
    form->addRow("Step delay", delay);
    body->addLayout(form, 2);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);

    int shown = -1;
    auto store = [&] {                                      // widgets -> configuration
        if (shown < 0 || shown >= int(work.configs.size())) return;
        LaunchConfig& c = work.configs[size_t(shown)];
        std::string oldName = c.name;
        c.name = s(name->text().trimmed().isEmpty() ? q(oldName) : name->text().trimmed());
        if (work.selected == oldName) work.selected = c.name;
        c.args = fromLines(args->toPlainText());
        c.cwd = s(cwd->text().trimmed());
        c.env.clear();
        for (auto& line : fromLines(env->toPlainText())) {
            size_t eq = line.find('=');
            if (eq != std::string::npos && eq > 0) c.env[line.substr(0, eq)] = line.substr(eq + 1);
        }
        c.pluginPaths = fromLines(plugins->toPlainText());
        c.stepDelayMs = delay->value();
        list->item(shown)->setText(q(c.name));
    };
    auto show = [&](int row) {                              // configuration -> widgets
        store();
        shown = -1;
        if (row < 0 || row >= int(work.configs.size())) return;
        const LaunchConfig& c = work.configs[size_t(row)];
        name->setText(q(c.name));
        args->setPlainText(toLines(c.args).join('\n'));
        cwd->setText(q(c.cwd));
        QStringList envLines;
        for (auto& [k, v] : c.env) envLines << q(k + "=" + v);
        env->setPlainText(envLines.join('\n'));
        plugins->setPlainText(toLines(c.pluginPaths).join('\n'));
        delay->setValue(c.stepDelayMs);
        shown = row;
    };
    auto fill = [&](int select) {
        shown = -1;
        list->clear();
        for (auto& c : work.configs) list->addItem(q(c.name));
        list->setCurrentRow(select);
        show(select);
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dlg, [&](int row) { if (row != shown) show(row); });
    QObject::connect(add, &QPushButton::clicked, &dlg, [&] {
        store();
        LaunchConfig c;
        c.name = "Configuration " + std::to_string(work.configs.size() + 1);
        work.configs.push_back(c);
        fill(int(work.configs.size()) - 1);
    });
    QObject::connect(dup, &QPushButton::clicked, &dlg, [&] {
        store();
        if (shown < 0) return;
        LaunchConfig c = work.configs[size_t(shown)];
        c.name += " copy";
        work.configs.push_back(c);
        fill(int(work.configs.size()) - 1);
    });
    QObject::connect(del, &QPushButton::clicked, &dlg, [&] {
        if (shown < 0 || work.configs.size() <= 1) return;       // there is always at least one configuration
        work.configs.erase(work.configs.begin() + shown);
        shown = -1;
        fill(0);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    int start = 0;
    for (size_t i = 0; i < work.configs.size(); ++i)
        if (work.configs[i].name == work.selected) start = int(i);
    fill(start);
    if (dlg.exec() != QDialog::Accepted) return;
    store();
    if (!work.find(work.selected)) work.selected = work.configs.front().name;
    // names must be unique: later duplicates get a number
    for (size_t i = 0; i < work.configs.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (work.configs[i].name == work.configs[j].name) work.configs[i].name += " (" + std::to_string(i + 1) + ")";
    launch_ = work;
    saveLaunchFile();
    for (auto& c : launch_.configs) loadPluginPaths(c);
    editor_->refreshLaunchEntries();
    if (scriptPath_.isEmpty()) editor_->log(0, "The launch configurations are saved together with the script.");
}

} // namespace flowstudio
