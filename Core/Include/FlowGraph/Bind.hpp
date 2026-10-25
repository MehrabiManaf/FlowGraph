// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Template machinery that turns C++ classes, member functions, data members, enums and free
// functions into script visible types and nodes. Included at the end of Registry.hpp.
#include <FlowGraph/Registry.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace fg {
namespace detail {

template <class T> using Bare = std::remove_cv_t<std::remove_reference_t<T>>;

template <class T> struct IsVector : std::false_type {};
template <class E, class A> struct IsVector<std::vector<E, A>> : std::true_type {};
template <class T> struct IsMap : std::false_type {};
template <class K, class V, class C, class A> struct IsMap<std::map<K, V, C, A>> : std::true_type {};
template <class T> struct IsShared : std::false_type {};
template <class T> struct IsShared<std::shared_ptr<T>> : std::true_type { using Elem = T; };
// The result of an asynchronous host function (see FunctionDesc::latent).
template <class T> struct IsFuture : std::false_type {};
template <class T> struct IsFuture<std::future<T>> : std::true_type { using Result = T; };
template <class T> struct IsFuture<std::shared_future<T>> : std::true_type { using Result = T; };
template <class T> struct IsUnique : std::false_type {};
template <class T, class D> struct IsUnique<std::unique_ptr<T, D>> : std::true_type { using Elem = T; };

// A registered (reflected) class used by value, reference or pointer.
template <class T>
inline constexpr bool IsClassType = std::is_class_v<T> && !std::is_same_v<T, std::string> && !std::is_same_v<T, Value> &&
                                    !std::is_same_v<T, Bytes> &&
                                    !IsVector<T>::value && !IsMap<T>::value && !IsShared<T>::value && !IsUnique<T>::value &&
                                    !IsFuture<T>::value;

// ---------------------------------------------------------------- function traits
template <class F, class = void> struct FnTraits : FnTraits<decltype(&F::operator())> {};
#define FG_FN_FREE(NOEXC, UNUSED)                                                                                 \
    template <class R, class... A> struct FnTraits<R (*)(A...) NOEXC, void> {                               \
        using Ret = R;                                                                                      \
        using Args = std::tuple<A...>;                                                                      \
        static constexpr bool isConst = false;                                                              \
    };
FG_FN_FREE(, )
FG_FN_FREE(noexcept, )
#define FG_FN_MEM(CV, NOEXC, ISC)                                                                           \
    template <class R, class C, class... A> struct FnTraits<R (C::*)(A...) CV NOEXC, void> {                \
        using Ret = R;                                                                                      \
        using Args = std::tuple<A...>;                                                                      \
        using Class = C;                                                                                    \
        static constexpr bool isConst = ISC;                                                                \
    };
FG_FN_MEM(, , false)
FG_FN_MEM(const, , true)
FG_FN_MEM(, noexcept, false)
FG_FN_MEM(const, noexcept, true)
#undef FG_FN_FREE
#undef FG_FN_MEM

template <class Tuple> struct TupleTail;
template <class H, class... Ts> struct TupleTail<std::tuple<H, Ts...>> {
    using type = std::tuple<Ts...>;
    using Head = H;
};
template <class T, class Tuple> struct TupleCons;
template <class T, class... Ts> struct TupleCons<T, std::tuple<Ts...>> { using type = std::tuple<T, Ts...>; };

// Turns a pointer to member function into a callable taking the object as first argument.
template <class T, class PM> struct MemberCaller;
#define FG_MEMBER_CALLER(CV, NOEXC)                                                                         \
    template <class T, class R, class C, class... A> struct MemberCaller<T, R (C::*)(A...) CV NOEXC> {     \
        R (C::*pm)(A...) CV NOEXC;                                                                          \
        R operator()(CV T& self, A... a) const { return (self.*pm)(std::forward<A>(a)...); }                \
    };
FG_MEMBER_CALLER(, )
FG_MEMBER_CALLER(const, )
FG_MEMBER_CALLER(, noexcept)
FG_MEMBER_CALLER(const, noexcept)
#undef FG_MEMBER_CALLER

// ---------------------------------------------------------------- C++ type <-> script value
// The registered type of a C++ type, or null. A C++ type is registered once and keeps its TypeDesc, so the answer is remembered per
// thread (for the last registry asked): the lookup takes a lock and hashes a type_info, and every call of a reflected function
// that takes an object would pay for it.
template <class T> const TypeDesc* registeredTypeOf(const Registry& r) {
    struct Cache {
        uint64_t registry = 0;
        const TypeDesc* type = nullptr;
    };
    static thread_local Cache cache;
    if (cache.registry == r.uid()) return cache.type;
    const TypeDesc* t = r.findTypeByCpp(typeid(std::remove_cv_t<T>));
    if (t) cache = {r.uid(), t};
    return t;
}

template <class T> const TypeDesc* classTypeOf(const Registry& r) {
    const TypeDesc* t = registeredTypeOf<T>(r);
    if (!t) throw std::logic_error(std::string("C++ type '") + typeid(T).name() + "' is not registered with the script registry");
    return t;
}

template <class T, class = void> struct Conv;

template <> struct Conv<bool> {
    static const TypeDesc* type(const Registry& r) { return r.tBool(); }
    static Value to(const Registry&, bool v) { return Value(v); }
    static bool from(const Registry&, const Value& v) { return v.toBool(); }
};
template <class T> struct Conv<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static const TypeDesc* type(const Registry& r) { return r.tInt(); }
    static Value to(const Registry&, T v) { return Value(static_cast<int64_t>(v)); }
    static T from(const Registry&, const Value& v) { return static_cast<T>(v.toInt()); }
};
template <class T> struct Conv<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static const TypeDesc* type(const Registry& r) { return r.tFloat(); }
    static Value to(const Registry&, T v) { return Value(static_cast<double>(v)); }
    static T from(const Registry&, const Value& v) { return static_cast<T>(v.toFloat()); }
};
template <> struct Conv<std::string> {
    static const TypeDesc* type(const Registry& r) { return r.tString(); }
    static Value to(const Registry&, std::string v) { return Value(std::move(v)); }
    static std::string from(const Registry& r, const Value& v) { return v.isString() ? v.str() : r.toString(v); }
};
template <> struct Conv<Value> {
    static const TypeDesc* type(const Registry& r) { return r.tAny(); }
    static Value to(const Registry&, Value v) { return v; }
    static Value from(const Registry&, const Value& v) { return v; }
};
// fg::Bytes <-> bytes (shared, no copy). Anything that is not bytes is an error: text becomes bytes with From Text.
template <> struct Conv<Bytes> {
    static const TypeDesc* type(const Registry& r) { return r.tBytes(); }
    static Value to(const Registry&, Bytes v) { return Value(std::move(v)); }
    static Bytes from(const Registry& r, const Value& v) { return v.isBytes() ? v.bytes() : r.convert(v, r.tBytes()).bytes(); }
};
// std::byte <-> byte (uint8_t stays an int, as it always was)
template <> struct Conv<std::byte> {
    static const TypeDesc* type(const Registry& r) { return r.tByte(); }
    static Value to(const Registry&, std::byte v) { return Value(static_cast<int64_t>(static_cast<uint8_t>(v))); }
    static std::byte from(const Registry&, const Value& v) { return static_cast<std::byte>(static_cast<uint8_t>(v.toInt())); }
};
template <class E> struct Conv<E, std::enable_if_t<std::is_enum_v<E>>> {
    static const TypeDesc* type(const Registry& r) {
        const TypeDesc* t = r.findTypeByCpp(typeid(E));
        return t ? t : r.tInt();
    }
    static Value to(const Registry&, E v) { return Value(static_cast<int64_t>(v)); }
    static E from(const Registry&, const Value& v) { return static_cast<E>(v.toInt()); }
};
// std::vector<T> <-> array<T> (copies)
template <class T, class A> struct Conv<std::vector<T, A>> {
    static const TypeDesc* type(const Registry& r) { return r.arrayOf(Conv<T>::type(r)); }
    static Value to(const Registry& r, const std::vector<T, A>& v) {
        auto arr = std::make_shared<ArrayData>();
        arr->elem = Conv<T>::type(r);
        arr->items.reserve(v.size());
        for (auto& e : v) arr->items.push_back(Conv<T>::to(r, e));
        return Value(std::move(arr));
    }
    static std::vector<T, A> from(const Registry& r, const Value& v) {
        std::vector<T, A> out;
        if (auto* a = v.arrayPtr()) {
            out.reserve(a->items.size());
            for (auto& e : a->items) out.push_back(Conv<T>::from(r, e));
        }
        return out;
    }
};
// std::map<K,V> <-> map<K,V> (copies)
template <class K, class V, class C, class A> struct Conv<std::map<K, V, C, A>> {
    static const TypeDesc* type(const Registry& r) { return r.mapOf(Conv<K>::type(r), Conv<V>::type(r)); }
    static Value to(const Registry& r, const std::map<K, V, C, A>& m) {
        auto md = std::make_shared<MapData>();
        md->key = Conv<K>::type(r);
        md->val = Conv<V>::type(r);
        for (auto& [k, v] : m) md->items.emplace(Conv<K>::to(r, k), Conv<V>::to(r, v));
        return Value(std::move(md));
    }
    static std::map<K, V, C, A> from(const Registry& r, const Value& v) {
        std::map<K, V, C, A> out;
        if (auto* m = v.mapPtr())
            for (auto& [k, val] : m->items) out.emplace(Conv<K>::from(r, k), Conv<V>::from(r, val));
        return out;
    }
};
// Raw pointers to registered classes (non-owning)
template <class T> struct Conv<T*, std::enable_if_t<IsClassType<std::remove_cv_t<T>>>> {
    using B = std::remove_cv_t<T>;
    static const TypeDesc* type(const Registry& r) { return classTypeOf<B>(r); }
    static Value to(const Registry& r, T* p) { return p ? r.wrapPtr(const_cast<B*>(p)) : Value(); }
    static T* from(const Registry& r, const Value& v) {
        if (v.isNull()) return nullptr;
        auto* h = v.objectPtr();
        if (!h) throw ScriptError("expected an object reference");
        void* p = castPtr(*h, classTypeOf<B>(r));
        if (!p) throw ScriptError("object of type '" + std::string(h->type ? h->type->name : "?") + "' is not a '" + classTypeOf<B>(r)->name + "'");
        return static_cast<T*>(static_cast<B*>(p));
    }
};
template <class T> struct Conv<std::shared_ptr<T>, std::enable_if_t<IsClassType<std::remove_cv_t<T>>>> {
    using B = std::remove_cv_t<T>;
    static const TypeDesc* type(const Registry& r) { return classTypeOf<B>(r); }
    static Value to(const Registry& r, const std::shared_ptr<T>& p) { return r.wrap(std::const_pointer_cast<B>(p)); }
    static std::shared_ptr<T> from(const Registry& r, const Value& v) {
        if (v.isNull()) return nullptr;
        auto* h = v.objectPtr();
        if (!h) throw ScriptError("expected an object reference");
        void* p = castPtr(*h, classTypeOf<B>(r));
        if (!p) throw ScriptError("object of type '" + std::string(h->type ? h->type->name : "?") + "' is not a '" + classTypeOf<B>(r)->name + "'");
        return std::shared_ptr<T>(h->ptr, static_cast<T*>(static_cast<B*>(p)));
    }
};
template <class T, class D> struct Conv<std::unique_ptr<T, D>, std::enable_if_t<IsClassType<T>>> {
    static const TypeDesc* type(const Registry& r) { return classTypeOf<T>(r); }
    static Value to(const Registry& r, std::unique_ptr<T, D> p) { return r.wrap(std::shared_ptr<T>(std::move(p))); }
};
// Registered classes by value (the script receives / produces a copy)
template <class T> struct Conv<T, std::enable_if_t<IsClassType<T> && !std::is_enum_v<T>>> {
    static const TypeDesc* type(const Registry& r) { return classTypeOf<T>(r); }
    static Value to(const Registry& r, T v) {
        if constexpr (std::is_move_constructible_v<T>) return r.wrap(std::make_shared<T>(std::move(v)));
        else throw ScriptError("class cannot be returned by value");
    }
    static T from(const Registry& r, const Value& v) {
        if constexpr (std::is_copy_constructible_v<T>) {
            T* p = Conv<T*>::from(r, v);
            if (!p) throw ScriptError("null object reference");
            return *p;
        } else throw ScriptError("class cannot be passed by value");
    }
};

// ---------------------------------------------------------------- arguments and results
// A `const std::string&` parameter reads the script's string in place; only a value that is not a string is converted.
struct StringRefArg {
    const std::string* ref = nullptr;
    std::string own;
    const std::string& get() const { return ref ? *ref : own; }
};

// P is the declared C++ parameter type.
template <class P> struct Arg {
    using D = std::decay_t<P>;
    static constexpr bool refToClass = std::is_lvalue_reference_v<P> && IsClassType<D>;
    static constexpr bool constStringRef = std::is_same_v<P, const std::string&>;
    using Store = std::conditional_t<refToClass, D*, std::conditional_t<constStringRef, StringRefArg, D>>;

    static const TypeDesc* type(const Registry& r) {
        if constexpr (refToClass) return Conv<D*>::type(r);
        else return Conv<D>::type(r);
    }
    static Store load(const Registry& r, const Value& v) {
        if constexpr (refToClass) return Conv<D*>::from(r, v);
        else if constexpr (constStringRef) {
            StringRefArg a;
            if (v.isString()) a.ref = &v.str();
            else a.own = r.toString(v);
            return a;
        } else return Conv<D>::from(r, v);
    }
    static decltype(auto) pass(Store& s) {
        if constexpr (constStringRef) {
            return static_cast<const std::string&>(s.get());
        } else if constexpr (refToClass) {
            if (!s) throw ScriptError("null object reference");
            return static_cast<P>(*s);
        } else if constexpr (std::is_lvalue_reference_v<P>) {
            return static_cast<P>(s);
        } else {
            return static_cast<P>(std::move(s));
        }
    }
};

template <class R> struct Ret {
    using B = Bare<R>;
    static constexpr bool refToClass = std::is_reference_v<R> && IsClassType<B>;
    static const TypeDesc* type(const Registry& r) {
        if constexpr (std::is_void_v<R>) return r.tVoid();
        else if constexpr (refToClass) return Conv<B*>::type(r);
        else return Conv<B>::type(r);
    }
    template <class V> static Value to(const Registry& r, V&& v) {
        if constexpr (refToClass) return Conv<B*>::to(r, &v);
        else return Conv<B>::to(r, std::forward<V>(v));
    }
};

template <class G, class R, class Tuple, size_t... I>
Value invokeTuple(const Registry& reg, G& fn, Value* args, std::index_sequence<I...>) {
    std::tuple<typename Arg<std::tuple_element_t<I, Tuple>>::Store...> store{
        Arg<std::tuple_element_t<I, Tuple>>::load(reg, args[I])...};
    (void)store;
    (void)args;
    if constexpr (std::is_void_v<R>) {
        fn(Arg<std::tuple_element_t<I, Tuple>>::pass(std::get<I>(store))...);
        return Value();
    } else {
        return Ret<R>::to(reg, fn(Arg<std::tuple_element_t<I, Tuple>>::pass(std::get<I>(store))...));
    }
}

// Calls `fn` and returns what it returned (a future of an asynchronous function).
template <class G, class R, class Tuple, size_t... I>
R callTuple(const Registry& reg, G& fn, Value* args, std::index_sequence<I...>) {
    std::tuple<typename Arg<std::tuple_element_t<I, Tuple>>::Store...> store{
        Arg<std::tuple_element_t<I, Tuple>>::load(reg, args[I])...};
    (void)store;
    (void)args;
    return fn(Arg<std::tuple_element_t<I, Tuple>>::pass(std::get<I>(store))...);
}

// The value of a ready future (get() rethrows what the asynchronous work threw).
template <class Fut> Value futureResult(const Registry& reg, Fut& f) {
    using T = typename IsFuture<Fut>::Result;
    if constexpr (std::is_void_v<T>) {
        f.get();
        return Value();
    } else {
        return Ret<T>::to(reg, f.get());
    }
}

template <class Tuple, size_t... I>
void describeParams(const Registry& r, std::vector<ParamDesc>& out, const std::vector<std::string>& names, size_t first,
                    std::index_sequence<I...>) {
    size_t n = 0;
    auto add = [&](const TypeDesc* t) {
        ParamDesc p;
        p.type = t;
        size_t idx = n++;
        p.name = idx < names.size() ? names[idx] : "Arg" + std::to_string(idx + 1);
        out.push_back(std::move(p));
    };
    (void)first;
    (void)add;
    (add(Arg<std::tuple_element_t<I + 0, Tuple>>::type(r)), ...);
}

// Splits "Category.Name" into category and title.
inline void splitId(const std::string& id, std::string& category, std::string& title) {
    auto dot = id.rfind('.');
    if (dot == std::string::npos) { category = "Functions"; title = id; }
    else { category = id.substr(0, dot); title = id.substr(dot + 1); }
}

// Fills `fd` from a callable. When `methodOf` is set the first parameter is the target object.
template <class F>
void bindCallable(Registry& reg, FunctionDesc& fd, F fn, const std::vector<std::string>& names, bool isMethod) {
    using Tr = FnTraits<F>;
    using Args = typename Tr::Args;
    using R = typename Tr::Ret;
    constexpr size_t N = std::tuple_size_v<Args>;
    if constexpr (IsFuture<Bare<R>>::value) fd.ret = Ret<typename IsFuture<Bare<R>>::Result>::type(reg);
    else fd.ret = Ret<R>::type(reg);
    // parameters (skip the target object of methods)
    std::vector<ParamDesc> all;
    describeParams<Args>(reg, all, {}, 0, std::make_index_sequence<N>{});
    size_t skip = isMethod ? 1 : 0;
    for (size_t i = skip; i < all.size(); ++i) {
        ParamDesc p = std::move(all[i]);
        size_t ni = i - skip;
        p.name = ni < names.size() ? names[ni] : "Arg" + std::to_string(ni + 1);
        fd.params.push_back(std::move(p));
    }
    const Registry* rp = &reg;
    if constexpr (IsFuture<Bare<R>>::value) {
        // Asynchronous: the node starts the call and the script waits for the future without blocking the thread. Whatever the
        // asynchronous work needs must be copied: the arguments only live until the function has returned.
        using Fut = Bare<R>;
        fd.latent = true;
        auto shared = std::make_shared<F>(std::move(fn));
        fd.start = [rp, shared](Value* args) -> PendingCall {
            auto fut = std::make_shared<Fut>(callTuple<F, R, Args>(*rp, *shared, args, std::make_index_sequence<N>{}));
            if (!fut->valid()) throw ScriptError("the asynchronous function returned no future");
            PendingCall call;
            call.ready = [fut] { return fut->wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
            call.result = [rp, fut] { return futureResult(*rp, *fut); };
            return call;
        };
        fd.invoke = [rp, shared](Value* args) -> Value {        // called directly: waits for the result
            Fut fut = callTuple<F, R, Args>(*rp, *shared, args, std::make_index_sequence<N>{});
            if (!fut.valid()) throw ScriptError("the asynchronous function returned no future");
            return futureResult(*rp, fut);
        };
    } else {
        fd.invoke = [rp, fn = std::move(fn)](Value* args) mutable -> Value {
            return invokeTuple<F, R, Args>(*rp, fn, args, std::make_index_sequence<N>{});
        };
    }
}

} // namespace detail

// ================================================================== Registry templates
template <class F>
FunctionDesc& Registry::addFunction(const std::string& id, F fn, std::vector<std::string> paramNames) {
    FunctionDesc& fd = createFunction(id);
    detail::splitId(id, fd.category, fd.title);
    detail::bindCallable(*this, fd, std::move(fn), paramNames, false);
    markDirty();
    return fd;
}

template <class T> Value Registry::wrap(std::shared_ptr<T> sp) const {
    if (!sp) return Value();
    using B = std::remove_cv_t<T>;
    const TypeDesc* td = nullptr;
    void* most = const_cast<B*>(sp.get());
    if constexpr (std::is_polymorphic_v<B>) {
        td = wrapType(typeid(B), typeid(*sp));
        if (td && td->cpp != std::type_index(typeid(B))) most = dynamic_cast<void*>(const_cast<B*>(sp.get()));
    } else {
        td = detail::registeredTypeOf<B>(*this);
    }
    if (!td) throw ScriptError(std::string("C++ type '") + typeid(B).name() + "' is not registered with the script registry");
    auto h = std::make_shared<ObjectHandle>();
    h->type = td;
    h->ptr = std::shared_ptr<void>(std::static_pointer_cast<const void>(sp), most);
    return Value(std::move(h));
}

template <class T> Value Registry::wrapPtr(T* p) const {
    if (!p) return Value();
    using B = std::remove_cv_t<T>;
    const TypeDesc* td = nullptr;
    void* most = const_cast<B*>(p);
    if constexpr (std::is_polymorphic_v<B>) {
        td = wrapType(typeid(B), typeid(*p));
        if (td && td->cpp != std::type_index(typeid(B))) most = dynamic_cast<void*>(const_cast<B*>(p));
    } else {
        td = detail::registeredTypeOf<B>(*this);
    }
    if (!td) throw ScriptError(std::string("C++ type '") + typeid(B).name() + "' is not registered with the script registry");
    auto h = std::make_shared<ObjectHandle>();
    h->type = td;
    h->ptr = std::shared_ptr<void>(std::shared_ptr<void>(), most);   // non-owning alias
    return Value(std::move(h));
}

template <class T> Value Registry::wrapRef(T& ref) const { return wrapPtr(&ref); }

template <class T> std::shared_ptr<T> Registry::unwrap(const Value& v) const {
    auto* h = v.objectPtr();
    if (!h) return nullptr;
    const TypeDesc* target = detail::registeredTypeOf<T>(*this);
    if (!target) return nullptr;
    void* p = castPtr(*h, target);
    if (!p) return nullptr;
    return std::shared_ptr<T>(h->ptr, static_cast<T*>(static_cast<std::remove_cv_t<T>*>(p)));
}

// ================================================================== builders
template <class T>
class ClassBuilder {
public:
    ClassBuilder(Registry& r, TypeDesc* t) : reg_(&r), type_(t) {}

    ClassBuilder& doc(std::string d) { type_->doc = std::move(d); return *this; }
    ClassBuilder& color(uint32_t rgb) { type_->color = rgb; type_->hasColor = true; return *this; }
    ClassBuilder& color(int r, int g, int b) { return color((uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b)); }
    ClassBuilder& abstract(bool a = true) { type_->abstract = a; return *this; }
    ClassBuilder& toString(std::function<std::string(const T&)> fn) {
        type_->toStringFn = [fn = std::move(fn)](const void* p) { return fn(*static_cast<const T*>(p)); };
        return *this;
    }

    // Declares a registered base class (call once per base for multiple inheritance).
    template <class B> ClassBuilder& base() {
        static_assert(std::is_base_of_v<B, T>, "B must be a base class of T");
        const TypeDesc* bt = detail::classTypeOf<B>(*reg_);
        auto up = [](void* p) -> void* { return static_cast<B*>(static_cast<T*>(p)); };
        if (!type_->base) { type_->base = bt; type_->toBase = up; }
        else type_->extraBases.push_back({bt, up});
        reg_->markDirty();
        return *this;
    }

    template <class... Args> ClassBuilder& constructor(std::vector<std::string> names = {}) {
        if constexpr (std::is_constructible_v<T, Args...> && !std::is_abstract_v<T>) {
            CtorDesc cd;
            std::vector<ParamDesc> params;
            using Tuple = std::tuple<Args...>;
            detail::describeParams<Tuple>(*reg_, params, names, 0, std::index_sequence_for<Args...>{});
            for (size_t i = 0; i < params.size(); ++i) params[i].name = i < names.size() ? names[i] : "Arg" + std::to_string(i + 1);
            cd.params = std::move(params);
            const Registry* rp = reg_;
            cd.create = [rp](Value* args) -> Value {
                auto make = [&](auto&&... a) { return rp->wrap(std::make_shared<T>(std::forward<decltype(a)>(a)...)); };
                return invokeCtor<Args...>(*rp, make, args, std::index_sequence_for<Args...>{});
            };
            type_->ctors.push_back(std::move(cd));
            reg_->markDirty();
        }
        return *this;
    }

    // Data member: read / write property.
    template <class M, class C> ClassBuilder& property(const std::string& name, M C::*pm) {
        static_assert(std::is_base_of_v<C, T>, "member must belong to T or one of its bases");
        PropertyDesc pd = makeProp(name, detail::Conv<std::conditional_t<detail::IsClassType<M>, M*, M>>::type(*reg_));
        const Registry* rp = reg_;
        const TypeDesc* owner = type_;
        pd.get = [rp, owner, pm](const ObjectHandle& h) -> Value {
            T* self = static_cast<T*>(castPtr(h, owner));
            if (!self) throw ScriptError("invalid object");
            if constexpr (detail::IsClassType<M>) return detail::Conv<M*>::to(*rp, &(self->*pm));
            else return detail::Conv<M>::to(*rp, self->*pm);
        };
        if constexpr (std::is_copy_assignable_v<M> && !std::is_const_v<M>) {
            pd.set = [rp, owner, pm](const ObjectHandle& h, const Value& v) {
                T* self = static_cast<T*>(castPtr(h, owner));
                if (!self) throw ScriptError("invalid object");
                if constexpr (detail::IsClassType<M>) {
                    M* src = detail::Conv<M*>::from(*rp, v);
                    if (!src) throw ScriptError("null object reference");
                    self->*pm = *src;
                } else self->*pm = detail::Conv<M>::from(*rp, v);
            };
        }
        addProp(std::move(pd));
        return *this;
    }
    // Read only data member or getter (member function pointer or lambda taking const T&).
    template <class G> ClassBuilder& readonly(const std::string& name, G getter) {
        if constexpr (std::is_member_object_pointer_v<G>) {
            using M = std::remove_reference_t<decltype(std::declval<const T&>().*getter)>;
            PropertyDesc pd = makeProp(name, detail::Conv<std::conditional_t<detail::IsClassType<M>, M*, M>>::type(*reg_));
            const Registry* rp = reg_;
            const TypeDesc* owner = type_;
            pd.get = [rp, owner, getter](const ObjectHandle& h) -> Value {
                T* self = static_cast<T*>(castPtr(h, owner));
                if (!self) throw ScriptError("invalid object");
                if constexpr (detail::IsClassType<M>) return detail::Conv<const M*>::to(*rp, &(self->*getter));
                else return detail::Conv<std::remove_cv_t<M>>::to(*rp, self->*getter);
            };
            addProp(std::move(pd));
            return *this;
        } else {
            return accessorImpl(name, makeCallable(std::move(getter)), nullptr);
        }
    }
    // Getter / setter pair (member function pointers or lambdas taking T&).
    template <class G, class S> ClassBuilder& accessor(const std::string& name, G getter, S setter) {
        return accessorImpl(name, makeCallable(std::move(getter)), makeCallable(std::move(setter)));
    }

    // Method: member function pointer, or a lambda taking the object as first parameter (T&, T*, const T&).
    template <class F> ClassBuilder& method(const std::string& name, F fn, std::vector<std::string> paramNames = {}) {
        auto callable = makeCallable(std::move(fn));
        using Tr = detail::FnTraits<decltype(callable)>;
        FunctionDesc& fd = reg_->createFunction(std::string(type_->name) + "." + name);
        fd.title = name;
        fd.category = type_->category.empty() ? type_->name : type_->category + "|" + type_->name;
        fd.owner = type_;
        fd.isConst = isConstCallable<F>();
        detail::bindCallable(*reg_, fd, std::move(callable), paramNames, true);
        // const member returning a value => pure node (no exec pins); an asynchronous one waits, which only an exec node can do
        if (fd.isConst && fd.ret != reg_->tVoid() && !fd.latent) fd.pure = true;
        last_ = &fd;
        (void)sizeof(Tr);
        reg_->markDirty();
        return *this;
    }
    // Modifiers for the member that was added last.
    ClassBuilder& pure(bool p = true) { if (last_) last_->pure = p; reg_->markDirty(); return *this; }
    ClassBuilder& title(std::string t) { if (last_) last_->title = std::move(t); return *this; }
    ClassBuilder& describe(std::string d) { if (last_) last_->doc = std::move(d); return *this; }
    ClassBuilder& keywords(std::string k) { if (last_) last_->keywords = std::move(k); return *this; }

    TypeDesc* type() const { return type_; }

private:
    template <class... Args, class Make, size_t... I>
    static Value invokeCtor(const Registry& reg, Make& make, Value* args, std::index_sequence<I...>) {
        std::tuple<typename detail::Arg<Args>::Store...> store{detail::Arg<Args>::load(reg, args[I])...};
        (void)args;
        return make(detail::Arg<Args>::pass(std::get<I>(store))...);
    }
    template <class F> static constexpr bool isConstCallable() {
        if constexpr (std::is_member_function_pointer_v<F>) return detail::FnTraits<F>::isConst;
        else return false;
    }
    template <class F> auto makeCallable(F fn) {
        if constexpr (std::is_member_function_pointer_v<F>) return detail::MemberCaller<T, F>{fn};
        else return fn;
    }
    PropertyDesc makeProp(const std::string& name, const TypeDesc* type) {
        PropertyDesc pd;
        pd.name = name;
        pd.owner = type_;
        pd.type = type;
        return pd;
    }
    void addProp(PropertyDesc pd) {
        type_->properties.push_back(std::move(pd));
        reg_->markDirty();
    }
    template <class G, class S> ClassBuilder& accessorImpl(const std::string& name, G getter, S setter) {
        using GT = detail::FnTraits<G>;
        using R = typename GT::Ret;
        PropertyDesc pd = makeProp(name, detail::Ret<R>::type(*reg_));
        const Registry* rp = reg_;
        const TypeDesc* owner = type_;
        pd.get = [rp, owner, getter](const ObjectHandle& h) mutable -> Value {
            T* self = static_cast<T*>(castPtr(h, owner));
            if (!self) throw ScriptError("invalid object");
            return detail::Ret<R>::to(*rp, getter(*self));
        };
        if constexpr (!std::is_same_v<S, std::nullptr_t>) {
            using ST = detail::FnTraits<S>;
            using SArgs = typename ST::Args;
            static_assert(std::tuple_size_v<SArgs> == 2, "setter must take (T&, value)");
            using P = std::tuple_element_t<1, SArgs>;
            pd.set = [rp, owner, setter](const ObjectHandle& h, const Value& v) mutable {
                T* self = static_cast<T*>(castPtr(h, owner));
                if (!self) throw ScriptError("invalid object");
                auto st = detail::Arg<P>::load(*rp, v);
                setter(*self, detail::Arg<P>::pass(st));
            };
        }
        addProp(std::move(pd));
        return *this;
    }

    Registry* reg_;
    TypeDesc* type_;
    FunctionDesc* last_ = nullptr;
};

template <class E>
class EnumBuilder {
public:
    EnumBuilder(Registry& r, TypeDesc* t) : reg_(&r), type_(t) {}
    EnumBuilder& value(const std::string& name, E v, std::string doc = "") {
        type_->enumValues.push_back({name, static_cast<int64_t>(v), std::move(doc)});
        reg_->markDirty();
        return *this;
    }
    EnumBuilder& doc(std::string d) { type_->doc = std::move(d); return *this; }
    EnumBuilder& color(uint32_t rgb) { type_->color = rgb; type_->hasColor = true; return *this; }
    EnumBuilder& color(int r, int g, int b) { return color((uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b)); }

private:
    Registry* reg_;
    TypeDesc* type_;
};

template <class T> ClassBuilder<T> Registry::registerClass(const std::string& name, const std::string& category) {
    static_assert(std::is_class_v<T>, "registerClass needs a class type");
    TypeDesc* t = createClassType(name, category, typeid(T));
    t->abstract = std::is_abstract_v<T>;
    t->polymorphic = std::is_polymorphic_v<T>;
    markDirty();
    return ClassBuilder<T>(*this, t);
}

template <class E> EnumBuilder<E> Registry::registerEnum(const std::string& name, const std::string& category) {
    static_assert(std::is_enum_v<E>, "registerEnum needs an enum type");
    TypeDesc* t = createEnumType(name, category, typeid(E));
    markDirty();
    return EnumBuilder<E>(*this, t);
}

} // namespace fg
