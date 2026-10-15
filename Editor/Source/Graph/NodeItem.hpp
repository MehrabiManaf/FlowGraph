#pragma once
#include "Style.hpp"

#include <QGraphicsObject>
#include <QPainterPath>

#include <optional>
#include <vector>

namespace fged {

class GraphScene;

// Reference to one pin of one node.
struct PinRef {
    fg::NodeId node = 0;
    std::string pin;
    fg::PinKind kind = fg::PinKind::Data;
    fg::PinDir dir = fg::PinDir::In;
    const fg::TypeDesc* type = nullptr;
    bool wildcard = false;
    bool valid() const { return node != 0; }
};

class NodeItem : public QGraphicsObject {
    Q_OBJECT
public:
    NodeItem(GraphScene* scene, fg::NodeId id);

    fg::NodeId nodeId() const { return id_; }
    // Rebuilds the geometry from the compiled node (called whenever the program changed).
    void refresh(const fg::CompiledNode& cn, bool hasError, bool hasWarning, const QString& diagText);
    bool isReroute() const { return reroute_; }
    const fg::CompiledNode& compiled() const { return cn_; }

    std::optional<PinRef> pinAt(const QPointF& scenePos) const;
    QPointF pinScenePos(const std::string& name, fg::PinDir dir) const;
    // Updates the preview text of a pin editor while it is being edited.
    void setEditorPreview(const std::string& pin, const QString& text);

    void flash() { flashStamp_ = nowMicros(); update(); }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

protected:
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget* w) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    enum class Editor { None, Check, Line, Choice };
    struct PinView {
        std::string name;
        QString label;
        fg::PinKind kind = fg::PinKind::Data;
        fg::PinDir dir = fg::PinDir::In;
        QColor color;
        QRectF connector, labelRect, editorRect;
        bool connected = false, byRef = false, container = false, wildcard = false, isConst = false;
        Editor editor = Editor::None;
        QString editorText;
        const fg::TypeDesc* type = nullptr;
        QString doc;
        bool hovered = false;
    };

    void layout();
    std::vector<PinView*> allPins();
    PinView* pinViewAt(const QPointF& local);
    const PinView* findPin(const std::string& name, fg::PinDir dir) const;
    PinRef refFor(const PinView& v) const;
    void layoutReroute();
    QRectF commentRect() const;

    GraphScene* gscene_;
    fg::NodeId id_;
    fg::CompiledNode cn_;
    bool reroute_ = false;
    bool error_ = false, warning_ = false;
    QString diagText_;
    QString title_;
    QColor header_;
    QRectF titleRect_, inputsRect_, outputsRect_;
    double bodyBottom_ = 0;
    std::vector<PinView> inExec_, inData_, outExec_, outData_;
    bool hovered_ = false;
    qint64 flashStamp_ = 0;
};

} // namespace fged
