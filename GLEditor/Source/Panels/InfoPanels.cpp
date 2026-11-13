// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Panels/InfoPanels.hpp"

#include "NodeTexts.hpp"

#include <FlowGraph/Program.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/ItemModel.hpp>
#include <GLWidgets/Widgets/Label.hpp>
#include <GLWidgets/Widgets/Layout.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/SearchField.hpp>
#include <GLWidgets/Widgets/TableView.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <cmath>
#include <optional>
#include <vector>

namespace fged::gl {

using glw::tr;

namespace {

constexpr float rowHeight = 24;
const glw::Color errorColor{0.92f, 0.36f, 0.36f, 1};
const glw::Color warningColor{0.95f, 0.70f, 0.30f, 1};
const glw::Color successColor{0.35f, 0.80f, 0.50f, 1};

} // namespace

glw::LocalizedText graphTitle(const std::string& graph) {
    return graph == "EventGraph" ? tr("EventGraph") : glw::LocalizedText(graph);
}

// A row: where (a graph, a node) and what, in three columns; its last in a color (an error's, a warning's) or none.
struct ResultRow {
    std::string graph;
    fg::NodeId node = 0;
    glw::LocalizedText columns[3];
    std::optional<glw::Color> color;
    fg::ScriptRef::Declaration declaration = fg::ScriptRef::Declaration::None;
    std::string name;                       // a declaration's
};

class ResultModel final : public glw::TableModel {
public:
    ResultModel(glw::LocalizedText a, glw::LocalizedText b, glw::LocalizedText c) : headers_{std::move(a), std::move(b), std::move(c)} {}
    std::vector<ResultRow> rows;

    void set(std::vector<ResultRow> r) {
        rows = std::move(r);
        notifyReset();
    }
    std::size_t rowCount() const override { return rows.size(); }
    std::size_t columnCount() const override { return 3; }
    glw::LocalizedText header(std::size_t column) const override { return headers_[column]; }

private:
    glw::LocalizedText headers_[3];
};

namespace {

// A cell's text, cut at its end; its tooltip the whole text (a long message).
class ResultCell final : public glw::Widget {
public:
    ResultCell() { setHitTestVisible(false); }
    void show(glw::LocalizedText text, std::optional<glw::Color> color) {
        text_ = std::move(text);
        color_ = color;
        layout_.reset();
        invalidateLayout();
        invalidatePaint();
    }
    const glw::LocalizedText& text() const { return text_; }

protected:
    glw::Size measureContent(glw::Size) override {
        const glw::UiContext* ui = context();
        if (!ui)
            return {60, rowHeight};
        const glw::text::TextLayout t(ui->translator.resolve(text_), glw::text::TextStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize});
        return {std::ceil(t.size().width) + 16, rowHeight};
    }
    void paint(glw::PaintContext& context) override {
        if (!layout_)
            layout_.emplace(context.ui.translator.resolve(text_), glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, context.ui.theme.fontSize});
        context.painter.pushClip({6, 0, std::max(0.0f, size().width - 10), size().height});
        context.drawText(*layout_, {6, (size().height - layout_->size().height) / 2 + layout_->opticalOffset()},
                         color_ ? *color_ : context.ui.theme.palette.text);
        context.painter.popClip();
    }

private:
    glw::LocalizedText text_;
    std::optional<glw::Color> color_;
    std::optional<glw::text::TextLayout> layout_;
};

class ResultDelegate final : public glw::TableDelegate {
public:
    explicit ResultDelegate(ResultModel& model) : model_(model) {}
    std::unique_ptr<glw::Widget> makeCell(std::size_t) override { return std::make_unique<ResultCell>(); }
    void bindCell(glw::Widget& cell, std::size_t row, std::size_t column) override {
        if (row >= model_.rows.size())
            return;
        const ResultRow& r = model_.rows[row];
        static_cast<ResultCell&>(cell).show(r.columns[column], column == 2 ? r.color : std::nullopt);
    }

private:
    ResultModel& model_;
};

// The table of a panel: its rows' whole texts in their tooltips.
class ResultTable final : public glw::TableView {
public:
    explicit ResultTable(std::shared_ptr<ResultModel> model) : model_(std::move(model)) {
        setModel(model_);
        setDelegate(std::make_unique<ResultDelegate>(*model_));
        setRowHeight(rowHeight);
        setSelectionMode(glw::SelectionMode::Single);
        setThemeStyle(&glw::Theme::itemView);       // as the theme's lists are (no cells' borders), as it changes
        setColumnSizing(0, glw::ColumnSizing::FitContent);
        setColumnSizing(1, glw::ColumnSizing::FitContent);
        setColumnSizing(2, glw::ColumnSizing::Stretch);
        setStretch(1);
    }

    std::optional<glw::ToolTip> toolTipAt(glw::Point point) const override {
        const std::optional<std::size_t> row = list().itemAt(list().mapFromRoot(mapToRoot(point)));
        if (!row || *row >= model_->rows.size())
            return std::nullopt;
        const glw::Point at = mapToRoot(point);
        for (std::size_t column = 0; column < 3; ++column)
            if (const glw::Widget* cell = cellWidget(*row, column)) {
                const glw::Point in = cell->mapFromRoot(at);
                if (in.x >= 0 && in.y >= 0 && in.x < cell->size().width && in.y < cell->size().height)
                    return glw::ToolTip{.text = model_->rows[*row].columns[column]};
            }
        return std::nullopt;
    }

private:
    std::shared_ptr<ResultModel> model_;
};

std::unique_ptr<glw::Label> summaryLabel() {
    auto label = std::make_unique<glw::Label>(glw::Label::Props{.muted = true});
    label->setMargin({2, 0, 2, 0});
    return label;
}

} // namespace

// ---- Compile Results

CompileResultsPanel::CompileResultsPanel(core::Document& doc) : doc_(doc) {
    setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 4.0f));
    setPadding(glw::Insets::all(6));
    summary_ = &add(summaryLabel());
    model_ = std::make_shared<ResultModel>(tr("Graph"), tr("Node"), tr("Message"));
    table_ = &add(std::make_unique<ResultTable>(model_));
    table_->setOnActivated([this](std::size_t row) {
        if (row < model_->rows.size() && model_->rows[row].node != 0 && onActivated_)
            onActivated_(model_->rows[row].graph, model_->rows[row].node);
    });
    compiledConnection_ = doc_.compiled.connect([this] { refresh(); });
    refresh();
}

CompileResultsPanel::~CompileResultsPanel() {
    doc_.compiled.disconnect(compiledConnection_);
}

std::size_t CompileResultsPanel::rowCount() const {
    return model_->rows.size();
}

void CompileResultsPanel::refresh() {
    std::vector<ResultRow> rows;
    errors_ = warnings_ = 0;
    if (const std::shared_ptr<const fg::Program> program = doc_.program())
        for (const fg::Diagnostic& d : program->diagnostics) {
            const bool error = d.severity == fg::Diagnostic::Severity::Error;
            ++(error ? errors_ : warnings_);
            const fg::CompiledGraph* cg = program->findGraph(d.graph);
            const fg::CompiledNode* cn = cg ? cg->findNode(d.node) : nullptr;
            rows.push_back({.graph = d.graph,
                            .node = d.node,
                            .columns = {graphTitle(d.graph), glw::LocalizedText(cn ? nodeTitle(*cn) : std::string()), glw::LocalizedText(d.message)},
                            .color = error ? errorColor : warningColor});
        }
    model_->set(std::move(rows));
    if (errors_ == 0 && warnings_ == 0) {
        summary_->setText(tr("Compiled successfully."));
        summary_->setColor(successColor);
    } else {
        summary_->setText(tr("{errors} error(s), {warnings} warning(s)").arg("errors", errors_).arg("warnings", warnings_));
        summary_->setColor(errors_ > 0 ? errorColor : warningColor);
    }
    if (onSummaryChanged_)
        onSummaryChanged_(errors_, warnings_);
}

// ---- Find Results

FindPanel::FindPanel(core::Document& doc) : doc_(doc) {
    setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 4.0f));
    setPadding(glw::Insets::all(6));
    field_ = &add(std::make_unique<glw::SearchField>(glw::SearchField::Props{
        .placeholder = tr("Find in script (node titles, comments, values)"),
        .onChanged = [this](const std::string& text) { search(text); },
        .onSubmit = [this](const std::string&) {
            if (!model_->rows.empty())
                activate(0);
        }}));
    summary_ = &add(summaryLabel());
    model_ = std::make_shared<ResultModel>(tr("Graph"), tr("Node"), tr("Where"));
    table_ = &add(std::make_unique<ResultTable>(model_));
    table_->setOnActivated([this](std::size_t row) { activate(row); });
    compiledConnection_ = doc_.compiled.connect([this] { refresh(); });
    refresh();
}

FindPanel::~FindPanel() {
    doc_.compiled.disconnect(compiledConnection_);
}

std::size_t FindPanel::resultCount() const {
    return model_->rows.size();
}

void FindPanel::findReferences(const fg::RefTarget& target) {
    references_ = true;
    target_ = target;
    field_->setText("");
    refresh();
}

void FindPanel::search(const std::string& text) {
    references_ = false;
    if (field_->text() != text)
        field_->setText(text);
    refresh();
}

void FindPanel::focusSearch() {
    if (glw::UiRoot* r = root())
        r->setFocus(&field_->field());
    field_->field().selectAll();
}

void FindPanel::refresh() {
    const std::shared_ptr<const fg::Program> program = doc_.program();
    std::vector<fg::ScriptRef> refs;
    glw::LocalizedText what;
    if (references_) {
        refs = fg::findReferences(doc_.script(), program.get(), target_);
        what = tr("References of {what}").arg("what", target_.describe());
    } else if (!field_->text().empty()) {
        refs = fg::searchScript(doc_.script(), program.get(), field_->text());
        what = tr("\"{text}\"").arg("text", field_->text());
    }
    std::vector<ResultRow> rows;
    for (const fg::ScriptRef& r : refs) {
        const fg::CompiledGraph* cg = program ? program->findGraph(r.graph) : nullptr;
        const fg::CompiledNode* cn = cg ? cg->findNode(r.node) : nullptr;
        rows.push_back({.graph = r.graph,
                        .node = r.node,
                        .columns = {r.graph.empty() ? glw::LocalizedText() : graphTitle(r.graph),
                                    glw::LocalizedText(cn ? nodeTitle(*cn) : r.title),      // the node's title in the editor's language
                                    glw::LocalizedText(r.detail)},
                        .declaration = r.declaration,
                        .name = r.title});
    }
    model_->set(std::move(rows));
    const glw::UiContext* ui = context();
    const std::string shown = ui ? ui->translator.resolve(what) : what.source;     // (in the summary's text)
    if (what.source.empty())
        summary_->setText(tr("Type to search, or use Find References on a node, variable, function or struct."));
    else if (refs.empty())
        summary_->setText(tr("{what}: nothing found").arg("what", shown));
    else
        summary_->setText(tr("{what}: {count} found").arg("what", shown).arg("count", refs.size()));
}

void FindPanel::activate(std::size_t row) {
    if (row >= model_->rows.size())
        return;
    const ResultRow& r = model_->rows[row];
    if (r.declaration != fg::ScriptRef::Declaration::None) {
        if (onDeclaration_)
            onDeclaration_(r.declaration, r.graph, r.name);
    } else if (onActivated_) {
        onActivated_(r.graph, r.node);
    }
}

} // namespace fged::gl
