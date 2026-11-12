// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The debugger: the call stack of the script paused, the variables (the parameters and locals of the frame chosen, and the
// script's) and watches - expressions such as `Items[500000..500099]` or `Orders[12].Customer`, evaluated against the
// script's state.
//
// A container shows its first elements; opened deeper than what was collected, its first ones are loaded then; a "N more"
// row (a double click, or the right button) loads the next. A million elements are never loaded at once. What was opened
// and loaded stays so through every step of the debugger. The watches belong to the script's file (setWatchStore).
#include <FlowGraphEditorCore/Document.hpp>
#include <FlowGraphEditorCore/RunSession.hpp>
#include <GLWidgets/Widgets/Widget.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace glw {
class LineEdit;
class TableView;
class TreeView;
} // namespace glw

namespace fged::gl {

class StackModel;
class ValueModel;

class DebugPanel final : public glw::Widget {
public:
    DebugPanel(core::Document& doc, core::RunSession& run);
    ~DebugPanel() override;

    void addWatch(const std::string& expression);
    void removeWatch(const std::string& expression);
    const std::vector<std::string>& watches() const { return watches_; }
    // Where the watches of a script's file are kept (the editor's settings): read when another file is opened, written as
    // they change. A script without a file keeps its watches as long as it is open (and takes them to the file it is saved
    // as).
    void setWatchStore(std::function<std::vector<std::string>(const std::string& path)> load,
                       std::function<void(const std::string& path, const std::vector<std::string>&)> save);
    // A frame of the stack chosen: its graph and node (shown by the editor).
    void setOnLocation(std::function<void(const std::string&, fg::NodeId)> handler) { onLocation_ = std::move(handler); }

    // Reads the run again (after a step, a pause, the end).
    void refresh();
    // What a variable (`watch`: a watch) named so shows as its value, wherever it is in its tree; none when it is not there.
    std::optional<std::string> valueOf(const std::string& name, bool watch = false) const;
    std::size_t stackDepth() const;

    glw::TreeView& variables() const { return *variables_; }
    glw::TreeView& watchTree() const { return *watchTree_; }
    glw::TableView& stack() const { return *stack_; }
    ValueModel& variableModel() const { return *variableModel_; }
    ValueModel& watchModel() const { return *watchModel_; }

    // ---- what its trees pass on ----

    // A container: `count` more of its elements loaded (after those that are), the trees made again.
    void loadMore(const std::string& key, std::size_t count);
    void menu(ValueModel& model, std::uint64_t node, glw::Point at, glw::Widget& from);

private:
    void refreshWatches();
    // The model's nodes the user opened (that show), remembered; then the model made again, with them opened again.
    void remember(ValueModel& model, glw::TreeView& tree);
    void reopen(ValueModel& model, glw::TreeView& tree);
    // A value as a node of `model` under `parent`, its children and what was loaded of them before.
    std::size_t append(ValueModel& model, std::optional<std::size_t> parent, const fg::ValueView& view, const std::string& prefix);
    void loadChildren(ValueModel& model, std::size_t item, std::size_t from, std::size_t count);
    void updateMore(ValueModel& model, std::size_t item);
    void scriptOpened();
    void pathChanged();
    void saveWatches();
    void editInField(const std::string& text);

    core::Document& doc_;
    core::RunSession& run_;
    glw::TableView* stack_ = nullptr;
    glw::TreeView* variables_ = nullptr;
    glw::TreeView* watchTree_ = nullptr;
    glw::LineEdit* watchField_ = nullptr;
    std::shared_ptr<StackModel> stackModel_;
    std::shared_ptr<ValueModel> variableModel_, watchModel_;
    std::vector<std::string> watches_;
    std::set<std::string> seenWatches_;                 // shown once (a new one opens its elements)
    std::set<std::string> expanded_;                    // the keys of what the user opened
    std::map<std::string, std::size_t> loaded_;         // a container's key: how many of its elements were loaded
    std::string watchesFor_;                            // the file the watches are of
    bool selecting_ = false;                            // the stack's current row set by it
    std::vector<std::function<void()>> disconnect_;     // from the document's and the run's signals
    std::function<std::vector<std::string>(const std::string&)> loadWatches_;
    std::function<void(const std::string&, const std::vector<std::string>&)> saveWatches_;
    std::function<void(const std::string&, fg::NodeId)> onLocation_;
};

} // namespace fged::gl
