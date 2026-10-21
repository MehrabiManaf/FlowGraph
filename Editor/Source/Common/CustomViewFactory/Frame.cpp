// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Frame.hpp"
#include <QApplication>
#include <QPainter>

CFrame::CFrame(KDDockWidgets::Core::Group* pController, QWidget* pParent):
    KDDockWidgets::QtWidgets::Group(pController, pParent)
{
    setAutoFillBackground(true);
}

void CFrame::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    // p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(palette().base().color(), 1.0));
    p.drawRect(rect().adjusted(1,1,-1,-1));
}
