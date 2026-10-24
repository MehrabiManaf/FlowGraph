// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Style.hpp"

#include <FlowGraph/Script.hpp>

#include <QGraphicsObject>
#include <QPainterPath>

namespace fged {

class GraphScene;

// A connection between two pins (or the temporary link that follows the mouse while dragging).
class LinkItem : public QGraphicsObject {
    Q_OBJECT
public:
    // `exec`: execution link (thicker, white). `temporary`: dashed link ending at the cursor.
    LinkItem(GraphScene* scene, const fg::Connection& c, bool exec, QColor color, bool temporary = false);

    const fg::Connection& connection() const { return conn_; }
    bool isTemporary() const { return temporary_; }     // the link being dragged: no connection of the script
    void setColor(const QColor& c) { color_ = c; update(); }
    void setExec(bool e) { exec_ = e; setZValue(e ? -2 : -3); update(); }
    // Recomputes the path from the current pin positions (or from `start`/`end` while temporary).
    void updatePath();
    void setTemporaryEnds(const QPointF& outPos, const QPointF& inPos);   // scene coordinates
    void fire();                                                          // execution animation

    QRectF boundingRect() const override { return path_.boundingRect().adjusted(-8, -8, 8, 8); }
    QPainterPath shape() const override;
    // Point on the path closest to `scenePos` (used to insert reroutes).
    QPointF midpoint() const { return mapToScene(path_.pointAtPercent(0.5)); }

protected:
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;

private:
    GraphScene* gscene_;
    fg::Connection conn_;
    bool exec_, temporary_;
    QColor color_;
    QPainterPath path_;
    QPointF tempOut_, tempIn_;
    qint64 fireStart_ = 0, fireLast_ = 0;
    bool fired_ = false;
};

} // namespace fged
