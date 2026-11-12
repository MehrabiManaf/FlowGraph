// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Panels/LaunchDialog.hpp"

#include <GLWidgets/Widgets/Button.hpp>
#include <GLWidgets/Widgets/Dialog.hpp>
#include <GLWidgets/Widgets/ItemModel.hpp>
#include <GLWidgets/Widgets/Label.hpp>
#include <GLWidgets/Widgets/Layout.hpp>
#include <GLWidgets/Widgets/LineEdit.hpp>
#include <GLWidgets/Widgets/ListView.hpp>
#include <GLWidgets/Widgets/SpinBox.hpp>
#include <GLWidgets/Widgets/TextEdit.hpp>

#include <memory>
#include <sstream>

namespace fged::gl {

using glw::tr;

namespace {

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines)
        out += (out.empty() ? "" : "\n") + l;
    return out;
}

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        while (!line.empty() && line.front() == ' ')
            line.erase(line.begin());
        if (!line.empty())
            out.push_back(line);
    }
    return out;
}

// What the dialog edits, and its fields (they live as long as the dialog; every change goes into `file` at once).
struct State {
    fg::standard::LaunchFile file;
    std::size_t shown = 0;
    bool filling = false;               // the fields set by it, not by the user
    std::shared_ptr<glw::StringListModel> names;
    glw::ListView* list = nullptr;
    glw::LineEdit* name = nullptr;
    glw::TextEdit* args = nullptr;
    glw::LineEdit* cwd = nullptr;
    glw::TextEdit* env = nullptr;
    glw::TextEdit* plugins = nullptr;
    glw::SpinBox* delay = nullptr;

    fg::standard::LaunchConfig* config() { return shown < file.configs.size() ? &file.configs[shown] : nullptr; }

    void show(std::size_t index) {
        shown = index;
        const fg::standard::LaunchConfig* c = config();
        if (!c)
            return;
        filling = true;
        name->setText(c->name);
        args->setText(joined(c->args));
        cwd->setText(c->cwd);
        std::vector<std::string> vars;
        for (const auto& [k, v] : c->env)
            vars.push_back(k + "=" + v);
        env->setText(joined(vars));
        plugins->setText(joined(c->pluginPaths));
        delay->setValue(c->stepDelayMs);
        filling = false;
    }
    void fill(std::size_t select) {
        std::vector<glw::LocalizedText> items;
        for (const fg::standard::LaunchConfig& c : file.configs)
            items.emplace_back(c.name);
        names->setItems(std::move(items));
        list->setCurrent(select);
        show(select);
    }
};

std::unique_ptr<glw::TextEdit> lines(float height) {
    auto edit = std::make_unique<glw::TextEdit>();
    edit->setLineNumbers(false);
    edit->setCurrentLineHighlighted(false);     // a field of a form, not code
    edit->setFramed(true);                      // drawn as the form's line fields are
    edit->setMinimumSize({0, height});
    edit->setMaximumSize({glw::unbounded, height});
    return edit;
}

} // namespace

void showLaunchConfigurations(glw::Widget& from, const fg::standard::LaunchFile& file, std::function<void(fg::standard::LaunchFile)> accepted) {
    auto state = std::make_shared<State>();
    state->file = file;
    state->file.current();          // one at least
    std::size_t start = 0;
    for (std::size_t i = 0; i < state->file.configs.size(); ++i)
        if (state->file.configs[i].name == state->file.selected)
            start = i;

    auto content = std::make_unique<glw::Widget>();
    content->setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Horizontal, 12.0f));
    content->setMinimumSize({680, 380});

    // the configurations, and what is done with them
    auto side = std::make_unique<glw::Widget>();
    side->setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Vertical, 6.0f));
    side->setMinimumSize({200, 0});
    side->setMaximumSize({200, glw::unbounded});
    state->names = std::make_shared<glw::StringListModel>();
    auto list = std::make_unique<glw::ListView>();
    list->setModel(state->names);
    list->setDelegate(std::make_unique<glw::TextDelegate>([names = state->names.get()](std::size_t i) { return names->text(i); }));
    list->setSelectionMode(glw::SelectionMode::Single);
    list->setStretch(1);
    State* s = state.get();
    list->setOnCurrentChanged([s](std::optional<std::size_t> row) {
        if (row && *row != s->shown && !s->filling)
            s->show(*row);
    });
    state->list = &side->add(std::move(list));
    auto buttons = std::make_unique<glw::Widget>();
    buttons->setLayout(std::make_unique<glw::StackLayout>(glw::Orientation::Horizontal, 4.0f));
    buttons->add(std::make_unique<glw::Button>(tr("Add"), [s] {
        fg::standard::LaunchConfig c;
        c.name = "Configuration " + std::to_string(s->file.configs.size() + 1);
        s->file.configs.push_back(c);
        s->fill(s->file.configs.size() - 1);
    }));
    buttons->add(std::make_unique<glw::Button>(tr("Duplicate"), [s] {
        if (const fg::standard::LaunchConfig* c = s->config()) {
            fg::standard::LaunchConfig copy = *c;
            copy.name += " copy";
            s->file.configs.push_back(copy);
            s->fill(s->file.configs.size() - 1);
        }
    }));
    buttons->add(std::make_unique<glw::Button>(tr("Remove"), [s] {
        if (s->file.configs.size() <= 1 || !s->config())
            return;             // there is always one
        s->file.configs.erase(s->file.configs.begin() + std::ptrdiff_t(s->shown));
        s->fill(0);
    }));
    side->add(std::move(buttons));
    content->add(std::move(side));

    // the one chosen
    auto form = std::make_unique<glw::Widget>();
    auto grid = std::make_unique<glw::GridLayout>(std::vector<glw::GridColumn>{{}, {.stretch = 1}}, 8.0f, 6.0f);
    glw::GridLayout& g = *grid;
    form->setLayout(std::move(grid));
    form->setStretch(1);
    const auto row = [&](glw::LocalizedText label, std::unique_ptr<glw::Widget> field, glw::LocalizedText tip = {}) -> glw::Widget& {
        auto l = std::make_unique<glw::Label>(std::move(label));
        l->setAlignment(glw::Alignment::Start, glw::Alignment::Start);
        l->setMargin({0, 4, 0, 0});
        form->add(std::move(l));
        if (!tip.source.empty())
            field->setToolTip(std::move(tip));
        return form->add(std::move(field));
    };
    state->name = &static_cast<glw::LineEdit&>(row(tr("Name"), std::make_unique<glw::LineEdit>(glw::LineEdit::Props{.onChanged = [s](const std::string& t) {
        fg::standard::LaunchConfig* c = s->config();
        if (s->filling || !c || t.empty())
            return;
        if (s->file.selected == c->name)
            s->file.selected = t;
        c->name = t;
        s->names->set(s->shown, glw::LocalizedText(t));
    }})));
    state->args = &static_cast<glw::TextEdit&>(row(tr("Arguments"), lines(70), tr("Main's arguments, one per line")));
    state->args->setOnChanged([s] {
        if (fg::standard::LaunchConfig* c = s->config(); c && !s->filling)
            c->args = linesOf(s->args->text());
    });
    state->cwd = &static_cast<glw::LineEdit&>(row(tr("Working directory"), std::make_unique<glw::LineEdit>(glw::LineEdit::Props{
        .placeholder = tr("relative to the script's folder; empty: unchanged"), .onChanged = [s](const std::string& t) {
            if (fg::standard::LaunchConfig* c = s->config(); c && !s->filling)
                c->cwd = t;
        }})));
    state->env = &static_cast<glw::TextEdit&>(row(tr("Environment"), lines(70), tr("NAME=value, one per line: set while the script runs")));
    state->env->setOnChanged([s] {
        fg::standard::LaunchConfig* c = s->config();
        if (!c || s->filling)
            return;
        c->env.clear();
        for (const std::string& line : linesOf(s->env->text()))
            if (const std::size_t eq = line.find('='); eq != std::string::npos && eq > 0)
                c->env[line.substr(0, eq)] = line.substr(eq + 1);
    });
    state->plugins = &static_cast<glw::TextEdit&>(row(tr("Plugin folders"), lines(56), tr("Folders of plugins, relative to the script's, one per line")));
    state->plugins->setOnChanged([s] {
        if (fg::standard::LaunchConfig* c = s->config(); c && !s->filling)
            c->pluginPaths = linesOf(s->plugins->text());
    });
    auto delay = std::make_unique<glw::SpinBox>(glw::SpinBox::Props{.minimum = 0, .maximum = 10000, .step = 50, .onChanged = [s](double v) {
        if (fg::standard::LaunchConfig* c = s->config(); c && !s->filling)
            c->stepDelayMs = int(v);
    }});
    delay->setAlignment(glw::Alignment::Start, glw::Alignment::Center);
    delay->setMinimumSize({120, 0});
    state->delay = &static_cast<glw::SpinBox&>(row(tr("Step delay (ms)"), std::move(delay), tr("A delay between two nodes while running: slow motion, to watch the links")));
    (void)g;
    content->add(std::move(form));

    state->fill(start);
    glw::showDialog(from, {.title = tr("Launch Configurations"),
                           .content = std::move(content),
                           .buttons = {tr("OK"), tr("Cancel")},
                           .defaultButton = 0,
                           .onClosed = [state, accepted = std::move(accepted)](std::optional<std::size_t> button) {
                               if (button == 0u && accepted)
                                   accepted(state->file);
                           }});
}

} // namespace fged::gl
