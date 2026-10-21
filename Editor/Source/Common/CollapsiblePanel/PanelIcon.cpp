// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "PanelIcon.hpp"
#include <QStyle>
#include <QPainter>
#include <QResizeEvent>


CPanelIcon::CPanelIcon(QWidget* parent):
    QWidget(parent)
{
    auto IconSize = style()->pixelMetric(QStyle::PM_ButtonIconSize);
    setFixedSize(IconSize, IconSize);
    m_IconRotation = 0.0;
    m_IsIconVisible = false;
}

CPanelIcon::~CPanelIcon()
{

}

void CPanelIcon::resizeEvent(QResizeEvent* pEvent)
{
    double Width = static_cast<double>(pEvent->size().width());
    double Height = static_cast<double>(pEvent->size().height());
    double HWidth = Width / 2;
    double HHeight = Height / 2;
    m_IconPolygon.clear();
    m_IconPolygon << QPoint(-HWidth * 3 / Width, -HHeight / 2)
              << QPoint( HWidth / 2,      0)
              << QPoint(-HWidth * 3 / Width,  HHeight / 2);
    return QWidget::resizeEvent(pEvent);
}

void CPanelIcon::paintEvent(QPaintEvent* /*pEvent*/)
{
    if(m_IsIconVisible)
    {
        QPainter Painter(this);
        Painter.setRenderHints(QPainter::Antialiasing);
        Painter.setBrush(Qt::NoBrush);
        Painter.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
        double CenterX = static_cast<double>(width()) / 2;
        double CenterY = static_cast<double>(height()) / 2;
        Painter.translate(CenterX, CenterY);
        Painter.rotate(m_IconRotation);
        if(m_IconPolygon.size() > 0)
            Painter.drawPolygon(m_IconPolygon);
    }
}

void CPanelIcon::RotateIcon(double RotationAngle)
{
    m_IconRotation = RotationAngle;
    update();
}

