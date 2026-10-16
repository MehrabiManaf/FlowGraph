#include "GraphScene.hpp"
#include "CommentItem.hpp"
#include "LinkItem.hpp"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QInputDialog>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QRegularExpressionValidator>
#include <algorithm>
#include <cmath>

#include <set>

namespace fged {

namespace {
const char* kClipMime = "application/x-flowgraph-nodes";

class PinLineEdit : public QLineEdit {
public:
    std::function<void()> onEscape;
protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape && onEscape) { onEscape(); return; }
        QLineEdit::keyPressEvent(e);
    }
};

std::string uniqueEventName(const fg::Script& s, const std::string& base) {
    auto events = s.customEvents();
    auto taken = [&](const std::string& n) {
        for (auto& e : events) if (e.name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 1;; ++i)
        if (!taken(base + "_" + std::to_string(i))) return base + "_" + std::to_string(i);
}
} // namespace

GraphScene::GraphScene(Document* doc, const QString& graphName, QObject* parent) : QGraphicsScene(parent), doc_(doc), name_(graphName) {
    setSceneRect(-20000, -20000, 40000, 40000);
    auto* le = new PinLineEdit();
    editor_ = le;
    le->onEscape = [this] { editCancelled_ = true; hideInplace(); };
    editorProxy_ = new QGraphicsProxyWidget();
    editorProxy_->setWidget(editor_);
    editorProxy_->setZValue(100);
    addItem(editorProxy_);
    editorProxy_->hide();
    connect(editor_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (editNode_ == 0) return;
        if (auto* n = nodeItem(editNode_)) {
            n->setEditorPreview(editPin_, t);              // re-lays out the node for the new text
            QRectF r = n->editorSceneRect(editPin_);       // the editor widget covers the pin's editor box
            if (!r.isNull()) {
                editor_->setFixedWidth(int(std::ceil(r.width())));
                editorProxy_->setPos(r.topLeft());
            }
        }
    });
    connect(editor_, &QLineEdit::editingFinished, this, [this] { onInplaceFinished(); });
    connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        if (syncing_) return;
        QList<int> ids;
        for (auto id : selectedNodeIds()) ids.push_back(id);
        emit selectionNodesChanged(ids);
    });
    animTimer_.setInterval(16);
    connect(&animTimer_, &QTimer::timeout, this, [this] {
        if (animating_) {
            animating_ = false;
            update();
        }
    });
    animTimer_.start();
}

GraphScene::~GraphScene() {
    animTimer_.stop();
    // the proxy widget owns the line edit; clear before the scene tears items down
    removeItem(editorProxy_);
    delete editorProxy_;
    editorProxy_ = nullptr;
    editor_ = nullptr;
}

fg::GraphKind GraphScene::kind() const {
    auto* g = doc_->graph(name_);
    return g ? g->kind : fg::GraphKind::Event;
}

QString GraphScene::linkKey(const fg::Connection& c) {
    return QString("%1:%2>%3:%4").arg(c.fromNode).arg(qs(c.fromPin)).arg(c.toNode).arg(qs(c.toPin));
}

NodeItem* GraphScene::nodeItem(fg::NodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second;
}

LinkItem* GraphScene::linkItem(const fg::Connection& c) const {
    auto it = links_.find(linkKey(c));
    return it == links_.end() ? nullptr : it->second;
}

QList<fg::NodeId> GraphScene::selectedNodeIds() const {
    QList<fg::NodeId> out;
    for (auto* it : selectedItems())
        if (auto* n = dynamic_cast<NodeItem*>(it)) out.push_back(n->nodeId());
    return out;
}

void GraphScene::selectOnly(fg::NodeId id) {
    clearSelection();
    if (auto* n = nodeItem(id)) n->setSelected(true);
}

// ------------------------------------------------------------------ synchronisation
void GraphScene::sync() {
    const fg::Graph* g = doc_->graph(name_);
    const fg::CompiledGraph* cg = doc_->compiledGraph(name_);
    syncing_ = true;
    bool removedAny = false;
    if (!g || !cg) {
        for (auto& [id, n] : nodes_) { removeItem(n); delete n; }
        for (auto& [k, l] : links_) { removeItem(l); delete l; }
        for (auto& [id, c] : comments_) { removeItem(c); delete c; }
        nodes_.clear(); links_.clear(); comments_.clear();
        syncing_ = false;
        return;
    }
    const auto& diags = doc_->program()->diagnostics;
    const std::string gname = name_.toStdString();

    // nodes
    std::set<fg::NodeId> alive;
    bool grabbing = mouseGrabberItem() != nullptr;
    for (const auto& cn : cg->nodes) {
        alive.insert(cn.id);
        NodeItem*& item = nodes_[cn.id];
        if (!item) {
            item = new NodeItem(this, cn.id);
            addItem(item);
            const fg::Node* sn0 = g->findNode(cn.id);
            item->setPos(sn0 ? QPointF(sn0->x, sn0->y) : QPointF(cn.x, cn.y));
        } else if (!grabbing) {
            // positions live in the script (layout edits do not recompile, so the compiled copy can be stale)
            const fg::Node* sn = g->findNode(cn.id);
            QPointF want = sn ? QPointF(sn->x, sn->y) : QPointF(cn.x, cn.y);
            if (item->pos() != want) item->setPos(want);
        }
        bool err = false, warn = false;
        QStringList text;
        for (auto& d : diags)
            if (d.graph == gname && d.node == cn.id) {
                (d.severity == fg::Diagnostic::Severity::Error ? err : warn) = true;
                text << qs(d.message);
            }
        item->refresh(cn, err, warn, text.join('\n'));
    }
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (alive.count(it->first)) { ++it; continue; }
        if (it->first == editNode_) cancelInplace();
        if (it->first == debugNode_) debugNode_ = 0;
        removeItem(it->second);
        delete it->second;
        it = nodes_.erase(it);
        removedAny = true;
    }

    // links
    std::set<QString> liveLinks;
    for (const auto& c : g->connections) {
        const fg::CompiledNode* from = cg->findNode(c.fromNode);
        const fg::CompiledNode* to = cg->findNode(c.toNode);
        if (!from || !to) continue;
        int fp = from->findPinIndex(c.fromPin, fg::PinDir::Out);
        int tp = to->findPinIndex(c.toPin, fg::PinDir::In);
        if (fp < 0 || tp < 0) continue;
        const fg::CompiledPin& pin = from->pins[size_t(fp)];
        bool exec = pin.kind == fg::PinKind::Exec;
        QColor color = pinColor(registry(), pin);
        QString key = linkKey(c);
        liveLinks.insert(key);
        auto it = links_.find(key);
        if (it == links_.end()) {
            auto* l = new LinkItem(this, c, exec, color);
            addItem(l);
            it = links_.emplace(key, l).first;
        } else {
            it->second->setExec(exec);
            it->second->setColor(color);
        }
    }
    for (auto it = links_.begin(); it != links_.end();) {
        if (liveLinks.count(it->first)) { ++it; continue; }
        removeItem(it->second);
        delete it->second;
        it = links_.erase(it);
        removedAny = true;
    }
    for (auto& [k, l] : links_) l->updatePath();

    // comments
    std::set<int> liveComments;
    for (const auto& c : g->comments) {
        liveComments.insert(c.id);
        CommentItem*& item = comments_[c.id];
        if (!item) {
            item = new CommentItem(this, c.id);
            addItem(item);
        }
        if (!grabbing) item->setPos(c.x, c.y);
        item->refresh(c);
    }
    for (auto it = comments_.begin(); it != comments_.end();) {
        if (liveComments.count(it->first)) { ++it; continue; }
        removeItem(it->second);
        delete it->second;
        it = comments_.erase(it);
        removedAny = true;
    }
    syncing_ = false;
    if (removedAny) {
        QList<int> ids;
        for (auto id : selectedNodeIds()) ids.push_back(id);
        emit selectionNodesChanged(ids);
    }
    update();
}

void GraphScene::updateLinksOf(fg::NodeId id) {
    for (auto& [k, l] : links_)
        if (l->connection().fromNode == id || l->connection().toNode == id) l->updatePath();
}

void GraphScene::nodeMoved(NodeItem* item) {
    if (!syncing_) updateLinksOf(item->nodeId());
}

void GraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    QGraphicsScene::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton) commitLayout();
}

void GraphScene::commitLayout() {
    const fg::Graph* g = doc_->graph(name_);
    if (!g) return;
    struct NodePos { fg::NodeId id; double x, y; };
    struct CommentGeo { int id; double x, y, w, h; };
    std::vector<NodePos> moved;
    std::vector<CommentGeo> resized;
    for (auto& [id, item] : nodes_) {
        const fg::Node* n = g->findNode(id);
        if (!n) continue;
        if (std::abs(n->x - item->pos().x()) > 0.01 || std::abs(n->y - item->pos().y()) > 0.01) moved.push_back({id, item->pos().x(), item->pos().y()});
    }
    for (auto& [id, item] : comments_) {
        const fg::CommentBox* c = nullptr;
        for (auto& cb : g->comments) if (cb.id == id) c = &cb;
        if (!c) continue;
        QRectF r = item->rect();
        if (std::abs(c->x - item->pos().x()) > 0.01 || std::abs(c->y - item->pos().y()) > 0.01 || std::abs(c->w - r.width()) > 0.01 || std::abs(c->h - r.height()) > 0.01)
            resized.push_back({id, item->pos().x(), item->pos().y(), r.width(), r.height()});
    }
    if (moved.empty() && resized.empty()) return;
    doc_->edit(moved.empty() ? "Edit comment" : "Move", [&](fg::Script& s) {
        fg::Graph* gr = mutableGraph(s);
        if (!gr) return;
        for (auto& m : moved) if (auto* n = gr->findNode(m.id)) { n->x = m.x; n->y = m.y; }
        for (auto& r : resized) if (auto* c = gr->findComment(r.id)) { c->x = r.x; c->y = r.y; c->w = r.w; c->h = r.h; }
    }, EditKind::Layout);
}

// ------------------------------------------------------------------ debugging
void GraphScene::setDebugNode(fg::NodeId id) {
    fg::NodeId old = debugNode_;
    debugNode_ = id;
    if (auto* n = nodeItem(old)) n->update();
    if (auto* n = nodeItem(id)) n->update();
}

void GraphScene::toggleBreakpoint(fg::NodeId id) {
    doc_->debugger().toggleBreakpoint(name_.toStdString(), id);
    if (auto* n = nodeItem(id)) n->update();
}

void GraphScene::fireLink(fg::NodeId from, int execOutOrdinal, fg::NodeId to) {
    NodeItem* fi = nodeItem(from);
    if (!fi) return;
    const fg::CompiledNode& cn = fi->compiled();
    if (execOutOrdinal < 0 || size_t(execOutOrdinal) >= cn.execOut.size()) return;
    const std::string& pin = cn.pins[size_t(cn.execOut[size_t(execOutOrdinal)])].name;
    for (auto& [k, l] : links_) {
        const auto& c = l->connection();
        if (c.fromNode == from && c.fromPin == pin && c.toNode == to) l->fire();
    }
    if (auto* t = nodeItem(to)) t->flash();
    requestAnimation();
}

QString GraphScene::pinTooltip(const PinRef& ref, const QString& doc) const {
    QString t = qs(ref.pin) + "  (" + (ref.kind == fg::PinKind::Exec ? QString("execution") : (ref.type ? qs(ref.type->name) : QString("?"))) + ")";
    if (ref.wildcard) t += "  [type resolved on connect]";
    if (!doc.isEmpty()) t += "\n" + doc;
    QString v;
    if (ref.kind == fg::PinKind::Data && pinValueProvider && pinValueProvider(ref, v)) t += "\nValue: " + v;
    return t;
}

// ------------------------------------------------------------------ pin editing
void GraphScene::setPinDefault(fg::NodeId node, const std::string& pin, const fg::Value& v) {
    doc_->edit("Edit value", [&](fg::Script& s) {
        if (auto* g = mutableGraph(s))
            if (auto* n = g->findNode(node)) n->defaults[pin] = v;
    }, EditKind::Structural, QString("pin:%1:%2:%3").arg(name_).arg(node).arg(qs(pin)));
}

void GraphScene::startInplaceEdit(NodeItem* item, const std::string& pin, const QPointF& scenePos, double width, const QString& text, const fg::TypeDesc* type) {
    onInplaceFinished();
    if (type && type->kind == fg::TypeKind::Int) editor_->setValidator(new QRegularExpressionValidator(QRegularExpression("-?\\d{0,18}"), editor_));
    else if (type && type->kind == fg::TypeKind::Float) editor_->setValidator(new QRegularExpressionValidator(QRegularExpression("-?\\d*\\.?\\d*([eE][-+]?\\d*)?"), editor_));
    else editor_->setValidator(nullptr);
    editNode_ = item->nodeId();
    editPin_ = pin;
    editOriginal_ = text;
    editCancelled_ = false;
    editor_->setText(text);
    editor_->setFixedWidth(int(std::max(width, 24.0)));
    editorProxy_->setPos(scenePos);
    editorProxy_->show();
    editor_->setFocus();
    editor_->selectAll();
}

void GraphScene::cancelInplace() {
    editNode_ = 0;      // forget the target first: hiding the editor emits editingFinished
    hideInplace();
}

void GraphScene::hideInplace() {
    editorProxy_->hide();
    editor_->clearFocus();
}

void GraphScene::onInplaceFinished() {
    if (editNode_ == 0) return;
    fg::NodeId id = editNode_;
    std::string pin = editPin_;
    QString text = editor_->text();
    bool cancelled = editCancelled_;
    editNode_ = 0;
    hideInplace();
    NodeItem* item = nodeItem(id);
    if (!item) return;
    item->clearEditorPreview();
    if (cancelled || text == editOriginal_) return;
    for (auto& p : item->compiled().pins)
        if (p.name == pin && p.dir == fg::PinDir::In && p.kind == fg::PinKind::Data) {
            setPinDefault(id, pin, parseDefault(p, text));
            return;
        }
}

void GraphScene::chooseEnumValue(NodeItem* item, const std::string& pin, const fg::TypeDesc* type, const QPoint& screenPos) {
    if (!type) return;
    QMenu menu;
    for (auto& e : type->enumValues) {
        QAction* a = menu.addAction(qs(e.name));
        a->setData(qlonglong(e.value));
        if (!e.doc.empty()) a->setToolTip(qs(e.doc));
    }
    if (QAction* a = menu.exec(screenPos)) setPinDefault(item->nodeId(), pin, fg::Value(int64_t(a->data().toLongLong())));
}

// ------------------------------------------------------------------ link dragging
void GraphScene::beginLinkDrag(const PinRef& from, const QPointF& scenePos, bool breakLinks) {
    if (breakLinks) {
        breakPinLinks(from);
        return;
    }
    drag_ = from;
    QColor color = from.kind == fg::PinKind::Exec ? qApp->palette().windowText().color() : (from.type ? rgb(registry().typeColor(from.type)) : QColor(160, 160, 168));
    dragLink_ = new LinkItem(this, fg::Connection{}, from.kind == fg::PinKind::Exec, color, true);
    addItem(dragLink_);
    updateLinkDrag(scenePos);
}

void GraphScene::updateLinkDrag(const QPointF& scenePos) {
    if (!dragLink_) return;
    NodeItem* n = nodeItem(drag_.node);
    QPointF pinPos = n ? n->pinScenePos(drag_.pin, drag_.dir) : scenePos;
    if (drag_.dir == fg::PinDir::Out) dragLink_->setTemporaryEnds(pinPos, scenePos);
    else dragLink_->setTemporaryEnds(scenePos, pinPos);
}

void GraphScene::finishLinkDrag(const QPointF& scenePos, const QPoint& screenPos) {
    PinRef from = drag_;
    drag_ = PinRef();
    if (dragLink_) {
        removeItem(dragLink_);
        delete dragLink_;
        dragLink_ = nullptr;
    }
    std::optional<PinRef> target;
    bool overNode = false;
    for (QGraphicsItem* it : items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder)) {
        if (auto* n = dynamic_cast<NodeItem*>(it)) {
            overNode = true;
            target = n->pinAt(scenePos);
            break;
        }
    }
    if (target) {
        if (target->node == from.node && target->pin == from.pin && target->dir == from.dir) return;
        QString why;
        if (!connectPins(from, *target, &why) && !why.isEmpty()) emit message(why);
    } else if (!overNode) {
        emit paletteRequested(from, scenePos, screenPos);
    }
}

namespace {
void addConnection(fg::Graph& g, const fg::Connection& c, bool exec) {
    auto& v = g.connections;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const fg::Connection& o) {
        if (o == c) return true;
        return exec ? (o.fromNode == c.fromNode && o.fromPin == c.fromPin) : (o.toNode == c.toNode && o.toPin == c.toPin);
    }), v.end());
    v.push_back(c);
}
} // namespace

bool GraphScene::connectPins(const PinRef& a, const PinRef& b, QString* why) {
    auto fail = [&](const QString& m) { if (why) *why = m; return false; };
    if (a.dir == b.dir) return fail("Connect an output pin to an input pin");
    const PinRef& out = a.dir == fg::PinDir::Out ? a : b;
    const PinRef& in = a.dir == fg::PinDir::Out ? b : a;
    if (out.node == in.node) return fail("A node cannot be connected to itself");
    if (out.kind != in.kind) return fail("Execution pins only connect to execution pins");
    if (out.kind == fg::PinKind::Data) {
        // a pin whose type is still open only fixes the "shape" (array / map) it can take
        auto shapeOk = [](const PinRef& w, const PinRef& other) {
            if (!w.type || !other.type || other.type->kind == fg::TypeKind::Any) return true;
            return w.type->isContainer() ? other.type->kind == w.type->kind : true;
        };
        bool ok = (out.wildcard || in.wildcard) ? (shapeOk(out.wildcard ? out : in, out.wildcard ? in : out) && (!(out.wildcard && in.wildcard) || shapeOk(in, out)))
                                                 : registry().canConnect(out.type, in.type);
        if (!ok)
            return fail(QString("Cannot connect %1 to %2").arg(out.type ? qs(out.type->name) : "?", in.type ? qs(in.type->name) : "?"));
    }
    fg::Connection c{out.node, out.pin, in.node, in.pin};
    bool exec = out.kind == fg::PinKind::Exec;
    doc_->edit("Connect pins", [&](fg::Script& s) {
        if (auto* g = mutableGraph(s)) addConnection(*g, c, exec);
    });
    return true;
}

void GraphScene::breakPinLinks(const PinRef& p) {
    doc_->edit("Break links", [&](fg::Script& s) {
        auto* g = mutableGraph(s);
        if (!g) return;
        auto& v = g->connections;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const fg::Connection& c) {
            return p.dir == fg::PinDir::Out ? (c.fromNode == p.node && c.fromPin == p.pin) : (c.toNode == p.node && c.toPin == p.pin);
        }), v.end());
    });
}

void GraphScene::insertReroute(const fg::Connection& c, const QPointF& scenePos) {
    NodeItem* from = nodeItem(c.fromNode);
    if (!from) return;
    for (auto& p : from->compiled().pins)
        if (p.name == c.fromPin && p.kind == fg::PinKind::Exec) {
            emit message("Reroute nodes are only available for data links");
            return;
        }
    doc_->edit("Add reroute", [&](fg::Script& s) {
        auto* g = mutableGraph(s);
        if (!g) return;
        g->connections.erase(std::remove(g->connections.begin(), g->connections.end(), c), g->connections.end());
        fg::NodeId id = g->addNode("reroute", scenePos.x() - 14, scenePos.y() - 9).id;
        g->connections.push_back({c.fromNode, c.fromPin, id, "In"});
        g->connections.push_back({id, "Out", c.toNode, c.toPin});
    });
}

// ------------------------------------------------------------------ adding / removing nodes
fg::NodeId GraphScene::addNodeFromTemplate(const fg::NodeTemplate& t, const QPointF& pos, const PinRef* from) {
    fg::NodeId newId = 0;
    std::string pinName;
    bool connect = false;
    fg::PinFilter filter;
    if (from && from->valid()) {
        filter.kind = from->kind;
        filter.dir = from->dir;
        filter.type = from->wildcard ? nullptr : from->type;
        connect = fg::templateAccepts(registry(), t, filter, &pinName);
    }
    doc_->edit("Add node", [&](fg::Script& s) {
        fg::Graph* g = mutableGraph(s);
        if (!g) return;
        fg::Node& n = fg::instantiate(*g, t, pos.x(), pos.y());
        newId = n.id;
        if (n.def == "event.custom") n.props["name"] = uniqueEventName(s, n.prop("name", "NewEvent"));
        if (connect) {
            fg::Connection c = from->dir == fg::PinDir::Out ? fg::Connection{from->node, from->pin, newId, pinName}
                                                            : fg::Connection{newId, pinName, from->node, from->pin};
            addConnection(*g, c, from->kind == fg::PinKind::Exec);
        }
    });
    selectOnly(newId);
    return newId;
}

fg::NodeId GraphScene::addVariableNode(const QString& name, const QString& scope, bool isSet, const QPointF& pos) {
    fg::NodeId id = 0;
    doc_->edit(isSet ? "Add Set variable" : "Add Get variable", [&](fg::Script& s) {
        if (auto* g = mutableGraph(s)) {
            fg::Node& n = g->addNode(isSet ? "var.set" : "var.get", pos.x(), pos.y());
            n.props["name"] = ss(name);
            n.props["scope"] = ss(scope);
            id = n.id;
        }
    });
    selectOnly(id);
    return id;
}

void GraphScene::deleteSelected() {
    if (editNode_ != 0) cancelInplace();
    std::set<fg::NodeId> nodes;
    std::vector<fg::Connection> links;
    std::set<int> comments;
    for (auto* it : selectedItems()) {
        if (auto* n = dynamic_cast<NodeItem*>(it)) {
            if (n->compiled().op != fg::NodeOp::FunctionEntry) nodes.insert(n->nodeId());
        } else if (auto* l = dynamic_cast<LinkItem*>(it)) links.push_back(l->connection());
        else if (auto* c = dynamic_cast<CommentItem*>(it)) comments.insert(c->commentId());
    }
    if (nodes.empty() && links.empty() && comments.empty()) return;
    doc_->edit("Delete", [&](fg::Script& s) {
        auto* g = mutableGraph(s);
        if (!g) return;
        auto& cv = g->connections;
        cv.erase(std::remove_if(cv.begin(), cv.end(), [&](const fg::Connection& c) {
            return nodes.count(c.fromNode) || nodes.count(c.toNode) || std::find(links.begin(), links.end(), c) != links.end();
        }), cv.end());
        auto& nv = g->nodes;
        nv.erase(std::remove_if(nv.begin(), nv.end(), [&](const fg::Node& n) { return nodes.count(n.id) > 0; }), nv.end());
        auto& mv = g->comments;
        mv.erase(std::remove_if(mv.begin(), mv.end(), [&](const fg::CommentBox& c) { return comments.count(c.id) > 0; }), mv.end());
    });
}

QByteArray GraphScene::buildClip(const std::set<fg::NodeId>& ids) const {
    const fg::Graph* g = doc_->graph(name_);
    if (!g || ids.empty()) return {};
    fg::Script tmp;
    tmp.name = "clipboard";
    tmp.graphs.emplace_back();
    fg::Graph& out = tmp.graphs.back();
    out.name = "clip";
    for (auto& n : g->nodes)
        if (ids.count(n.id)) out.nodes.push_back(n);
    for (auto& c : g->connections)
        if (ids.count(c.fromNode) && ids.count(c.toNode)) out.connections.push_back(c);
    return QByteArray::fromStdString(tmp.toText(0));
}

void GraphScene::copySelected() {
    std::set<fg::NodeId> ids;
    for (auto id : selectedNodeIds()) {
        NodeItem* n = nodeItem(id);
        if (n && n->compiled().op != fg::NodeOp::FunctionEntry) ids.insert(id);
    }
    QByteArray data = buildClip(ids);
    if (data.isEmpty()) return;
    auto* mime = new QMimeData;
    mime->setData(kClipMime, data);
    mime->setText(QString::fromUtf8(data));
    QApplication::clipboard()->setMimeData(mime);
}

bool GraphScene::canPaste() const { return QApplication::clipboard()->mimeData()->hasFormat(kClipMime); }

void GraphScene::paste(const QPointF& at) {
    const QMimeData* m = QApplication::clipboard()->mimeData();
    if (m->hasFormat(kClipMime)) pasteClip(m->data(kClipMime), at, false);
}

void GraphScene::pasteClip(const QByteArray& data, const QPointF& at, bool keepOffset) {
    fg::Script clip;
    try {
        clip = fg::Script::parse(data.toStdString());
    } catch (const std::exception&) {
        return;
    }
    if (clip.graphs.empty() || clip.graphs.front().nodes.empty()) return;
    const fg::Graph& src = clip.graphs.front();
    double minX = 1e18, minY = 1e18;
    for (auto& n : src.nodes) { minX = std::min(minX, n.x); minY = std::min(minY, n.y); }
    QPointF delta = keepOffset ? at : QPointF(at.x() - minX, at.y() - minY);
    std::vector<fg::NodeId> created;
    doc_->edit("Paste", [&](fg::Script& s) {
        fg::Graph* g = mutableGraph(s);
        if (!g) return;
        std::map<fg::NodeId, fg::NodeId> remap;
        for (auto n : src.nodes) {
            fg::NodeId old = n.id;
            n.id = g->nextId++;
            n.x += delta.x();
            n.y += delta.y();
            if (n.def == "event.custom") n.props["name"] = uniqueEventName(s, n.prop("name", "NewEvent"));
            remap[old] = n.id;
            created.push_back(n.id);
            g->nodes.push_back(std::move(n));
        }
        for (auto c : src.connections) {
            c.fromNode = remap[c.fromNode];
            c.toNode = remap[c.toNode];
            g->connections.push_back(std::move(c));
        }
    });
    clearSelection();
    for (auto id : created)
        if (auto* n = nodeItem(id)) n->setSelected(true);
}

void GraphScene::duplicateSelected() {
    std::set<fg::NodeId> ids;
    for (auto id : selectedNodeIds()) {
        NodeItem* n = nodeItem(id);
        if (n && n->compiled().op != fg::NodeOp::FunctionEntry) ids.insert(id);
    }
    QByteArray data = buildClip(ids);
    if (!data.isEmpty()) pasteClip(data, QPointF(40, 40), true);
}

void GraphScene::addComment(const QPointF& at) {
    QRectF r;
    for (auto* it : selectedItems())
        if (dynamic_cast<NodeItem*>(it)) r = r.united(it->sceneBoundingRect());
    if (r.isNull()) r = QRectF(at, QSizeF(320, 180));
    else r.adjust(-30, -50, 30, 30);
    doc_->edit("Add comment", [&](fg::Script& s) {
        if (auto* g = mutableGraph(s)) {
            auto& c = g->addComment(r.x(), r.y(), r.width(), r.height());
            c.text = "Comment";
        }
    });
}

void GraphScene::editCommentText(int id) {
    const fg::Graph* g = doc_->graph(name_);
    if (!g) return;
    QString cur;
    for (auto& c : g->comments) if (c.id == id) cur = qs(c.text);
    bool ok = false;
    QString t = QInputDialog::getText(views().isEmpty() ? static_cast<QWidget*>(nullptr) : static_cast<QWidget*>(views().front()), "Comment", "Text:", QLineEdit::Normal, cur, &ok);
    if (!ok) return;
    doc_->edit("Edit comment", [&](fg::Script& s) {
        if (auto* gr = mutableGraph(s))
            if (auto* c = gr->findComment(id)) c->text = ss(t);
    }, EditKind::Layout);
}

void GraphScene::setNodeComment(fg::NodeId id, const QString& text) {
    doc_->edit("Edit node comment", [&](fg::Script& s) {
        if (auto* g = mutableGraph(s))
            if (auto* n = g->findNode(id)) n->comment = ss(text);
    }, EditKind::Structural, QString("comment:%1:%2").arg(name_).arg(id));
}

QRectF GraphScene::contentRect() const {
    QRectF r;
    for (auto& [id, n] : nodes_) r = r.united(n->sceneBoundingRect());
    for (auto& [id, c] : comments_) r = r.united(c->sceneBoundingRect());
    return r;
}

} // namespace fged
