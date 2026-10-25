// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "TypeEdit.hpp"

#include <QCompleter>
#include <QLineEdit>

namespace fged {

TypeEdit::TypeEdit(std::shared_ptr<const fg::TypeScope> types, QWidget* parent) : QComboBox(parent), types_(std::move(types)) {
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    // base types only: the container (array / map) is chosen in its own column
    QStringList items;
    for (auto* t : types_->types()) items << QString::fromStdString(t->name);
    addItems(items);
    if (completer()) completer()->setCaseSensitivity(Qt::CaseInsensitive);
    connect(lineEdit(), &QLineEdit::editingFinished, this, [this] { commit(); });
    connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int) { commit(); });
}

void TypeEdit::setType(const QString& t) {
    current_ = t;
    blockSignals(true);
    setCurrentText(t);
    blockSignals(false);
}

void TypeEdit::commit() {
    QString t = currentText().trimmed();
    if (t == current_) return;
    if (!types_->findType(t.toStdString())) {
        setCurrentText(current_);
        return;
    }
    current_ = t;
    emit typeEdited(t);
}

} // namespace fged
