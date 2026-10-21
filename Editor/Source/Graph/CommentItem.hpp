// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Style.hpp"

#include <FlowGraph/Script.hpp>

#include <QGraphicsObject>

namespace fged {

class GraphScene;
class NodeItem;

// A colored frame with a caption behind a group of nodes. Dragging its header moves the nodes inside.
class CommentItem : public QGraphicsObject {
    Q_OBJECT
public:
    CommentItem(GraphScene* scene, int id);
    int commentId() const { return id_; }
    void refresh(const fg::CommentBox& c);
    QRectF rect() const { return QRectF(0, 0, w_, h_); }
    QColor color() const { return color_; }
    QString text() const { return text_; }

    QRectF boundingRect() const override { return QRectF(-2, -2, w_ + 4, h_ + 4); }
    QPainterPath shape() const override;

protected:
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;

private:
    static constexpr double kHeader = 26.0;
    static constexpr double kGrip = 16.0;
    QRectF headerRect() const { return QRectF(0, 0, w_, kHeader); }
    QRectF gripRect() const { return QRectF(w_ - kGrip, h_ - kGrip, kGrip, kGrip); }

    GraphScene* gscene_;
    int id_;
    double w_ = 300, h_ = 160;
    QColor color_;
    QString text_;
    enum class Drag { None, Move, Resize } drag_ = Drag::None;
    QPointF pressScene_, pressPos_;
    QSizeF pressSize_;
    std::vector<std::pair<NodeItem*, QPointF>> carried_;
};

} // namespace fged
