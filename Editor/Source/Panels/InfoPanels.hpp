#pragma once
#include "../Document.hpp"
#include "../Studio/RunController.hpp"

#include <QHash>
#include <QPlainTextEdit>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace fged {

// "Compile Results": errors and warnings of the last compile.
class DiagnosticsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(Document* doc, QWidget* parent = nullptr);
signals:
    void activated(const QString& graph, int node);
    void summaryChanged(int errors, int warnings);
private:
    void refresh();
    Document* doc_;
    QLabel* summary_;
    QTreeWidget* tree_;
};

// Output of Print String nodes, run messages and editor notifications.
class LogPanel : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);
    void append(int level, const QString& text);   // level: fg::LogLevel
    // The right click menu: the standard entries of a read-only text (Copy, Select All) and "Clear". The caller owns the menu.
    QMenu* buildContextMenu();
protected:
    void contextMenuEvent(QContextMenuEvent* e) override;
};

// Call stack, variables and watches of the running / paused task.
// Containers only show their first elements; a "N more" row (double-click, or the context menu) loads the next ones, and the
// Watch area evaluates expressions such as `Items[500000..500099]` or `Orders[12].Customer` against the script's state, so a
// million element array is never loaded as a whole. Expansion, loaded elements and the watch list survive every refresh.
class DebugPanel : public QWidget {
    Q_OBJECT
public:
    DebugPanel(Document* doc, RunController* run, QWidget* parent = nullptr);
    void addWatch(const QString& expression);
    void removeWatch(const QString& expression);
    const QStringList& watchExpressions() const { return watchList_; }
signals:
    void locationActivated(const QString& graph, int node);
private:
    void refresh();
    void refreshWatches();
    void restore(QTreeWidgetItem* item);
    void loadMore(QTreeWidgetItem* container, qulonglong count);
    void loadChildren(QTreeWidgetItem* container, qulonglong from, qulonglong count);
    void showContextMenu(QTreeWidget* tree, const QPoint& pos);
    QString settingsKey() const;
    void saveWatches();
    void onDocumentPath();
    void onScriptOpened();

    Document* doc_;
    RunController* run_;
    QTreeWidget* stack_;
    QTreeWidget* vars_;
    QTreeWidget* watches_;
    QLineEdit* watchEdit_;
    QStringList watchList_;
    QSet<QString> seenWatches_;           // watches that were shown once (a new one opens its elements)
    QSet<QString> expanded_;              // keys of the items the user opened
    QHash<QString, qulonglong> loaded_;   // key of a container -> index after the last child that was loaded
    QString watchesFor_;                  // the script path the watch list belongs to
    bool updating_ = false;
};

} // namespace fged
