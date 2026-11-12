#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "plumb/batch.hpp"
#include "plumb/clock.hpp"

namespace plumb {

/// @brief The fixed start of a raw file. Stamp cost figures are in nanoseconds.
struct RawHeader {
    std::array<char, 8> magic;
    std::uint32_t       version;
    std::uint32_t       tsc_invariant;
    std::uint32_t       measured;
    std::uint32_t       reserved;
    std::uint64_t       n;
    std::uint64_t       count;
    double              ghz;
    std::uint64_t       stamp_count;
    double              stamp_min;
    double              stamp_max;
    double              stamp_mean;
    double              stamp_variance;

    bool operator==(const RawHeader&) const = default;
};

static_assert(std::is_trivially_copyable_v<RawHeader>);
static_assert(sizeof(RawHeader) == 88, "the raw header layout changed, bump raw_version");
static_assert(std::endian::native == std::endian::little,
              "raw files are written in native byte order, which must be little-endian");

inline constexpr std::array<char, 8> raw_magic   = {'p', 'l', 'u', 'm', 'b', 'r', 'a', 'w'};
inline constexpr std::uint32_t       raw_version = 2;

/// @brief One repetition's raw file: the header, then nanoseconds per batch.
struct Raw {
    RawHeader           header;
    std::vector<double> ns;

    bool operator==(const Raw&) const = default;
};

/// @brief A repetition's batches in nanoseconds, ticks at the clock's rate. Nothing subtracted.
inline Raw to_raw(const detail::Batches& b, const TscClock& clock, bool invariant) {
    double g = clock.ghz();
    Raw    raw{.header = {.magic          = raw_magic,
                          .version        = raw_version,
                          .tsc_invariant  = invariant ? 1u : 0u,
                          .measured       = b.measured ? 1u : 0u,
                          .reserved       = 0,
                          .n              = b.n,
                          .count          = b.figures.size(),
                          .ghz            = g,
                          .stamp_count    = b.stamp_cost.count(),
                          .stamp_min      = b.stamp_cost.min() / g,
                          .stamp_max      = b.stamp_cost.max() / g,
                          .stamp_mean     = b.stamp_cost.mean() / g,
                          .stamp_variance = b.stamp_cost.variance() / (g * g)},
               .ns     = {}};
    raw.ns.reserve(b.figures.size());
    for (double f : b.figures) raw.ns.push_back(b.measured ? f : f / g);
    return raw;
}

namespace detail {

struct FileCloser {
    void operator()(std::FILE* f) const noexcept { std::fclose(f); }
};

using File = std::unique_ptr<std::FILE, FileCloser>;

}  // namespace detail

/// @brief Writes `raw` to `path`, replacing any file there.
inline std::expected<void, std::string> write_raw(const std::filesystem::path& path,
                                                  const Raw&                   raw) {
    detail::File f{std::fopen(path.c_str(), "wb")};
    if (!f) return std::unexpected(std::format("cannot open {} to write", path.string()));
    bool ok = std::fwrite(&raw.header, sizeof raw.header, 1, f.get()) == 1 &&
              std::fwrite(raw.ns.data(), sizeof(double), raw.ns.size(), f.get()) == raw.ns.size();
    if (!ok || std::fclose(f.release()) != 0)
        return std::unexpected(std::format("cannot write {}", path.string()));
    return {};
}

/// @brief Reads the raw file at `path`, refusing one that is damaged or of another version.
inline std::expected<Raw, std::string> read_raw(const std::filesystem::path& path) {
    auto         where = path.string();
    detail::File f{std::fopen(path.c_str(), "rb")};
    if (!f) return std::unexpected(std::format("cannot open {}", where));

    Raw raw{};
    if (std::fread(&raw.header, sizeof raw.header, 1, f.get()) != 1)
        return std::unexpected(std::format("{}: truncated header", where));
    if (raw.header.magic != raw_magic)
        return std::unexpected(std::format("{}: not a plumb raw file", where));
    if (raw.header.version != raw_version)
        return std::unexpected(
            std::format("{}: raw version {}, expected {}", where, raw.header.version, raw_version));

    raw.ns.resize(raw.header.count);
    std::size_t got = std::fread(raw.ns.data(), sizeof(double), raw.ns.size(), f.get());
    if (got != raw.ns.size())
        return std::unexpected(
            std::format("{}: truncated samples, {} of {}", where, got, raw.header.count));
    if (std::fgetc(f.get()) != EOF)
        return std::unexpected(std::format("{}: trailing bytes after the samples", where));
    return raw;
}

}  // namespace plumb
