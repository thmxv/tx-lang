#pragma once

#include "tx/utils.hxx"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <type_traits>

namespace tx {

template <typename T, typename SizeT, SizeT C>
    requires std::is_integral_v<SizeT>
class FixedCapacityArray {
    static_assert(C > 0, "Capacity must be positive.");

    using MutDataType = std::array<std::byte, sizeof(T)>;
    using DataType = MutDataType;
    // std::conditional_t<std::is_const_v<T>, const MutDataType, MutDataType>;

    // using StorageType = std::array<DataType, static_cast<std::size_t>(C)>;
    // NOLINTNEXTLINE(*-avoid-c-arrays)
    using StorageType = DataType[static_cast<std::size_t>(C)];

    SizeT count = 0;
    alignas(T) StorageType data_buff{};

  public:
    using value_type = T;
    using size_type = SizeT;
    using iterator = T*;
    using const_iterator = const T*;
    using difference_type = std::ptrdiff_t;

    constexpr FixedCapacityArray() noexcept = default;

    constexpr FixedCapacityArray(SizeT len, T val) noexcept {
        resize(len, val);
    }

    constexpr FixedCapacityArray(const FixedCapacityArray& other) noexcept
            : count(other.count) {
        std::uninitialized_copy_n(other.cbegin(), count, begin());
    }

    constexpr FixedCapacityArray(FixedCapacityArray&& other) noexcept
            : count(other.count) {
        std::uninitialized_move_n(other.begin(), count, begin());
    }

    constexpr ~FixedCapacityArray() noexcept
        requires(std::is_trivially_destructible_v<T>)
    = default;

    constexpr ~FixedCapacityArray() noexcept(std::is_nothrow_destructible_v<T>)
        requires(!std::is_trivially_destructible_v<T>)
    {
        clear();
    }

    [[nodiscard]] constexpr FixedCapacityArray& operator=(
        const FixedCapacityArray& rhs
    ) noexcept {
        if (this != &rhs) {
            clear();
            count = rhs.count;
            std::uninitialized_copy_n(rhs.cbegin(), count, begin());
        }
        return *this;
    }

    [[nodiscard]] constexpr FixedCapacityArray& operator=(
        FixedCapacityArray&& rhs
    ) noexcept {
        return operator=(rhs);  // NOLINT
    }

    [[nodiscard]] constexpr SizeT size() const noexcept { return count; }

    [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }

    constexpr void resize(SizeT new_size) noexcept { resize(new_size, T()); }

    constexpr void resize(
        SizeT len,
        const T& val
    ) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        if (len > count) {
            std::uninitialized_fill_n(begin(), count, val);
        } else if (len < count) {
            auto diff = count - len;
            std::destroy_n(std::prev(begin(), diff), diff);
        }
        count = len;
    }

    constexpr void clear() noexcept(std::is_nothrow_destructible_v<T>) {
        std::destroy_n(data_buff, count);
        count = 0;
    }

    constexpr T* erase(
        const T* first,
        const T* last
    ) noexcept(std::is_nothrow_destructible_v<T>&&
                   std::is_nothrow_assignable_v<T, T>) {
        assert(first < last);
        assert((data_buff <= first) && (first <= cend()));
        assert((data_buff <= last) && (last <= cend()));
        T* write_first = begin()
                         + static_cast<SizeT>(std::distance(cbegin(), first));
        T* write_last = begin()
                        + static_cast<SizeT>(std::distance(cbegin(), last));
        const auto diff = std::distance(first, last);
        const auto [_, new_end] = std::ranges::move(
            write_last,
            end(),
            write_first
        );
        std::destroy(new_end, end());
        count -= static_cast<SizeT>(diff);
        return write_last;
    }

    constexpr void push_back(T value
    ) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        if (count == C) {
            report_and_abort(
                "Inserting into a fixed capacity array at full capacity"
            );
        }
        // NOTE: Do not launder
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::construct_at(reinterpret_cast<T*>(&data_buff[count++]), value);
    }

    constexpr void push_back_unsafe(T value
    ) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        assert(count < C);
        // NOTE: Do not launder
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::construct_at(reinterpret_cast<T*>(&data_buff[count++]), value);
    }

    template <typename... Args>
    constexpr T& emplace_back(Args&&... args) noexcept(
        noexcept(std::construct_at(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<T*>(&data_buff[count++]),
            std::forward<Args>(args)...
        ))
    ) {
        assert(count < C);
        // NOTE: Do not launder
        return *std::construct_at(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<T*>(&data_buff[count++]),
            std::forward<Args>(args)...
        );
    }

    constexpr void pop_back() noexcept(std::is_nothrow_destructible_v<T>) {
        std::destroy_at(&back());
        count--;
    }

    [[nodiscard]] constexpr T& operator[](SizeT idx) noexcept {
        assert(idx < count);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return *std::launder(reinterpret_cast<T*>(&data_buff[idx]));
    }

    [[nodiscard]] constexpr const T& operator[](SizeT idx) const noexcept {
        assert(idx < count);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return *std::launder(reinterpret_cast<const T*>(&data_buff[idx]));
    }

    [[nodiscard]] constexpr T& back() noexcept { return operator[](count - 1); }

    [[nodiscard]] constexpr const T& back() const noexcept {
        return operator[](count - 1);
    }

    [[nodiscard]] constexpr T* data() noexcept {
        return std::launder(reinterpret_cast<T*>(data_buff));
    }

    [[nodiscard]] constexpr const T* data() const noexcept {
        return std::launder(reinterpret_cast<const T*>(data_buff));
    }

    [[nodiscard]] constexpr T* begin() noexcept { return data(); }
    [[nodiscard]] constexpr const T* begin() const noexcept { return data(); }
    [[nodiscard]] constexpr const T* cbegin() const noexcept { return data(); }

    [[nodiscard]] constexpr T* end() noexcept {
        return std::next(data(), count);
    }

    [[nodiscard]] constexpr const T* end() const noexcept {
        return std::next(data(), count);
    }

    [[nodiscard]] constexpr const T* cend() const noexcept {
        return std::next(data(), count);
    }
};

}  // namespace tx
