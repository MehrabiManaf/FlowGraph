// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <kddockwidgets/qtwidgets/views/Stack.h>


class CExpandTabButton: public QWidget
{
    Q_OBJECT
public:
    explicit CExpandTabButton(QWidget* pParent = nullptr);
    ~CExpandTabButton() override;
public:
    void SetAnimationProgress(const double& Opacity);
protected:
    void UpdateHoverState(const QPointF& Point);
protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;
    void enterEvent(QEnterEvent* pEvent) override;
    void leaveEvent(QEvent* pEvent) override;
signals:
    void Clicked();
protected:
    bool        m_IsHovered;
    QPolygonF   m_Polygon;
    double      m_Opacity;
};

// ======================================================================

class CTabWidget: public KDDockWidgets::QtWidgets::Stack
{
    Q_OBJECT
public:
    explicit CTabWidget(KDDockWidgets::Core::Stack* pController, QWidget* pParent = nullptr);
    ~CTabWidget() override;

public:
    void SetAnimationProgress(const double& Progress);
protected:
    void init() override;
private slots:
    void OnExpandTabsClicked();
private:
    CExpandTabButton*           mp_ExpandTabButton;
    int                         m_ExpandTabButtonSize;
};




