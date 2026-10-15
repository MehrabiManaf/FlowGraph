#pragma once

#include <QTableView>


class CTableView: public QTableView
{
    Q_OBJECT
public:
    CTableView(QWidget* pParent = nullptr);
    ~CTableView();

public:

protected:
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
signals:
    void SelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
protected:
    // CEventGraphModel*                           mp_EventGraphItemModel;

    // QPointer<CProxyModel>                                   mp_ProxyModel;
};




