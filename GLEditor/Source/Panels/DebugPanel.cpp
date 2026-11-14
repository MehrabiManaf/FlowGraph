// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Panels/DebugPanel.hpp"
#include "Panels/Elide.hpp"
#include "Icons.hpp"

#include "NodeTexts.hpp"
#include "Panels/InfoPanels.hpp"

#include <FlowGraph/Program.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/ItemModel.hpp>
#include <GLWidgets/Widgets/Label.hpp>
#include <GLWidgets/Widgets/Layout.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/Menu.hpp>
#include <GLWidgets/Widgets/Splitter.hpp>
#include <GLWidgets/Widgets/TableView.hpp>
#include <GLWidgets/Widgets/TreeView.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace fged::gl {

using glw::tr;

namespace {

constexpr float rowHeight = 22;
constexpr std::size_t firstPage = 64;       // the elements of a container loaded when it is opened (InspectOptions::maxItems)
constexpr std::size_t page = 256;           // what a "more" row loads
constexpr std::size_t maxLoad = 50000;      // the most one request loads
const glw::Color errorColor{0.92f, 0.36f, 0.36f, 1};

// 1234567 as 1,234,567.
std::string thousands(std::size_t n) {
    std::string digits = std::to_string(n), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return out;
}

} // namespace

// ---- the call stack

struct StackRow {
    std::string graph;
    fg::NodeId node = 0;
    std::string shown, nodeTitle;
};

class StackModel final : public glw::TableModel {
public:
    std::vector<StackRow> rows;
    void set(std::vector<StackRow> r) {
        rows = std::move(r);
        notifyReset();
    }
    std::size_t rowCount() const override { return rows.size(); }
    std::size_t columnCount() const override { return 2; }
    glw::LocalizedText header(std::size_t column) const override { return column == 0 ? tr("Call Stack") : tr("Node"); }
};

// ---- the values: the variables', the watches'

// A node of a value tree: a group (Locals, Variables), a value, a watch, the "more" row of a container.
struct ValueItem {
    enum class Kind { Group, Value, Watch, More };
    Kind kind = Kind::Value;
    std::string key;                    // what it is known by through the refreshes (its node's id comes from it)
    std::string prefix;                 // of the keys of its tree's values ("l:", "g:", "w:<expression>|")
    glw::LocalizedText name;
    std::string text, type;
    std::string path;                   // the expression that addresses it (none: it cannot be)
    std::size_t total = 0;              // its elements, entries or properties
    std::size_t end = 0;                // after the last of them loaded
    bool error = false, muted = false;
    std::optional<std::size_t> parent;
    std::vector<std::size_t> children;

    bool container() const { return (kind == Kind::Value || kind == Kind::Watch) && !path.empty() && total > 0; }
};

class ValueModel final : public glw::TreeModel {
public:
    explicit ValueModel(glw::LocalizedText first) : first_(std::move(first)) {}

    std::vector<ValueItem> items;
    std::vector<std::size_t> top;
    // A container opened that was collected without its elements: they are loaded now (by the panel).
    std::function<void(std::size_t)> loadFirst;

    static std::uint64_t idOf(const std::string& key) { return std::hash<std::string>{}(key) | 1; }
    glw::TreeNode node(std::size_t item) const { return {idOf(items[item].key)}; }
    std::optional<std::size_t> itemOf(glw::TreeNode node) const {
        const auto it = byId_.find(node.id);
        return it == byId_.end() ? std::nullopt : std::optional<std::size_t>(it->second);
    }
    std::optional<std::size_t> itemOf(const std::string& key) const { return itemOf(glw::TreeNode{idOf(key)}); }

    void clear() {
        items.clear();
        top.clear();
        byId_.clear();
    }
    std::size_t add(ValueItem item) {
        const std::size_t index = items.size();
        // a key two items share (two values that cannot be addressed, alike): the second made another
        while (byId_.contains(idOf(item.key)))
            item.key += "'";
        byId_[idOf(item.key)] = index;
        if (item.parent)
            items[*item.parent].children.push_back(index);
        else
            top.push_back(index);
        items.push_back(std::move(item));
        return index;
    }
    void removeChild(std::size_t parent, std::size_t child) {
        std::erase(items[parent].children, child);
        byId_.erase(idOf(items[child].key));
    }
    void reset() { notifyReset(); }

    std::size_t childCount(std::optional<glw::TreeNode> parent) const override {
        if (!parent)
            return top.size();
        const std::optional<std::size_t> item = itemOf(*parent);
        if (!item)
            return 0;
        const ValueItem& it = items[*item];
        if (it.container() && it.children.empty() && it.end == 0 && loadFirst)
            loadFirst(*item);
        return items[*item].children.size();
    }
    glw::TreeNode child(std::optional<glw::TreeNode> parent, std::size_t index) const override {
        if (!parent)
            return node(top[index]);
        return node(items[*itemOf(*parent)].children[index]);
    }
    std::optional<glw::TreeNode> parent(glw::TreeNode n) const override {
        const std::optional<std::size_t> item = itemOf(n);
        if (!item || !items[*item].parent)
            return std::nullopt;
        return node(*items[*item].parent);
    }
    bool hasChildren(glw::TreeNode n) const override {
        const std::optional<std::size_t> item = itemOf(n);
        if (!item)
            return false;
        const ValueItem& it = items[*item];
        return !it.children.empty() || it.container();
    }
    std::size_t columnCount() const override { return 3; }
    glw::LocalizedText header(std::size_t column) const override {
        return column == 0 ? first_ : column == 1 ? tr("Value") : tr("Type");
    }

private:
    glw::LocalizedText first_;
    std::unordered_map<std::uint64_t, std::size_t> byId_;
};

namespace {

// A cell of a value tree: its text, muted (a type, a "more" row) or red (an error).
class ValueCell final : public glw::Widget {
public:
    ValueCell() { setHitTestVisible(false); }
    void show(glw::LocalizedText text, std::optional<glw::Color> color, bool muted) {
        text_ = std::move(text);
        color_ = color;
        muted_ = muted;
        layout_.reset();
        invalidateLayout();
        invalidatePaint();
    }

protected:
    glw::Size measureContent(glw::Size) override {
        const glw::UiContext* ui = context();
        if (!ui)
            return {60, rowHeight};
        const glw::text::TextLayout t(shortened(ui->translator.resolve(text_), 200, true),
                                      glw::text::TextStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize});
        return {std::min(std::ceil(t.size().width) + 12, 420.0f), rowHeight};
    }
    void paint(glw::PaintContext& context) override {
        const glw::Theme& t = context.ui.theme;
        // narrower than its text: cut with "..."
        const float room = std::max(0.0f, size().width - 8);
        if (!layout_ || laidFor_ != room) {
            layout_.emplace(elidedLayout(context.ui.translator.resolve(text_),
                                         glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, t.fontSize}, room));
            laidFor_ = room;
        }
        context.painter.pushClip({4, 0, room, size().height});
        context.drawText(*layout_, {4, (size().height - layout_->size().height) / 2 + layout_->opticalOffset()},
                         color_ ? *color_ : muted_ ? t.palette.mutedText : t.palette.text);
        context.painter.popClip();
    }

private:
    glw::LocalizedText text_;
    std::optional<glw::Color> color_;
    bool muted_ = false;
    std::optional<glw::text::TextLayout> layout_;
    float laidFor_ = -1;                    // the room it was laid out (cut) for
};

class ValueDelegate final : public glw::TreeDelegate {
public:
    explicit ValueDelegate(ValueModel& model) : model_(model) {}
    std::unique_ptr<glw::Widget> makeCell(std::size_t) override { return std::make_unique<ValueCell>(); }
    void bindCell(glw::Widget& cell, glw::TreeNode node, std::size_t column) override {
        const std::optional<std::size_t> item = model_.itemOf(node);
        if (!item)
            return;
        const ValueItem& it = model_.items[*item];
        auto& c = static_cast<ValueCell&>(cell);
        switch (column) {
        case 0: c.show(it.name, std::nullopt, it.kind == ValueItem::Kind::More); break;
        case 1: c.show(glw::LocalizedText(it.text), it.error ? std::optional<glw::Color>(errorColor) : std::nullopt, it.muted); break;
        default: c.show(glw::LocalizedText(it.type), std::nullopt, true); break;
        }
    }

private:
    ValueModel& model_;
};

// Around a value tree: its right button menu and (a watch's tree) Delete, which the tree leaves to whoever has it.
class TreeFrame final : public glw::Widget {
public:
    TreeFrame(DebugPanel& panel, ValueModel& model, std::unique_ptr<glw::TreeView> tree, std::function<void(std::uint64_t)> deleted = {})
        : panel_(panel), model_(model), deleted_(std::move(deleted)) {
        setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical));
        tree_ = &add(std::move(tree));
        tree_->setStretch(1);
    }

protected:
    void onPreviewMouse(glw::MouseEvent& event) override {
        if (event.type != glw::MouseEventType::Press || event.button != glw::platform::MouseButton::Right)
            return;
        glw::ListView& list = tree_->list();
        const std::optional<std::size_t> row = list.itemAt(list.mapFromRoot(mapToRoot(event.position)));
        if (!row || *row >= tree_->shownCount())
            return;
        event.handled = true;
        const glw::TreeNode node = tree_->shownNode(*row);
        tree_->setCurrent(node);
        panel_.menu(model_, node.id, tree_->mapFromRoot(mapToRoot(event.position)), *tree_);
    }
    void onPreviewKey(glw::KeyEvent& event) override {
        if (!deleted_ || !event.pressed || event.key != glw::platform::Key::Delete)
            return;
        if (const std::optional<glw::TreeNode> current = tree_->current()) {
            event.handled = true;
            deleted_(current->id);
        }
    }

private:
    DebugPanel& panel_;
    ValueModel& model_;
    glw::TreeView* tree_ = nullptr;
    std::function<void(std::uint64_t)> deleted_;
};

std::unique_ptr<glw::TreeView> valueTree(const std::shared_ptr<ValueModel>& model) {
    auto tree = std::make_unique<glw::TreeView>();
    tree->setModel(model);
    tree->setDelegate(std::make_unique<ValueDelegate>(*model));
    tree->setRowHeight(rowHeight);
    tree->setSelectionMode(glw::SelectionMode::Single);
    // the names and the types as wide as they need, the values all the room they leave (at least 80); narrower than all
    // that, the types give way, then the names (cut with "...") before the tree scrolls across
    tree->setColumnSizing(0, glw::ColumnSizing::FitContent);
    tree->setColumnYields(0, true);
    tree->setColumnSizing(1, glw::ColumnSizing::Stretch);
    tree->setColumnWidth(1, 80);
    tree->setColumnSizing(2, glw::ColumnSizing::FitContent);
    tree->setColumnYields(2, true);
    return tree;
}

} // namespace

DebugPanel::DebugPanel(core::Document& doc, core::RunSession& run) : doc_(doc), run_(run) {
    setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical));
    auto split = std::make_unique<glw::Splitter>(glw::Splitter::Props{.orientation = glw::Orientation::Vertical});
    split->setStretch(1);

    // the stack: a frame chosen, its variables and its node
    stackModel_ = std::make_shared<StackModel>();
    auto stack = std::make_unique<glw::TableView>();
    stack_ = stack.get();
    stack->setModel(stackModel_);
    stack->setDelegate(std::make_unique<glw::TableTextDelegate>([this](std::size_t row, std::size_t column) {
        if (row >= stackModel_->rows.size())
            return glw::LocalizedText();
        return glw::LocalizedText(column == 0 ? stackModel_->rows[row].shown : stackModel_->rows[row].nodeTitle);
    }));
    stack->setRowHeight(rowHeight);
    stack->setSelectionMode(glw::SelectionMode::Single);
    stack->setColumnSizing(0, glw::ColumnSizing::FitContent);
    stack->setColumnSizing(1, glw::ColumnSizing::Stretch);
    stack->setOnCurrentChanged([this](std::optional<std::size_t> row) {
        if (selecting_ || !row || *row >= stackModel_->rows.size())
            return;
        run_.setSelectedFrame(int(*row));
        refresh();
        if (onLocation_)
            onLocation_(stackModel_->rows[*row].graph, stackModel_->rows[*row].node);
    });
    stack->setStretch(1);
    split->add(std::move(stack));

    // the variables
    variableModel_ = std::make_shared<ValueModel>(tr("Variable"));
    auto variables = valueTree(variableModel_);
    variables_ = variables.get();
    auto variablesFrame = std::make_unique<TreeFrame>(*this, *variableModel_, std::move(variables));
    variablesFrame->setStretch(3);
    split->add(std::move(variablesFrame));

    // the watches: a field to add one (Enter), their tree
    watchModel_ = std::make_shared<ValueModel>(tr("Watch"));
    auto watchBox = std::make_unique<glw::Widget>();
    watchBox->setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 2.0f));
    watchBox->setStretch(2);
    watchField_ = &watchBox->add(std::make_unique<glw::LineEdit>(glw::LineEdit::Props{
        .placeholder = tr("Watch: name, name[3].Prop, name[\"key\"], name[1000..1099]  (Enter adds)"),
        .onSubmit = [this](const std::string& text) {
            addWatch(text);
            watchField_->setText("");
        }}));
    watchField_->setToolTip(glw::ToolTip{
        .text = tr("An expression to watch. A range name[a..b] loads those elements (inclusive): a big array is never loaded as a whole."),
        .note = tr("The locals and parameters of the frame chosen are found first, then the variables of the script.")});
    auto watches = valueTree(watchModel_);
    watchTree_ = watches.get();
    auto watchFrame = std::make_unique<TreeFrame>(*this, *watchModel_, std::move(watches), [this](std::uint64_t node) {
        const std::optional<std::size_t> item = watchModel_->itemOf(glw::TreeNode{node});
        if (item && watchModel_->items[*item].kind == ValueItem::Kind::Watch && !watchModel_->items[*item].parent)
            removeWatch(watchModel_->items[*item].name.source);
    });
    watchFrame->setStretch(1);
    watchBox->add(std::move(watchFrame));
    split->add(std::move(watchBox));
    add(std::move(split));

    // a container opened that has no elements yet: its first ones; a "more" row: the next ones
    for (ValueModel* model : {variableModel_.get(), watchModel_.get()}) {
        model->loadFirst = [this, model](std::size_t item) {
            loadChildren(*model, item, 0, firstPage);
            expanded_.insert(model->items[item].key);
        };
    }
    for (auto [tree, model] : {std::pair{variables_, variableModel_.get()}, std::pair{watchTree_, watchModel_.get()}}) {
        tree->setOnActivated([this, model](glw::TreeNode node) {
            const std::optional<std::size_t> item = model->itemOf(node);
            if (item && model->items[*item].kind == ValueItem::Kind::More && model->items[*item].parent)
                loadMore(model->items[*model->items[*item].parent].key, page);
        });
    }

    // the groups open at first (closed by the user: closed from then on)
    expanded_ = {"group:locals", "group:globals"};
    {
        const int a = run_.stateChanged.connect([this](core::RunState) { refresh(); });
        const int b = run_.paused.connect([this](const std::string&, int) { refresh(); });
        const int c = run_.finished.connect([this](bool, const std::string&) { refresh(); });
        disconnect_.push_back([this, a, b, c] {
            run_.stateChanged.disconnect(a);
            run_.paused.disconnect(b);
            run_.finished.disconnect(c);
        });
        const int d = doc_.scriptOpened.connect([this] { scriptOpened(); });
        const int e = doc_.titleChanged.connect([this] { pathChanged(); });
        disconnect_.push_back([this, d, e] {
            doc_.scriptOpened.disconnect(d);
            doc_.titleChanged.disconnect(e);
        });
    }
    watchesFor_ = doc_.path();
    refresh();
}

DebugPanel::~DebugPanel() {
    for (const auto& d : disconnect_)
        d();
}

void DebugPanel::setWatchStore(std::function<std::vector<std::string>(const std::string&)> load,
                               std::function<void(const std::string&, const std::vector<std::string>&)> save) {
    loadWatches_ = std::move(load);
    saveWatches_ = std::move(save);
    watchesFor_ = doc_.path();
    if (!watchesFor_.empty() && loadWatches_)
        watches_ = loadWatches_(watchesFor_);
    refreshWatches();
}

// ---- the trees

void DebugPanel::remember(ValueModel& model, glw::TreeView& tree) {
    for (std::size_t i = 0; i < model.items.size(); ++i) {
        const glw::TreeNode node = model.node(i);
        if (!tree.shownIndexOf(node))
            continue;           // under what is closed: as it was
        if (tree.isExpanded(node))
            expanded_.insert(model.items[i].key);
        else
            expanded_.erase(model.items[i].key);
    }
}

void DebugPanel::reopen(ValueModel& model, glw::TreeView& tree) {
    model.reset();
    for (std::size_t i = 0; i < model.items.size(); ++i)      // (opening one may load its elements: items grow)
        if (expanded_.contains(model.items[i].key))
            tree.setExpanded(model.node(i), true);
}

std::size_t DebugPanel::append(ValueModel& model, std::optional<std::size_t> parent, const fg::ValueView& view, const std::string& prefix) {
    ValueItem item;
    item.kind = ValueItem::Kind::Value;
    item.prefix = prefix;
    item.name = glw::LocalizedText(view.name);
    item.text = view.text;
    item.type = view.type;
    item.path = view.path;
    item.total = view.total;
    item.end = view.offset + view.children.size();
    item.parent = parent;
    item.key = !view.path.empty() ? prefix + view.path
               : parent           ? model.items[*parent].key + "#" + std::to_string(model.items[*parent].children.size())
                                  : prefix + view.name;
    const std::size_t index = model.add(std::move(item));
    for (const fg::ValueView& child : view.children)
        append(model, index, child, prefix);
    // as much of it loaded as before
    ValueItem& it = model.items[index];
    const std::string key = it.key;
    std::size_t want = std::min(loaded_.contains(key) ? loaded_[key] : 0, it.total);
    if (expanded_.contains(key) && it.end == 0 && it.total > 0)
        want = std::max(want, std::min(it.total, firstPage));
    if (it.container() && want > it.end)
        loadChildren(model, index, model.items[index].end, std::min(want - model.items[index].end, maxLoad));
    updateMore(model, index);
    return index;
}

void DebugPanel::loadChildren(ValueModel& model, std::size_t item, std::size_t from, std::size_t count) {
    const ValueItem& it = model.items[item];
    if (it.path.empty() || count == 0 || from >= it.total)
        return;
    count = std::min({count, maxLoad, it.total - from});
    std::vector<fg::EvalResult> results;
    const std::string expression = it.path + "[" + std::to_string(from) + ".." + std::to_string(from + count - 1) + "]";
    if (!run_.evaluate({expression}, {}, results) || results.size() != 1 || !results[0].ok)
        return;
    const fg::ValueView& v = results[0].view;
    const std::string prefix = it.prefix;
    // the "more" row goes: the elements after those there, then it again
    for (std::size_t c : std::vector<std::size_t>(model.items[item].children))
        if (model.items[c].kind == ValueItem::Kind::More)
            model.removeChild(item, c);
    for (const fg::ValueView& child : v.children)
        append(model, item, child, prefix);
    ValueItem& now = model.items[item];
    now.total = v.total;
    now.end = from + v.children.size();
    loaded_[now.key] = now.end;
    updateMore(model, item);
}

void DebugPanel::updateMore(ValueModel& model, std::size_t item) {
    for (std::size_t c : std::vector<std::size_t>(model.items[item].children))
        if (model.items[c].kind == ValueItem::Kind::More)
            model.removeChild(item, c);
    const ValueItem& it = model.items[item];
    if (!it.container() || it.total <= it.end || it.end == 0)
        return;             // (not loaded at all: its first ones come when it is opened)
    ValueItem more;
    more.kind = ValueItem::Kind::More;
    more.key = it.key + "#more";
    more.name = glw::LocalizedText("...");
    const glw::LocalizedText text = tr("{left} more - double-click to load {next} more")
                                        .arg("left", thousands(it.total - it.end))
                                        .arg("next", thousands(std::min(page, it.total - it.end)));
    const glw::UiContext* ui = context();
    more.text = ui ? ui->translator.resolve(text) : thousands(it.total - it.end) + " more";
    more.muted = true;
    more.parent = item;
    model.add(std::move(more));
}

void DebugPanel::loadMore(const std::string& key, std::size_t count) {
    for (ValueModel* model : {variableModel_.get(), watchModel_.get()})
        if (const std::optional<std::size_t> item = model->itemOf(key)) {
            loaded_[key] = std::min(model->items[*item].end + count, model->items[*item].total);
            expanded_.insert(key);
        }
    refresh();
}

// ---- reading the run

void DebugPanel::refresh() {
    core::RunSession::DebugSnapshot snap;
    const bool have = run_.snapshot(snap);
    const std::shared_ptr<const fg::Program> program = doc_.program();
    std::vector<StackRow> rows;
    for (const core::RunSession::StackFrame& f : snap.stack) {
        const fg::CompiledGraph* cg = program ? program->findGraph(f.graph) : nullptr;
        const fg::CompiledNode* cn = cg ? cg->findNode(f.node) : nullptr;
        const glw::UiContext* ui = context();
        const std::string graph = ui ? ui->translator.resolve(graphTitle(f.graph)) : f.graph;
        rows.push_back({.graph = f.graph,
                        .node = f.node,
                        .shown = graph + (f.isEvent ? " " + (ui ? ui->translator.resolve(tr("(event)")) : std::string("(event)")) : std::string()),
                        .nodeTitle = cn ? nodeTitle(*cn) : std::string()});
    }
    selecting_ = true;
    stackModel_->set(std::move(rows));
    if (!stackModel_->rows.empty())
        stack_->setCurrent(std::min<std::size_t>(std::size_t(std::max(run_.selectedFrame(), 0)), stackModel_->rows.size() - 1));
    selecting_ = false;

    remember(*variableModel_, *variables_);
    variableModel_->clear();
    if (have) {
        const auto group = [&](const char* key, glw::LocalizedText name, const std::vector<fg::VariableView>& vars, const std::string& prefix) {
            ValueItem g;
            g.kind = ValueItem::Kind::Group;
            g.key = key;
            g.name = std::move(name);
            const std::size_t index = variableModel_->add(std::move(g));
            for (const fg::VariableView& v : vars)
                append(*variableModel_, index, v.value, prefix);
        };
        if (run_.state() == core::RunState::Paused)
            group("group:locals", tr("Locals / Parameters"), snap.frameVariables, "l:");
        group("group:globals", tr("Variables"), snap.globals, "g:");
    }
    reopen(*variableModel_, *variables_);
    refreshWatches();
}

void DebugPanel::refreshWatches() {
    remember(*watchModel_, *watchTree_);
    watchModel_->clear();
    std::vector<fg::EvalResult> results;
    const bool have = !watches_.empty() && run_.evaluate(watches_, {}, results) && results.size() == watches_.size();
    for (std::size_t i = 0; i < watches_.size(); ++i) {
        const std::string& expression = watches_[i];
        const std::string prefix = "w:" + expression + "|";
        std::size_t index = 0;
        if (have && results[i].ok) {
            fg::ValueView view = std::move(results[i].view);
            view.name = expression;
            // a watch just added shows its elements at once
            if (!view.children.empty() && seenWatches_.insert(expression).second)
                expanded_.insert(view.path.empty() ? prefix + expression : prefix + view.path);
            index = append(*watchModel_, std::nullopt, view, prefix);
        } else {
            ValueItem item;
            item.key = prefix;
            item.name = glw::LocalizedText(expression);
            item.text = have ? "<" + results[i].error + ">" : std::string();
            item.error = have;
            if (!have) {
                const glw::UiContext* ui = context();
                item.text = ui ? ui->translator.resolve(tr("(not available: run the script first)")) : "(not available: run the script first)";
                item.muted = true;
            }
            index = watchModel_->add(std::move(item));
        }
        watchModel_->items[index].kind = ValueItem::Kind::Watch;
        watchModel_->items[index].name = glw::LocalizedText(expression);
    }
    reopen(*watchModel_, *watchTree_);
}

std::optional<std::string> DebugPanel::valueOf(const std::string& name, bool watch) const {
    const ValueModel& model = watch ? *watchModel_ : *variableModel_;
    for (const ValueItem& item : model.items)
        if (item.kind != ValueItem::Kind::Group && item.kind != ValueItem::Kind::More && item.name.source == name)
            return item.text;
    return std::nullopt;
}

std::size_t DebugPanel::stackDepth() const {
    return stackModel_->rows.size();
}

// ---- the watches

void DebugPanel::addWatch(const std::string& expression) {
    std::string e = expression;
    while (!e.empty() && e.front() == ' ')
        e.erase(e.begin());
    while (!e.empty() && e.back() == ' ')
        e.pop_back();
    if (e.empty() || std::find(watches_.begin(), watches_.end(), e) != watches_.end())
        return;
    watches_.push_back(e);
    saveWatches();
    refreshWatches();
}

void DebugPanel::removeWatch(const std::string& expression) {
    if (std::erase(watches_, expression) == 0)
        return;
    seenWatches_.erase(expression);
    saveWatches();
    refreshWatches();
}

void DebugPanel::saveWatches() {
    if (!watchesFor_.empty() && saveWatches_)       // a script without a file: its watches live as long as it is open
        saveWatches_(watchesFor_, watches_);
}

void DebugPanel::scriptOpened() {
    // another script: its own watches, nothing of what was opened or loaded for the other one
    expanded_ = {"group:locals", "group:globals"};
    loaded_.clear();
    seenWatches_.clear();
    watchesFor_ = doc_.path();
    watches_ = !watchesFor_.empty() && loadWatches_ ? loadWatches_(watchesFor_) : std::vector<std::string>();
    refresh();
}

void DebugPanel::pathChanged() {
    const std::string path = doc_.path();
    if (path == watchesFor_)
        return;
    if (watchesFor_.empty() && !watches_.empty()) {     // a new script saved: its watches go to its file
        watchesFor_ = path;
        saveWatches();
        return;
    }
    watchesFor_ = path;
    seenWatches_.clear();
    watches_ = !path.empty() && loadWatches_ ? loadWatches_(path) : std::vector<std::string>();
    refreshWatches();
}

void DebugPanel::editInField(const std::string& text) {
    watchField_->setText(text);
    if (glw::UiRoot* r = root())
        r->setFocus(watchField_);
    watchField_->selectAll();
}

void DebugPanel::menu(ValueModel& model, std::uint64_t nodeId, glw::Point at, glw::Widget& from) {
    const std::optional<std::size_t> found = model.itemOf(glw::TreeNode{nodeId});
    if (!found)
        return;
    const ValueItem item = model.items[*found];
    const ValueItem container = item.kind == ValueItem::Kind::More && item.parent ? model.items[*item.parent] : item;
    std::vector<glw::MenuItem> items;
    if (container.container() && container.total > container.end) {
        const std::string key = container.key;
        for (std::size_t n : {page, std::size_t(1000), std::size_t(10000)})
            items.push_back({.text = tr("Load the next {count}").arg("count", thousands(n)), .onTriggered = [this, key, n] { loadMore(key, n); }});
        const std::size_t rest = container.total - container.end;
        items.push_back({.text = rest > maxLoad ? tr("Load the next {count} (of {left} left)").arg("count", thousands(maxLoad)).arg("left", thousands(rest))
                                                : tr("Load all ({left} left)").arg("left", thousands(rest)),
                         .onTriggered = [this, key] { loadMore(key, maxLoad); }});
        items.push_back(glw::MenuItem::separatorItem());
    }
    if (!container.path.empty()) {
        const std::string path = container.path;
        if (container.total > 0) {
            const std::size_t last = std::min<std::size_t>(container.total, 100) - 1;
            items.push_back({.text = tr("Watch a Range..."), .onTriggered = [this, path, last] {
                                 editInField(path + "[0.." + std::to_string(last) + "]");
                             }});
        }
        items.push_back({.text = tr("Add to Watch"), .onTriggered = [this, path] { addWatch(path); }});
    }
    if (item.kind != ValueItem::Kind::More && !item.text.empty())
        items.push_back({.text = tr("Copy Value"), .onTriggered = [this, text = item.text] {
                             if (glw::UiRoot* r = root())
                                 if (glw::UiHost* host = r->host())
                                     host->setClipboardText(text);
                         }});
    if (&model == watchModel_.get() && item.kind == ValueItem::Kind::Watch && !item.parent) {
        const std::string expression = item.name.source;
        items.push_back(glw::MenuItem::separatorItem());
        items.push_back({.text = tr("Edit Watch"), .onTriggered = [this, expression] {
                             removeWatch(expression);
                             editInField(expression);
                         }});
        items.push_back({.text = tr("Remove Watch"), .onTriggered = [this, expression] { removeWatch(expression); }, .shortcut = tr("Del")});
        items.push_back({.text = tr("Remove All Watches"), .onTriggered = [this] {
                             watches_.clear();
                             seenWatches_.clear();
                             saveWatches();
                             refreshWatches();
                         }});
    }
    if (!items.empty()) {
        giveIcons(items);
        glw::showContextMenu(from, at, std::move(items));
    }
}

} // namespace fged::gl
