#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

namespace plumb {

/// @brief Count, min, max, mean and sample variance of a series, added one value at a time.
class Summary {
   public:
    void add(double x) noexcept {
        ++count_;
        min_       = std::min(min_, x);
        max_       = std::max(max_, x);
        double was = mean_;
        // Running mean and squared deviations, accurate for values far from zero.
        mean_ += (x - was) / static_cast<double>(count_);
        squares_ += (x - was) * (x - mean_);
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }

    [[nodiscard]] double min() const noexcept pre(count_ > 0) { return min_; }

    [[nodiscard]] double max() const noexcept pre(count_ > 0) { return max_; }

    [[nodiscard]] double mean() const noexcept pre(count_ > 0) { return mean_; }

    [[nodiscard]] double variance() const noexcept pre(count_ > 1) {
        return squares_ / static_cast<double>(count_ - 1);
    }

   private:
    std::size_t count_   = 0;
    double      min_     = std::numeric_limits<double>::infinity();
    double      max_     = -std::numeric_limits<double>::infinity();
    double      mean_    = 0.0;
    double      squares_ = 0.0;
};

}  // namespace plumb
