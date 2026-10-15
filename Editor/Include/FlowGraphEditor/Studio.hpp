#pragma once
// Public entry points of the FlowGraph editor library.
#include <FlowGraph/Runtime.hpp>

#include <QApplication>
#include <QWidget>
#include <QString>

namespace fged {

// Applies the dark palette / Fusion style used by FlowGraph Studio.
void applyDarkTheme(QApplication& app);
// Initialises KDDockWidgets with the Studio's view factory. Call once after creating the QApplication.
void initEditor();
// Creates and shows the main window; the runtime must outlive it. Returns the window (owned by the caller).
QWidget* createStudio(fg::Runtime& rt, const QString& openFile = QString());

// Developer aid (used by the screenshot / smoke test hooks): runs `entry`, optionally with a breakpoint "graph:nodeId".
void devRun(QWidget* studio, const QString& entry, const QString& breakpoint = QString());

void devSelect(QWidget* studio, const QString& spec);

} // namespace fged
