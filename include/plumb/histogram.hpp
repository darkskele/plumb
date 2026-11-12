#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace plumb {

/// @brief Counts of whole values. Values below `per_group` get a bucket each. Above, every
/// doubling gets `per_group` buckets, so a value reads back within one part in `per_group`.
/// Values from 2^`range_bits` on share the last bucket.
class Histogram {
   public:
    /// @brief A bucket's index and how many values it holds.
    using Bucket = std::pair<std::size_t, std::uint64_t>;

    static constexpr int          precision_bits = 7;
    static constexpr int          range_bits     = 50;
    static constexpr std::int64_t per_group      = std::int64_t{1} << precision_bits;
    // Group 0 holds the values below `per_group`.
    static constexpr std::size_t buckets = (range_bits - precision_bits + 1) * per_group;

    Histogram() = default;

    /// @brief A histogram from its filled buckets, its negative count, and its smallest and
    /// largest values. `min` and `max` are ignored when no bucket is filled.
    Histogram(std::span<const Bucket> filled, std::uint64_t negative, std::int64_t min,
              std::int64_t max) noexcept
        : negative_(negative) {
        for (auto [i, n] : filled) {
            contract_assert(i < buckets);
            counts_[i] += n;
            count_ += n;
        }
        if (count_ > 0) min_ = min, max_ = max;
    }

    /// @brief Counts `v`. A negative value is counted apart and kept out of the buckets.
    void record(std::int64_t v) noexcept {
        if (v < 0) {
            ++negative_;
            return;
        }
        ++counts_[index(v)];
        ++count_;
        min_ = std::min(min_, v);
        max_ = std::max(max_, v);
    }

    /// @brief Adds `other`'s counts to this one's.
    Histogram& operator+=(const Histogram& other) noexcept {
        for (std::size_t i = 0; i < buckets; ++i) counts_[i] += other.counts_[i];
        count_ += other.count_;
        negative_ += other.negative_;
        min_ = std::min(min_, other.min_);
        max_ = std::max(max_, other.max_);
        return *this;
    }

    /// @brief The bucket holding `v`.
    static constexpr std::size_t index(std::int64_t v) noexcept pre(v >= 0) {
        if (v < per_group) return static_cast<std::size_t>(v);
        // The doubling from 2^high_bit, cut into `per_group` buckets of 2^bucket_bits each.
        int high_bit = std::bit_width(static_cast<std::uint64_t>(v)) - 1;
        if (high_bit >= range_bits) return buckets - 1;
        int          bucket_bits = high_bit - precision_bits;
        std::int64_t start       = std::int64_t{1} << high_bit;
        std::int64_t group       = std::int64_t{bucket_bits + 1} << precision_bits;
        return static_cast<std::size_t>(group + ((v - start) >> bucket_bits));
    }

    /// @brief The smallest value in bucket `i`.
    static constexpr std::int64_t lower(std::size_t i) noexcept pre(i < buckets) {
        if (i < per_group) return static_cast<std::int64_t>(i);
        int          bucket_bits = static_cast<int>(i >> precision_bits) - 1;
        std::int64_t start       = std::int64_t{1} << (bucket_bits + precision_bits);
        return start + (static_cast<std::int64_t>(i % per_group) << bucket_bits);
    }

    /// @brief The largest value in bucket `i`.
    static constexpr std::int64_t upper(std::size_t i) noexcept pre(i < buckets) {
        if (i < per_group) return static_cast<std::int64_t>(i);
        int bucket_bits = static_cast<int>(i >> precision_bits) - 1;
        return lower(i) + (std::int64_t{1} << bucket_bits) - 1;
    }

    /// @brief The value at or below which a share `p` of the counts fall, as the top of its
    /// bucket, never past the largest value counted.
    [[nodiscard]] std::int64_t percentile(double p) const noexcept pre(count_ > 0)
        pre(p >= 0 && p <= 1) {
        if (p == 0) return min_;
        auto rank = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::ceil(p * static_cast<double>(count_))));
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < buckets; ++i) {
            seen += counts_[i];
            if (seen >= rank) return std::min(upper(i), max_);
        }
        return max_;
    }

    /// @brief Calls `f(lower, upper, count)` for every bucket that holds a value, lowest first.
    template <class F>
    void each(F&& f) const {
        for (std::size_t i = 0; i < buckets; ++i)
            if (counts_[i] != 0) f(lower(i), upper(i), counts_[i]);
    }

    /// @brief The buckets that hold a value, lowest first.
    [[nodiscard]] std::vector<Bucket> filled() const {
        std::vector<Bucket> out;
        for (std::size_t i = 0; i < buckets; ++i)
            if (counts_[i] != 0) out.emplace_back(i, counts_[i]);
        return out;
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

    [[nodiscard]] std::uint64_t negative() const noexcept { return negative_; }

    [[nodiscard]] std::int64_t min() const noexcept pre(count_ > 0) { return min_; }

    [[nodiscard]] std::int64_t max() const noexcept pre(count_ > 0) { return max_; }

    bool operator==(const Histogram&) const = default;

   private:
    std::vector<std::uint64_t> counts_   = std::vector<std::uint64_t>(buckets);
    std::uint64_t              count_    = 0;
    std::uint64_t              negative_ = 0;
    std::int64_t               min_      = std::numeric_limits<std::int64_t>::max();
    std::int64_t               max_      = 0;
};

}  // namespace plumb
