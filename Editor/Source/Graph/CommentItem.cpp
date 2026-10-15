#include "CommentItem.hpp"
#include "GraphScene.hpp"
#include "NodeItem.hpp"

#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QPainter>

namespace fged {

CommentItem::CommentItem(GraphScene* scene, int id) : gscene_(scene), id_(id) {
    setFlags(ItemIsSelectable | ItemIsFocusable);
    setZValue(-10);
}

void CommentItem::refresh(const fg::CommentBox& c) {
    prepareGeometryChange();
    w_ = c.w;
    h_ = c.h;
    color_ = rgb(c.color);
    text_ = qs(c.text);
    update();
}

QPainterPath CommentItem::shape() const {
    QPainterPath p;
    p.addRect(headerRect());
    p.addRect(gripRect());
    return p;
}

void CommentItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    const QPalette& pal = qApp->palette();
    QColor fill = color_;
    fill.setAlpha(45);
    QColor head = color_;
    head.setAlpha(150);
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(isSelected() ? pal.highlight().color() : color_, isSelected() ? 2.0 : 1.0));
    p->setBrush(fill);
    p->drawRoundedRect(rect(), 6, 6);
    p->setPen(Qt::NoPen);
    p->setBrush(head);
    p->save();
    p->setClipRect(headerRect());
    p->drawRoundedRect(rect(), 6, 6);
    p->restore();
    QFont f = qApp->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 1);
    p->setFont(f);
    p->setPen(Qt::white);
    p->drawText(headerRect().adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text_);
    p->setPen(QPen(color_, 2));
    QRectF g = gripRect();
    p->drawLine(g.bottomRight() - QPointF(3, 12), g.bottomRight() - QPointF(12, 3));
    p->drawLine(g.bottomRight() - QPointF(3, 7), g.bottomRight() - QPointF(7, 3));
}

void CommentItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return QGraphicsObject::mousePressEvent(e);
    if (!isSelected()) {
        if (!(e->modifiers() & Qt::ControlModifier)) scene()->clearSelection();
        setSelected(true);
    }
    pressScene_ = e->scenePos();
    pressPos_ = pos();
    pressSize_ = QSizeF(w_, h_);
    if (gripRect().contains(e->pos())) drag_ = Drag::Resize;
    else {
        drag_ = Drag::Move;
        carried_.clear();
        QRectF mine = mapRectToScene(rect());
        for (QGraphicsItem* it : scene()->items(mine, Qt::ContainsItemBoundingRect))
            if (auto* n = dynamic_cast<NodeItem*>(it)) carried_.push_back({n, n->pos()});
    }
    e->accept();
}

void CommentItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    QPointF d = e->scenePos() - pressScene_;
    if (drag_ == Drag::Move) {
        setPos(pressPos_ + d);
        for (auto& [n, p0] : carried_) n->setPos(p0 + d);
    } else if (drag_ == Drag::Resize) {
        prepareGeometryChange();
        w_ = std::max(120.0, pressSize_.width() + d.x());
        h_ = std::max(60.0, pressSize_.height() + d.y());
        update();
    }
    e->accept();
}

void CommentItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    if (drag_ != Drag::None) gscene_->commitLayout();
    drag_ = Drag::None;
    carried_.clear();
    e->accept();
}

void CommentItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) {
    if (headerRect().contains(e->pos())) gscene_->editCommentText(id_);
    e->accept();
}

} // namespace fged
