#pragma once

#include "tx/chunk.hxx"
#include "tx/common.hxx"
#include "tx/type_traits.hxx"
#include "tx/utils.hxx"

#include <gsl/gsl>

#include <gsl/util>
#include <string_view>
#include <type_traits>
#include <cassert>
#include <optional>

namespace tx {

class VM;

enum class ObjType : u8 {
    FUNCTION,
    NATIVE,
    STRING,
};

struct Obj {
    ObjType type;
    // bool is_marked = false;
    gsl::owner<Obj*> next_object = nullptr;

    Obj() = delete;
    constexpr explicit Obj(ObjType typ) noexcept : type(typ) {}

    constexpr Obj(const Obj& other) noexcept = delete;
    constexpr Obj(Obj&& other) noexcept = delete;

    constexpr ~Obj() noexcept {
        // NOLINTNEXTLINE(*-decay)
        assert(next_object == nullptr);
    }

    constexpr Obj* operator=(const Obj& other) noexcept = delete;
    constexpr Obj* operator=(Obj&& other) noexcept = delete;

    template <typename T>
        requires std::is_base_of_v<Obj, T>
    [[nodiscard]] constexpr T& as() {
        return *static_cast<T*>(this);
    }

    template <typename T>
        requires std::is_base_of_v<Obj, T>
    [[nodiscard]] constexpr const T& as() const {
        return *static_cast<const T*>(this);
    }

    [[nodiscard]] constexpr bool is_string() const noexcept {
        return type == ObjType::STRING;
    }

    // [[nodiscard]] constexpr
    // bool is_closure() const noexcept {
    //     return type == ObjType::CLOSURE;
    // }

    [[nodiscard]] constexpr bool is_function() const noexcept {
        return type == ObjType::FUNCTION;
    }

    [[nodiscard]] constexpr bool is_native() const noexcept {
        return type == ObjType::NATIVE;
    }

    // friend constexpr std::partial_ordering
    // operator<=>(const Obj& lhs, const Obj& rhs) noexcept;
    //
    // friend constexpr bool operator==(const Obj& lhs, const Obj& rhs)
    // noexcept;
};

template <typename T, typename... Args>
T* allocate_object(VM& tvm, Args&&... args) noexcept;

struct ObjString : Obj {
    static constexpr bool IS_TRIVIALLY_RELOCATABLE = true;

    size_t length = 0;
    u32 hash = 0;
    bool owns_chars = false;
    gsl::owner<const char*> data_ptr = nullptr;

// clang-format off
    #ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc99-extensions"
    #endif
    // NOLINTNEXTLINE(*-c-arrays)
    __extension__ char data[];
    #ifdef __clang__
    #pragma clang diagnostic pop
    #endif
    // clang-format on

    constexpr explicit ObjString() noexcept = delete;

    constexpr explicit ObjString(
        bool copy,
        std::string_view strv,
        u32 hsh
    ) noexcept
            : Obj(ObjType::STRING)
            , length(gsl::narrow_cast<size_t>(strv.length()))
            , hash(hsh)
            , owns_chars(copy) {
        if (strv.empty()) { return; }
        if (owns_chars) {
            std::memcpy(&data[0], strv.data(), strv.length());
            data_ptr = &data[0];
        } else {
            data_ptr = strv.data();
        }
    }

    ObjString(const ObjString& other) = delete;
    ObjString(ObjString&& other) = delete;

    constexpr ~ObjString() noexcept = default;

    constexpr ObjString* operator=(const ObjString& rhs) noexcept = delete;
    constexpr ObjString* operator=(ObjString&& rhs) noexcept = delete;

    // implicit
    constexpr operator std::string_view() const noexcept {
        return std::string_view{data_ptr, static_cast<std::size_t>(length)};
    }

    friend constexpr std::partial_ordering
    operator<=>(const ObjString& lhs, const ObjString& rhs) noexcept;

    friend constexpr bool
    operator==(const ObjString& lhs, const ObjString& rhs) noexcept;
};

[[nodiscard]] ObjString*
make_string(VM& tvm, bool copy, std::string_view strv) noexcept;

struct ObjFunction : Obj {
    i32 arity{0};
    i32 max_slots{0};
    Chunk chunk;
    ObjString* name{nullptr};

    constexpr explicit ObjFunction(i32 reserved_slots) noexcept
            : Obj{ObjType::FUNCTION}
            , max_slots(reserved_slots) {}

    constexpr void destroy(VM& tvm) noexcept { chunk.destroy(tvm); }

    // [[nodiscard]] constexpr std::string_view get_debug_name() const noexcept
    // {
    //     if (name == nullptr) { return "<script>"; }
    //     auto result = std::string_view(*name);
    //     if (result.empty()) { return "<fn>"; }
    //     return fmt::format_to(ctx.out(), "<fn {:s}>", result);
    // }

    [[nodiscard]] constexpr std::string_view get_display_name() const noexcept {
        if (name == nullptr) { return "<script>"; }
        auto result = std::string_view(*name);
        if (result.empty()) { return "<anonymous>"; }
        return result;
    }
};

enum struct NativeResult : bool {
    RUNTIME_ERROR,
    SUCCESS,
};

struct NativeInOut {
    std::span<Value> range;

    constexpr NativeInOut(Value* start, size_t length) noexcept
            : range(start, static_cast<std::size_t>(length)) {}

    [[nodiscard]] constexpr const Value& return_value() const noexcept {
        return range[0];
    }

    [[nodiscard]] constexpr Value& return_value() noexcept { return range[0]; }

    [[nodiscard]] constexpr std::span<const Value> args() const noexcept {
        return range.subspan(1, range.size() - 1);
    }

    [[nodiscard]] constexpr std::span<Value> args() noexcept {
        return range.subspan(1, range.size() - 1);
    }
};

using NativeFn = NativeResult (*)(VM& tvm, NativeInOut inout);

struct ObjNative : Obj {
    NativeFn function{nullptr};
    // TODO: arity

    constexpr explicit ObjNative(NativeFn fun) noexcept
            : Obj{ObjType::NATIVE}
            , function(fun) {}
};

}  // namespace tx
