#pragma once
// A type expression split into the parts the editors show in separate columns:
// container kind (single / array / map), the key type (maps) and the element / value type.
#include <FlowGraph/TypeExpr.hpp>

#include <QString>

namespace fged {

struct TypeSpec {
    enum class Container { Single, Array, Map };
    Container container = Container::Single;
    QString key = "string";      // only used by maps
    QString value = "int";       // the type itself for single values

    static TypeSpec parse(const QString& expr) {
        TypeSpec s;
        fg::TypeExpr e = fg::TypeExpr::parse(expr.toStdString());
        if (e.valid && e.isArray()) {
            s.container = Container::Array;
            s.value = QString::fromStdString(e.args[0].str());
        } else if (e.valid && e.isMap()) {
            s.container = Container::Map;
            s.key = QString::fromStdString(e.args[0].str());
            s.value = QString::fromStdString(e.args[1].str());
        } else {
            s.value = expr;
        }
        return s;
    }

    QString str() const {
        switch (container) {
        case Container::Array: return "array<" + value + ">";
        case Container::Map: return "map<" + key + "," + value + ">";
        default: return value;
        }
    }
};

} // namespace fged
