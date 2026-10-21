// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QIcon>
#include <QLineEdit>

#include "Theme.hpp"

namespace fged {

// The look of every search box of the editor (My Script, Details, Find Results): a magnifier on the left, a clear button on the
// right and a placeholder text.
inline void makeSearchField(QLineEdit* edit, const QString& placeholder) {
    edit->setPlaceholderText(placeholder);
    edit->setClearButtonEnabled(true);
    edit->addAction(themedIcon(":/Icons/find.svg"), QLineEdit::LeadingPosition);
}

} // namespace fged
