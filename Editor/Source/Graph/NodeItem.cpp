// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "NodeItem.hpp"
#include "../Common/Theme.hpp"
#include "GraphScene.hpp"

#include <QCheckBox>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOptionButton>
#include <QStyleOptionFrame>
#include <QToolTip>

namespace fged {

namespace {

// A reroute is a small pill: an input circle, a stretch of body that can be grabbed to move it, and an output circle.
constexpr double kRerouteWidth = 36.0, kRerouteHeight = 18.0;
constexpr double kRerouteCenterOffset = 10.0;            // distance of the circles from the ends
constexpr double kRerouteHitRadius = 5.5;                // only the circles start a link; the rest of the pill moves the node

struct Metrics {
    QFont font, bold;
    QMarginsF titleMargins{10.0, 6.0, 10.0, 6.0};
    QMarginsF pinMargins{6.0, 2.0, 6.0, 2.0};
    double pinSpacing = 5.0;
    double sideSpacing = 14.0;
    QSizeF dataConnector{12.0, 12.0};
    QSizeF arrayConnector{kArrayConnectorWidth, 12.0};      // [ o o ] is wider than the other connectors
    QSizeF execConnector{12.0, 15.0};
    double pinHeight = 0;
    double editorMin = 22.0, editorMax = 400.0;
    QPainterPath execShape, refShape;
};

const Metrics& metrics() {
    static Metrics m = [] {
        Metrics r;
        r.font = qApp->font();
        r.bold = r.font;
        r.bold.setBold(true);
        r.bold.setPointSize(r.bold.pointSize() + 1);
        QLineEdit le;
        QCheckBox cb;
        r.pinHeight = QFontMetricsF(r.font).height();
        r.pinHeight = std::max({r.pinHeight, double(le.sizeHint().height()), double(cb.sizeHint().height()), r.dataConnector.height(), r.execConnector.height()});
        r.pinHeight += r.pinMargins.top() + r.pinMargins.bottom();
        double hw = r.execConnector.width() / 2, hh = r.execConnector.height() / 2;
        r.execShape.moveTo(-hw, -hh);
        r.execShape.lineTo(0, -hh);
        r.execShape.lineTo(hw, 0);
        r.execShape.lineTo(0, hh);
        r.execShape.lineTo(-hw, hh);
        r.execShape.closeSubpath();
        r.refShape.moveTo(0, -4.5);
        r.refShape.lineTo(4.5, 0);
        r.refShape.lineTo(0, 4.5);
        r.refShape.lineTo(-4.5, 0);
        r.refShape.closeSubpath();
        return r;
    }();
    return m;
}

template <class V> QColor c0(const V& v, const QPalette& pal) { return v.hovered ? pal.highlight().color() : v.color; }

bool hiddenLabel(const fg::CompiledPin& p) {
    return p.kind == fg::PinKind::Exec && (p.name == "In" || p.name == "Then");
}

} // namespace

NodeItem::NodeItem(GraphScene* scene, fg::NodeId id) : gscene_(scene), id_(id) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemIsFocusable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(0);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && gscene_ && gscene_->snapsUserMoves()) return gscene_->snapped(value.toPointF());
    if (change == ItemPositionHasChanged && gscene_) gscene_->nodeMoved(this);
    return QGraphicsObject::itemChange(change, value);
}

void NodeItem::refresh(const fg::CompiledNode& cn, bool hasError, bool hasWarning, const QString& diagText) {
    prepareGeometryChange();
    cn_ = cn;
    error_ = hasError;
    warning_ = hasWarning;
    diagText_ = diagText;
    reroute_ = cn.op == fg::NodeOp::Reroute;
    title_ = qs(cn.title);
    header_ = headerColor(cn.def ? cn.def->style : fg::NodeStyle::Function);
    if (!cn.def) header_ = QColor(120, 40, 120);
    layout();
    update();
}

void NodeItem::layout() {
    const Metrics& M = metrics();
    const fg::Registry& reg = gscene_->registry();
    inExec_.clear(); inData_.clear(); outExec_.clear(); outData_.clear();

    auto makeView = [&](const fg::CompiledPin& p) {
        PinView v;
        v.name = p.name;
        v.label = hiddenLabel(p) ? QString() : qs(p.name);
        v.kind = p.kind;
        v.dir = p.dir;
        v.color = pinColor(reg, p);
        v.keyColor = p.type && p.type->kind == fg::TypeKind::Map && !p.wildcard && p.type->keyType ? rgb(reg.typeColor(p.type->keyType)) : v.color;
        v.connected = p.connected;
        v.byRef = p.byRef;
        v.isConst = p.isConst;
        v.wildcard = p.wildcard;
        v.container = p.type && p.type->isContainer();
        v.containerKind = !p.type ? 0 : p.type->kind == fg::TypeKind::Array ? 1 : p.type->kind == fg::TypeKind::Map ? 2 : 0;
        v.type = p.type;
        v.doc = qs(p.doc);
        return v;
    };
    for (int pi : cn_.execIn) if (!cn_.pins[size_t(pi)].hidden) inExec_.push_back(makeView(cn_.pins[size_t(pi)]));
    for (int pi : cn_.dataIn) if (!cn_.pins[size_t(pi)].hidden) {
        const fg::CompiledPin& p = cn_.pins[size_t(pi)];
        PinView v = makeView(p);
        if (!p.connected && !p.byRef && p.type) {
            switch (p.type->kind) {
            case fg::TypeKind::Bool: v.editor = Editor::Check; break;
            case fg::TypeKind::Int:
            case fg::TypeKind::Float:
            case fg::TypeKind::String: v.editor = Editor::Line; break;
            case fg::TypeKind::Enum: v.editor = Editor::Choice; break;
            default: break;
            }
            if (v.editor != Editor::None) {
                v.editorText = defaultText(p);
                auto pv = preview_.find(p.name);       // text that is being typed right now
                if (pv != preview_.end() && v.editor == Editor::Line) v.editorText = pv->second;
            }
        }
        inData_.push_back(std::move(v));
    }
    for (int pi : cn_.execOut) if (!cn_.pins[size_t(pi)].hidden) outExec_.push_back(makeView(cn_.pins[size_t(pi)]));
    for (int pi : cn_.dataOut) if (!cn_.pins[size_t(pi)].hidden) outData_.push_back(makeView(cn_.pins[size_t(pi)]));

    if (reroute_) { layoutReroute(); return; }

    QFontMetricsF fm(M.font), fmb(M.bold);
    QSizeF titleSize = fmb.size(Qt::TextSingleLine, title_);
    titleRect_ = QRectF(0, 0, M.titleMargins.left() + titleSize.width() + M.titleMargins.right(),
                        M.titleMargins.top() + titleSize.height() + M.titleMargins.bottom());

    auto placeInput = [&](PinView& v, double& top) {
        QSizeF cs = v.kind == fg::PinKind::Exec ? M.execConnector : v.containerKind == 1 ? M.arrayConnector : M.dataConnector;
        v.connector = QRectF(QPointF(M.pinMargins.left(), top + (M.pinHeight - cs.height()) / 2), cs);
        QSizeF ls = fm.size(Qt::TextSingleLine, v.label);
        double x = v.connector.right() + (v.label.isEmpty() ? 0 : M.pinSpacing);
        if (v.isConst) {       // lock symbol between the connector and the name
            v.lockRect = QRectF(QPointF(v.connector.right() + 3, top + (M.pinHeight - 11) / 2), QSizeF(8, 11));
            x = v.lockRect.right() + M.pinSpacing;
        }
        v.labelRect = QRectF(QPointF(x, top + (M.pinHeight - ls.height()) / 2), ls);
        double right = v.labelRect.right();
        if (v.editor != Editor::None) {
            double w = v.editor == Editor::Check ? 16.0 : fm.horizontalAdvance(v.editorText) + 12.0;
            w = std::clamp(w, M.editorMin, M.editorMax);
            double h = v.editor == Editor::Check ? 16.0 : M.pinHeight - 2;
            v.editorRect = QRectF(QPointF(right + M.pinSpacing, top + (M.pinHeight - h) / 2), QSizeF(w, h));
            right = v.editorRect.right();
        }
        inputsRect_ = inputsRect_.united(QRectF(0, top, right + M.pinMargins.right(), M.pinHeight));
        top += M.pinHeight;
    };
    double top = titleRect_.bottom();
    inputsRect_ = QRectF(0, top, 0, 0);
    for (auto& v : inExec_) placeInput(v, top);
    for (auto& v : inData_) placeInput(v, top);

    auto placeOutput = [&](PinView& v, double& top) {
        QSizeF cs = v.kind == fg::PinKind::Exec ? M.execConnector : v.containerKind == 1 ? M.arrayConnector : M.dataConnector;
        v.connector = QRectF(QPointF(-(cs.width() + M.pinMargins.right()), top + (M.pinHeight - cs.height()) / 2), cs);
        QSizeF ls = fm.size(Qt::TextSingleLine, v.label);
        double x = v.connector.left() - (v.label.isEmpty() ? 0 : M.pinSpacing) - ls.width();
        if (v.isConst) {
            v.lockRect = QRectF(QPointF(v.connector.left() - 3 - 8, top + (M.pinHeight - 11) / 2), QSizeF(8, 11));
            x = v.lockRect.left() - M.pinSpacing - ls.width();
        }
        v.labelRect = QRectF(QPointF(x, top + (M.pinHeight - ls.height()) / 2), ls);
        double w = -(v.labelRect.left() - M.pinMargins.left());
        outputsRect_ = outputsRect_.united(QRectF(-w, top, w, M.pinHeight));
        top += M.pinHeight;
    };
    top = titleRect_.bottom();
    outputsRect_ = QRectF(0, top, 0, 0);
    for (auto& v : outExec_) placeOutput(v, top);
    for (auto& v : outData_) placeOutput(v, top);

    double width = std::max({titleRect_.width(), inputsRect_.width() + M.sideSpacing + outputsRect_.width(), 60.0});
    outputsRect_.translate(width, 0);
    for (auto* list : {&outExec_, &outData_})
        for (auto& v : *list) {
            v.connector.translate(width, 0);
            v.labelRect.translate(width, 0);
            v.lockRect.translate(width, 0);
        }
    titleRect_.setWidth(width);
    double bottom = std::max(inputsRect_.bottom(), outputsRect_.bottom());
    if (bottom < titleRect_.bottom() + 4) bottom = titleRect_.bottom() + 4;   // nodes without pins keep a body
    inputsRect_.setRight(std::max(inputsRect_.right(), 0.0));
    bodyBottom_ = bottom;
}

void NodeItem::layoutReroute() {
    const Metrics& M = metrics();
    titleRect_ = QRectF(0, 0, 0, 0);
    inputsRect_ = QRectF(0, 0, kRerouteWidth / 2, kRerouteHeight);
    outputsRect_ = QRectF(kRerouteWidth / 2, 0, kRerouteWidth / 2, kRerouteHeight);
    bodyBottom_ = kRerouteHeight;
    const QSizeF cs = M.dataConnector;
    const double cy = kRerouteHeight / 2;
    if (!inData_.empty()) inData_[0].connector = QRectF(QPointF(kRerouteCenterOffset - cs.width() / 2, cy - cs.height() / 2), cs);
    if (!outData_.empty()) outData_[0].connector = QRectF(QPointF(kRerouteWidth - kRerouteCenterOffset - cs.width() / 2, cy - cs.height() / 2), cs);
    for (auto* list : {&inData_, &outData_})
        for (auto& v : *list) { v.label.clear(); v.labelRect = QRectF(); v.editor = Editor::None; }
}

QRectF NodeItem::boundingRect() const {
    QRectF r = reroute_ ? QRectF(0, 0, kRerouteWidth, kRerouteHeight) : QRectF(0, 0, titleRect_.width(), bodyBottom_);
    r = r.united(inputsRect_).united(outputsRect_);
    for (auto* list : {&outExec_, &outData_, &inExec_, &inData_})
        for (auto& v : *list) r = r.united(v.connector.adjusted(-4, -4, 4, 4));
    if (!cn_.comment.empty()) r = r.united(commentRect());
    return r.adjusted(-6, -14, 6, 6);
}

QRectF NodeItem::commentRect() const {
    QFontMetricsF fm(metrics().font);
    QSizeF s = fm.size(Qt::TextSingleLine, qs(cn_.comment));
    return QRectF(0, -s.height() - 10, s.width() + 12, s.height() + 4);
}

QPainterPath NodeItem::shape() const {
    QPainterPath p;
    p.setFillRule(Qt::WindingFill);     // the pin rectangles overlap the body: odd-even filling would punch holes into it
    QRectF body = reroute_ ? QRectF(0, 0, kRerouteWidth, kRerouteHeight) : QRectF(0, 0, titleRect_.width(), bodyBottom_);
    p.addRect(body.united(inputsRect_).united(outputsRect_));
    for (auto* list : {&outExec_, &outData_, &inExec_, &inData_})
        for (auto& v : *list) p.addRect(v.connector.adjusted(-3, -3, 3, 3));
    return p;
}

std::vector<NodeItem::PinView*> NodeItem::allPins() {
    std::vector<PinView*> out;
    for (auto* list : {&inExec_, &inData_, &outExec_, &outData_})
        for (auto& v : *list) out.push_back(&v);
    return out;
}

NodeItem::PinView* NodeItem::pinViewAt(const QPointF& local) {
    for (PinView* v : allPins()) {
        QRectF hit = v->connector.united(v->labelRect);
        if (reroute_) hit = QRectF(v->connector.center() - QPointF(kRerouteHitRadius, kRerouteHitRadius), QSizeF(2 * kRerouteHitRadius, 2 * kRerouteHitRadius));
        if (hit.contains(local)) return v;
    }
    return nullptr;
}

const NodeItem::PinView* NodeItem::findPin(const std::string& name, fg::PinDir dir) const {
    for (auto* list : {&inExec_, &inData_, &outExec_, &outData_})
        for (auto& v : *list)
            if (v.dir == dir && v.name == name) return &v;
    return nullptr;
}

PinRef NodeItem::refFor(const PinView& v) const {
    PinRef r;
    r.node = id_;
    r.pin = v.name;
    r.kind = v.kind;
    r.dir = v.dir;
    r.type = v.type;
    r.wildcard = v.wildcard;
    return r;
}

std::optional<PinRef> NodeItem::pinAt(const QPointF& scenePos) const {
    QPointF local = mapFromScene(scenePos);
    if (auto* v = const_cast<NodeItem*>(this)->pinViewAt(local)) return refFor(*v);
    return std::nullopt;
}

QPointF NodeItem::pinScenePos(const std::string& name, fg::PinDir dir) const {
    if (auto* v = findPin(name, dir)) return mapToScene(v->connector.center());
    return scenePos();
}

void NodeItem::setEditorPreview(const std::string& pin, const QString& text) {
    // the whole node follows the text that is being typed: node width, output pins and links
    prepareGeometryChange();
    preview_[pin] = text;
    layout();
    update();
    gscene_->nodeMoved(this);
}

void NodeItem::clearEditorPreview() {
    if (preview_.empty()) return;
    prepareGeometryChange();
    preview_.clear();
    layout();
    update();
    gscene_->nodeMoved(this);
}

QRectF NodeItem::editorSceneRect(const std::string& pin) const {
    for (auto& v : inData_)
        if (v.name == pin) return mapRectToScene(v.editorRect);
    return QRectF();
}

// ------------------------------------------------------------------ painting
void NodeItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget* widget) {
    const Metrics& M = metrics();
    const QPalette& pal = qApp->palette();
    const fg::Registry& reg = gscene_->registry();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    bool debugHere = gscene_->debugNode() == id_;
    qint64 since = nowMicros() - flashStamp_;
    double flash = flashStamp_ && since < 600000 ? 1.0 - double(since) / 600000.0 : 0.0;
    if (flash > 0) gscene_->requestAnimation();

    // comment bubble
    if (!cn_.comment.empty()) {
        QRectF r = commentRect();
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(230, 200, 90, 220));
        p->drawRoundedRect(r, 4, 4);
        p->setPen(QColor(30, 30, 30));
        p->setFont(M.font);
        p->drawText(r, Qt::AlignCenter, qs(cn_.comment));
    }

    QColor border = pal.windowText().color();
    double bw = 1.0;
    if (hovered_) { border = pal.highlight().color(); }
    if (isSelected()) { border = pal.highlight().color(); bw = 2.0; }
    if (error_) { border = QColor(230, 60, 60); bw = 2.0; }
    else if (warning_ && !isSelected()) border = QColor(230, 190, 60);
    if (flash > 0) {
        QColor f = pal.highlight().color();
        border = QColor::fromRgbF(border.redF() + (f.redF() - border.redF()) * flash, border.greenF() + (f.greenF() - border.greenF()) * flash,
                                  border.blueF() + (f.blueF() - border.blueF()) * flash);
        bw = 1.0 + 2.0 * flash;
    }
    if (debugHere) { border = QColor(255, 170, 0); bw = 3.0; }

    if (reroute_) {
        QColor c = outData_.empty() ? QColor(150, 150, 150) : outData_[0].color;
        p->setPen(QPen(border, bw));
        p->setBrush(themeColors().nodeBody);
        p->drawRoundedRect(QRectF(0, 0, kRerouteWidth, kRerouteHeight), kRerouteHeight / 2, kRerouteHeight / 2);
        for (auto* list : {&inData_, &outData_})
            for (auto& v : *list) {
                p->setPen(v.hovered ? pal.highlight().color() : c);
                p->setBrush(v.connected ? c : Qt::transparent);
                p->drawEllipse(v.connector.center(), 4.0, 4.0);
            }
        p->restore();
        return;
    }

    QRectF body(0, 0, titleRect_.width(), bodyBottom_);
    // body + header: the header is a plain rectangle clipped to the rounded body, so its corners are exactly the
    // body's (no second rounded shape, no seam where a rounded rect and a rect meet); the border is drawn once on top
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(body, 5, 5);
    p->setPen(Qt::NoPen);
    p->setBrush(themeColors().nodeBody);
    p->drawPath(bodyPath);
    p->save();
    p->setClipPath(bodyPath);
    p->fillRect(titleRect_, header_);
    p->restore();
    p->setPen(QPen(border, bw));
    p->setBrush(Qt::NoBrush);
    p->drawPath(bodyPath);
    p->setPen(Qt::white);
    p->setFont(M.bold);
    p->drawText(titleRect_, Qt::AlignCenter, title_);
    p->setFont(M.font);

    if (gscene_->isBreakpoint(id_)) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(220, 40, 40));
        p->drawEllipse(QPointF(-4, -4), 6, 6);
    }
    if (error_ || warning_) {
        p->setPen(Qt::NoPen);
        p->setBrush(error_ ? QColor(230, 60, 60) : QColor(230, 190, 60));
        p->drawEllipse(QPointF(body.right() + 2, -4), 7, 7);
        p->setPen(Qt::white);
        p->drawText(QRectF(body.right() - 5, -11, 14, 14), Qt::AlignCenter, "!");
    }

    auto drawPin = [&](const PinView& v) {
        QColor c = v.hovered ? pal.highlight().color() : v.color;
        p->setPen(c);
        p->setBrush(v.connected ? c : Qt::transparent);
        if (v.kind == fg::PinKind::Exec) {
            p->save();
            p->translate(v.connector.center());
            p->drawPath(M.execShape);
            p->restore();
        } else {
            PinGlyph glyph;
            glyph.container = v.containerKind;
            glyph.byRef = v.byRef;
            glyph.connected = v.connected;
            glyph.color = c0(v, pal);
            glyph.keyColor = v.hovered ? pal.highlight().color() : v.keyColor;
            glyph.hollow = themeColors().nodeBody;
            drawPinGlyph(p, v.connector.center(), glyph);
        }
        p->setPen(v.hovered ? pal.highlight().color() : pal.windowText().color());       // the label lights up with the connector
        p->drawText(v.labelRect, Qt::AlignCenter, v.label);
        if (v.isConst) {
            // padlock: shackle + body
            QRectF l = v.lockRect;
            p->save();
            p->setPen(QPen(themeColors().constLock, 1.4));
            p->setBrush(Qt::NoBrush);
            p->drawArc(QRectF(l.left() + 1.5, l.top() + 0.5, l.width() - 3, 7), 0, 180 * 16);
            p->setBrush(themeColors().constLock);
            p->drawRoundedRect(QRectF(l.left(), l.top() + 4.5, l.width(), 6), 1.5, 1.5);
            p->restore();
        }
        if (v.editor == Editor::Check) {
            QStyleOptionButton o;
            o.rect = v.editorRect.toRect();
            o.state = QStyle::State_Enabled | (v.editorText.isEmpty() ? QStyle::State_Off : QStyle::State_On);
            QApplication::style()->drawControl(QStyle::CE_CheckBox, &o, p, widget);
        } else if (v.editor == Editor::Line || v.editor == Editor::Choice) {
            QStyleOptionFrame o;
            o.rect = v.editorRect.toRect();
            o.state = QStyle::State_Enabled;
            o.lineWidth = QApplication::style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &o, widget);
            QApplication::style()->drawPrimitive(QStyle::PE_PanelLineEdit, &o, p, widget);
            p->setPen(pal.text().color());
            p->save();
            p->setClipRect(v.editorRect.adjusted(3, 0, -3, 0));
            QString t = v.editorText.size() > 200 ? v.editorText.left(200) : v.editorText;
            p->drawText(v.editorRect.adjusted(4, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, t);
            p->restore();
        }
    };
    for (auto* list : {&inExec_, &inData_, &outExec_, &outData_})
        for (auto& v : *list) drawPin(v);
    (void)reg;
    p->restore();
}

// ------------------------------------------------------------------ interaction
void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return QGraphicsObject::mousePressEvent(e);
    QPointF pos = e->pos();
    if (PinView* v = pinViewAt(pos)) {
        gscene_->beginLinkDrag(refFor(*v), mapToScene(pos), e->modifiers() & Qt::AltModifier);
        e->accept();
        return;
    }
    for (auto& v : inData_) {
        if (v.editor == Editor::None || !v.editorRect.contains(pos)) continue;
        e->accept();
        if (v.editor == Editor::Check) gscene_->setPinDefault(id_, v.name, fg::Value(v.editorText.isEmpty()));
        else if (v.editor == Editor::Line) gscene_->startInplaceEdit(this, v.name, mapToScene(v.editorRect.topLeft()), v.editorRect.width(), v.editorText, v.type);
        else gscene_->chooseEnumValue(this, v.name, v.type, e->screenPos());
        return;
    }
    QGraphicsObject::mousePressEvent(e);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (gscene_->isLinkDragging()) {
        gscene_->updateLinkDrag(e->scenePos());
        e->accept();
        return;
    }
    QGraphicsObject::mouseMoveEvent(e);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    if (gscene_->isLinkDragging()) {
        gscene_->finishLinkDrag(e->scenePos(), e->screenPos());
        e->accept();
        return;
    }
    QGraphicsObject::mouseReleaseEvent(e);
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) {
    if (pinViewAt(e->pos())) return;
    gscene_->nodeActivated(id_);
    QGraphicsObject::mouseDoubleClickEvent(e);
}

void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* e) {
    hovered_ = true;
    setZValue(1);
    update();
    QGraphicsObject::hoverEnterEvent(e);
}

void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* e) {
    bool changed = false;
    PinView* under = pinViewAt(e->pos());
    for (PinView* v : allPins()) {
        bool h = v == under;
        changed |= v->hovered != h;
        v->hovered = h;
    }
    if (changed) update();
    if (under) {
        QString tip = gscene_->pinTooltip(refFor(*under), under->doc);
        const bool call = cn_.op == fg::NodeOp::CallFunction;
        const bool input = under->dir == fg::PinDir::In;
        if (under->isConst)
            tip += QChar('\n') + (call ? QString("const: cannot be modified by the function")
                                       : input ? QString("const reference: this node reads the container without copying it")
                                               : QString("const reference: the container cannot be modified"));
        if (under->byRef && !(under->isConst && !call))
            tip += QChar('\n') + (call ? QString("by reference: refers to the caller's variable")
                                       : input ? QString("by reference: this node modifies the container in place")
                                               : QString("reference: refers to a container that exists elsewhere, not a copy"));
        if (under->containerKind == 1) tip += QChar('\n') + QString("array");
        else if (under->containerKind == 2) tip += QChar('\n') + QString("map");
        setToolTip(tip);
    }
    else if (error_ || warning_) setToolTip(diagText_);
    else setToolTip(cn_.def ? qs(cn_.def->doc) : QString());
    QGraphicsObject::hoverMoveEvent(e);
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* e) {
    hovered_ = false;
    setZValue(0);
    for (PinView* v : allPins()) v->hovered = false;
    update();
    QGraphicsObject::hoverLeaveEvent(e);
}

} // namespace fged
