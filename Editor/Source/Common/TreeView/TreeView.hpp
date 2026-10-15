#pragma once

#include <QTreeView>


class CTreeView: public QTreeView
{
    Q_OBJECT
public:
    CTreeView(QWidget* pParent = nullptr);
    ~CTreeView();

public:

protected:
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
signals:
    void SelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
};
