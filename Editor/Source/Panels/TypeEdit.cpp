#include "TypeEdit.hpp"

#include <QCompleter>
#include <QLineEdit>

namespace fged {

TypeEdit::TypeEdit(const fg::Registry& reg, QWidget* parent, bool) : QComboBox(parent), reg_(reg) {
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    QStringList items;
    for (auto* t : reg.types()) items << QString::fromStdString(t->name);
    QStringList all = items;
    for (auto& n : items) all << "array<" + n + ">";
    for (auto& n : items) all << "map<string," + n + ">";
    addItems(all);
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
    if (!reg_.findType(t.toStdString())) {
        setCurrentText(current_);
        return;
    }
    current_ = t;
    emit typeEdited(t);
}

} // namespace fged
