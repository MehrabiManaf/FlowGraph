// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// What is selected, its properties edited: a variable (its name, type, default, category, description), a function or an
// event graph (its name, description, and a function's inputs and outputs), a struct (its fields, and the default and
// description of the one chosen), or a node (its settings, type arguments, pins and comment; the entry or return node of a
// function, and a call of one, show that function first). A search field above shows only the rows that have the words
// typed. Every change is one undoable step of the document (core::PropertyEdits, core::ScriptEdits).
//
// Inputs, outputs, fields and a node's own pins are tables: a name renamed by a double click or F2, its type, container
// (single, array, map) and a map's key chosen from lists, by reference and const checked; the + of the section adds one,
// the right button moves or removes it.
#include <FlowGraphEditorCore/Document.hpp>
#include <FlowGraphEditorCore/PropertyEdits.hpp>
#include <FlowGraphEditorCore/ScriptEdits.hpp>
#include <GLWidgets/Widgets/Localization.hpp>
#include <GLWidgets/Widgets/Widget.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace glw {
class CollapsiblePanel;
class GridLayout;
class LineEdit;
class SearchField;
class ScrollArea;
} // namespace glw

namespace fged::gl {

class DetailsPanel final : public glw::Widget {
public:
    enum class Mode { None, Variable, Graph, Struct, Nodes };

    explicit DetailsPanel(core::Document& doc);
    ~DetailsPanel() override;

    void showNothing();
    void showVariable(const std::string& owner, const std::string& name);      // `owner` empty: the script's
    void showGraph(const std::string& name);
    void showStruct(const std::string& name);
    void showNodes(const std::string& graph, std::vector<fg::NodeId> nodes);
    Mode mode() const { return mode_; }

    // The sections (Inputs, Outputs, Fields) the user closed: each opens as it was left, whatever is shown.
    void setClosedSections(std::set<std::string> keys) { closed_ = std::move(keys); }
    void setOnSectionsChanged(std::function<void(const std::set<std::string>&)> handler) { onSectionsChanged_ = std::move(handler); }
    void setOnMessage(std::function<void(glw::LocalizedText)> handler) { onMessage_ = std::move(handler); }
    void setOnGraphRenamed(std::function<void(const std::string&, const std::string&)> handler) { onGraphRenamed_ = std::move(handler); }

    // A field shown, by what it edits ("name", "type", "container", "key", "default", "category", "description", "comment",
    // "inputs", "outputs", "fields", "nodeInputs", "nodeOutputs", "setting:<key>", "typeArg:<name>", "field"): none when it is not.
    glw::Widget* field(const std::string& id) const;
    // Made again as the document is now (done by itself, after the event that changed it).
    void rebuild();

    // ---- what its tables pass on ----

    struct Table;
    void tableChanged(Table& table);                                            // a cell edited: the list applied
    void tableMenu(Table& table, std::size_t row, glw::Point at);
    void tableSelected(Table& table, std::size_t row);

protected:
    void onRootChanged() override;

private:
    struct Row {
        glw::Widget* label = nullptr;
        glw::Widget* field = nullptr;
        std::function<std::string()> text;      // what the search looks in
        bool heading = false;
    };

    void scheduleRebuild();
    std::string signature() const;              // what is shown: made again when it changes
    bool shownExists() const;
    void applyFilter();

    // ---- the form ----
    void heading(glw::LocalizedText text);
    void note(glw::LocalizedText text, std::optional<glw::Color> color = std::nullopt);
    void addRow(glw::LocalizedText label, std::unique_ptr<glw::Widget> field, const std::string& id, std::function<std::string()> text = {});
    void addWide(std::unique_ptr<glw::Widget> widget, const std::string& id, std::function<std::string()> text = {});
    // A field of text: `done` with what was typed when Enter is pressed or it loses the focus (and it changed).
    std::unique_ptr<glw::LineEdit> textField(const std::string& text, std::function<void(const std::string&)> done);
    // A default's editor (a primitive's, an enum's); a note when the type has none.
    void addDefault(const fg::TypeDesc* type, const fg::Value& stored, std::function<void(const fg::Value&)> set);
    glw::CollapsiblePanel& section(glw::LocalizedText title, const std::string& key, std::function<void()> add);
    Table& addTable(glw::CollapsiblePanel* in, const std::string& id);

    void buildVariable();
    void buildGraph(const std::string& graph);
    void buildStruct();
    void buildNodes();
    void buildNodePins(const fg::Node& node, const fg::CompiledNode& cn);

    std::string words(const glw::LocalizedText& text) const;
    glw::Color typeColor(const std::string& type) const;
    void tell(glw::LocalizedText text) const;
    void tellListProblem(core::ListProblem problem);
    void edited(bool rebuildAfter);             // after one of its edits

    core::Document& doc_;
    core::PropertyEdits props_;
    core::ScriptEdits script_;
    Mode mode_ = Mode::None;
    std::string owner_, name_;                  // a local's function, a node's graph; the variable, graph, struct
    std::vector<fg::NodeId> nodes_;
    std::size_t fieldRow_ = 0;                  // the struct's field shown below its table
    glw::SearchField* search_ = nullptr;
    glw::ScrollArea* scroll_ = nullptr;
    glw::Widget* body_ = nullptr;
    glw::GridLayout* grid_ = nullptr;
    glw::Widget* noMatch_ = nullptr;
    std::vector<Row> rows_;
    std::map<std::string, glw::Widget*> fields_;
    std::vector<std::unique_ptr<Table>> tables_;
    std::vector<std::shared_ptr<std::vector<std::string>>> typeLists_;     // what the tables' types are chosen from
    std::set<std::string> closed_;
    std::set<std::string> opening_;             // closed sections whose + was pressed: made closed, then opened animated
    std::vector<glw::CollapsiblePanel*> toOpen_;
    std::string shown_;
    unsigned generation_ = 0;                   // the form's: a field's late callback of a form made again is ignored
    bool rebuildQueued_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);     // what was posted for later: not when it is gone
    bool editing_ = false;                      // its own edit: the form stays (its field has what it shows)
    int changedConnection_ = 0, replacedConnection_ = 0, compiledConnection_ = 0;

    std::function<void(const std::set<std::string>&)> onSectionsChanged_;
    std::function<void(glw::LocalizedText)> onMessage_;
    std::function<void(const std::string&, const std::string&)> onGraphRenamed_;
};

} // namespace fged::gl
