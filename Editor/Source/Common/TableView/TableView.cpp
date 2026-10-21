// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "TableView.hpp"
#include <QHeaderView>



CTableView::CTableView(QWidget* pParent):
    QTableView(pParent)
{
    // mp_FunctionListItemModel = new CScriptFunctionListItemModel(this);
    // mp_ProxyModel = new CProxyModel(this);
    // mp_ProxyModel->setSourceModel(mp_FunctionListItemModel);
    // mp_ProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    // setModel(mp_ProxyModel);

    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);

    horizontalHeader()->setStretchLastSection(true);

    horizontalHeader()->setMinimumSectionSize(20);
    horizontalHeader()->setMaximumSectionSize(150);
    horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    horizontalHeader()->hide();
    verticalHeader()->hide();

    // setSortingEnabled(true);
    resizeColumnsToContents();
    horizontalHeader()->setSectionsMovable(true);
    setAlternatingRowColors(true);
    // setDragEnabled(true);
    // setDragDropMode(QAbstractItemView::DragOnly);
}

CTableView::~CTableView()
{

}

void CTableView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    QTableView::selectionChanged(selected, deselected);
    emit SelectionChanged(selected, deselected);
}



