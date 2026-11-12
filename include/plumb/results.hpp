#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "plumb/fold.hpp"
#include "plumb/histogram.hpp"
#include "plumb/plan.hpp"
#include "plumb/raw.hpp"

namespace plumb {

/// @brief A case in a run's results. Its sizing, its voids, and its repetitions in the order they
/// ran.
struct CaseResult {
    std::string             name;
    Calibration             calibration;
    std::uint64_t           voids;
    bool                    dropped;
    std::vector<Repetition> reps;

    bool operator==(const CaseResult&) const = default;
};

/// @brief A run's results. `start_ns` is its start in nanoseconds since the Unix epoch.
struct Results {
    std::uint64_t           seed;
    double                  ghz;
    bool                    tsc_invariant;
    std::int64_t            start_ns;
    std::uint64_t           rounds;
    std::vector<CaseResult> cases;

    bool operator==(const Results&) const = default;
};

inline constexpr std::array<char, 8> results_magic   = {'p', 'l', 'u', 'm', 'b', 'r', 'e', 's'};
inline constexpr std::uint32_t       results_version = 1;

namespace detail {

static_assert(std::endian::native == std::endian::little,
              "results files are written in native byte order, which must be little-endian");

/// @brief Bytes written in order, little-endian.
struct Out {
    std::string bytes;

    template <class T>
        requires std::is_trivially_copyable_v<T>
    void put(const T& v) {
        bytes.append(reinterpret_cast<const char*>(&v), sizeof v);
    }

    void put(std::string_view s) {
        put(std::uint64_t{s.size()});
        bytes.append(s);
    }
};

/// @brief Bytes read in order. Reading past the end gives zeros and marks it short.
struct In {
    std::string_view bytes;
    std::size_t      at     = 0;
    bool             short_ = false;

    template <class T>
        requires std::is_trivially_copyable_v<T>
    T get() {
        T v{};
        if (bytes.size() - at < sizeof v) {
            short_ = true;
            at     = bytes.size();
            return v;
        }
        std::memcpy(&v, bytes.data() + at, sizeof v);
        at += sizeof v;
        return v;
    }

    // A length, or zero and short when that many `each`-byte items cannot follow.
    std::size_t length(std::size_t each) {
        auto n = get<std::uint64_t>();
        if (n > (bytes.size() - at) / each) {
            short_ = true;
            at     = bytes.size();
            return 0;
        }
        return static_cast<std::size_t>(n);
    }

    std::string get_string() {
        auto        n = length(1);
        std::string s(bytes.substr(at, n));
        at += n;
        return s;
    }
};

inline void put(Out& out, const Histogram& h) {
    auto filled = h.filled();
    out.put(h.negative());
    out.put(h.count() > 0 ? h.min() : std::int64_t{0});
    out.put(h.count() > 0 ? h.max() : std::int64_t{0});
    out.put(std::uint64_t{filled.size()});
    for (auto [i, n] : filled) {
        out.put(static_cast<std::uint32_t>(i));
        out.put(n);
    }
}

inline void put(Out& out, const Repetition& rep) {
    out.put(std::uint64_t{rep.round});
    out.put(rep.header);
    for (double f :
         {rep.figures.min, rep.figures.p50, rep.figures.p90, rep.figures.p99, rep.figures.max})
        out.put(f);
    put(out, rep.histogram);
    out.put(std::uint64_t{rep.windows.size()});
    for (double w : rep.windows) out.put(w);
}

}  // namespace detail

/// @brief Writes `r` to `path`, replacing any file there.
inline std::expected<void, std::string> write_results(const std::filesystem::path& path,
                                                      const Results&               r) {
    detail::Out out;
    out.put(results_magic);
    out.put(results_version);
    out.put(r.seed);
    out.put(r.ghz);
    out.put(std::uint8_t{r.tsc_invariant});
    out.put(r.start_ns);
    out.put(r.rounds);
    out.put(std::uint64_t{r.cases.size()});
    for (const CaseResult& c : r.cases) {
        out.put(std::string_view(c.name));
        out.put(std::uint64_t{c.calibration.plan.n});
        out.put(std::uint64_t{c.calibration.plan.count});
        out.put(c.calibration.batch_ns);
        out.put(c.voids);
        out.put(std::uint8_t{c.dropped});
        out.put(std::uint64_t{c.reps.size()});
        for (const Repetition& rep : c.reps) detail::put(out, rep);
    }

    detail::File f{std::fopen(path.c_str(), "wb")};
    if (!f) return std::unexpected(std::format("cannot open {} to write", path.string()));
    bool ok = std::fwrite(out.bytes.data(), 1, out.bytes.size(), f.get()) == out.bytes.size();
    if (!ok || std::fclose(f.release()) != 0)
        return std::unexpected(std::format("cannot write {}", path.string()));
    return {};
}

/// @brief Reads the results at `path`, refusing a file that is damaged or of another version.
inline std::expected<Results, std::string> read_results(const std::filesystem::path& path) {
    auto          where = path.string();
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::format("cannot open {}", where));
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    detail::In  in{.bytes = bytes};

    if (in.get<std::array<char, 8>>() != results_magic)
        return std::unexpected(std::format("{}: not a plumb results file", where));
    if (auto v = in.get<std::uint32_t>(); v != results_version)
        return std::unexpected(
            std::format("{}: results version {}, expected {}", where, v, results_version));

    Results r{};
    r.seed          = in.get<std::uint64_t>();
    r.ghz           = in.get<double>();
    r.tsc_invariant = in.get<std::uint8_t>() != 0;
    r.start_ns      = in.get<std::int64_t>();
    r.rounds        = in.get<std::uint64_t>();
    r.cases.resize(in.length(1));
    for (CaseResult& c : r.cases) {
        c.name                   = in.get_string();
        c.calibration.plan.n     = in.get<std::uint64_t>();
        c.calibration.plan.count = in.get<std::uint64_t>();
        c.calibration.batch_ns   = in.get<double>();
        c.voids                  = in.get<std::uint64_t>();
        c.dropped                = in.get<std::uint8_t>() != 0;
        c.reps.resize(in.length(1));
        for (Repetition& rep : c.reps) {
            rep.round   = in.get<std::uint64_t>();
            rep.header  = in.get<RawHeader>();
            rep.figures = {.min = in.get<double>(),
                           .p50 = in.get<double>(),
                           .p90 = in.get<double>(),
                           .p99 = in.get<double>(),
                           .max = in.get<double>()};

            auto                           negative = in.get<std::uint64_t>();
            auto                           min      = in.get<std::int64_t>();
            auto                           max      = in.get<std::int64_t>();
            std::vector<Histogram::Bucket> filled(in.length(12));
            for (auto& [i, n] : filled) {
                i = in.get<std::uint32_t>();
                n = in.get<std::uint64_t>();
                if (i >= Histogram::buckets)
                    return std::unexpected(std::format("{}: bad bucket {}", where, i));
            }
            rep.histogram = Histogram(filled, negative, min, max);

            rep.windows.resize(in.length(8));
            for (double& w : rep.windows) w = in.get<double>();
        }
    }
    if (in.short_) return std::unexpected(std::format("{}: truncated", where));
    if (in.at != bytes.size()) return std::unexpected(std::format("{}: trailing bytes", where));
    return r;
}

}  // namespace plumb
