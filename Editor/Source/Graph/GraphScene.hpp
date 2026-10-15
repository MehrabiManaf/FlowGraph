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
    void setPinDefault(fg::NodeId node, const std::string& pin, const fg::Value& v);
    void startInplaceEdit(NodeItem* item, const std::string& pin, const QPointF& scenePos, double width, const QString& text, const fg::TypeDesc* type);
    void chooseEnumValue(NodeItem* item, const std::string& pin, const fg::TypeDesc* type, const QPoint& screenPos);
    void nodeMoved(NodeItem* item);
    void nodeActivated(fg::NodeId id) { emit nodeDoubleClicked(id); }
    void requestAnimation() { animating_ = true; }
    void editCommentText(int id);

    // ---- editing commands
    bool connectPins(const PinRef& a, const PinRef& b, QString* why = nullptr);
    void breakPinLinks(const PinRef& p);
    void insertReroute(const fg::Connection& c, const QPointF& scenePos);
    fg::NodeId addNodeFromTemplate(const fg::NodeTemplate& t, const QPointF& pos, const PinRef* from = nullptr);
    fg::NodeId addVariableNode(const QString& name, const QString& scope, bool isSet, const QPointF& pos);
    void deleteSelected();
    void copySelected();
    bool canPaste() const;
    void paste(const QPointF& at);
    void duplicateSelected();
    void addComment(const QPointF& at);
    void commitLayout();                               // writes moved / resized items back into the script
    void setNodeComment(fg::NodeId id, const QString& text);
    QRectF contentRect() const;

signals:
    void selectionNodesChanged(const QList<int>& ids);
    void paletteRequested(const PinRef& from, const QPointF& scenePos, const QPoint& screenPos);
    void nodeDoubleClicked(int id);
    void message(const QString& text);

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    fg::Graph* mutableGraph(fg::Script& s) const { return s.findGraph(name_.toStdString()); }
    void onInplaceFinished();
    void hideInplace();
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
