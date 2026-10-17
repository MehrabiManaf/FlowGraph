#pragma once
// One table of the application: a dock widget with a toolbar (load / edit / save the script of the table), the table itself and a status
// line. A table owns a script (an fg::Script in memory; it starts as the default script and can be loaded from a .flow file) and runs it for
// the messages: first for the ones the store already has, then for every new one as it arrives. It is ONE run (one instance of the script), so
// the variables of the script keep their values from message to message. The run starts again, with fresh variables, when
//   - the table is created,
//   - a script is loaded into the table, and
//   - the script editor saves: Save in the editor hands the edited script to the table (the application's script editor is a
//     fged::EditorMode::Embedded one), which replaces its script and runs the new one.
// The script runs on an fg::AsyncExecutor (a worker thread); a timer brings what it produced into the table. The editor is not part of the
// table: the application has one script editor (see ScriptEditorHub) that edits the script of the table whose "Edit Script" was pressed last.
#include "MessageStore.hpp"

#include <QDockWidget>
#include <QPointer>
#include <QTimer>

#include <map>
#include <memory>

class QLabel;
class QTableWidget;

namespace msgapp {

class ScriptEditorHub;

inline constexpr int kMaxRows = 5000;           // a table shows the latest rows only

class TableDock : public QDockWidget {
    Q_OBJECT
public:
    TableDock(int number, fg::Runtime& rt, MessageStore& store, ScriptEditorHub& hub, QWidget* parent = nullptr);
    ~TableDock() override;

    int number() const { return number_; }

    // The toolbar of the table (they can be called from code as well)
    void loadScript();                          // asks for a .flow file
    // Loads the file as the script of the table and runs it. Only a script that can be used replaces the script of the table (see
    // whyNotUsable): otherwise nothing changes, false is returned and `reason` (if given) says why.
    bool openScript(const QString& path, QString* reason = nullptr);
    void editScript();                          // the script editor edits the script of this table
    bool saveScript();                          // writes the script of the table to its file (asks for one the first time)
    void restart();                             // the script runs again from the start: fresh variables, the messages of the store, then the new ones

    // The script of the table. Setting it restarts the run (this is what Save of the script editor does).
    const fg::Script& script() const { return script_; }
    void setScript(const fg::Script& script);
    QString scriptPath() const { return scriptPath_; }          // the file the script was loaded from / saved to; empty for the default script
    QString scriptTitle() const;                                // for the title of the editor

    // The executor of the run (an editor can attach its debugger to it); null when the script does not run (it could not be used).
    std::shared_ptr<fg::AsyncExecutor> executor() const;

    bool busy() const { return run_ && run_->pending() > 0; }   // messages are waiting for their handler (or stopped by a debugger)
    int processed() const { return processed_; }                // messages the current run has handled
    QTableWidget* table() const { return table_; }
    QString statusText() const;
    QStringList scriptLog() const { return log_; }              // what Print String wrote (the latest lines)

signals:
    void updated();                             // the table got new rows, or the run started again

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void stopRun();
    void showFailure(const QString& text);
    void poll();                                // brings what the script produced into the table
    void appendRows(const std::vector<Row>& rows);
    void setColumns(const std::vector<std::string>& columns);
    void showStatus();
    void updateTitle();

    int number_;
    fg::Runtime& rt_;
    MessageStore& store_;
    QPointer<ScriptEditorHub> hub_;             // the window owns the hub; it may be gone first when the application closes
    fg::Script script_;
    QString scriptPath_;
    bool changedSinceFile_ = false;             // the script was replaced (by the editor) after it was loaded from / saved to its file
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* scriptLabel_ = nullptr;
    QStringList log_;

    std::unique_ptr<ScriptRun> run_;            // the running script; null when it cannot run
    QTimer pollTimer_;
    std::vector<std::string> columns_;
    std::map<std::string, int> columnIndex_;
    int processed_ = 0, failed_ = 0, rows_ = 0;
    QString firstError_;
};

} // namespace msgapp
