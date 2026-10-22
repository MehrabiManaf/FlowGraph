// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "GraphView.hpp"
#include "../Common/Theme.hpp"
#include "LinkItem.hpp"
#include "PalettePopup.hpp"

#include <QContextMenuEvent>
#include <QCursor>
#include <QInputDialog>
#include <QLineEdit>
#include <algorithm>
#include <cmath>
#include <QDragEnterEvent>
#include <QGraphicsProxyWidget>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QTimer>
#include <QScrollBar>
#include <QResizeEvent>
#include <QWheelEvent>

namespace fged {

GraphView::GraphView(GraphScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    // execution flows from left to right in every language: the canvas is never mirrored, whatever the layout direction
    setLayoutDirection(Qt::LeftToRight);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setOptimizationFlag(QGraphicsView::DontSavePainterState);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorUnderMouse);
    setViewportUpdateMode(FullViewportUpdate);
    setDragMode(RubberBandDrag);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(scene, &GraphScene::paletteRequested, this, [this](const PinRef& from, const QPointF& sp, const QPoint& screen) { showPalette(from, sp, screen); });
}

void GraphView::setZoom(double z) {
    zoom_ = std::clamp(z, 0.25, 2.0);
    QTransform t;
    t.scale(zoom_, zoom_);
    setTransform(t);
}

void GraphView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (autoFrame_) frameAll();
}

void GraphView::wheelEvent(QWheelEvent* e) {
    autoFrame_ = false;
    int d = e->angleDelta().y();
    if (d == 0) { e->ignore(); return; }
    setZoom(d > 0 ? zoom_ * 1.15 : zoom_ / 1.15);
    e->accept();
}

void GraphView::drawBackground(QPainter* p, const QRectF& rect) {
    p->save();
    const ThemeColors& tc = themeColors();
    p->fillRect(rect, tc.canvas);
    int major = zoom_ < 0.6 ? 500 : 100;
    int minor = major / 5;
    auto lines = [&](int spacing, double w) {
        p->setPen(QPen(spacing == major ? tc.gridMajor : tc.gridMinor, w));
        double y = rect.top() - std::fmod(rect.top(), spacing);
        for (; y < rect.bottom(); y += spacing) p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        double x = rect.left() - std::fmod(rect.left(), spacing);
        for (; x < rect.right(); x += spacing) p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    };
    lines(minor, 0.7 / zoom_);
    lines(major, 2.0 / zoom_);
    p->restore();
}

void GraphView::frameAll() {
    QRectF r = graphScene()->contentRect();
    if (r.isNull()) { setZoom(1.0); centerOn(0, 0); return; }
    r.adjust(-80, -80, 80, 80);
    fitInView(r, Qt::KeepAspectRatio);
    zoom_ = std::clamp(transform().m11(), 0.25, 1.0);
    QTransform t;
    t.scale(zoom_, zoom_);
    setTransform(t);
    centerOn(r.center());
}

void GraphView::centerOnNode(fg::NodeId id, bool animated) {
    autoFrame_ = false;
    NodeItem* n = graphScene()->nodeItem(id);
    if (!n) return;
    QPointF target = n->sceneBoundingRect().center();
    if (!animated || QLineF(sceneCenter(), target).length() < 50) {
        centerOn(target);
        return;
    }
    auto* a = new QPropertyAnimation(this, "SceneCenter");
    a->setDuration(300);
    a->setStartValue(sceneCenter());
    a->setEndValue(target);
    a->setEasingCurve(QEasingCurve::InOutCubic);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void GraphView::showPalette(const PinRef& from, const QPointF& scenePos, const QPoint& screenPos) {
    auto* pop = new PalettePopup(graphScene(), from, this);
    GraphScene* sc = graphScene();
    connect(pop, &PalettePopup::chosen, sc, [sc, from, scenePos](const fg::NodeTemplate& t) {
        sc->addNodeFromTemplate(t, scenePos, from.valid() ? &from : nullptr);
    });
    pop->popup(screenPos);
}

// ------------------------------------------------------------------ mouse
void GraphView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        QGraphicsItem* under = itemAt(e->pos());
        if (e->button() == Qt::MiddleButton || !under || dynamic_cast<LinkItem*>(under)) {
            autoFrame_ = false;
            panning_ = true;
            panMoved_ = false;
            panButton_ = e->button();
            panLast_ = e->pos();
            if (e->button() == Qt::MiddleButton) { setCursor(Qt::ClosedHandCursor); e->accept(); return; }
            // right button: keep the event for the context menu if it turns out to be a click
            return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void GraphView::mouseMoveEvent(QMouseEvent* e) {
    // the release can be lost (e.g. a popup grabbed the mouse): never keep panning without the button held
    if (panning_ && !(e->buttons() & panButton_)) {
        panning_ = false;
        pendingContext_ = false;
        unsetCursor();
    }
    if (panning_) {
        QPoint d = e->pos() - panLast_;
        if (!panMoved_ && (e->pos() - panLast_).manhattanLength() < 4 && panButton_ == Qt::RightButton) return;
        panMoved_ = true;
        panLast_ = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        if (panButton_ == Qt::RightButton) setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void GraphView::mouseReleaseEvent(QMouseEvent* e) {
    if (panning_ && e->button() == panButton_) {
        panning_ = false;
        unsetCursor();
        suppressContext_ = panMoved_ && panButton_ == Qt::RightButton;
        if (panButton_ == Qt::MiddleButton) { e->accept(); return; }
        if (pendingContext_) {
            pendingContext_ = false;
            bool show = !suppressContext_;
            suppressContext_ = false;
            e->accept();
            if (show) {
                QContextMenuEvent ce(QContextMenuEvent::Mouse, e->pos(), e->globalPosition().toPoint(), e->modifiers());
                contextMenuEvent(&ce);
            }
            return;
        }
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void GraphView::contextMenuEvent(QContextMenuEvent* e) {
    if (suppressContext_) { suppressContext_ = false; return; }
    // platforms that send the context menu on press: wait for the release so a right-drag can pan
    // and the popup doesn't swallow the release (which would leave the view stuck panning)
    if (panning_ && panButton_ == Qt::RightButton && e->reason() == QContextMenuEvent::Mouse) {
        pendingContext_ = true;
        e->accept();
        return;
    }
    GraphScene* sc = graphScene();
    QPointF sp = mapToScene(e->pos());
    QGraphicsItem* under = nullptr;
    for (QGraphicsItem* it : items(e->pos()))
        if (dynamic_cast<NodeItem*>(it) || dynamic_cast<LinkItem*>(it)) { under = it; break; }

    if (auto* node = dynamic_cast<NodeItem*>(under)) {
        if (auto pin = node->pinAt(sp)) {
            QMenu m;
            QAction* brk = m.addAction(themedIcon(":/Icons/break-links.svg"), tr("Break all links"));
            if (m.exec(e->globalPos()) == brk) sc->breakPinLinks(*pin);
            return;
        }
        if (!node->isSelected()) { sc->clearSelection(); node->setSelected(true); }
        QMenu m;
        QAction* cut = m.addAction(themedIcon(":/Icons/cut.svg"), tr("Cut"));
        QAction* copy = m.addAction(themedIcon(":/Icons/copy.svg"), tr("Copy"));
        QAction* dup = m.addAction(themedIcon(":/Icons/duplicate.svg"), tr("Duplicate"));
        QAction* del = m.addAction(themedIcon(":/Icons/delete.svg"), tr("Delete"));
        m.addSeparator();
        const bool hasBreakpoint = sc->isBreakpoint(node->nodeId());
        QAction* bp = m.addAction(themedIcon(hasBreakpoint ? ":/Icons/breakpoint-remove.svg" : ":/Icons/breakpoint-add.svg"),
                                  hasBreakpoint ? tr("Remove breakpoint") : tr("Add breakpoint"));
        QAction* note = m.addAction(themedIcon(":/Icons/edit-comment.svg"), tr("Edit node comment..."));
        QAction* open = nullptr;
        if (node->compiled().op == fg::NodeOp::CallFunction) open = m.addAction(themedIcon(":/Icons/open-function.svg"), tr("Open function graph"));
        QAction* refs = m.addAction(themedIcon(":/Icons/find-references.svg"), tr("Find References"));
        refs->setShortcut(QKeySequence("Shift+F12"));
        QAction* collapse = m.addAction(themedIcon(":/Icons/function.svg"), tr("Collapse to Function..."));
        collapse->setEnabled(sc->canCollapseSelection());
        QAction* a = m.exec(e->globalPos());
        if (!a) return;
        if (a == cut) { sc->copySelected(); sc->deleteSelected(); }
        else if (a == copy) sc->copySelected();
        else if (a == dup) sc->duplicateSelected();
        else if (a == del) sc->deleteSelected();
        else if (a == bp) sc->toggleBreakpoint(node->nodeId());
        else if (a == note) {
            bool ok = false;
            QString t = QInputDialog::getText(this, tr("Node comment"), tr("Comment:"), QLineEdit::Normal, qs(node->compiled().comment), &ok);
            if (ok) sc->setNodeComment(node->nodeId(), t);
        } else if (a == open) sc->nodeActivated(node->nodeId());
        else if (a == refs) sc->findReferences(node->nodeId());
        else if (a == collapse) sc->collapseSelection();
        return;
    }
    if (auto* link = dynamic_cast<LinkItem*>(under)) {
        QMenu m;
        QAction* reroute = m.addAction(themedIcon(":/Icons/reroute.svg"), tr("Add reroute node"));
        QAction* del = m.addAction(themedIcon(":/Icons/delete.svg"), tr("Delete link"));
        QAction* a = m.exec(e->globalPos());
        if (a == reroute) sc->insertReroute(link->connection(), sp);
        else if (a == del) { sc->clearSelection(); link->setSelected(true); sc->deleteSelected(); }
        return;
    }
    showPalette(PinRef(), sp, e->globalPos());
}

// ------------------------------------------------------------------ keyboard
void GraphView::keyPressEvent(QKeyEvent* e) {
    GraphScene* sc = graphScene();
    // while a pin value is being edited the keys belong to the editor (Backspace must not delete nodes)
    if (dynamic_cast<QGraphicsProxyWidget*>(sc->focusItem())) {
        QGraphicsView::keyPressEvent(e);
        return;
    }
    QPointF cursor = mapToScene(mapFromGlobal(QCursor::pos()));
    bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) { sc->deleteSelected(); e->accept(); return; }
    if (ctrl && e->key() == Qt::Key_C) { sc->copySelected(); e->accept(); return; }
    if (ctrl && e->key() == Qt::Key_X) { sc->copySelected(); sc->deleteSelected(); e->accept(); return; }
    if (ctrl && e->key() == Qt::Key_V) { sc->paste(cursor); e->accept(); return; }
    if (ctrl && e->key() == Qt::Key_D) { sc->duplicateSelected(); e->accept(); return; }
    if (ctrl && e->key() == Qt::Key_A) {
        QPainterPath p;
        p.addRect(sc->contentRect());
        sc->setSelectionArea(p);
        e->accept();
        return;
    }
    if (!ctrl && e->key() == Qt::Key_C) { sc->addComment(cursor); e->accept(); return; }
    if (e->key() == Qt::Key_Home) { frameAll(); e->accept(); return; }
    if (e->key() == Qt::Key_F9) {
        for (auto id : sc->selectedNodeIds()) sc->toggleBreakpoint(id);
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

// ------------------------------------------------------------------ drag & drop of variables
static bool isOurs(const QMimeData* m) { return m->hasFormat(GraphView::variableMime()) || m->hasFormat(GraphView::functionMime()); }

void GraphView::dragEnterEvent(QDragEnterEvent* e) {
    if (isOurs(e->mimeData())) e->acceptProposedAction();
    else QGraphicsView::dragEnterEvent(e);
}
void GraphView::dragMoveEvent(QDragMoveEvent* e) {
    if (isOurs(e->mimeData())) e->acceptProposedAction();
    else QGraphicsView::dragMoveEvent(e);
}

void GraphView::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasFormat(functionMime())) {
        // a function dropped on the graph: a node that calls it
        const QString function = QString::fromUtf8(e->mimeData()->data(functionMime()));
        if (!function.isEmpty()) graphScene()->addFunctionCall(function, mapToScene(e->position().toPoint()));
        e->acceptProposedAction();
        return;
    }
    if (!e->mimeData()->hasFormat(variableMime())) { QGraphicsView::dropEvent(e); return; }
    QStringList parts = QString::fromUtf8(e->mimeData()->data(variableMime())).split('|');
    if (parts.size() < 2) return;
    QString scope = parts[0], name = parts[1];
    QPointF sp = mapToScene(e->position().toPoint());
    bool isSet;
    if (e->modifiers() & Qt::ControlModifier) isSet = false;
    else if (e->modifiers() & Qt::AltModifier) isSet = true;
    else {
        // Neither key: the user chooses in a menu. It is shown when the drop is over, not inside it: a popup that opens while the drag and
        // drop of the system is still handing the drop over can lose the mouse at once and vanish before anybody sees it.
        e->acceptProposedAction();
        const QPoint at = viewport()->mapToGlobal(e->position().toPoint());
        QPointer<GraphView> self(this);
        QTimer::singleShot(0, this, [self, name, scope, sp, at] {
            if (!self) return;
            QMenu m;
            m.addAction(themedIcon(":/Icons/variable-get.svg"), tr("Get %1").arg(name))->setData(false);
            m.addAction(themedIcon(":/Icons/variable-set.svg"), tr("Set %1").arg(name))->setData(true);
            QAction* a = m.exec(at);
            if (a && self) self->graphScene()->addVariableNode(name, scope, a->data().toBool(), sp);
        });
        return;
    }
    graphScene()->addVariableNode(name, scope, isSet, sp);
    e->acceptProposedAction();
}

} // namespace fged
