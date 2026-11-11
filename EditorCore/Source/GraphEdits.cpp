// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraphEditorCore/GraphEdits.hpp>

#include <FlowGraph/Compiler.hpp>
#include <FlowGraph/Intellisense.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace fged::core {

namespace {

constexpr const char* clipName = "clipboard";

// `c` added; the link an input of data, or an output of execution, had goes (one each).
void addConnection(fg::Graph& g, const fg::Connection& c, bool exec) {
    auto& v = g.connections;
    std::erase_if(v, [&](const fg::Connection& o) {
        if (o == c)
            return true;
        return exec ? (o.fromNode == c.fromNode && o.fromPin == c.fromPin) : (o.toNode == c.toNode && o.toPin == c.toPin);
    });
    v.push_back(c);
}

} // namespace

PinEnd PinEnd::of(const fg::CompiledNode& node, const std::string& pin, fg::PinDir dir) {
    for (const fg::CompiledPin& p : node.pins) {
        if (p.dir == dir && p.name == pin)
            return {node.id, p.name, p.kind, p.dir, p.type, p.wildcard};
    }
    return {};
}

LinkProblem GraphEdits::linkProblem(const PinEnd& a, const PinEnd& b) const {
    if (a.dir == b.dir)
        return LinkProblem::SameDirection;
    const PinEnd& out = a.dir == fg::PinDir::Out ? a : b;
    const PinEnd& in = a.dir == fg::PinDir::Out ? b : a;
    if (out.node == in.node)
        return LinkProblem::SameNode;
    if (out.kind != in.kind)
        return LinkProblem::KindsDiffer;
    if (out.kind == fg::PinKind::Data) {
        // a pin whose type is still open only fixes the shape (array, map) it can take
        const auto shapeOk = [](const PinEnd& open, const PinEnd& other) {
            if (!open.type || !other.type || other.type->kind == fg::TypeKind::Any)
                return true;
            return open.type->isContainer() ? other.type->kind == open.type->kind : true;
        };
        const bool ok = (out.wildcard || in.wildcard)
                            ? (shapeOk(out.wildcard ? out : in, out.wildcard ? in : out) && (!(out.wildcard && in.wildcard) || shapeOk(in, out)))
                            : doc_.registry().canConnect(out.type, in.type);
        if (!ok)
            return LinkProblem::TypesDiffer;
    }
    // what the script could not run: a cycle of pure data (a value made of itself), an execution loop with no way out
    if (const fg::CompiledGraph* cg = doc_.compiledGraph(graph_)) {
        const auto index = [&](fg::NodeId id) {
            const auto it = cg->nodeIndex.find(id);
            return it == cg->nodeIndex.end() ? -1 : it->second;
        };
        const int from = index(out.node), to = index(in.node);
        if (out.kind == fg::PinKind::Data && fg::closesPureDataCycle(*cg, from, to))
            return LinkProblem::DataCycle;
        if (out.kind == fg::PinKind::Exec && fg::closesEndlessExecLoop(*cg, from, to))
            return LinkProblem::EndlessLoop;
    }
    return LinkProblem::None;
}

bool GraphEdits::link(const PinEnd& a, const PinEnd& b, const std::string& label, LinkProblem* why) {
    const LinkProblem problem = linkProblem(a, b);
    if (why)
        *why = problem;
    if (problem != LinkProblem::None)
        return false;
    const PinEnd& out = a.dir == fg::PinDir::Out ? a : b;
    const PinEnd& in = a.dir == fg::PinDir::Out ? b : a;
    const fg::Connection c{out.node, out.pin, in.node, in.pin};
    const bool exec = out.kind == fg::PinKind::Exec;
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = graphOf(s))
            addConnection(*g, c, exec);
    });
    return true;
}

void GraphEdits::breakLinks(const PinEnd& p, const std::string& label) {
    const fg::Graph* g = doc_.graph(graph_);
    const auto linked = [&](const fg::Connection& c) {
        return p.dir == fg::PinDir::Out ? (c.fromNode == p.node && c.fromPin == p.pin) : (c.toNode == p.node && c.toPin == p.pin);
    };
    if (!g || std::none_of(g->connections.begin(), g->connections.end(), linked))
        return;
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* graph = graphOf(s))
            std::erase_if(graph->connections, linked);
    });
}

bool GraphEdits::insertReroute(const fg::Connection& c, double x, double y, const std::string& label) {
    const fg::CompiledGraph* cg = doc_.compiledGraph(graph_);
    const fg::CompiledNode* from = cg ? cg->findNode(c.fromNode) : nullptr;
    const fg::CompiledPin* pin = from ? from->findPin(c.fromPin, fg::PinDir::Out, fg::PinKind::Data) : nullptr;
    if (!pin)
        return false;       // execution (or not there): reroutes are for data
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* g = graphOf(s);
        if (!g)
            return;
        std::erase(g->connections, c);
        const fg::NodeId id = g->addNode("reroute", x, y).id;
        g->connections.push_back({c.fromNode, c.fromPin, id, "In"});
        g->connections.push_back({id, "Out", c.toNode, c.toPin});
    });
    return true;
}

fg::NodeId GraphEdits::addNode(const fg::NodeTemplate& t, double x, double y, const PinEnd* from, const std::string& label,
                               std::string* linkedPin, const std::string& coalesceKey) {
    std::string pinName;
    bool connect = false;
    if (from && from->valid()) {
        fg::PinFilter filter;
        filter.kind = from->kind;
        filter.dir = from->dir;
        filter.type = from->wildcard ? nullptr : from->type;
        connect = fg::templateAccepts(doc_.registry(), t, filter, &pinName, doc_.types().get());
    }
    fg::NodeId made = 0;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* g = graphOf(s);
        if (!g)
            return;
        made = fg::instantiate(*g, t, x, y).id;
        if (connect) {
            const fg::Connection c = from->dir == fg::PinDir::Out ? fg::Connection{from->node, from->pin, made, pinName}
                                                                  : fg::Connection{made, pinName, from->node, from->pin};
            addConnection(*g, c, from->kind == fg::PinKind::Exec);
        }
    }, EditKind::Structural, coalesceKey);
    if (linkedPin)
        *linkedPin = connect ? pinName : std::string();
    return made;
}

bool GraphEdits::removable(const fg::CompiledNode& node) {
    return node.op != fg::NodeOp::Event && node.op != fg::NodeOp::FunctionEntry;
}

void GraphEdits::remove(const std::set<fg::NodeId>& nodes, const std::vector<fg::Connection>& links, const std::set<int>& comments,
                        const std::string& label) {
    std::set<fg::NodeId> gone;
    if (const fg::CompiledGraph* cg = doc_.compiledGraph(graph_)) {
        for (fg::NodeId id : nodes) {
            const fg::CompiledNode* n = cg->findNode(id);
            if (!n || removable(*n))
                gone.insert(id);
        }
    } else {
        gone = nodes;
    }
    if (gone.empty() && links.empty() && comments.empty())
        return;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* g = graphOf(s);
        if (!g)
            return;
        std::erase_if(g->connections, [&](const fg::Connection& c) {
            return gone.contains(c.fromNode) || gone.contains(c.toNode) || std::find(links.begin(), links.end(), c) != links.end();
        });
        std::erase_if(g->nodes, [&](const fg::Node& n) { return gone.contains(n.id); });
        std::erase_if(g->comments, [&](const fg::CommentBox& c) { return comments.contains(c.id); });
    });
}

void GraphEdits::place(const std::vector<NodePlace>& nodes, const std::vector<CommentPlace>& comments, const std::string& label,
                       const std::string& coalesceKey) {
    const fg::Graph* g = doc_.graph(graph_);
    if (!g)
        return;
    const auto moved = [](double a, double b) { return std::abs(a - b) > 0.01; };
    std::vector<NodePlace> changedNodes;
    for (const NodePlace& p : nodes) {
        const fg::Node* n = g->findNode(p.node);
        if (n && (moved(n->x, p.x) || moved(n->y, p.y)))
            changedNodes.push_back(p);
    }
    std::vector<CommentPlace> changedComments;
    for (const CommentPlace& p : comments) {
        for (const fg::CommentBox& c : g->comments) {
            if (c.id == p.comment && (moved(c.x, p.x) || moved(c.y, p.y) || moved(c.w, p.w) || moved(c.h, p.h)))
                changedComments.push_back(p);
        }
    }
    if (changedNodes.empty() && changedComments.empty())
        return;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* graph = graphOf(s);
        if (!graph)
            return;
        for (const NodePlace& p : changedNodes) {
            if (fg::Node* n = graph->findNode(p.node)) {
                n->x = p.x;
                n->y = p.y;
            }
        }
        for (const CommentPlace& p : changedComments) {
            if (fg::CommentBox* c = graph->findComment(p.comment)) {
                c->x = p.x;
                c->y = p.y;
                c->w = p.w;
                c->h = p.h;
            }
        }
    }, EditKind::Layout, coalesceKey);
}

std::string GraphEdits::copy(const std::set<fg::NodeId>& nodes) const {
    const fg::Graph* g = doc_.graph(graph_);
    const fg::CompiledGraph* cg = doc_.compiledGraph(graph_);
    if (!g || nodes.empty())
        return {};
    std::set<fg::NodeId> taken;
    for (fg::NodeId id : nodes) {
        const fg::CompiledNode* n = cg ? cg->findNode(id) : nullptr;
        if (!n || removable(*n))
            taken.insert(id);
    }
    if (taken.empty())
        return {};
    fg::Script clip;
    clip.name = clipName;
    fg::Graph& out = clip.graphs.emplace_back();
    out.name = "clip";
    for (const fg::Node& n : g->nodes) {
        if (taken.contains(n.id))
            out.nodes.push_back(n);
    }
    for (const fg::Connection& c : g->connections) {
        if (taken.contains(c.fromNode) && taken.contains(c.toNode))
            out.connections.push_back(c);
    }
    return clip.toText(0);
}

bool GraphEdits::isClip(const std::string& text) {
    if (text.find(clipName) == std::string::npos)
        return false;
    try {
        const fg::Script clip = fg::Script::parse(text);
        return clip.name == clipName && !clip.graphs.empty() && !clip.graphs.front().nodes.empty();
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<fg::NodeId> GraphEdits::paste(const std::string& text, double x, double y, bool byOffset, const std::string& label) {
    if (!isClip(text))
        return {};
    const fg::Script clip = fg::Script::parse(text);
    const fg::Graph& src = clip.graphs.front();
    double minX = 1e18, minY = 1e18;
    for (const fg::Node& n : src.nodes) {
        minX = std::min(minX, n.x);
        minY = std::min(minY, n.y);
    }
    const double dx = byOffset ? x : x - minX, dy = byOffset ? y : y - minY;
    std::vector<fg::NodeId> made;
    doc_.edit(label, [&](fg::Script& s) {
        fg::Graph* g = graphOf(s);
        if (!g)
            return;
        std::map<fg::NodeId, fg::NodeId> remap;
        for (fg::Node n : src.nodes) {
            if (n.def.rfind("event:", 0) == 0) {        // an event has one handler only
                bool handled = false;
                for (const fg::Graph& other : s.graphs)
                    for (const fg::Node& o : other.nodes)
                        handled = handled || o.def == n.def;
                if (handled)
                    continue;
            }
            const fg::NodeId old = n.id;
            n.id = g->nextId++;
            n.x += dx;
            n.y += dy;
            remap[old] = n.id;
            made.push_back(n.id);
            g->nodes.push_back(std::move(n));
        }
        for (fg::Connection c : src.connections) {
            if (!remap.contains(c.fromNode) || !remap.contains(c.toNode))
                continue;
            c.fromNode = remap[c.fromNode];
            c.toNode = remap[c.toNode];
            g->connections.push_back(std::move(c));
        }
    });
    return made;
}

void GraphEdits::setPinValue(fg::NodeId node, const std::string& pin, const fg::Value& value, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = graphOf(s))
            if (fg::Node* n = g->findNode(node))
                n->defaults[pin] = value;
    }, EditKind::Structural, "pin:" + graph_ + ":" + std::to_string(node) + ":" + pin);
}

int GraphEdits::addComment(double x, double y, double w, double h, const std::string& text, const std::string& label) {
    int id = 0;
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = graphOf(s)) {
            fg::CommentBox& c = g->addComment(x, y, w, h);
            c.text = text;
            id = c.id;
        }
    });
    return id;
}

void GraphEdits::setCommentText(int comment, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = graphOf(s))
            if (fg::CommentBox* c = g->findComment(comment))
                c->text = text;
    }, EditKind::Layout, "comment-box:" + graph_ + ":" + std::to_string(comment));
}

void GraphEdits::setNodeComment(fg::NodeId node, const std::string& text, const std::string& label) {
    doc_.edit(label, [&](fg::Script& s) {
        if (fg::Graph* g = graphOf(s))
            if (fg::Node* n = g->findNode(node))
                n->comment = text;
    }, EditKind::Structural, "comment:" + graph_ + ":" + std::to_string(node));
}

} // namespace fged::core
