// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "TreeView.hpp"
#include <QHeaderView>



CTreeView::CTreeView(QWidget* pParent):
    QTreeView(pParent)
{

    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);

    header()->setStretchLastSection(true);

    // horizontalHeader()->setMinimumSectionSize(20);
    // horizontalHeader()->setMaximumSectionSize(150);
    // horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // setSortingEnabled(true);
    // resizeColumnsToContents();
    setAlternatingRowColors(true);
    // setDragEnabled(true);
    // setDragDropMode(QAbstractItemView::DragOnly);
}

CTreeView::~CTreeView()
{

}

void CTreeView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    QTreeView::selectionChanged(selected, deselected);
    emit SelectionChanged(selected, deselected);
}




