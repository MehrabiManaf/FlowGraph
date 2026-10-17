#pragma once
#include "GraphScene.hpp"

#include <QGraphicsView>
#include <QPropertyAnimation>

namespace fged {

class GraphView : public QGraphicsView {
    Q_OBJECT
    Q_PROPERTY(QPointF SceneCenter READ sceneCenter WRITE setSceneCenter)
public:
    explicit GraphView(GraphScene* scene, QWidget* parent = nullptr);
    GraphScene* graphScene() const { return static_cast<GraphScene*>(scene()); }

    void frameAll();
    void centerOnNode(fg::NodeId id, bool animated);
    void showPalette(const PinRef& from, const QPointF& scenePos, const QPoint& screenPos);

    QPointF sceneCenter() const { return mapToScene(viewport()->rect().center()); }
    void setSceneCenter(const QPointF& p) { centerOn(p); }

    static const char* variableMime() { return "application/x-flowgraph-variable"; }      // "global|Name" or "local|Name"
    static const char* functionMime() { return "application/x-flowgraph-function"; }      // the name of a function of the script

protected:
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void drawBackground(QPainter* p, const QRectF& r) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void setZoom(double z);

    double zoom_ = 1.0;
    bool autoFrame_ = true;      // keep the whole graph in view until the user zooms or pans
    bool panning_ = false;
    bool panMoved_ = false;
    QPoint panLast_;
    Qt::MouseButton panButton_ = Qt::NoButton;
    bool suppressContext_ = false;
    bool pendingContext_ = false; // a press-time context menu (X11/Wayland) deferred until the right button is released
};

} // namespace fged
