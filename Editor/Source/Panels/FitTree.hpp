// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QTreeWidget>

namespace fged {

// A tree without header that reports a size hint matching its rows, so it can live inside a collapsible panel.
class FitTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit FitTree(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    // Called after the content changed so the parent re-layouts.
    void fit();
    // Mime payload of dragged items: "scope|name" (set per item with Qt::UserRole+2).
    void setDragMimeType(const QString& t) { mime_ = t; }

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;

private:
    QString mime_;
};

} // namespace fged
