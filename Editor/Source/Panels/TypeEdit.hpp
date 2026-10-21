// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <FlowGraph/Registry.hpp>

#include <QComboBox>

namespace fged {

// Editable combo box listing the registry's types (plus array<..> / map<string,..> variants).
// It only accepts type expressions the registry understands.
class TypeEdit : public QComboBox {
    Q_OBJECT
public:
    TypeEdit(const fg::Registry& reg, QWidget* parent = nullptr, bool allowVoidlessAny = true);
    QString type() const { return current_; }
    void setType(const QString& t);

signals:
    void typeEdited(const QString& type);

private:
    void commit();
    const fg::Registry& reg_;
    QString current_;
};

} // namespace fged
