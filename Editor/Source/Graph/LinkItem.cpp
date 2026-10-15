#include "LinkItem.hpp"
#include "GraphScene.hpp"
#include "NodeItem.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>

namespace fged {

namespace {
constexpr qint64 kFireTotal = 1'000'000;
constexpr qint64 kFireFade = 250'000;

double lerp(double a, double b, double t) { return a + (b - a) * std::clamp(t, 0.0, 1.0); }
QColor lerp(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(lerp(a.redF(), b.redF(), t), lerp(a.greenF(), b.greenF(), t), lerp(a.blueF(), b.blueF(), t));
}
} // namespace

LinkItem::LinkItem(GraphScene* scene, const fg::Connection& c, bool exec, QColor color, bool temporary)
    : gscene_(scene), conn_(c), exec_(exec), temporary_(temporary), color_(color) {
    setZValue(exec ? -2 : -3);
    setFlag(ItemIsSelectable, !temporary);
    setAcceptedMouseButtons(temporary ? Qt::NoButton : Qt::LeftButton);
}

QPainterPath LinkItem::shape() const {
    QPainterPathStroker s;
    s.setWidth(10);
    return s.createStroke(path_);
}

void LinkItem::setTemporaryEnds(const QPointF& outPos, const QPointF& inPos) {
    tempOut_ = outPos;
    tempIn_ = inPos;
    updatePath();
}

void LinkItem::updatePath() {
    prepareGeometryChange();
    path_.clear();
    QPointF a, b;
    if (temporary_) {
        a = tempOut_;
        b = tempIn_;
    } else {
        NodeItem* from = gscene_->nodeItem(conn_.fromNode);
        NodeItem* to = gscene_->nodeItem(conn_.toNode);
        if (!from || !to) return;
        a = from->pinScenePos(conn_.fromPin, fg::PinDir::Out);
        b = to->pinScenePos(conn_.toPin, fg::PinDir::In);
    }
    a = mapFromScene(a);
    b = mapFromScene(b);
    QPointF d = b - a;
    if (d.x() < 80) d.setX(160 - d.x());
    d.setY(0);
    path_.moveTo(a);
    path_.cubicTo(a + d / 2, b - d / 2, b);
}

void LinkItem::fire() {
    qint64 now = nowMicros();
    if (!fired_) { fired_ = true; fireStart_ = now; }
    fireLast_ = now;
    gscene_->requestAnimation();
    update();
}

void LinkItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    const QPalette& pal = qApp->palette();
    QColor color = color_;
    double width = exec_ ? 3.0 : 2.0;
    if (isSelected()) { color = pal.highlight().color(); width += 1; }
    p->setRenderHint(QPainter::Antialiasing);
    if (fired_) {
        qint64 now = nowMicros();
        qint64 since = now - fireLast_;
        if (since > kFireTotal) fired_ = false;
        else {
            gscene_->requestAnimation();
            double t = since > kFireTotal - kFireFade ? double(kFireTotal - since) / kFireFade : double(now - fireStart_) / kFireFade;
            color = lerp(color, pal.highlight().color(), t);
            width = lerp(width, 6.0, t);
            double len = path_.length();
            if (len > 10.0) {
                double r = lerp(0.5, 4.0, t);
                double phase = double((now - fireStart_) % kFireTotal) / kFireTotal;
                double pct = phase * 200.0 / len;
                p->setPen(QPen(color, width));
                p->setBrush(color);
                while (pct <= 1.0) {
                    p->drawEllipse(path_.pointAtPercent(pct), r, r);
                    pct += 200.0 / len;
                }
            }
        }
    }
    p->setPen(QPen(color, width, temporary_ ? Qt::DashLine : Qt::SolidLine));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path_);
}

void LinkItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) {
    gscene_->insertReroute(conn_, e->scenePos());
    e->accept();
}

} // namespace fged
