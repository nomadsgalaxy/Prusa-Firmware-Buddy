#pragma once

template <typename... T>
struct TypeList {
    static constexpr size_t size = sizeof...(T);
};

/// Subclass of \p Parent - the only thing it does is passing \p args to the parent's constructor
/// If runtime arguments are provided, they are passed first, before the templated ones
/// Useful in GUI
template <typename Parent, auto... args>
class WithConstructorArgs final : public Parent {
    static_assert(sizeof...(args) > 0, "It is pointless to use WithConstructorArgs without any args");

public:
    template <typename... Args2>
    WithConstructorArgs(Args2 &&...args2)
        : Parent(std::forward<Args2>(args2)..., args...) {}
};
