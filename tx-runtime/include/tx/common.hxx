#pragma once

// This file will be generated automatically when you run the CMake
// configuration step. It creates a namespace called `tx::cmake`.
// You can modify the source template at `configured_files/config.hpp.in`.
#include "internal_use_only/config.hxx"

#include <fmt/core.h>

#include <cstddef>
#include <cstdint>

// TODO remove and force clang 15
#ifdef __clang__
#include <experimental/source_location>
using source_location = std::experimental::source_location;
#else
#include <source_location>
using source_location = std::source_location;
#endif

#define TX_ENABLE_COMPUTED_GOTO

#ifdef TX_ENABLE_COMPUTED_GOTO
#define TX_VM_CONSTEXPR
#else
#define TX_VM_CONSTEXPR constexpr
#endif

namespace tx {

#ifdef NDEBUG
inline constexpr bool IS_DEBUG_BUILD = false;
#else
inline constexpr bool IS_DEBUG_BUILD = true;
#endif

inline constexpr std::string_view VERSION = cmake::project_version;
inline constexpr int VERSION_MAJOR = cmake::project_version_major;
inline constexpr int VERSION_MINOR = cmake::project_version_minor;
inline constexpr int VERSION_PATCH = cmake::project_version_patch;
inline constexpr int VERSION_TWEAK = cmake::project_version_tweak;
inline constexpr std::string_view GIT_SHA = cmake::git_sha;

inline constexpr bool HAS_DEBUG_FEATURES = cmake::has_debug_features;

inline constexpr size_t STACK_MAX = 256;

// Usefull short type aliases
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using isize = std::ptrdiff_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usize = std::size_t;

// NOTE: Configurable size type used in all the code
using size_t = i32;
// using size_t = u64;

template <typename Enum>
[[nodiscard]] constexpr std::underlying_type_t<Enum> to_underlying(
    Enum enumeration
) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(enumeration);
}

[[noreturn]] inline void unreachable(
    const source_location location = source_location::current()
) {
    if constexpr (IS_DEBUG_BUILD) {
        fmt::print(
            stderr,
            FMT_STRING("[{:s}:{:d}:{:d}] Error in {:s}():"
                       "This code should have been unreachable\n"),
            location.file_name(),
            location.line(),
            location.column(),
            location.function_name()
        );
        std::abort();
    }
    __builtin_unreachable();
}

}  // namespace tx