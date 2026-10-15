#include <FlowGraph/Types.hpp>

namespace fg {

bool TypeDesc::isA(const TypeDesc* other) const {
    if (this == other) return true;
    if (base && base->isA(other)) return true;
    for (auto& b : extraBases)
        if (b.type->isA(other)) return true;
    return false;
}

bool TypeDesc::hasDirectBase(const TypeDesc* b) const {
    if (base == b) return true;
    for (auto& e : extraBases)
        if (e.type == b) return true;
    return false;
}

const PropertyDesc* TypeDesc::findProperty(const std::string& n) const {
    for (auto& p : properties)
        if (p.name == n) return &p;
    if (base)
        if (auto* p = base->findProperty(n)) return p;
    for (auto& b : extraBases)
        if (auto* p = b.type->findProperty(n)) return p;
    return nullptr;
}

std::vector<const PropertyDesc*> TypeDesc::allProperties() const {
    std::vector<const PropertyDesc*> out;
    auto add = [&](const PropertyDesc* p) {
        for (auto* e : out)
            if (e->name == p->name) return;     // derived classes override
        out.push_back(p);
    };
    for (auto& p : properties) add(&p);
    if (base)
        for (auto* p : base->allProperties()) add(p);
    for (auto& b : extraBases)
        for (auto* p : b.type->allProperties()) add(p);
    return out;
}

namespace {
void* walk(const TypeDesc* t, void* p, const TypeDesc* target) {
    if (!t || !p) return nullptr;
    if (t == target) return p;
    if (t->base && t->toBase)
        if (void* r = walk(t->base, t->toBase(p), target)) return r;
    for (auto& b : t->extraBases)
        if (void* r = walk(b.type, b.up(p), target)) return r;
    return nullptr;
}
} // namespace

void* castPtr(const ObjectHandle& h, const TypeDesc* target) {
    return walk(h.type, h.ptr.get(), target);
}

} // namespace fg
