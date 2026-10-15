#pragma once

#include <QWidget>
#include <QPolygon>


class CPanelIcon: public QWidget
{
    Q_OBJECT
public:
    CPanelIcon(QWidget* parent = nullptr);
    ~CPanelIcon();


public:
    void paintEvent(QPaintEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;
    void RotateIcon(double RotationAngle);
    void SetIconVisible(bool Visible) { m_IsIconVisible = Visible; }
protected:
    QPolygon            m_IconPolygon;
    double              m_IconRotation;
    bool                m_IsIconVisible;
};
