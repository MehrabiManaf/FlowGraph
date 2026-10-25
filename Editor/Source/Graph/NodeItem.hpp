// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Style.hpp"

#include <QGraphicsObject>
#include <QPainterPath>

#include <map>
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
    // The center of a pin's connector in the node's coordinates; none when the node has no such pin (shown).
    std::optional<QPointF> pinLocalPos(const std::string& name, fg::PinDir dir) const;
    // Updates the preview text of a pin editor while it is being edited.
    void setEditorPreview(const std::string& pin, const QString& text);
    void clearEditorPreview();
    QRectF editorSceneRect(const std::string& pin) const;       // where the editor of an input pin is drawn

    void flash() { flashStamp_ = nowMicros(); update(); }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;

protected:
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget* w) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    bool sceneEvent(QEvent* e) override;
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
        QColor color, keyColor;       // keyColor: the key type of a map
        QRectF connector, labelRect, editorRect, lockRect;
        int containerKind = 0;      // 0 = single value, 1 = array, 2 = map
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
    int dragSerial_ = -1;              // the drag of the scene (GraphScene::dragSerial) dragStart_ belongs to
    QPointF dragStart_;                // where the node was when that drag started
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
    std::map<std::string, QString> preview_;   // text currently typed into pin editors (not committed yet)
};

} // namespace fged
