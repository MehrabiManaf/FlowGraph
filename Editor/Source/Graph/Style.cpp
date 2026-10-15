#include "Style.hpp"

namespace fged {

QString defaultText(const fg::CompiledPin& p) {
    if (!p.type) return {};
    switch (p.type->kind) {
    case fg::TypeKind::Bool: return p.def.toBool() ? "X" : "";
    case fg::TypeKind::Int: return QString::number(p.def.toInt());
    case fg::TypeKind::Float: return QString::number(p.def.toFloat(), 'g', 10);
    case fg::TypeKind::String: return p.def.isString() ? qs(p.def.str()) : QString();
    case fg::TypeKind::Enum: return qs(p.type->enumName(p.def.toInt()));
    default: return {};
    }
}

fg::Value parseDefault(const fg::CompiledPin& p, const QString& text) {
    switch (p.type ? p.type->kind : fg::TypeKind::Any) {
    case fg::TypeKind::Bool: return fg::Value(!text.isEmpty());
    case fg::TypeKind::Int: return fg::Value(static_cast<int64_t>(text.toLongLong()));
    case fg::TypeKind::Float: return fg::Value(text.toDouble());
    case fg::TypeKind::String: return fg::Value(ss(text));
    case fg::TypeKind::Enum:
        for (auto& e : p.type->enumValues)
            if (qs(e.name) == text) return fg::Value(e.value);
        return fg::Value(static_cast<int64_t>(text.toLongLong()));
    default: return fg::Value();
    }
}

} // namespace fged
