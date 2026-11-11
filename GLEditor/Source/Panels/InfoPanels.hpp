// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The panels that tell about the script, beside the Output Log:
//
//   Compile Results - the errors and warnings of the last compile, with how many of each;
//   Find Results    - the nodes whose title, comment or values have the text typed (Find in Script), or that use a variable,
//                     call a function, are of a kind or work with a struct (Find References). They follow every edit.
//
// A row clicked twice, or Enter on it, shows where it is (its node, in its graph; a declaration in the Details panel).
#include <FlowGraph/Search.hpp>
#include <FlowGraphEditorCore/Document.hpp>
#include <GLWidgets/Widgets/Localization.hpp>
#include <GLWidgets/Widgets/Widget.hpp>

#include <functional>
#include <memory>
#include <string>

namespace glw {
class Label;
class LineEdit;
class TableView;
} // namespace glw

namespace fged::gl {

class ResultModel;

// The graph's name as the editor shows it (the event graph's in its language).
glw::LocalizedText graphTitle(const std::string& graph);

class CompileResultsPanel final : public glw::Widget {
public:
    explicit CompileResultsPanel(core::Document& doc);
    ~CompileResultsPanel() override;

    int errors() const { return errors_; }
    int warnings() const { return warnings_; }
    std::size_t rowCount() const;
    glw::TableView& table() const { return *table_; }

    // A row chosen: its graph and node.
    void setOnActivated(std::function<void(const std::string&, fg::NodeId)> handler) { onActivated_ = std::move(handler); }
    // After every compile: how many errors and warnings.
    void setOnSummaryChanged(std::function<void(int, int)> handler) { onSummaryChanged_ = std::move(handler); }
    void refresh();

private:
    core::Document& doc_;
    glw::Label* summary_ = nullptr;
    glw::TableView* table_ = nullptr;
    std::shared_ptr<ResultModel> model_;
    int errors_ = 0, warnings_ = 0;
    int compiledConnection_ = 0;
    std::function<void(const std::string&, fg::NodeId)> onActivated_;
    std::function<void(int, int)> onSummaryChanged_;
};

class FindPanel final : public glw::Widget {
public:
    explicit FindPanel(core::Document& doc);
    ~FindPanel() override;

    void findReferences(const fg::RefTarget& target);
    void search(const std::string& text);
    // The search field with the focus, its text selected.
    void focusSearch();
    std::size_t resultCount() const;
    glw::TableView& table() const { return *table_; }

    // A node chosen (0: a comment's graph).
    void setOnActivated(std::function<void(const std::string&, fg::NodeId)> handler) { onActivated_ = std::move(handler); }
    // A declaration that uses a struct: a variable (`graph` empty), a local, an input or output of the function `graph`, a
    // field ("Struct.Field").
    void setOnDeclaration(std::function<void(fg::ScriptRef::Declaration, const std::string&, const std::string&)> handler) {
        onDeclaration_ = std::move(handler);
    }
    void refresh();

private:
    void activate(std::size_t row);

    core::Document& doc_;
    glw::LineEdit* field_ = nullptr;
    glw::Label* summary_ = nullptr;
    glw::TableView* table_ = nullptr;
    std::shared_ptr<ResultModel> model_;
    bool references_ = false;               // the references of target_ (otherwise: the text searched for)
    fg::RefTarget target_;
    int compiledConnection_ = 0;
    std::function<void(const std::string&, fg::NodeId)> onActivated_;
    std::function<void(fg::ScriptRef::Declaration, const std::string&, const std::string&)> onDeclaration_;
};

} // namespace fged::gl
