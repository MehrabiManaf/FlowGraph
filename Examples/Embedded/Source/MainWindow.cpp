// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QIcon>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>

namespace msgapp {

MainWindow::MainWindow(fg::Runtime& rt, MessageStore& store, QWidget* parent) : QMainWindow(parent), rt_(rt), store_(store) {
    setWindowTitle("Message Tables - FlowGraph embedded example");
    resize(1200, 700);
    setDockOptions(AllowTabbedDocks | AllowNestedDocks | AnimatedDocks);
    hub_ = new ScriptEditorHub(rt_, store_, this);       // the application's only script editor (made when it is first needed)

    // the messages themselves are not shown: only how many there are (what the scripts make of them is in the tables)
    count_ = new QLabel(this);
    count_->setAlignment(Qt::AlignCenter);
    QFont big = count_->font();
    big.setPointSize(big.pointSize() + 14);
    count_->setFont(big);
    setCentralWidget(count_);

    QToolBar* bar = addToolBar("Main");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QAction* table = bar->addAction(fged::themedIcon(":/Icons/add.svg"), "New Table", this, [this] { newTable(); });
    table->setShortcut(QKeySequence::New);
    table->setToolTip("Adds a table. Its script decides which columns it has and what goes into its rows.");
    // the source of the messages can be stopped, e.g. to look at a table, or while a script is stopped at a breakpoint
    QAction* source = bar->addAction(fged::themedIcon(":/Icons/interrupt.svg"), "Pause Messages");
    source->setCheckable(true);
    source->setToolTip("Stops the source of the messages (a random message, two per second); the scripts get no new messages until you go on");
    connect(source, &QAction::toggled, this, [this, source](bool paused) {
        if (paused) store_.stopGenerating();
        else store_.startGenerating();
        source->setText(paused ? "Resume Messages" : "Pause Messages");
        source->setIcon(fged::themedIcon(paused ? ":/Icons/continue.svg" : ":/Icons/interrupt.svg"));
        showCount();
    });

    QMenu* file = menuBar()->addMenu("&File");
    file->addAction(table);
    file->addAction(source);
    file->addSeparator();
    file->addAction("E&xit", this, &QWidget::close);

    connect(&store_, &MessageStore::messageAdded, this, [this] { showCount(); });
    connect(&store_, &MessageStore::cleared, this, [this] { showCount(); });
    showCount();
}

void MainWindow::showCount() {
    count_->setText(QString("%1 messages received\n%2").arg(store_.total())
                        .arg(store_.generating() ? "(a random one, two per second)" : "(paused)"));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!hub_->closeEditor()) {                          // the editor window is a window of its own: it has to go too, and may refuse
        e->ignore();
        return;
    }
    store_.stopGenerating();
    QMainWindow::closeEvent(e);
}

TableDock* MainWindow::newTable() {
    auto* dock = new TableDock(nextTable_++, rt_, store_, *hub_, this);
    const QList<TableDock*> existing = tables();
    if (existing.size() > 1) tabifyDockWidget(existing.first() == dock ? existing[1] : existing.first(), dock);   // tables share the area as tabs
    else addDockWidget(Qt::RightDockWidgetArea, dock);
    if (existing.size() == 1) resizeDocks({dock}, {560}, Qt::Horizontal);                // the first table gets room for its columns
    dock->show();
    dock->raise();
    return dock;
}

QList<TableDock*> MainWindow::tables() const { return findChildren<TableDock*>(QString(), Qt::FindDirectChildrenOnly); }

} // namespace msgapp
