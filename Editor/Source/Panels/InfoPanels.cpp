// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "InfoPanels.hpp"
#include "../Common/Theme.hpp"
#include "../Graph/Style.hpp"

#include <QCoreApplication>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include "../Common/NodeTexts.hpp"
#include "../Common/SearchField.hpp"
#include "../Common/Settings.hpp"
#include <QSettings>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
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
    tree_->setHeaderLabels({tr("Graph"), tr("Node"), tr("Message")});
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
    connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] { refresh(); });     // colours of messages and summary
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
            it->setText(0, Document::graphTitle(d.graph));
            const fg::CompiledGraph* cg = prog->findGraph(d.graph);
            const fg::CompiledNode* cn = cg ? cg->findNode(d.node) : nullptr;
            it->setText(1, cn ? nodeTitle(*cn) : QString());
            it->setText(2, qs(d.message));
            it->setForeground(2, err ? themeColors().error : themeColors().warning);
            it->setData(0, Qt::UserRole, qs(d.graph));
            it->setData(0, Qt::UserRole + 1, d.node);
        }
    if (errors == 0 && warnings == 0) summary_->setText("<span style='color:" + themeColors().css(themeColors().success) + "'>" + tr("Compiled successfully.") + "</span>");
    else summary_->setText(tr("%1 error(s), %2 warning(s)").arg(errors).arg(warnings));
    emit summaryChanged(errors, warnings);
}

// ------------------------------------------------------------------ find
FindPanel::FindPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    edit_ = new QLineEdit(this);
    makeSearchField(edit_, tr("Find in script (node titles, comments, values)"));
    summary_ = new QLabel(this);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Graph"), tr("Node"), tr("Where")});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    lay->addWidget(edit_);
    lay->addWidget(summary_);
    lay->addWidget(tree_);
    connect(edit_, &QLineEdit::textEdited, this, [this](const QString& t) { search(t); });
    connect(edit_, &QLineEdit::returnPressed, this, [this] {
        if (auto* first = tree_->topLevelItem(0)) emit tree_->itemActivated(first, 0);
    });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) {
        if (!it) return;
        if (const int declaration = it->data(0, Qt::UserRole + 2).toInt())
            emit declarationActivated(declaration, it->data(0, Qt::UserRole).toString(), it->data(0, Qt::UserRole + 3).toString());
        else
            emit activated(it->data(0, Qt::UserRole).toString(), it->data(0, Qt::UserRole + 1).toInt());
    });
    connect(doc_, &Document::compiled, this, [this] { refresh(); });
    refresh();
}

void FindPanel::findReferences(const fg::RefTarget& target) {
    references_ = true;
    target_ = target;
    edit_->clear();
    refresh();
}

void FindPanel::search(const QString& text) {
    references_ = false;
    if (edit_->text() != text) edit_->setText(text);
    refresh();
}

void FindPanel::focusSearch() {
    edit_->setFocus(Qt::ShortcutFocusReason);
    edit_->selectAll();
}

int FindPanel::resultCount() const { return tree_->topLevelItemCount(); }

void FindPanel::refresh() {
    tree_->clear();
    const fg::Program* prog = doc_->program().get();
    std::vector<fg::ScriptRef> refs;
    QString what;
    if (references_) {
        refs = fg::findReferences(doc_->script(), prog, target_);
        what = tr("References of %1").arg(qs(target_.describe()));
    } else if (!edit_->text().isEmpty()) {
        refs = fg::searchScript(doc_->script(), prog, ss(edit_->text()));
        what = "\"" + edit_->text() + "\"";
    }
    for (auto& r : refs) {
        auto* it = new QTreeWidgetItem(tree_);
        it->setText(0, Document::graphTitle(r.graph));
        const fg::CompiledGraph* cg = prog ? prog->findGraph(r.graph) : nullptr;
        const fg::CompiledNode* cn = cg ? cg->findNode(r.node) : nullptr;
        it->setText(1, cn ? nodeTitle(*cn) : qs(r.title));          // the node's title in the language of the editor
        it->setText(2, qs(r.detail));
        it->setToolTip(2, qs(r.detail));
        it->setData(0, Qt::UserRole, qs(r.graph));
        it->setData(0, Qt::UserRole + 1, r.node);
        it->setData(0, Qt::UserRole + 2, int(r.declaration));
        it->setData(0, Qt::UserRole + 3, qs(r.title));
    }
    if (what.isEmpty()) summary_->setText(tr("Type to search, or use Find References on a node, variable, function or struct."));
    else summary_->setText(what + ": " + (refs.empty() ? tr("nothing found") : tr("%1 found").arg(refs.size())));
}

// ------------------------------------------------------------------ log
LogPanel::LogPanel(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setMaximumBlockCount(5000);
    setLineWrapMode(NoWrap);
    QFont f("Consolas");
    f.setStyleHint(QFont::Monospace);
    setFont(f);
    connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] { recolor(); });
}

namespace {
QColor logColor(int level) {
    if (level == int(fg::LogLevel::Error)) return themeColors().error;
    if (level == int(fg::LogLevel::Warning)) return themeColors().warning;
    return QColor();                            // info: the text colour of the palette
}
} // namespace

void LogPanel::append(int level, const QString& text) {
    const QColor color = logColor(level);
    QString html = text.toHtmlEscaped().replace(' ', "&nbsp;");
    if (color.isValid()) html = "<span style='color:" + themeColors().css(color) + "'>" + html + "</span>";
    appendHtml(html);
    document()->lastBlock().setUserState(level);       // the level of the line, for recolor()
}

// Warnings and errors in the colours of the theme in use: the lines that are already there take the new shade too.
void LogPanel::recolor() {
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const QColor color = logColor(b.userState());
        if (!color.isValid()) continue;
        QTextCursor c(b);
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat fmt;
        fmt.setForeground(color);
        c.mergeCharFormat(fmt);
    }
}

QMenu* LogPanel::buildContextMenu() {
    QMenu* menu = createStandardContextMenu();
    menu->addSeparator();
    QAction* clearAction = menu->addAction(themedIcon(":/Icons/clear.svg"), tr("Clear"));
    clearAction->setEnabled(!document()->isEmpty());
    connect(clearAction, &QAction::triggered, this, &QPlainTextEdit::clear);
    return menu;
}

void LogPanel::contextMenuEvent(QContextMenuEvent* e) {
    QMenu* menu = buildContextMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(e->globalPos());
}

// ------------------------------------------------------------------ debug
namespace {

// Roles of the items of the variable and watch trees.
constexpr int kPathRole = Qt::UserRole + 10;     // expression that addresses the value (QString, empty = not addressable)
constexpr int kKeyRole = Qt::UserRole + 11;      // stable identity across refreshes: tree + root + path
constexpr int kMoreRole = Qt::UserRole + 12;     // bool: this is the "N more" row of its parent
constexpr int kEndRole = Qt::UserRole + 13;      // qulonglong: index after the last loaded child of a container
constexpr int kTotalRole = Qt::UserRole + 14;    // qulonglong: number of children the container has
constexpr int kWatchRole = Qt::UserRole + 15;    // bool: a top level item of the watch tree

constexpr size_t kFirstPage = 64;                // children loaded with a container (InspectOptions::maxItems)
constexpr size_t kPage = 256;                    // what double-clicking "more" loads
constexpr size_t kMaxLoad = 50000;               // most children one request loads

QString thousands(qulonglong n) { return QLocale(QLocale::English).toString(n); }

bool isMoreRow(const QTreeWidgetItem* it) { return it && it->data(0, kMoreRole).toBool(); }

QTreeWidgetItem* findMoreRow(QTreeWidgetItem* container) {
    for (int i = container->childCount() - 1; i >= 0; --i)
        if (isMoreRow(container->child(i))) return container->child(i);
    return nullptr;
}

// Keeps one "N more" row at the end of a container whose children are not all loaded.
void updateMoreRow(QTreeWidgetItem* container) {
    delete findMoreRow(container);
    qulonglong total = container->data(0, kTotalRole).toULongLong();
    qulonglong end = container->data(0, kEndRole).toULongLong();
    if (container->data(0, kPathRole).toString().isEmpty() || total <= end) return;
    auto* more = new QTreeWidgetItem(container);
    more->setData(0, kMoreRole, true);
    more->setText(0, "...");
    more->setText(1, QCoreApplication::translate("fged::DebugPanel", "%1 more - double-click to load %2 more").arg(thousands(total - end), thousands(std::min<qulonglong>(kPage, total - end))));
    QFont f = more->font(1);
    f.setItalic(true);
    more->setFont(1, f);
    more->setForeground(1, qApp->palette().color(QPalette::PlaceholderText));
}

void buildItem(QTreeWidgetItem* it, const fg::ValueView& v, const QString& keyPrefix) {
    it->setText(0, qs(v.name));
    it->setText(1, qs(v.text));
    it->setText(2, qs(v.type));
    it->setForeground(2, qApp->palette().color(QPalette::PlaceholderText));
    QString path = qs(v.path);
    it->setData(0, kPathRole, path);
    it->setData(0, kKeyRole, path.isEmpty() ? QString() : keyPrefix + path);
    it->setData(0, kTotalRole, qulonglong(v.total));
    it->setData(0, kEndRole, qulonglong(v.offset + v.children.size()));
    for (auto& c : v.children) buildItem(new QTreeWidgetItem(it), c, keyPrefix);
    updateMoreRow(it);
}

QString keyPrefixOf(const QTreeWidgetItem* it) {
    QString key = it->data(0, kKeyRole).toString();
    QString path = it->data(0, kPathRole).toString();
    return key.left(key.size() - path.size());
}

} // namespace

DebugPanel::DebugPanel(Document* doc, RunController* run, QWidget* parent) : QWidget(parent), doc_(doc), run_(run) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter(Qt::Vertical, this);
    lay->addWidget(split);
    stack_ = new QTreeWidget(split);
    stack_->setHeaderLabels({tr("Call stack"), tr("Node")});
    stack_->setRootIsDecorated(false);

    auto setupTree = [](QTreeWidget* t, const QStringList& headers) {
        t->setHeaderLabels(headers);
        t->setUniformRowHeights(true);
        t->setContextMenuPolicy(Qt::CustomContextMenu);
        t->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        t->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        t->setColumnWidth(0, 150);
    };
    vars_ = new QTreeWidget(split);
    setupTree(vars_, {tr("Variable"), tr("Value"), tr("Type")});

    auto* watchBox = new QWidget(split);
    auto* wl = new QVBoxLayout(watchBox);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    watchEdit_ = new QLineEdit(watchBox);
    watchEdit_->setClearButtonEnabled(true);
    watchEdit_->setPlaceholderText(tr("Watch: name, name[3].Prop, name[\"key\"], name[1000..1099]  (Enter adds)"));
    watchEdit_->setToolTip(tr("An expression to watch. A range name[a..b] loads those elements (inclusive); a big array is never loaded as a whole.\n"
                           "Locals and parameters of the selected stack frame are found first, then the variables of the script."));
    watches_ = new QTreeWidget(watchBox);
    setupTree(watches_, {tr("Watch"), tr("Value"), tr("Type")});
    wl->addWidget(watchEdit_);
    wl->addWidget(watches_);

    split->addWidget(stack_);
    split->addWidget(vars_);
    split->addWidget(watchBox);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);

    connect(run_, &RunController::stateChanged, this, [this] { refresh(); });
    connect(run_, &RunController::paused, this, [this] { refresh(); });
    connect(run_, &RunController::finished, this, [this] { refresh(); });
    connect(stack_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (!cur || updating_) return;
        run_->setSelectedFrame(cur->data(0, Qt::UserRole).toInt());
        refresh();
        emit locationActivated(cur->data(0, Qt::UserRole + 1).toString(), cur->data(0, Qt::UserRole + 2).toInt());
    });

    for (QTreeWidget* t : {vars_, watches_}) {
        connect(t, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
            if (isMoreRow(it)) loadMore(it->parent(), kPage);
        });
        connect(t, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
            if (updating_) return;
            QString key = it->data(0, kKeyRole).toString();
            if (!key.isEmpty()) expanded_.insert(key);
            // a container below the depth that was collected has no children yet: load the first ones when it is opened
            if (it->data(0, kEndRole).toULongLong() == 0 && it->data(0, kTotalRole).toULongLong() > 0) loadMore(it, kFirstPage);
        });
        connect(t, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
            if (updating_) return;
            expanded_.remove(it->data(0, kKeyRole).toString());
        });
        connect(t, &QTreeWidget::customContextMenuRequested, this, [this, t](const QPoint& pos) { showContextMenu(t, pos); });
    }
    connect(watchEdit_, &QLineEdit::returnPressed, this, [this] {
        addWatch(watchEdit_->text());
        watchEdit_->clear();
    });
    auto* removeKey = new QAction(watches_);
    removeKey->setShortcut(QKeySequence::Delete);
    removeKey->setShortcutContext(Qt::WidgetShortcut);
    connect(removeKey, &QAction::triggered, this, [this] {
        if (auto* it = watches_->currentItem())
            if (it->data(0, kWatchRole).toBool()) removeWatch(it->text(0));
    });
    watches_->addAction(removeKey);

    connect(doc_, &Document::scriptOpened, this, [this] { onScriptOpened(); });
    connect(doc_, &Document::titleChanged, this, [this] { onDocumentPath(); });
    onDocumentPath();
}

void DebugPanel::refresh() {
    RunController::DebugSnapshot snap;
    bool have = run_->snapshot(snap);       // read on the worker thread: the script may be running
    updating_ = true;
    stack_->clear();
    for (size_t i = 0; i < snap.stack.size(); ++i) {
        const auto& e = snap.stack[i];
        auto* it = new QTreeWidgetItem(stack_);
        it->setText(0, Document::graphTitle(e.graph) + (e.isEvent ? " " + tr("(event)") : QString()));
        const fg::CompiledGraph* cg = doc_->program() ? doc_->program()->findGraph(ss(e.graph)) : nullptr;
        const fg::CompiledNode* cn = cg ? cg->findNode(e.node) : nullptr;
        it->setText(1, cn ? nodeTitle(*cn) : QString());
        it->setData(0, Qt::UserRole, int(i));
        it->setData(0, Qt::UserRole + 1, e.graph);
        it->setData(0, Qt::UserRole + 2, e.node);
    }
    if (stack_->topLevelItemCount())
        stack_->setCurrentItem(stack_->topLevelItem(std::min(run_->selectedFrame(), stack_->topLevelItemCount() - 1)));

    vars_->clear();
    if (have) {
        if (run_->state() == RunController::State::Paused) {
            auto* locals = new QTreeWidgetItem(vars_, QStringList{tr("Locals / parameters")});
            for (auto& v : snap.frameVariables) buildItem(new QTreeWidgetItem(locals), v.value, "l:");
            locals->setExpanded(true);
        }
        auto* globals = new QTreeWidgetItem(vars_, QStringList{tr("Variables")});
        for (auto& v : snap.globals) buildItem(new QTreeWidgetItem(globals), v.value, "g:");
        globals->setExpanded(true);
        restore(vars_->invisibleRootItem());
    }
    updating_ = false;
    refreshWatches();
}

// Expansion and the number of loaded children survive a refresh (every step of the debugger refreshes the trees).
void DebugPanel::restore(QTreeWidgetItem* item) {
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* c = item->child(i);
        if (isMoreRow(c)) continue;
        QString key = c->data(0, kKeyRole).toString();
        if (!key.isEmpty()) {
            qulonglong total = c->data(0, kTotalRole).toULongLong();
            qulonglong want = std::min<qulonglong>(loaded_.value(key, 0), total);
            qulonglong end = c->data(0, kEndRole).toULongLong();
            if (expanded_.contains(key) && end == 0 && total > 0) want = std::max<qulonglong>(want, std::min<qulonglong>(total, kFirstPage));
            if (want > end) loadChildren(c, end, std::min<qulonglong>(want - end, kMaxLoad));
            if (expanded_.contains(key)) c->setExpanded(true);
        }
        restore(c);
    }
}

void DebugPanel::refreshWatches() {
    const bool wasUpdating = updating_;
    updating_ = true;
    watches_->clear();
    std::vector<std::string> expressions;
    for (auto& w : watchList_) expressions.push_back(ss(w));
    std::vector<fg::EvalResult> results;
    const bool have = !expressions.empty() && run_->evaluate(expressions, {}, results) && results.size() == expressions.size();
    for (int i = 0; i < watchList_.size(); ++i) {
        auto* it = new QTreeWidgetItem(watches_);
        it->setData(0, kWatchRole, true);
        it->setText(0, watchList_[i]);
        const QString prefix = "w:" + watchList_[i] + "|";
        if (!have) {
            it->setText(1, tr("(not available: run the script first)"));
            it->setForeground(1, qApp->palette().color(QPalette::PlaceholderText));
        } else if (!results[size_t(i)].ok) {
            it->setText(1, "<" + qs(results[size_t(i)].error) + ">");
            it->setForeground(1, themeColors().error);
        } else {
            fg::ValueView view = std::move(results[size_t(i)].view);
            view.name = ss(watchList_[i]);
            buildItem(it, view, prefix);
            if (!view.children.empty() && !seenWatches_.contains(watchList_[i])) {       // a freshly added watch shows its elements at once
                seenWatches_.insert(watchList_[i]);
                expanded_.insert(prefix + qs(view.path));
            }
        }
        it->setToolTip(0, watchList_[i]);
    }
    restore(watches_->invisibleRootItem());
    updating_ = wasUpdating;
}

// Loads `count` more children of a container, continuing after the ones that are loaded.
void DebugPanel::loadMore(QTreeWidgetItem* container, qulonglong count) {
    if (!container) return;
    qulonglong end = container->data(0, kEndRole).toULongLong();
    loadChildren(container, end, count);
}

void DebugPanel::loadChildren(QTreeWidgetItem* container, qulonglong from, qulonglong count) {
    QString path = container->data(0, kPathRole).toString();
    qulonglong total = container->data(0, kTotalRole).toULongLong();
    if (path.isEmpty() || count == 0 || from >= total) return;
    count = std::min<qulonglong>(count, std::min<qulonglong>(kMaxLoad, total - from));
    std::vector<fg::EvalResult> results;
    QString expr = QString("%1[%2..%3]").arg(path).arg(from).arg(from + count - 1);
    if (!run_->evaluate({ss(expr)}, {}, results) || results.size() != 1) return;
    const bool wasUpdating = updating_;
    updating_ = true;
    if (!results[0].ok) {
        if (auto* more = findMoreRow(container)) more->setText(1, "<" + qs(results[0].error) + ">");
    } else {
        const fg::ValueView& v = results[0].view;
        const QString prefix = keyPrefixOf(container);
        delete findMoreRow(container);
        for (auto& c : v.children) buildItem(new QTreeWidgetItem(container), c, prefix);
        container->setData(0, kTotalRole, qulonglong(v.total));
        qulonglong end = from + v.children.size();
        container->setData(0, kEndRole, end);
        loaded_[container->data(0, kKeyRole).toString()] = end;
        updateMoreRow(container);
    }
    updating_ = wasUpdating;
}

void DebugPanel::addWatch(const QString& expression) {
    QString e = expression.trimmed();
    if (e.isEmpty() || watchList_.contains(e)) return;
    watchList_.push_back(e);
    saveWatches();
    refreshWatches();
}

void DebugPanel::removeWatch(const QString& expression) {
    if (watchList_.removeAll(expression) == 0) return;
    seenWatches_.remove(expression);
    saveWatches();
    refreshWatches();
}

void DebugPanel::showContextMenu(QTreeWidget* tree, const QPoint& pos) {
    QTreeWidgetItem* item = tree->itemAt(pos);
    if (!item) return;
    QTreeWidgetItem* container = isMoreRow(item) ? item->parent() : item;
    QString path = container->data(0, kPathRole).toString();
    qulonglong total = container->data(0, kTotalRole).toULongLong();
    qulonglong end = container->data(0, kEndRole).toULongLong();
    QMenu menu(this);
    if (!path.isEmpty() && total > end) {
        for (qulonglong n : {qulonglong(kPage), qulonglong(1000), qulonglong(10000)})
            menu.addAction(themedIcon(":/Icons/load-more.svg"), tr("Load next %1").arg(thousands(n)), [this, container, n] { loadMore(container, n); });
        qulonglong rest = total - end;
        menu.addAction(themedIcon(":/Icons/load-more.svg"),
                       rest > kMaxLoad ? tr("Load next %1 (of %2 left)").arg(thousands(kMaxLoad), thousands(rest)) : tr("Load all (%1 left)").arg(thousands(rest)),
                       [this, container] { loadMore(container, kMaxLoad); });
        menu.addSeparator();
    }
    if (!path.isEmpty()) {
        if (total > 0)
            menu.addAction(themedIcon(":/Icons/watch-range.svg"), tr("Watch a range..."), [this, path, total] {
                bool ok = false;
                QString e = QInputDialog::getText(this, tr("Watch a range"), tr("Expression (a range is inclusive):"), QLineEdit::Normal,
                                                  QString("%1[0..%2]").arg(path).arg(std::min<qulonglong>(total, 100) - 1), &ok);
                if (ok) addWatch(e);
            });
        menu.addAction(themedIcon(":/Icons/watch-add.svg"), tr("Add to watch"), [this, path] { addWatch(path); });
    }
    if (!isMoreRow(item) && !item->text(1).isEmpty())
        menu.addAction(themedIcon(":/Icons/copy.svg"), tr("Copy value"),[item] { QGuiApplication::clipboard()->setText(item->text(1)); });
    if (tree == watches_ && item->data(0, kWatchRole).toBool()) {
        menu.addSeparator();
        QString expr = item->text(0);
        menu.addAction(themedIcon(":/Icons/watch-edit.svg"), tr("Edit watch..."), [this, expr] {
            bool ok = false;
            QString e = QInputDialog::getText(this, tr("Edit watch"), tr("Expression:"), QLineEdit::Normal, expr, &ok).trimmed();
            if (!ok || e.isEmpty() || e == expr) return;
            int at = watchList_.indexOf(expr);
            if (at < 0 || watchList_.contains(e)) return;
            watchList_[at] = e;
            seenWatches_.remove(expr);
            saveWatches();
            refreshWatches();
        });
        menu.addAction(themedIcon(":/Icons/watch-remove.svg"), tr("Remove watch"), [this, expr] { removeWatch(expr); });
        menu.addAction(themedIcon(":/Icons/watch-clear.svg"), tr("Remove all watches"), [this] {
            watchList_.clear();
            saveWatches();
            refreshWatches();
        });
    }
    if (menu.isEmpty()) return;
    menu.exec(tree->viewport()->mapToGlobal(pos));
}

// The watches belong to the script file: they are kept in the user's settings under its path.
QString DebugPanel::settingsKey() const { return "debugger/watches/" + QString::fromLatin1(doc_->path().toUtf8().toBase64(QByteArray::Base64UrlEncoding)); }

void DebugPanel::saveWatches() {
    if (watchesFor_.isEmpty()) return;                     // an unsaved script: the watches live as long as the window
    QSettings settings = editorSettings();
    if (watchList_.isEmpty()) settings.remove(settingsKey());          // no watches: leave nothing behind
    else settings.setValue(settingsKey(), watchList_);
}

// Another script: its own watches (those stored for its file), and nothing that was opened, loaded or shown for the previous one.
void DebugPanel::onScriptOpened() {
    expanded_.clear();
    loaded_.clear();
    seenWatches_.clear();
    watchesFor_ = doc_->path();
    watchList_.clear();
    if (!watchesFor_.isEmpty()) watchList_ = editorSettings().value(settingsKey()).toStringList();
    refresh();
}

void DebugPanel::onDocumentPath() {
    QString p = doc_->path();
    if (p == watchesFor_) return;
    if (watchesFor_.isEmpty() && !watchList_.isEmpty()) {  // "Save As" of a new script: its watches move to the file
        watchesFor_ = p;
        saveWatches();
        return;
    }
    watchesFor_ = p;
    watchList_.clear();
    seenWatches_.clear();
    if (!p.isEmpty()) watchList_ = editorSettings().value(settingsKey()).toStringList();
    refreshWatches();
}

} // namespace fged
