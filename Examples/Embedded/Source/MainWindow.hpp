#pragma once
// The main window of the embedded example: how many messages have been received (in the middle), and a dock widget for every table the user adds.
#include "MessageStore.hpp"
#include "ScriptEditorHub.hpp"
#include "TableDock.hpp"

#include <QMainWindow>

class QLabel;

namespace msgapp {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(fg::Runtime& rt, MessageStore& store, QWidget* parent = nullptr);

    TableDock* newTable();                       // what the "New Table" button does
    QList<TableDock*> tables() const;
    ScriptEditorHub& editorHub() { return *hub_; }     // the one script editor of the application

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void showCount();

    fg::Runtime& rt_;
    MessageStore& store_;
    ScriptEditorHub* hub_ = nullptr;             // owned by the window
    QLabel* count_ = nullptr;                    // the number of messages, big
    int nextTable_ = 1;
};

} // namespace msgapp
