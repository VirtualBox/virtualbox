#pragma once
// https://stackoverflow.com/a/76255307

/* (associative) type sum */

template <class... Args>
struct template_concat;

template <class... Args1, typename... Remaining>
struct template_concat<template_concat<Args1...>, Remaining...> {
    using type = typename template_concat<Args1..., Remaining...>::type;
};

template <template <class...> class T, class... Args1, class... Args2, typename... Remaining>
struct template_concat<T<Args1...>, T<Args2...>, Remaining...> {
    using type = typename template_concat<T<Args1..., Args2...>, Remaining...>::type;
};

template <template <class...> class T, class... Args1>
struct template_concat<T<Args1...>> {
    using type = T<Args1...>;
};

template <class... Args>
using template_concat_t = typename template_concat<Args...>::type;

/* overload */

template<class... Ts> struct patterns : Ts... { using Ts::operator()...; };
template<class... Ts> patterns(Ts...) -> patterns<Ts...>;