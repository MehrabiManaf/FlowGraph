#include "DetailsPanel.hpp"
#include "../Graph/Style.hpp"
#include "../Studio/RunController.hpp"
#include "TypeEdit.hpp"
#include "TypeSpec.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <set>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace fged {

namespace {

QLabel* heading(const QString& t) {
    auto* l = new QLabel("<b>" + t.toHtmlEscaped() + "</b>");
    return l;
}

QLabel* dim(const QString& t) {
    auto* l = new QLabel(t);
    l->setWordWrap(true);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

// After a parameter of a function / event was renamed, the pins of the nodes that use it must follow.
void renamePins(fg::Script& s, const std::string& graphName, bool isFunction, bool isInput, const std::string& from, const std::string& to,
                const std::string& eventName = "") {
    auto renameOnNode = [&](fg::Graph& g, fg::NodeId nodeId, bool asOutput) {
        for (auto& c : g.connections) {
            if (asOutput && c.fromNode == nodeId && c.fromPin == from) c.fromPin = to;
            if (!asOutput && c.toNode == nodeId && c.toPin == from) c.toPin = to;
        }
        if (!asOutput)
            if (auto* n = g.findNode(nodeId)) {
                auto it = n->defaults.find(from);
                if (it != n->defaults.end()) { n->defaults[to] = it->second; n->defaults.erase(from); }
            }
    };
    for (auto& g : s.graphs) {
        for (auto& n : g.nodes) {
            if (isFunction) {
                if (g.name == graphName && isInput && n.def == "func.entry") renameOnNode(g, n.id, true);
                if (g.name == graphName && !isInput && n.def == "func.return") renameOnNode(g, n.id, false);
                if (n.def == "func.call" && n.prop("function") == graphName) renameOnNode(g, n.id, !isInput);
            } else {
                if (n.def == "event.custom" && n.prop("name") == eventName) renameOnNode(g, n.id, true);
                if (n.def == "event.call" && n.prop("event") == eventName) renameOnNode(g, n.id, false);
            }
        }
    }
}

void removePins(fg::Script& s, const std::string& graphName, bool isFunction, bool isInput, const std::string& pin, const std::string& eventName = "") {
    auto dropOnNode = [&](fg::Graph& g, fg::NodeId id, bool asOutput) {
        auto& v = g.connections;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const fg::Connection& c) {
            return asOutput ? (c.fromNode == id && c.fromPin == pin) : (c.toNode == id && c.toPin == pin);
        }), v.end());
    };
    for (auto& g : s.graphs)
        for (auto& n : g.nodes) {
            if (isFunction) {
                if (g.name == graphName && isInput && n.def == "func.entry") dropOnNode(g, n.id, true);
                if (g.name == graphName && !isInput && n.def == "func.return") dropOnNode(g, n.id, false);
                if (n.def == "func.call" && n.prop("function") == graphName) dropOnNode(g, n.id, !isInput);
            } else {
                if (n.def == "event.custom" && n.prop("name") == eventName) dropOnNode(g, n.id, true);
                if (n.def == "event.call" && n.prop("event") == eventName) dropOnNode(g, n.id, false);
            }
        }
}

} // namespace

DetailsPanel::DetailsPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setAlignment(Qt::AlignTop);
    body_ = new QWidget(this);
    form_ = new QFormLayout(body_);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    outer->addWidget(body_);
    outer->addStretch(1);
    connect(doc_, &Document::scriptReplaced, this, [this] { rebuild(); });
    connect(doc_, &Document::compiled, this, [this] {
        if (selfEdit_ || rebuildQueued_ || mode_ != Mode::Nodes) return;
        rebuildQueued_ = true;
        QTimer::singleShot(0, this, [this] { rebuildQueued_ = false; rebuild(); });
    });
    rebuild();
}

void DetailsPanel::showNothing() { mode_ = Mode::None; rebuild(); }
void DetailsPanel::showVariable(const QString& owner, const QString& name) { mode_ = Mode::Variable; owner_ = owner; name_ = name; rebuild(); }
void DetailsPanel::showGraph(const QString& name) { mode_ = Mode::Graph; name_ = name; rebuild(); }
void DetailsPanel::showNodes(const QString& graph, const QList<int>& ids) {
    if (mode_ == Mode::Nodes && owner_ == graph && ids_ == ids && !ids.isEmpty()) return;
    mode_ = ids.isEmpty() ? Mode::None : Mode::Nodes;
    owner_ = graph;
    ids_ = ids;
    rebuild();
}

void DetailsPanel::edit(const QString& label, const std::function<void(fg::Script&)>& fn, const QString& key, bool rebuildAfter) {
    selfEdit_ = true;
    doc_->edit(label, fn, EditKind::Structural, key);
    selfEdit_ = false;
    if (rebuildAfter) QTimer::singleShot(0, this, [this] { rebuild(); });
}

void DetailsPanel::clearForm() {
    while (form_->rowCount() > 0) form_->removeRow(0);
}

void DetailsPanel::rebuild() {
    clearForm();
    switch (mode_) {
    case Mode::None: form_->addRow(dim("Select a variable, function or node to see its properties.")); break;
    case Mode::Variable: buildVariable(); break;
    case Mode::Graph: buildGraph(); break;
    case Mode::Nodes: buildNodes(); break;
    }
}

// ------------------------------------------------------------------ variables
void DetailsPanel::buildVariable() {
    const fg::Script& s = doc_->script();
    const bool local = !owner_.isEmpty();
    const fg::VariableDecl* v = nullptr;
    if (local) {
        if (auto* g = s.findGraph(ss(owner_))) v = g->findLocal(ss(name_));
    } else v = s.findVariable(ss(name_));
    if (!v) { form_->addRow(dim("The variable no longer exists.")); return; }
    form_->addRow(heading(local ? "Local variable" : "Variable"));

    auto mutate = [this, local](const std::function<void(fg::VariableDecl&)>& fn, const QString& label, const QString& key) {
        QString owner = owner_, name = name_;
        edit(label, [=](fg::Script& sc) {
            fg::VariableDecl* d = nullptr;
            if (!owner.isEmpty()) { if (auto* g = sc.findGraph(ss(owner))) d = g->findLocal(ss(name)); }
            else d = sc.findVariable(ss(name));
            if (d) fn(*d);
        }, key);
        (void)local;
    };

    auto* nameEdit = new QLineEdit(qs(v->name));
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit, local] {
        QString nn = nameEdit->text().trimmed();
        if (nn.isEmpty() || nn == name_) { nameEdit->setText(name_); return; }
        const fg::Script& sc = doc_->script();
        bool clash = local ? (sc.findGraph(ss(owner_)) && sc.findGraph(ss(owner_))->findLocal(ss(nn))) : sc.findVariable(ss(nn)) != nullptr;
        if (clash) { QMessageBox::warning(this, "Rename", "That name is already used."); nameEdit->setText(name_); return; }
        QString old = name_;
        edit("Rename variable", [&](fg::Script& s2) {
            if (local) s2.renameLocal(ss(owner_), ss(old), ss(nn)); else s2.renameVariable(ss(old), ss(nn));
        });
        name_ = nn;
    });
    form_->addRow("Name", nameEdit);

    {
        TypeSpec spec = TypeSpec::parse(qs(v->type));
        auto* valueEdit = new TypeEdit(doc_->registry());
        valueEdit->setType(spec.value);
        auto* containerBox = new QComboBox;
        containerBox->addItems({"Single", "Array", "Map"});
        containerBox->setCurrentIndex(int(spec.container));
        auto* keyEdit = new TypeEdit(doc_->registry());
        keyEdit->setType(spec.key);
        auto apply = [=]() {
            TypeSpec ns;
            ns.container = TypeSpec::Container(containerBox->currentIndex());
            ns.key = keyEdit->type();
            ns.value = valueEdit->type();
            mutate([&](fg::VariableDecl& d) { d.type = ss(ns.str()); d.def = fg::Value(); }, "Change variable type", QString());
            QTimer::singleShot(0, this, [this] { rebuild(); });
        };
        connect(valueEdit, &TypeEdit::typeEdited, this, apply);
        connect(keyEdit, &TypeEdit::typeEdited, this, apply);
        connect(containerBox, qOverload<int>(&QComboBox::activated), this, apply);
        form_->addRow("Type", valueEdit);
        form_->addRow("Container", containerBox);
        if (spec.container == TypeSpec::Container::Map) form_->addRow("Key type", keyEdit);
    }

    const fg::TypeDesc* t = doc_->registry().findType(v->type);
    if (t && (t->isPrimitive() || t->isEnum())) {
        fg::CompiledPin cp;
        cp.type = t;
        try { cp.def = doc_->registry().convert(v->def, t); } catch (...) { cp.def = doc_->registry().defaultValue(t); }
        if (t->kind == fg::TypeKind::Bool) {
            auto* cb = new QCheckBox;
            cb->setChecked(cp.def.toBool());
            connect(cb, &QCheckBox::toggled, this, [=](bool on) { mutate([&](fg::VariableDecl& d) { d.def = fg::Value(on); }, "Edit default value", "vardef:" + name_); });
            form_->addRow("Default", cb);
        } else {
            auto* le = new QLineEdit(t->isEnum() ? qs(t->enumName(cp.def.toInt())) : qs(cp.def.toString() == "null" ? "" : (cp.def.isString() ? cp.def.str() : cp.def.toString())));
            connect(le, &QLineEdit::editingFinished, this, [=] {
                fg::Value nv = RunController::parseArgument(doc_->registry(), t, le->text());
                mutate([&](fg::VariableDecl& d) { d.def = nv; }, "Edit default value", "vardef:" + name_);
            });
            form_->addRow("Default", le);
        }
    } else {
        form_->addRow("Default", dim(t && t->isContainer() ? "Containers start empty." : "Objects start as null."));
    }
    auto* cat = new QLineEdit(qs(v->category));
    connect(cat, &QLineEdit::editingFinished, this, [=] { mutate([&](fg::VariableDecl& d) { d.category = ss(cat->text()); }, "Edit category", "varcat:" + name_); });
    form_->addRow("Category", cat);
    auto* doc = new QLineEdit(qs(v->doc));
    connect(doc, &QLineEdit::editingFinished, this, [=] { mutate([&](fg::VariableDecl& d) { d.doc = ss(doc->text()); }, "Edit description", "vardoc:" + name_); });
    form_->addRow("Description", doc);
}

// ------------------------------------------------------------------ parameter tables
QTableWidget* DetailsPanel::buildParamTable(bool inputs, const std::vector<fg::ParamDecl>& params, std::function<void(std::vector<fg::ParamDecl>)> apply) {
    enum { ColName, ColType, ColContainer, ColKey, ColRef, ColConst };
    auto* table = new QTableWidget(int(params.size()), inputs ? 6 : 4);
    QStringList headers{"Name", "Type", "Container", "Key"};
    if (inputs) headers << "Ref" << "Const";
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColContainer, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColKey, QHeaderView::ResizeToContents);
    if (inputs) {
        table->horizontalHeader()->setSectionResizeMode(ColRef, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(ColConst, QHeaderView::ResizeToContents);
    }
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setMinimumHeight(40 + int(params.size()) * 28);
    table->setMaximumHeight(40 + int(params.size()) * 28 + 4);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto collect = [table, inputs]() {
        std::vector<fg::ParamDecl> out;
        for (int r = 0; r < table->rowCount(); ++r) {
            fg::ParamDecl p;
            p.name = ss(table->item(r, ColName)->text().trimmed());
            TypeSpec spec;
            spec.value = static_cast<TypeEdit*>(table->cellWidget(r, ColType))->type();
            spec.container = TypeSpec::Container(static_cast<QComboBox*>(table->cellWidget(r, ColContainer))->currentIndex());
            spec.key = static_cast<TypeEdit*>(table->cellWidget(r, ColKey))->type();
            p.type = ss(spec.str());
            if (inputs) {
                p.byRef = static_cast<QCheckBox*>(table->cellWidget(r, ColRef))->isChecked();
                p.isConst = static_cast<QCheckBox*>(table->cellWidget(r, ColConst))->isChecked();
            }
            out.push_back(std::move(p));
        }
        return out;
    };
    for (int r = 0; r < table->rowCount(); ++r) {
        const fg::ParamDecl& p = params[size_t(r)];
        TypeSpec spec = TypeSpec::parse(qs(p.type));
        table->setItem(r, ColName, new QTableWidgetItem(qs(p.name)));
        auto* te = new TypeEdit(doc_->registry());
        te->setType(spec.value);
        te->setMinimumWidth(110);
        table->setCellWidget(r, ColType, te);
        auto* cont = new QComboBox;
        cont->addItems({"Single", "Array", "Map"});
        cont->setCurrentIndex(int(spec.container));
        table->setCellWidget(r, ColContainer, cont);
        auto* key = new TypeEdit(doc_->registry());
        key->setType(spec.key);
        key->setMinimumWidth(90);
        key->setEnabled(spec.container == TypeSpec::Container::Map);
        table->setCellWidget(r, ColKey, key);
        connect(te, &TypeEdit::typeEdited, this, [=] { apply(collect()); });
        connect(key, &TypeEdit::typeEdited, this, [=] { apply(collect()); });
        connect(cont, qOverload<int>(&QComboBox::activated), this, [=](int) {
            key->setEnabled(cont->currentIndex() == int(TypeSpec::Container::Map));
            apply(collect());
        });
        if (inputs) {
            auto* ref = new QCheckBox;
            ref->setChecked(p.byRef);
            auto* cst = new QCheckBox;
            cst->setChecked(p.isConst);
            table->setCellWidget(r, ColRef, ref);
            table->setCellWidget(r, ColConst, cst);
            connect(ref, &QCheckBox::toggled, this, [=] { apply(collect()); });
            connect(cst, &QCheckBox::toggled, this, [=] { apply(collect()); });
        }
    }
    connect(table, &QTableWidget::itemChanged, this, [=](QTableWidgetItem*) { apply(collect()); });
    return table;
}

// ------------------------------------------------------------------ graphs / functions
void DetailsPanel::buildGraph() {
    const fg::Graph* g = doc_->script().findGraph(ss(name_));
    if (!g) { form_->addRow(dim("The graph no longer exists.")); return; }
    const bool fn = g->kind == fg::GraphKind::Function;
    form_->addRow(heading(fn ? "Function" : "Event graph"));
    auto* nameEdit = new QLineEdit(qs(g->name));
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit, fn] {
        QString nn = nameEdit->text().trimmed();
        if (nn.isEmpty() || nn == name_) { nameEdit->setText(name_); return; }
        if (doc_->script().graphIndex(ss(nn)) >= 0) { QMessageBox::warning(this, "Rename", "A graph with that name exists."); nameEdit->setText(name_); return; }
        QString old = name_;
        edit("Rename graph", [&](fg::Script& s) {
            if (fn) s.renameFunction(ss(old), ss(nn));
            else if (auto* gr = s.findGraph(ss(old))) gr->name = ss(nn);
        });
        name_ = nn;
        emit graphRenamed(old, nn);
    });
    form_->addRow("Name", nameEdit);
    auto* docEdit = new QLineEdit(qs(g->doc));
    connect(docEdit, &QLineEdit::editingFinished, this, [this, docEdit] {
        QString name = name_, text = docEdit->text();
        edit("Edit description", [=](fg::Script& s) { if (auto* gr = s.findGraph(ss(name))) gr->doc = ss(text); }, "graphdoc:" + name_);
    });
    form_->addRow("Description", docEdit);
    if (!fn) return;

    auto section = [&](bool inputs) {
        const auto& params = inputs ? g->inputs : g->outputs;
        form_->addRow(heading(inputs ? "Inputs" : "Outputs"));
        QString gname = name_;
        auto apply = [this, gname, inputs](std::vector<fg::ParamDecl> np) {
            const fg::Graph* cur = doc_->script().findGraph(ss(gname));
            if (!cur) return;
            const auto& old = inputs ? cur->inputs : cur->outputs;
            std::set<std::string> names;
            for (auto& p : np)
                if (p.name.empty() || !names.insert(p.name).second) {
                    QMessageBox::warning(this, "Parameters", "Parameter names must be unique and not empty.");
                    QTimer::singleShot(0, this, [this] { rebuild(); });
                    return;
                }
            std::vector<fg::ParamDecl> oldCopy = old;
            for (auto& p : np)       // keep defaults of unchanged rows
                for (auto& o : oldCopy) if (o.name == p.name) p.def = o.def;
            edit("Edit parameters", [&](fg::Script& s) {
                auto* gr = s.findGraph(ss(gname));
                if (!gr) return;
                auto& dst = inputs ? gr->inputs : gr->outputs;
                if (oldCopy.size() == np.size())
                    for (size_t i = 0; i < np.size(); ++i)
                        if (oldCopy[i].name != np[i].name) renamePins(s, ss(gname), true, inputs, oldCopy[i].name, np[i].name);
                dst = np;
            });
        };
        auto* table = buildParamTable(inputs, params, apply);
        auto* buttons = new QWidget;
        auto* bl = new QHBoxLayout(buttons);
        bl->setContentsMargins(0, 0, 0, 0);
        auto* add = new QPushButton("Add");
        auto* del = new QPushButton("Remove");
        bl->addWidget(add);
        bl->addWidget(del);
        bl->addStretch(1);
        connect(add, &QPushButton::clicked, this, [this, gname, inputs] {
            edit("Add parameter", [&](fg::Script& s) {
                auto* gr = s.findGraph(ss(gname));
                if (!gr) return;
                auto& dst = inputs ? gr->inputs : gr->outputs;
                std::string base = inputs ? "Input" : "Output", n = base;
                for (int i = 1;; ++i) {
                    bool used = false;
                    for (auto& p : dst) used |= p.name == n;
                    if (!used) break;
                    n = base + std::to_string(i);
                }
                dst.push_back({n, "int", false, false, fg::Value()});
            }, QString(), true);
        });
        connect(del, &QPushButton::clicked, this, [this, gname, inputs, table] {
            int row = table->currentRow();
            if (row < 0) row = table->rowCount() - 1;
            if (row < 0) return;
            edit("Remove parameter", [&](fg::Script& s) {
                auto* gr = s.findGraph(ss(gname));
                if (!gr) return;
                auto& dst = inputs ? gr->inputs : gr->outputs;
                if (size_t(row) >= dst.size()) return;
                removePins(s, ss(gname), true, inputs, dst[size_t(row)].name);
                dst.erase(dst.begin() + row);
            }, QString(), true);
        });
        form_->addRow(table);
        form_->addRow(buttons);
    };
    section(true);
    section(false);
}

// ------------------------------------------------------------------ custom event parameters
void DetailsPanel::buildEventParams(const fg::Node& node) {
    form_->addRow(heading("Event parameters"));
    QString graph = owner_;
    fg::NodeId id = node.id;
    std::string eventName = node.prop("name");
    auto apply = [this, graph, id, eventName](std::vector<fg::ParamDecl> np) {
        std::set<std::string> names;
        for (auto& p : np)
            if (p.name.empty() || !names.insert(p.name).second) {
                QMessageBox::warning(this, "Parameters", "Parameter names must be unique and not empty.");
                QTimer::singleShot(0, this, [this] { rebuild(); });
                return;
            }
        edit("Edit event parameters", [&](fg::Script& s) {
            auto* g = s.findGraph(ss(graph));
            auto* n = g ? g->findNode(id) : nullptr;
            if (!n) return;
            if (n->params.size() == np.size())
                for (size_t i = 0; i < np.size(); ++i)
                    if (n->params[i].name != np[i].name) renamePins(s, "", false, false, n->params[i].name, np[i].name, eventName);
            n->params = np;
        });
    };
    auto* table = buildParamTable(false, node.params, apply);
    auto* buttons = new QWidget;
    auto* bl = new QHBoxLayout(buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    auto* add = new QPushButton("Add");
    auto* del = new QPushButton("Remove");
    bl->addWidget(add);
    bl->addWidget(del);
    bl->addStretch(1);
    connect(add, &QPushButton::clicked, this, [this, graph, id] {
        edit("Add event parameter", [&](fg::Script& s) {
            auto* g = s.findGraph(ss(graph));
            auto* n = g ? g->findNode(id) : nullptr;
            if (!n) return;
            std::string name = "Param";
            for (int i = 1;; ++i) {
                bool used = false;
                for (auto& p : n->params) used |= p.name == name;
                if (!used) break;
                name = "Param" + std::to_string(i);
            }
            n->params.push_back({name, "int", false, false, fg::Value()});
        }, QString(), true);
    });
    connect(del, &QPushButton::clicked, this, [this, graph, id, table, eventName] {
        int row = table->currentRow();
        if (row < 0) row = table->rowCount() - 1;
        if (row < 0) return;
        edit("Remove event parameter", [&](fg::Script& s) {
            auto* g = s.findGraph(ss(graph));
            auto* n = g ? g->findNode(id) : nullptr;
            if (!n || size_t(row) >= n->params.size()) return;
            removePins(s, "", false, false, n->params[size_t(row)].name, eventName);
            n->params.erase(n->params.begin() + row);
        }, QString(), true);
    });
    form_->addRow(table);
    form_->addRow(buttons);
}

// ------------------------------------------------------------------ nodes
void DetailsPanel::buildNodes() {
    const fg::Graph* g = doc_->script().findGraph(ss(owner_));
    const fg::CompiledGraph* cg = doc_->compiledGraph(owner_);
    if (!g || !cg) { form_->addRow(dim("")); return; }
    if (ids_.size() > 1) {
        form_->addRow(heading(QString::number(ids_.size()) + " nodes selected"));
        return;
    }
    const fg::Node* node = g->findNode(ids_.front());
    const fg::CompiledNode* cn = cg->findNode(ids_.front());
    if (!node || !cn) { form_->addRow(dim("")); return; }
    form_->addRow(heading(qs(cn->title)));
    if (cn->def && !cn->def->doc.empty()) form_->addRow(dim(qs(cn->def->doc)));
    form_->addRow("Definition", new QLabel(qs(cn->defId) + (cn->def && !cn->def->source.empty() ? "  [" + qs(cn->def->source) + "]" : QString())));

    for (auto& d : doc_->diagnosticsFor(owner_, node->id)) {
        auto* l = new QLabel(qs(d.message));
        l->setWordWrap(true);
        l->setStyleSheet(d.severity == fg::Diagnostic::Severity::Error ? "color:#ff6b6b" : "color:#e6be3c");
        form_->addRow(l);
    }

    QString graph = owner_;
    fg::NodeId id = node->id;
    auto mutateNode = [this, graph, id](const std::function<void(fg::Node&)>& fn, const QString& label, const QString& key) {
        edit(label, [=](fg::Script& s) {
            if (auto* gr = s.findGraph(ss(graph)))
                if (auto* n = gr->findNode(id)) fn(*n);
        }, key);
    };

    if (cn->defId == "event.custom") {
        auto* nameEdit = new QLineEdit(qs(node->prop("name")));
        connect(nameEdit, &QLineEdit::editingFinished, this, [=] {
            QString nn = nameEdit->text().trimmed(), old = qs(node->prop("name"));
            if (nn.isEmpty() || nn == old) { nameEdit->setText(old); return; }
            for (auto& e : doc_->script().customEvents())
                if (qs(e.name) == nn) { QMessageBox::warning(this, "Rename", "An event with that name exists."); nameEdit->setText(old); return; }
            edit("Rename event", [&](fg::Script& s) { s.renameEvent(ss(old), ss(nn)); }, QString(), true);
        });
        form_->addRow("Event name", nameEdit);
        buildEventParams(*node);
    }
    if (cn->op == fg::NodeOp::GetVariable || cn->op == fg::NodeOp::SetVariable)
        form_->addRow("Variable", new QLabel(qs(node->prop("name")) + "  (" + qs(node->prop("scope")) + ")"));
    if (cn->defId == "flow.sequence" || cn->defId == "array.make") {
        auto* sp = new QSpinBox;
        sp->setRange(cn->defId == "flow.sequence" ? 1 : 0, cn->defId == "flow.sequence" ? 32 : 64);
        sp->setValue(qs(node->prop("count", "2")).toInt());
        connect(sp, qOverload<int>(&QSpinBox::valueChanged), this, [=](int v) {
            mutateNode([&](fg::Node& n) { n.props["count"] = std::to_string(v); }, "Change pin count", "count:" + graph + QString::number(id));
        });
        form_->addRow(cn->defId == "flow.sequence" ? "Output pins" : "Elements", sp);
    }
    // type parameters
    if (cn->def)
        for (auto& tp : cn->def->typeParams) {
            auto* combo = new QComboBox;
            auto it = cn->typeArgs.find(tp.name);
            QString resolved = it != cn->typeArgs.end() ? qs(it->second) : QString();
            combo->addItem("Auto" + (resolved.isEmpty() ? QString() : " (" + resolved + ")"), QString());
            QStringList choices;
            if (!tp.allowed.empty()) for (auto& a : tp.allowed) choices << qs(a);
            else for (auto* t : doc_->registry().types()) choices << qs(t->name);
            for (auto& c : choices) combo->addItem(c, c);
            auto ex = node->typeArgs.find(tp.name);
            if (ex != node->typeArgs.end()) combo->setCurrentIndex(std::max(0, combo->findData(qs(ex->second))));
            QString pname = qs(tp.name);
            connect(combo, qOverload<int>(&QComboBox::activated), this, [=](int idx) {
                QString v = combo->itemData(idx).toString();
                mutateNode([&](fg::Node& n) {
                    if (v.isEmpty()) n.typeArgs.erase(ss(pname));
                    else n.typeArgs[ss(pname)] = ss(v);
                }, "Change node type", QString());
            });
            form_->addRow("Type " + pname, combo);
        }
    auto* comment = new QLineEdit(qs(node->comment));
    connect(comment, &QLineEdit::editingFinished, this, [=] {
        QString t = comment->text();
        mutateNode([&](fg::Node& n) { n.comment = ss(t); }, "Edit node comment", "comment:" + graph + QString::number(id));
    });
    form_->addRow("Comment", comment);
}

} // namespace fged
