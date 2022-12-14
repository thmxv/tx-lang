#pragma once

#include <bits/stl_uninitialized.h>
#include <type_traits>

namespace tx {

template <typename T>
struct is_trivially_relocatable : std::__is_bitwise_relocatable<T> {};

template <typename T>
    requires T::IS_TRIVIALLY_RELOCATABLE
struct is_trivially_relocatable<T>
        : std::bool_constant<T::IS_TRIVIALLY_RELOCATABLE> {};

template <typename T>
inline constexpr bool is_trivially_relocatable_v =
    is_trivially_relocatable<T>::value;

}  // namespace tx
