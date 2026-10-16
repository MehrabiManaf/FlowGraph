#include "FitTree.hpp"

#include <QHeaderView>
#include <QMimeData>
#include <QTreeWidgetItemIterator>

namespace fged {

FitTree::FitTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(10);
    setUniformRowHeights(true);
    setSelectionMode(SingleSelection);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFrameShape(QFrame::NoFrame);
    setEditTriggers(EditKeyPressed);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

QSize FitTree::sizeHint() const {
    int rows = 0;
    for (QTreeWidgetItemIterator it(const_cast<FitTree*>(this)); *it; ++it) {
        // children of collapsed items are not visible
        bool visible = true;
        for (QTreeWidgetItem* p = (*it)->parent(); p; p = p->parent()) visible &= p->isExpanded();
        rows += visible;
    }
    int rowH = topLevelItemCount() > 0 ? sizeHintForRow(0) : fontMetrics().height() + 6;
    if (rowH <= 0) rowH = fontMetrics().height() + 6;
    if (rows == 0) rows = 1;
    return QSize(160, rows * rowH + 6);
}

void FitTree::fit() {
    updateGeometry();
    setFixedHeight(sizeHint().height());
}

QMimeData* FitTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    if (mime_.isEmpty() || items.isEmpty()) return QTreeWidget::mimeData(items);
    auto* m = new QMimeData;
    m->setData(mime_, items.front()->data(0, Qt::UserRole + 2).toString().toUtf8());
    return m;
}

} // namespace fged
