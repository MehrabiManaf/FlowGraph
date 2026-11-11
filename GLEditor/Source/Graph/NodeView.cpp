// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Graph/NodeView.hpp"

#include "Graph/GraphView.hpp"
#include "NodeTexts.hpp"

#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Widgets/Bevel.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>

namespace fged::gl {

namespace {

// A reroute: a small pill, its input and its output circles near its ends.
constexpr float rerouteWidth = 36, rerouteHeight = 18, rerouteInset = 10, rerouteHit = 5.5f;
constexpr glw::Insets titleMargins{10, 6, 10, 6};
constexpr glw::Insets pinMargins{6, 2, 6, 2};
constexpr float pinSpacing = 5, sideSpacing = 14;
constexpr glw::Size dataConnector{12, 12}, arrayConnector{arrayConnectorWidth, 12}, execConnector{12, 15};
constexpr float editorMinimum = 22, editorMaximum = 400, editorHeight = 20, checkSize = 14;
constexpr std::size_t valueShown = 200;                 // of a long text, what its box shows

// "In" and "Then" are what every execution pin is: their names are not shown.
bool hiddenLabel(const fg::CompiledPin& p) {
    return p.kind == fg::PinKind::Exec && (p.name == "In" || p.name == "Then");
}

std::string typeName(const fg::TypeDesc* t) {
    if (!t)
        return {};
    switch (t->kind) {
    case fg::TypeKind::Array: return "array<" + typeName(t->elem) + ">";
    case fg::TypeKind::Map: return "map<" + typeName(t->keyType) + ", " + typeName(t->elem) + ">";
    default: return t->name;
    }
}

} // namespace

NodeView::NodeView(GraphView& graph, fg::NodeId id) : graph_(graph), id_(id) {}

void NodeView::setNode(const fg::CompiledNode& node, bool error, bool warning, std::string diagnostics) {
    node_ = node;
    error_ = error;
    warning_ = warning;
    diagnostics_ = std::move(diagnostics);
    reroute_ = node.op == fg::NodeOp::Reroute;
    title_ = nodeTitle(node);
    header_ = node.def ? headerColor(node.def->style) : rgb(120, 40, 120);
    laidOut_ = false;
    invalidateLayout();
    invalidatePaint();
}

void NodeView::setSelected(bool selected) {
    if (selected == selected_)
        return;
    selected_ = selected;
    invalidatePaint();
}

void NodeView::onStyleChanged() {
    laidOut_ = false;
    invalidateLayout();
}

glw::Size NodeView::measureContent(glw::Size) {
    if (!laidOut_)
        layout();
    return {width_, height_};
}

void NodeView::layout() {
    const glw::UiContext* ui = context();
    if (!ui)
        return;
    laidOut_ = true;
    const GraphColors colors = GraphColors::of(ui->theme);
    const fg::Registry& reg = graph_.registry();
    const glw::text::TextStyle textStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize};
    inputs_.clear();
    outputs_.clear();

    const auto make = [&](const fg::CompiledPin& p) {
        Pin v;
        v.name = p.name;
        v.label = hiddenLabel(p) ? std::string() : pinLabel(node_, p.name);
        v.kind = p.kind;
        v.dir = p.dir;
        v.color = pinColor(reg, p, colors);
        v.keyColor = p.type && p.type->kind == fg::TypeKind::Map && !p.wildcard && p.type->keyType ? rgb(reg.typeColor(p.type->keyType)) : v.color;
        v.connected = p.connected;
        v.byRef = p.byRef;
        v.isConst = p.isConst;
        v.wildcard = p.wildcard;
        v.container = !p.type ? 0 : p.type->kind == fg::TypeKind::Array ? 1 : p.type->kind == fg::TypeKind::Map ? 2 : 0;
        v.typeName = p.kind == fg::PinKind::Exec ? std::string() : typeName(p.type);
        v.doc = pinDoc(node_.defId, p.name, p.doc);
        if (p.kind == fg::PinKind::Data && p.dir == fg::PinDir::In && !p.connected && !p.byRef && p.type) {
            switch (p.type->kind) {
            case fg::TypeKind::Bool: v.editor = Editor::Check; break;
            case fg::TypeKind::Int:
            case fg::TypeKind::Float:
            case fg::TypeKind::String:
            case fg::TypeKind::Byte:
            case fg::TypeKind::Bytes: v.editor = Editor::Line; break;
            case fg::TypeKind::Enum: v.editor = Editor::Choice; break;
            default: break;
            }
            v.value = v.editor == Editor::Check ? (p.def.toBool() ? "x" : "") : defaultText(p);
        }
        if (!v.label.empty())
            v.labelText.emplace(v.label, textStyle);
        if (v.editor == Editor::Line || v.editor == Editor::Choice) {
            std::string shown = v.value.size() > valueShown ? v.value.substr(0, valueShown) + "…" : v.value;
            std::replace(shown.begin(), shown.end(), '\n', ' ');
            v.valueText.emplace(std::move(shown), textStyle);
        }
        return v;
    };
    for (int i : node_.execIn)
        if (!node_.pins[std::size_t(i)].hidden)
            inputs_.push_back(make(node_.pins[std::size_t(i)]));
    for (int i : node_.dataIn)
        if (!node_.pins[std::size_t(i)].hidden)
            inputs_.push_back(make(node_.pins[std::size_t(i)]));
    for (int i : node_.execOut)
        if (!node_.pins[std::size_t(i)].hidden)
            outputs_.push_back(make(node_.pins[std::size_t(i)]));
    for (int i : node_.dataOut)
        if (!node_.pins[std::size_t(i)].hidden)
            outputs_.push_back(make(node_.pins[std::size_t(i)]));

    if (reroute_) {
        layoutReroute();
        return;
    }

    titleText_.emplace(title_, glw::text::TextStyle{{&ui->fonts.defaultFont()}, ui->theme.fontSize + 1});
    const glw::Size titleSize = titleText_->size();
    titleRect_ = {0, 0, titleMargins.start + titleSize.width + titleMargins.end, titleMargins.top + titleSize.height + titleMargins.bottom};
    const float textHeight = glw::text::TextLayout("Ag", textStyle).size().height;
    const float pinHeight = std::max({textHeight, editorHeight, execConnector.height}) + pinMargins.top + pinMargins.bottom;

    const auto connectorSize = [](const Pin& v) {
        return v.kind == fg::PinKind::Exec ? execConnector : v.container == 1 ? arrayConnector : dataConnector;
    };
    const auto labelSize = [](const Pin& v) { return v.labelText ? v.labelText->size() : glw::Size{}; };

    // the inputs from the start; how wide they are
    float top = titleRect_.bottom(), inputsWidth = 0;
    for (Pin& v : inputs_) {
        const glw::Size cs = connectorSize(v), ls = labelSize(v);
        v.connector = {pinMargins.start, top + (pinHeight - cs.height) / 2, cs.width, cs.height};
        float x = v.connector.right() + (v.label.empty() ? 0 : pinSpacing);
        if (v.isConst) {
            v.lockRect = {v.connector.right() + 3, top + (pinHeight - 11) / 2, 8, 11};
            x = v.lockRect.right() + pinSpacing;
        }
        v.labelRect = {x, top + (pinHeight - ls.height) / 2, ls.width, ls.height};
        float right = v.labelRect.right();
        if (v.editor != Editor::None) {
            const float w = v.editor == Editor::Check ? checkSize
                                                     : std::clamp((v.valueText ? v.valueText->size().width : 0) + 12 + (v.editor == Editor::Choice ? 14 : 0),
                                                                  editorMinimum, editorMaximum);
            const float h = v.editor == Editor::Check ? checkSize : editorHeight;
            v.editorRect = {right + pinSpacing, top + (pinHeight - h) / 2, w, h};
            right = v.editorRect.right();
        }
        inputsWidth = std::max(inputsWidth, right + pinMargins.end);
        top += pinHeight;
    }
    const float inputsBottom = top;
    // the outputs from the end: placed from 0 leftwards, then moved to the end
    top = titleRect_.bottom();
    float outputsWidth = 0;
    for (Pin& v : outputs_) {
        const glw::Size cs = connectorSize(v), ls = labelSize(v);
        v.connector = {-(cs.width + pinMargins.end), top + (pinHeight - cs.height) / 2, cs.width, cs.height};
        float x = v.connector.x - (v.label.empty() ? 0 : pinSpacing) - ls.width;
        if (v.isConst) {
            v.lockRect = {v.connector.x - 3 - 8, top + (pinHeight - 11) / 2, 8, 11};
            x = v.lockRect.x - pinSpacing - ls.width;
        }
        v.labelRect = {x, top + (pinHeight - ls.height) / 2, ls.width, ls.height};
        outputsWidth = std::max(outputsWidth, -(v.labelRect.x - pinMargins.start));
        top += pinHeight;
    }
    const float outputsBottom = top;

    width_ = std::max({titleRect_.width, inputsWidth + sideSpacing + outputsWidth, 60.0f});
    for (Pin& v : outputs_) {
        v.connector.x += width_;
        v.labelRect.x += width_;
        v.lockRect.x += width_;
    }
    titleRect_.width = width_;
    height_ = std::max({inputsBottom, outputsBottom, titleRect_.bottom() + 4});
}

void NodeView::layoutReroute() {
    titleRect_ = {};
    width_ = rerouteWidth;
    height_ = rerouteHeight;
    const float cy = rerouteHeight / 2;
    for (Pin& v : inputs_) {
        v.connector = {rerouteInset - dataConnector.width / 2, cy - dataConnector.height / 2, dataConnector.width, dataConnector.height};
        v.label.clear();
        v.labelText.reset();
        v.editor = Editor::None;
    }
    for (Pin& v : outputs_) {
        v.connector = {rerouteWidth - rerouteInset - dataConnector.width / 2, cy - dataConnector.height / 2, dataConnector.width, dataConnector.height};
        v.label.clear();
        v.labelText.reset();
        v.editor = Editor::None;
    }
}

std::vector<const NodeView::Pin*> NodeView::pins() const {
    std::vector<const Pin*> all;
    for (const Pin& v : inputs_)
        all.push_back(&v);
    for (const Pin& v : outputs_)
        all.push_back(&v);
    return all;
}

const NodeView::Pin* NodeView::pinAtPoint(glw::Point point) const {
    for (const Pin* v : pins()) {
        glw::Rect hit = v->connector.united(v->labelRect);
        if (reroute_)
            hit = {v->connector.center().x - rerouteHit, v->connector.center().y - rerouteHit, 2 * rerouteHit, 2 * rerouteHit};
        if (hit.inflated(2).contains(point))
            return v;
    }
    return nullptr;
}

std::optional<glw::Point> NodeView::pinCenter(const std::string& name, fg::PinDir dir) const {
    for (const Pin* v : pins()) {
        if (v->dir == dir && v->name == name)
            return v->connector.center();
    }
    return std::nullopt;
}

std::optional<NodeView::PinHit> NodeView::pinAt(glw::Point point) const {
    if (const Pin* v = pinAtPoint(point))
        return PinHit{v->name, v->dir, v->kind};
    return std::nullopt;
}

glw::Rect NodeView::bodyRect() const {
    return {0, 0, width_, height_};
}

// ---- painting ----

void NodeView::paint(glw::PaintContext& context) {
    glw::paint::Painter& p = context.painter;
    const glw::Theme& theme = context.ui.theme;
    const glw::NodeStyle& style = theme.node;           // the theme's nodes: their body, header, shadow, corners
    const GraphColors colors = GraphColors::of(theme);
    const glw::Rect body = bodyRect();
    const bool debugHere = graph_.debugNode() == id_;
    // the corners: the style's, as round as the header allows
    const float radius = std::min(style.cornerRadius, reroute_ ? rerouteHeight / 2 : titleRect_.height / 2);

    glw::Color border = colors.border;
    float borderWidth = 1;
    if (hoveredBody_ || hovered_)
        border = colors.highlight;
    if (error_) {
        border = colors.error;
        borderWidth = 2;
    } else if (warning_ && !selected_) {
        border = colors.warning;
    }
    if (debugHere) {
        border = colors.debug;
        borderWidth = 3;
    }

    const auto isHovered = [this](const Pin& v) { return hovered_ && hovered_->name == v.name && hovered_->dir == v.dir; };

    // selected: the theme's ring around it
    const auto selectionRing = [&](float r) {
        if (selected_)
            p.strokeRoundedRect(body.inflated(2.5f), r + 2.5f, 2, colors.selection);
    };

    if (reroute_) {
        const glw::Color c = outputs_.empty() ? rgb(150, 150, 150) : outputs_.front().color;
        if (style.shadow.a > 0)
            p.drawShadow({body.x, body.y + 2, body.width, body.height}, rerouteHeight / 2, 8, style.shadow);
        glw::paintRaised(p, body, rerouteHeight / 2, colors.body, {0, 0, 0, 0}, style.bodyBevel);
        p.strokeRoundedRect(body, rerouteHeight / 2, borderWidth, border);
        selectionRing(rerouteHeight / 2);
        for (const Pin* v : pins()) {
            const glw::Color line = isHovered(*v) ? colors.highlight : c;
            if (v->connected)
                p.fillCircle(v->connector.center(), 4, c);
            p.strokeCircle(v->connector.center(), 4, 1.2f, line);
        }
        return;
    }

    // as the theme's nodes are: a shadow under it, its body raised as the style raises it, its header (in its kind's color)
    // its top, rounded with the body (drawn taller and cut at its bottom), the border over them
    if (style.shadow.a > 0)
        p.drawShadow({body.x, body.y + 3, body.width, body.height}, radius, 12, style.shadow);
    glw::paintRaised(p, body, radius, colors.body, {0, 0, 0, 0}, style.bodyBevel);
    p.pushClip(titleRect_);
    glw::paintRaised(p, {0, 0, width_, titleRect_.height + radius}, radius, header_, {0, 0, 0, 0}, style.bevel);
    p.popClip();
    p.strokeRoundedRect(body, radius, borderWidth, border);
    selectionRing(radius);
    if (titleText_)
        context.drawText(*titleText_, {(width_ - titleText_->size().width) / 2, titleMargins.top}, {1, 1, 1, 1});

    if (graph_.isBreakpoint(id_))
        p.fillCircle({-4, -4}, 6, colors.breakpoint);
    if (error_ || warning_) {
        const glw::Point at{body.right() + 2, -4};
        p.fillCircle(at, 7, error_ ? colors.error : colors.warning);
        p.drawLine({at.x, at.y - 3.5f}, {at.x, at.y + 1}, 1.6f, {1, 1, 1, 1});
        p.fillCircle({at.x, at.y + 3.2f}, 0.9f, {1, 1, 1, 1});
    }

    for (const Pin* pv : pins()) {
        const Pin& v = *pv;
        const bool lit = isHovered(v);
        const glw::Color c = lit ? colors.highlight : v.color;
        if (v.kind == fg::PinKind::Exec) {
            drawExecGlyph(p, v.connector.center(), c, v.connected, colors.body);
        } else {
            drawPinGlyph(p, v.connector.center(),
                         {.container = v.container,
                          .byRef = v.byRef,
                          .connected = v.connected,
                          .color = c,
                          .keyColor = lit ? colors.highlight : v.keyColor,
                          .hollow = colors.body});
        }
        if (v.labelText)
            context.drawText(*v.labelText, {v.labelRect.x, v.labelRect.y}, lit ? colors.highlight : colors.text);
        if (v.isConst) {
            // a padlock: its shackle, its body
            const glw::Rect l = v.lockRect;
            const glw::Point shackle[] = {{l.x + 1.5f, l.y + 5},
                                          {l.x + 1.5f, l.y + 2.5f},
                                          {l.x + 4, l.y + 0.5f},
                                          {l.right() - 1.5f, l.y + 2.5f},
                                          {l.right() - 1.5f, l.y + 5}};
            p.drawPolyline(shackle, 1.4f, colors.lock);
            p.fillRoundedRect({l.x, l.y + 4.5f, l.width, 6}, 1.5f, colors.lock);
        }
        if (v.editor == Editor::Check) {
            // the theme's check box: sunk, or raised in its checked color with its mark
            const glw::CheckBoxStyle& cb = theme.checkBox;
            const glw::Rect r = v.editorRect;
            const float cr = std::min(cb.cornerRadius, r.height / 2);
            if (!v.value.empty()) {
                glw::paintRaised(p, r, cr, cb.checkedBackground.normal, cb.checkedBorder, cb.checkedBevel);
                const glw::Point mark[] = {{r.x + 3, r.y + r.height / 2}, {r.x + r.width / 2 - 1, r.bottom() - 3.5f}, {r.right() - 3, r.y + 3.5f}};
                p.drawPolyline(mark, 1.6f, cb.check);
            } else {
                glw::paintSunken(p, r, cr, cb.background.normal, cb.border.normal, cb.bevel);
            }
        } else if (v.editor == Editor::Line || v.editor == Editor::Choice) {
            // the theme's field, sunk
            const glw::LineEditStyle& field = theme.lineEdit;
            const glw::Rect r = v.editorRect;
            glw::paintSunken(p, r, std::min(field.cornerRadius, r.height / 2), colors.field, colors.fieldBorder, field.bevel);
            if (v.valueText) {
                p.pushClip({r.x + 3, r.y, std::max(0.0f, r.width - 6 - (v.editor == Editor::Choice ? 14 : 0)), r.height});
                context.drawText(*v.valueText, {r.x + 6, r.y + (r.height - v.valueText->size().height) / 2}, colors.fieldText);
                p.popClip();
            }
            if (v.editor == Editor::Choice) {
                const float x = r.right() - 9, y = r.y + r.height / 2;
                const glw::Point chevron[] = {{x - 3.5f, y - 1.75f}, {x, y + 1.75f}, {x + 3.5f, y - 1.75f}};
                p.drawPolyline(chevron, 1.3f, colors.mutedText);
            }
        }
    }
}

// ---- the mouse over it ----

void NodeView::onMouse(glw::MouseEvent& event) {
    // pressed, dragged, let go: the graph's (selecting, moving, linking)
    graph_.nodeMouse(*this, event);
    if (event.type != glw::MouseEventType::Move)
        return;
    std::optional<PinHit> pin = pinAt(event.position);
    const bool changed = pin.has_value() != hovered_.has_value() || (pin && (pin->name != hovered_->name || pin->dir != hovered_->dir)) || !hoveredBody_;
    hovered_ = std::move(pin);
    hoveredBody_ = true;
    if (changed)
        invalidatePaint();
}

void NodeView::onHoverChanged(bool hovered) {
    if (!hovered) {
        hovered_.reset();
        hoveredBody_ = false;
        invalidatePaint();
    }
}

std::optional<glw::ToolTip> NodeView::toolTipAt(glw::Point point) const {
    if (const Pin* v = pinAtPoint(point)) {
        const std::string name = v->label.empty() ? v->name : v->label;
        return glw::ToolTip{.title = glw::LocalizedText(v->typeName.empty() ? name : name + ": " + v->typeName),
                            .text = glw::LocalizedText(v->doc)};
    }
    if (!diagnostics_.empty())
        return glw::ToolTip{.title = glw::LocalizedText(title_), .text = glw::LocalizedText(diagnostics_)};
    if (titleRect_.contains(point))
        return glw::ToolTip{.title = glw::LocalizedText(title_), .text = glw::LocalizedText(nodeDoc(node_.def))};
    return std::nullopt;
}

} // namespace fged::gl
