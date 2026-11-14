// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// What the script has, in sections that open and close, each a table of two columns (a member's name, with its type's
// color, and what it is): the events its event graphs handle (the application's), its functions, its variables, the local
// variables of the function shown, its structs. A search field above them shows only what has the words typed.
//
//   a click selects a member (the Details panel shows it), the keys go up and down; a double click or Enter opens it (a
//   function's graph, an event's node in view); F2 renames it, in its cell;
//   the + of a section adds one (its name typed at once); the right button: open, rename, duplicate, delete;
//   a variable or a function dragged onto a graph makes a node there (a Get or a Set, a call).
// Every change is one undoable step of the document (core::ScriptEdits).
#include <FlowGraphEditorCore/Document.hpp>
#include <FlowGraphEditorCore/ScriptEdits.hpp>
#include <GLWidgets/Widgets/Localization.hpp>
#include <GLWidgets/Widgets/Widget.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace glw {
class CollapsiblePanel;
class LineEdit;
class SearchField;
class Button;
class TableView;
} // namespace glw

namespace fged::gl {

// What is dragged from the panel onto a graph (glw::DragData::format): its content the function's name, or
// "global|name" / "local|name" for a variable.
inline constexpr const char* functionDragFormat = "flowgraph/function";
inline constexpr const char* variableDragFormat = "flowgraph/variable";

class MyScriptPanel final : public glw::Widget {
public:
    explicit MyScriptPanel(core::Document& doc);
    ~MyScriptPanel() override;

    // The graph shown (its local variables listed when it is a function's).
    void setCurrentGraph(const std::string& graph);

    // What is selected in it: none when nothing is.
    struct Selection {
        core::Member member = core::Member::Function;
        std::string owner;              // a local's function
        std::string name;
    };
    const std::optional<Selection>& selection() const { return selected_; }
    void clearSelection();
    // The widget of a member's name (its cell), while it shows: where it is.
    glw::Widget* rowOf(core::Member member, const std::string& owner, const std::string& name) const;
    // The button beside the search that opens the menu of what can be added (tests).
    glw::Widget* addButton() const;

    // ---- what it asks of the editor ----

    void setOnOpenGraph(std::function<void(const std::string&)> handler) { onOpenGraph_ = std::move(handler); }
    // An event's handler: (its graph, its node) selected (`show`: and in view).
    void setOnEventChosen(std::function<void(const std::string&, fg::NodeId, bool show)> handler) { onEventChosen_ = std::move(handler); }
    void setOnSelectionChanged(std::function<void()> handler) { onSelectionChanged_ = std::move(handler); }
    void setOnMessage(std::function<void(glw::LocalizedText)> handler) { onMessage_ = std::move(handler); }
    // Find References chosen in a member's menu: what refers to it (an event: its handlers; `name` the event's).
    void setOnFindReferences(std::function<void(core::Member, const std::string& owner, const std::string& name)> handler) {
        onFindReferences_ = std::move(handler);
    }
    // Whether the focus is in it (Shift+F12 finds the references of what it has selected then).
    bool hasFocusInside() const;

    // ---- what its sections pass on ----

    struct Section;
    void selected(Section& section);                            // its table's current row changed
    void activated(Section& section, std::size_t row);          // a double click or Enter
    void menu(Section& section, std::size_t row, glw::Point at);
    void renamed(Section& section, std::size_t row, const std::string& to);

protected:
    void onRootChanged() override;

private:
    void rebuild();                     // the tables' rows made again, as the script is
    void scheduleRebuild();
    void applyFilter();
    void fit(Section& section);         // its table as tall as its rows that show
    void addMember(core::Member member);
    void showAddMenu(glw::Widget& button);     // what can be added, under the button beside the search
    void beginRename(core::Member member, const std::string& owner, const std::string& name);
    Section* sectionOf(core::Member member) const;
    std::optional<std::size_t> rowIn(const Section& section, const std::string& owner, const std::string& name) const;
    std::string words(const glw::LocalizedText& text) const;
    void tell(glw::LocalizedText text) const;
    std::string signature() const;      // what the tables show: made again when it changes

    core::Document& doc_;
    core::ScriptEdits edits_;
    std::string current_;
    glw::SearchField* search_ = nullptr;
    glw::Button* addButton_ = nullptr;          // beside the search: a menu of what can be added
    std::vector<std::unique_ptr<Section>> sections_;            // events, functions, variables, locals, structs
    std::optional<Selection> selected_;
    std::string shown_;                 // the signature of what the tables show
    bool rebuilt_ = false;              // once: its sections close and open eased from then on
    bool rebuildQueued_ = false;
    bool selecting_ = false;            // choosing in one table clears the others
    int changedConnection_ = 0, replacedConnection_ = 0;

    std::function<void(const std::string&)> onOpenGraph_;
    std::function<void(const std::string&, fg::NodeId, bool)> onEventChosen_;
    std::function<void()> onSelectionChanged_;
    std::function<void(glw::LocalizedText)> onMessage_;
    std::function<void(core::Member, const std::string&, const std::string&)> onFindReferences_;
};

} // namespace fged::gl
