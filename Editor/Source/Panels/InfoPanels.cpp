#include "InfoPanels.hpp"
#include "../Graph/Style.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fged {

// ------------------------------------------------------------------ diagnostics
DiagnosticsPanel::DiagnosticsPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    summary_ = new QLabel(this);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({"Graph", "Node", "Message"});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    lay->addWidget(summary_);
    lay->addWidget(tree_);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) {
        if (!it || it->data(0, Qt::UserRole + 1).toInt() == 0) return;
        emit activated(it->data(0, Qt::UserRole).toString(), it->data(0, Qt::UserRole + 1).toInt());
    });
    connect(doc_, &Document::compiled, this, [this] { refresh(); });
    refresh();
}

void DiagnosticsPanel::refresh() {
    tree_->clear();
    auto prog = doc_->program();
    int errors = 0, warnings = 0;
    if (prog)
        for (auto& d : prog->diagnostics) {
            bool err = d.severity == fg::Diagnostic::Severity::Error;
            (err ? errors : warnings)++;
            auto* it = new QTreeWidgetItem(tree_);
            it->setText(0, qs(d.graph));
            const fg::CompiledGraph* cg = prog->findGraph(d.graph);
            const fg::CompiledNode* cn = cg ? cg->findNode(d.node) : nullptr;
            it->setText(1, cn ? qs(cn->title) : QString());
            it->setText(2, qs(d.message));
            it->setForeground(2, err ? QColor(255, 120, 120) : QColor(230, 190, 60));
            it->setData(0, Qt::UserRole, qs(d.graph));
            it->setData(0, Qt::UserRole + 1, d.node);
        }
    if (errors == 0 && warnings == 0) summary_->setText("<span style='color:#7bd88f'>Compiled successfully.</span>");
    else summary_->setText(QString("%1 error(s), %2 warning(s)").arg(errors).arg(warnings));
    emit summaryChanged(errors, warnings);
}

// ------------------------------------------------------------------ log
LogPanel::LogPanel(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setMaximumBlockCount(5000);
    setLineWrapMode(NoWrap);
    QFont f("Consolas");
    f.setStyleHint(QFont::Monospace);
    setFont(f);
}

void LogPanel::append(int level, const QString& text) {
    QString color = level == int(fg::LogLevel::Error) ? "#ff7b7b" : level == int(fg::LogLevel::Warning) ? "#e6be3c" : QString();
    QString html = text.toHtmlEscaped().replace(' ', "&nbsp;");
    if (!color.isEmpty()) html = "<span style='color:" + color + "'>" + html + "</span>";
    appendHtml(html);
}

// ------------------------------------------------------------------ debug
namespace {
void addView(QTreeWidgetItem* parent, const fg::ValueView& v) {
    auto* it = new QTreeWidgetItem(parent);
    it->setText(0, qs(v.name));
    it->setText(1, qs(v.text));
    it->setText(2, qs(v.type));
    it->setForeground(2, qApp->palette().color(QPalette::PlaceholderText));
    for (auto& c : v.children) addView(it, c);
}
} // namespace

DebugPanel::DebugPanel(Document* doc, RunController* run, QWidget* parent) : QWidget(parent), doc_(doc), run_(run) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter(Qt::Vertical, this);
    lay->addWidget(split);
    stack_ = new QTreeWidget(split);
    stack_->setHeaderLabels({"Call stack", "Node"});
    stack_->setRootIsDecorated(false);
    vars_ = new QTreeWidget(split);
    vars_->setHeaderLabels({"Variable", "Value", "Type"});
    vars_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    vars_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    split->addWidget(stack_);
    split->addWidget(vars_);
    connect(run_, &RunController::stateChanged, this, [this] { refresh(); });
    connect(run_, &RunController::paused, this, [this] { refresh(); });
    connect(run_, &RunController::finished, this, [this] { refresh(); });
    connect(stack_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur || updating_) return;
        run_->setSelectedFrame(cur->data(0, Qt::UserRole).toInt());
        fillVariables();
        emit locationActivated(cur->data(0, Qt::UserRole + 1).toString(), cur->data(0, Qt::UserRole + 2).toInt());
    });
}

void DebugPanel::refresh() {
    updating_ = true;
    stack_->clear();
    fg::Task* t = run_->task();
    if (t && run_->state() == RunController::State::Paused) {
        auto stack = t->callStack();
        for (size_t i = 0; i < stack.size(); ++i) {
            auto* it = new QTreeWidgetItem(stack_);
            it->setText(0, qs(stack[i].graph) + (stack[i].isEvent ? " (event)" : ""));
            const fg::CompiledGraph* cg = doc_->program() ? doc_->program()->findGraph(stack[i].graph) : nullptr;
            const fg::CompiledNode* cn = cg ? cg->findNode(stack[i].node) : nullptr;
            it->setText(1, cn ? qs(cn->title) : QString());
            it->setData(0, Qt::UserRole, int(i));
            it->setData(0, Qt::UserRole + 1, qs(stack[i].graph));
            it->setData(0, Qt::UserRole + 2, stack[i].node);
        }
        if (stack_->topLevelItemCount()) stack_->setCurrentItem(stack_->topLevelItem(std::min(run_->selectedFrame(), stack_->topLevelItemCount() - 1)));
    }
    updating_ = false;
    fillVariables();
}

void DebugPanel::fillVariables() {
    vars_->clear();
    fg::Instance* inst = run_->instance();
    if (!inst) return;
    fg::Task* t = run_->task();
    if (t && run_->state() == RunController::State::Paused) {
        size_t frameIndex = t->depth() - 1 - size_t(std::min<int>(run_->selectedFrame(), int(t->depth()) - 1));
        auto* locals = new QTreeWidgetItem(vars_, QStringList{"Locals / parameters"});
        for (auto& v : fg::inspectFrame(*t, frameIndex)) addView(locals, v.value);
        locals->setExpanded(true);
    }
    auto* globals = new QTreeWidgetItem(vars_, QStringList{"Variables"});
    for (auto& v : fg::inspectGlobals(*inst)) addView(globals, v.value);
    globals->setExpanded(true);
}

} // namespace fged
