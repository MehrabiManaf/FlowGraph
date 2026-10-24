// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../Document.hpp"
#include "NodeItem.hpp"

#include <FlowGraph/Intellisense.hpp>

#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QTimer>

#include <functional>
#include <map>
#include <set>

class QLineEdit;

namespace fged {

class LinkItem;
class CommentItem;

// The visual representation of one graph of the document. It never owns data: every change goes
// through Document::edit and the scene reconciles its items with the compiled graph afterwards.
class GraphScene : public QGraphicsScene {
    Q_OBJECT
public:
    GraphScene(Document* doc, const QString& graphName, QObject* parent = nullptr);
    ~GraphScene() override;

    Document* document() const { return doc_; }
    const fg::Registry& registry() const { return doc_->registry(); }
    const QString& graphName() const { return name_; }
    void setGraphName(const QString& n) { name_ = n; }
    fg::GraphKind kind() const;

    void sync();                                      // reconcile items with the document
    NodeItem* nodeItem(fg::NodeId id) const;
    QList<fg::NodeId> selectedNodeIds() const;
    void selectOnly(fg::NodeId id);

    // ---- debugging
    void setDebugNode(fg::NodeId id);
    fg::NodeId debugNode() const { return debugNode_; }
    bool isBreakpoint(fg::NodeId id) const { return doc_->debugger().hasBreakpoint(name_.toStdString(), id); }
    void toggleBreakpoint(fg::NodeId id);
    void fireLink(fg::NodeId from, int execOutOrdinal, fg::NodeId to);
    std::function<bool(const PinRef&, QString&)> pinValueProvider;   // set while a task is paused
    QString pinTooltip(const PinRef& ref, const QString& doc) const;

    // ---- pin interaction (called by the items)
    void beginLinkDrag(const PinRef& from, const QPointF& scenePos, bool breakLinks);
    bool isLinkDragging() const { return drag_.valid(); }
    void updateLinkDrag(const QPointF& scenePos);
    void finishLinkDrag(const QPointF& scenePos, const QPoint& screenPos);
    void cancelLinkDrag();            // drops the link being dragged, if any (the right button, a release that was lost)
    void setPinDefault(fg::NodeId node, const std::string& pin, const fg::Value& v);
    void startInplaceEdit(NodeItem* item, const std::string& pin, const QPointF& scenePos, double width, const QString& text, const fg::TypeDesc* type);
    bool isInplaceEditing() const { return editNode_ != 0; }
    QPointF inplaceEditorPos() const;                       // scene position of the in-place editor (tests)
    void chooseEnumValue(NodeItem* item, const std::string& pin, const fg::TypeDesc* type, const QPoint& screenPos);
    void nodeMoved(NodeItem* item);
    void nodeActivated(fg::NodeId id) { emit nodeDoubleClicked(id); }
    void findReferences(fg::NodeId id) { emit referencesRequested(id); }
    void collapseSelection() { emit collapseRequested(); }
    // Collapse to Function is offered for a selection of at least this many nodes (one node is better left where it is)
    static constexpr int kMinCollapseNodes = 2;
    bool canCollapseSelection() const { return selectedNodeIds().size() >= kMinCollapseNodes; }
    void requestAnimation() { animating_ = true; }
    void editCommentText(int id);

    // ---- editing commands
    bool connectPins(const PinRef& a, const PinRef& b, QString* why = nullptr);
    void breakPinLinks(const PinRef& p);
    void insertReroute(const fg::Connection& c, const QPointF& scenePos);
    fg::NodeId addNodeFromTemplate(const fg::NodeTemplate& t, const QPointF& pos, const PinRef* from = nullptr);
    fg::NodeId addVariableNode(const QString& name, const QString& scope, bool isSet, const QPointF& pos);
    fg::NodeId addFunctionCall(const QString& function, const QPointF& pos);        // a Call Function node for a function of the script; 0 when there is none
    void deleteSelected();
    void copySelected();
    bool canPaste() const;
    void paste(const QPointF& at);
    void duplicateSelected();
    void addComment(const QPointF& at);
    void commitLayout();                               // writes moved / resized items back into the script
    void setNodeComment(fg::NodeId id, const QString& text);
    QRectF contentRect() const;

    // ---- snap to grid: nodes moved by the user (and new nodes) land on a grid of `step` scene units, by their top left corner.
    // Holding Alt while dragging moves freely.
    void setSnapToGrid(bool on) { snap_ = on; }
    bool snapToGrid() const { return snap_; }
    void setSnapStep(double step) { snapStep_ = step >= 1.0 ? step : 1.0; }
    double snapStep() const { return snapStep_; }
    QPointF snapped(const QPointF& p) const;                                   // p moved to the nearest grid point (when snapping is on)
    bool snapsUserMoves() const { return snap_ && userMoving_ && !altHeld_ && !syncing_; }

signals:
    void selectionNodesChanged(const QList<int>& ids);
    void paletteRequested(const PinRef& from, const QPointF& scenePos, const QPoint& screenPos);
    void nodeDoubleClicked(int id);
    void referencesRequested(int id);           // the user asked for the references of the node (its variable, function or kind)
    void collapseRequested();                   // ... to collapse the selected nodes into a function
    void message(const QString& text);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    fg::Graph* mutableGraph(fg::Script& s) const { return s.findGraph(name_.toStdString()); }
    void onInplaceFinished();
    void hideInplace();
    void cancelInplace();
    LinkItem* linkItem(const fg::Connection& c) const;
    static QString linkKey(const fg::Connection& c);
    QByteArray buildClip(const std::set<fg::NodeId>& ids) const;
    void pasteClip(const QByteArray& data, const QPointF& at, bool keepOffset);
    void updateLinksOf(fg::NodeId id);

    Document* doc_;
    QString name_;
    std::map<fg::NodeId, NodeItem*> nodes_;
    std::map<QString, LinkItem*> links_;
    std::map<int, CommentItem*> comments_;
    bool syncing_ = false;
    bool snap_ = false;
    double snapStep_ = 20.0;
    bool userMoving_ = false;          // the left mouse button is down: item moves come from the user
    bool altHeld_ = false;             // Alt switches snapping off while dragging
    bool animating_ = false;
    QTimer animTimer_;
    fg::NodeId debugNode_ = 0;

    // link dragging
    PinRef drag_;
    LinkItem* dragLink_ = nullptr;

    // inplace pin editor
    QLineEdit* editor_ = nullptr;
    QGraphicsProxyWidget* editorProxy_ = nullptr;
    fg::NodeId editNode_ = 0;
    std::string editPin_;
    QString editOriginal_;
    bool editCancelled_ = false;
};

} // namespace fged
