// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <kddockwidgets/qtwidgets/views/Group.h>


class CFrame: public KDDockWidgets::QtWidgets::Group
{
    Q_OBJECT
public:
    explicit CFrame(KDDockWidgets::Core::Group* pController, QWidget* pParent = nullptr);
    void paintEvent(QPaintEvent*) override;
};
