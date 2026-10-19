#include "TableDock.hpp"
#include "ScriptEditorHub.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QScrollBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace msgapp {

namespace {
// Red for errors, readable on the background of the theme the application shows (light or dark palette).
QString errorColor() { return qApp->palette().color(QPalette::Window).lightness() < 128 ? QStringLiteral("#ff7b7b") : QStringLiteral("#c62828"); }
} // namespace

TableDock::TableDock(int number, fg::Runtime& rt, MessageStore& store, ScriptEditorHub& hub, QWidget* parent)
    : QDockWidget(parent), number_(number), rt_(rt), store_(store), hub_(&hub), script_(makeDefaultScript()) {
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName("Table" + QString::number(number));

    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* bar = new QToolBar(body);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QAction* load = bar->addAction(fged::themedIcon(":/Icons/open.svg"), "Load Script", this, [this] { loadScript(); });
    QAction* edit = bar->addAction(fged::themedIcon(":/Icons/configure.svg"), "Edit Script", this, [this] { editScript(); });
    QAction* save = bar->addAction(fged::themedIcon(":/Icons/save.svg"), "Save Script", this, [this] { saveScript(); });
    load->setToolTip("Load a .flow script into this table");
    edit->setToolTip("Open the script of this table in the script editor. Save in the editor makes the table run the edited script.");
    save->setToolTip("Write the script of this table to a .flow file");
    bar->addSeparator();
    scriptLabel_ = new QLabel(bar);
    bar->addWidget(scriptLabel_);
    layout->addWidget(bar);

    table_ = new QTableWidget(body);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setDefaultSectionSize(22);
    layout->addWidget(table_, 1);

    status_ = new QLabel(body);
    status_->setContentsMargins(4, 0, 4, 2);
    layout->addWidget(status_);
    setWidget(body);

    // what the script produces comes into the table a few times a second
    pollTimer_.setInterval(40);
    connect(&pollTimer_, &QTimer::timeout, this, [this] { poll(); });
    // every new message is a NewMessage for the running script
    connect(&store_, &MessageStore::messageAdded, this, [this](MessagePtr m) {
        if (run_) run_->post(m);
    });
    connect(&store_, &MessageStore::cleared, this, [this] { restart(); });

    hub_->addTable(this);
    updateTitle();
    restart();                                                    // a new table runs the default script
}

TableDock::~TableDock() {
    stopRun();
    if (hub_) {
        hub_->release(this, false);
        hub_->removeTable(this);
    }
}

QString TableDock::scriptTitle() const {
    return "Table " + QString::number(number_) + (scriptPath_.isEmpty() ? QString() : " - " + QFileInfo(scriptPath_).fileName());
}

void TableDock::updateTitle() {
    const QString name = scriptPath_.isEmpty() ? QString("default script") : QFileInfo(scriptPath_).fileName();
    setWindowTitle("Table " + QString::number(number_));
    scriptLabel_->setText("  " + name + (changedSinceFile_ ? " (changed in the editor)" : QString()));
    scriptLabel_->setToolTip(scriptPath_);
}

std::shared_ptr<fg::AsyncExecutor> TableDock::executor() const { return run_ ? run_->executor() : nullptr; }

// ---- the toolbar
void TableDock::loadScript() {
    const QString dir = scriptPath_.isEmpty() ? QString() : QFileInfo(scriptPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Load Script", dir, "FlowGraph scripts (*.flow)");
    if (path.isEmpty()) return;
    QString reason;
    if (!openScript(path, &reason)) QMessageBox::warning(this, "Load Script", reason + "\n\nThe script of the table is unchanged.");
}

bool TableDock::openScript(const QString& path, QString* reason) {
    auto refuse = [&](const QString& why) {
        if (reason) *reason = why;
        return false;
    };
    fg::Runtime::LoadedScript loaded = rt_.loadScript(path.toStdString());
    if (!loaded.error.empty()) return refuse(QString::fromStdString(loaded.error));
    // only a script that fits this application replaces the script of the table: a standalone script (Main...) or one without a NewMessage
    // handler is refused, and the table, its script and the editor stay as they are
    const std::string why = whyNotUsable(loaded.script, rt_.registry());
    if (!why.empty()) return refuse("Cannot use '" + QFileInfo(path).fileName() + "':\n" + QString::fromStdString(why));
    // the table gets another script: if the editor was editing the old one it lets go of this table (the next "Edit Script" shows the new file)
    if (hub_ && !hub_->release(this)) return false;
    script_ = std::move(loaded.script);
    scriptPath_ = path;
    changedSinceFile_ = false;
    updateTitle();
    restart();
    return true;
}

// The script editor is the only one of the application: it edits the script of this table from now on, and saving in it runs this table again.
void TableDock::editScript() {
    if (hub_) hub_->edit(this);
}

bool TableDock::saveScript() {
    QString path = scriptPath_;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save Script", QString(), "FlowGraph scripts (*.flow)");
        if (path.isEmpty()) return false;
        if (!path.contains('.')) path += ".flow";
    }
    try {
        script_.save(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Script", QString::fromStdString(e.what()));
        return false;
    }
    scriptPath_ = path;
    changedSinceFile_ = false;
    updateTitle();
    return true;
}

// What Save in the script editor does: the table has another script, and runs it.
void TableDock::setScript(const fg::Script& script) {
    script_ = script;
    changedSinceFile_ = !scriptPath_.isEmpty() || changedSinceFile_;
    updateTitle();
    restart();
}

// ---- running the script
void TableDock::stopRun() {
    pollTimer_.stop();
    if (run_) run_->cancel();
    run_.reset();
}

void TableDock::showFailure(const QString& text) {
    table_->setRowCount(0);
    table_->setColumnCount(0);
    status_->setText("<span style='color:" + errorColor() + "'>" + text.toHtmlEscaped() + "</span>");
    status_->setToolTip(text);
    log_.clear();
    emit updated();
}

void TableDock::setColumns(const std::vector<std::string>& columns) {
    columns_ = columns;
    columnIndex_.clear();
    table_->setColumnCount(int(columns_.size()));
    for (int i = 0; i < int(columns_.size()); ++i) {
        columnIndex_[columns_[size_t(i)]] = i;
        table_->setHorizontalHeaderItem(i, new QTableWidgetItem(QString::fromStdString(columns_[size_t(i)])));
    }
}

void TableDock::restart() {
    stopRun();
    const fg::CompatibilityReport compat = fg::checkCompatibility(script_, rt_.registry());
    if (!compat.ok()) {
        showFailure("This script needs things this application does not provide:\n" + QString::fromStdString(compat.toText()));
        return;
    }
    std::shared_ptr<fg::Program> program = rt_.compile(script_);
    if (!program->ok()) {
        QString first = "The script has compile errors.";
        for (auto& d : program->diagnostics)
            if (d.severity == fg::Diagnostic::Severity::Error) { first = "Compile error: " + QString::fromStdString(d.message); break; }
        showFailure(first);
        return;
    }
    if (!program->events.count(kNewMessageEvent)) {
        showFailure("The script has no NewMessage event: nothing runs.");
        return;
    }
    // a new run: an empty table with the columns of the script (so that it has headers before the first row), fresh variables
    table_->setRowCount(0);
    setColumns(declaredColumns(*program));
    processed_ = failed_ = rows_ = 0;
    firstError_.clear();
    log_.clear();
    run_ = std::make_unique<ScriptRun>(rt_, program);
    // a script editor that is attached to this table's script takes the new executor before the first message is run
    if (hub_) hub_->executorCreated(this, run_->executor());
    for (const MessagePtr& m : store_.messages()) run_->post(m);          // what has been received, then (messageAdded) what comes
    showStatus();
    pollTimer_.start();
    emit updated();
}

void TableDock::appendRows(const std::vector<Row>& rows) {
    for (const Row& row : rows)
        for (auto& [name, cell] : row.cells)
            if (!columnIndex_.count(name)) {                      // a column the script did not declare (several Add Row nodes can disagree)
                std::vector<std::string> columns = columns_;
                columns.push_back(name);
                setColumns(columns);
            }
    const int first = table_->rowCount();
    table_->setRowCount(first + int(rows.size()));
    for (int r = 0; r < int(rows.size()); ++r)
        for (auto& [name, cell] : rows[size_t(r)].cells) {
            auto* item = new QTableWidgetItem;
            if (auto* i = std::get_if<int64_t>(&cell)) item->setData(Qt::DisplayRole, qlonglong(*i));      // numbers stay numbers
            else if (auto* f = std::get_if<double>(&cell)) item->setData(Qt::DisplayRole, *f);
            else if (auto* b = std::get_if<bool>(&cell)) item->setText(*b ? "true" : "false");
            else if (auto* s = std::get_if<std::string>(&cell)) item->setText(QString::fromStdString(*s));
            table_->setItem(first + r, columnIndex_.at(name), item);
        }
    rows_ += int(rows.size());
    // a table shows the latest rows only
    while (table_->rowCount() > kMaxRows) table_->removeRow(0);
}

void TableDock::poll() {
    if (!run_) return;
    ScriptRun::Update u = run_->takeUpdate();
    if (u.empty()) return;
    QScrollBar* bar = table_->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;       // follow the newest rows, unless the user scrolled up to look at older ones
    if (!u.rows.empty()) appendRows(u.rows);
    processed_ += u.processed;
    failed_ += u.failed;
    firstError_ = QString::fromStdString(u.firstError);
    for (auto& l : u.log) log_ << QString::fromStdString(l);
    while (log_.size() > int(kMaxLog)) log_.removeFirst();
    if (atEnd) table_->scrollToBottom();
    showStatus();
    emit updated();
}

void TableDock::showStatus() {
    QString text = QString("%1 rows (%2 shown), %3 messages handled").arg(rows_).arg(table_->rowCount()).arg(processed_);
    if (failed_) text = "<span style='color:" + errorColor() + "'>" + text + QString(" - %1 failed (%2)</span>").arg(failed_).arg(firstError_.toHtmlEscaped());
    status_->setText(text);
    status_->setToolTip(log_.isEmpty() ? QString() : "Print String output:\n" + log_.join('\n'));
}

QString TableDock::statusText() const { return status_->text(); }

void TableDock::closeEvent(QCloseEvent* e) {
    if (hub_ && !hub_->release(this)) {                           // the editor edits this table and has unsaved changes: the user may cancel
        e->ignore();
        return;
    }
    QDockWidget::closeEvent(e);
}

} // namespace msgapp
