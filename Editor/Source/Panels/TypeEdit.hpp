// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <FlowGraph/TypeScope.hpp>

#include <QComboBox>

namespace fged {

// Editable combo box listing the types a script can use: the registry's and the script's structs. The container (array / map) is
// chosen elsewhere. It only accepts type expressions the scope understands.
class TypeEdit : public QComboBox {
    Q_OBJECT
public:
    TypeEdit(std::shared_ptr<const fg::TypeScope> types, QWidget* parent = nullptr);
    QString type() const { return current_; }
    void setType(const QString& t);

signals:
    void typeEdited(const QString& type);

private:
    void commit();
    std::shared_ptr<const fg::TypeScope> types_;
    QString current_;
};

} // namespace fged
