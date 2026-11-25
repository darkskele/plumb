#pragma once

#include <algorithm>
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
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "plumb/fold.hpp"
#include "plumb/histogram.hpp"
#include "plumb/plan.hpp"
#include "plumb/raw.hpp"

namespace plumb {

/// @brief A case in a run's results. Its sizing, why each void child was void, and its repetitions
/// in the order they ran.
struct CaseResult {
    std::string              name;
    Calibration              calibration;
    std::vector<std::string> voids;
    bool                     dropped;
    std::vector<Repetition>  reps;

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

inline constexpr std::array<char, 8> results_magic = {'p', 'l', 'u', 'm', 'b', 'r', 'e', 's'};
/// @brief How values are encoded. The layout of the values is the schema each file carries.
inline constexpr std::uint32_t results_version = 6;

/// @brief A filled histogram bucket as stored. Its values from `lower` to `upper` picoseconds.
struct StoredBucket {
    std::int64_t  lower;
    std::int64_t  upper;
    std::uint64_t count;
};

/// @brief A histogram as stored. Its smallest and largest values are 0 when nothing was counted.
struct StoredHistogram {
    std::uint64_t             negative;
    std::int64_t              min;
    std::int64_t              max;
    std::vector<StoredBucket> buckets;
};

/// @brief How a type is stored when not member by member. None unless specialised.
/// @tparam T The type in memory.
template <class T>
struct Stored {};

template <>
struct Stored<Histogram> {
    using type = StoredHistogram;

    static StoredHistogram to(const Histogram& h) {
        StoredHistogram s{.negative = h.negative(),
                          .min      = h.count() > 0 ? h.min() : 0,
                          .max      = h.count() > 0 ? h.max() : 0,
                          .buckets  = {}};
        h.each([&](std::int64_t lo, std::int64_t hi, std::uint64_t n) {
            s.buckets.push_back({.lower = lo, .upper = hi, .count = n});
        });
        return s;
    }

    static std::expected<Histogram, std::string> from(const StoredHistogram& s) {
        std::vector<Histogram::Bucket> filled;
        filled.reserve(s.buckets.size());
        for (auto [lo, hi, n] : s.buckets) {
            auto i = lo < 0 ? Histogram::buckets : Histogram::index(lo);
            if (i >= Histogram::buckets || Histogram::lower(i) != lo || Histogram::upper(i) != hi)
                return std::unexpected(std::format("bad bucket {} to {}", lo, hi));
            filled.emplace_back(i, n);
        }
        return Histogram(filled, s.negative, s.min, s.max);
    }
};

namespace detail {

static_assert(std::endian::native == std::endian::little,
              "results files are written in native byte order, which must be little-endian");

template <class T>
concept Restored = requires { typename Stored<T>::type; };

template <class T>
inline constexpr bool is_vector = false;
template <class T>
inline constexpr bool is_vector<std::vector<T>> = true;

template <class T>
inline constexpr bool is_array = false;
template <class T, std::size_t N>
inline constexpr bool is_array<std::array<T, N>> = true;

template <class T>
inline constexpr auto fields = std::define_static_array(
    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));

template <class T>
consteval std::string_view scalar_name() {
    if constexpr (std::same_as<T, bool>)
        return "bool";
    else if constexpr (std::same_as<T, char>)
        return "char";
    else if constexpr (std::same_as<T, double>)
        return "f64";
    else {
        static_assert(std::is_integral_v<T>, "results store integers, bool, char and double");
        constexpr std::array<std::string_view, 4> s{"i8", "i16", "i32", "i64"};
        constexpr std::array<std::string_view, 4> u{"u8", "u16", "u32", "u64"};
        constexpr auto                            at = std::bit_width(sizeof(T)) - 1;
        return std::is_signed_v<T> ? s[at] : u[at];
    }
}

/// @brief Each struct's line, `Name: field type, field type`, in the order first met.
struct Schema {
    std::vector<std::string> names;
    std::vector<std::string> lines;
};

template <class T>
std::string describe(Schema& s);

template <class T>
std::string describe_struct(Schema& s, std::string_view name) {
    static_assert(std::is_aggregate_v<T>, "results store aggregates member by member");
    if (std::ranges::contains(s.names, name)) return std::string(name);
    auto at = s.lines.size();
    s.names.emplace_back(name);
    s.lines.emplace_back();
    std::string line(name);
    line += ':';
    const char* sep = " ";
    template for (constexpr auto m : fields<T>) {
        using M                       = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
        constexpr std::string_view id = std::meta::identifier_of(m);
        line += std::format("{}{} {}", sep, id, describe<M>(s));
        sep = ", ";
    }
    s.lines[at] = std::move(line);
    return std::string(name);
}

template <class T>
std::string describe(Schema& s) {
    if constexpr (Restored<T>)
        return describe_struct<typename Stored<T>::type>(s, std::meta::identifier_of(^^T));
    else if constexpr (std::is_arithmetic_v<T>)
        return std::string(scalar_name<T>());
    else if constexpr (std::same_as<T, std::string>)
        return "str";
    else if constexpr (is_array<T>)
        return std::format("{}[{}]", describe<typename T::value_type>(s), std::tuple_size_v<T>);
    else if constexpr (is_vector<T>)
        return "list<" + describe<typename T::value_type>(s) + ">";
    else
        return describe_struct<T>(s, std::meta::identifier_of(^^T));
}

/// @brief The fewest bytes a stored `T` takes.
template <class T>
consteval std::size_t least() {
    if constexpr (Restored<T>)
        return least<typename Stored<T>::type>();
    else if constexpr (std::is_arithmetic_v<T>)
        return sizeof(T);
    else if constexpr (std::same_as<T, std::string> || is_vector<T>)
        return 8;
    else if constexpr (is_array<T>)
        return std::tuple_size_v<T> * least<typename T::value_type>();
    else {
        std::size_t n = 0;
        template for (constexpr auto m : fields<T>) n +=
            least<std::remove_cvref_t<typename[:std::meta::type_of(m):]>>();
        return n;
    }
}

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

/// @brief Bytes read in order. Reading past the end gives zeros and marks it short. A failure
/// keeps its reason and stops the reading.
struct In {
    std::string_view bytes;
    std::size_t      at     = 0;
    bool             short_ = false;
    std::string      error  = {};

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

    void fail(std::string why) {
        if (error.empty()) error = std::move(why);
        at = bytes.size();
    }
};

template <class T>
void put(Out& out, const T& v) {
    if constexpr (Restored<T>)
        put(out, Stored<T>::to(v));
    else if constexpr (std::is_arithmetic_v<T>)
        out.put(v);
    else if constexpr (std::same_as<T, std::string>)
        out.put(std::string_view(v));
    else if constexpr (is_array<T>)
        for (const auto& e : v) put(out, e);
    else if constexpr (is_vector<T>) {
        out.put(std::uint64_t{v.size()});
        for (const auto& e : v) put(out, e);
    } else {
        template for (constexpr auto m : fields<T>) put(out, v.[:m:]);
    }
}

template <class T>
void get(In& in, T& v) {
    if constexpr (Restored<T>) {
        typename Stored<T>::type s{};
        get(in, s);
        if (in.short_ || !in.error.empty()) return;
        if (auto r = Stored<T>::from(s))
            v = std::move(*r);
        else
            in.fail(std::move(r.error()));
    } else if constexpr (std::same_as<T, bool>)
        v = in.get<std::uint8_t>() != 0;
    else if constexpr (std::is_arithmetic_v<T>)
        v = in.get<T>();
    else if constexpr (std::same_as<T, std::string>)
        v = in.get_string();
    else if constexpr (is_array<T>)
        for (auto& e : v) get(in, e);
    else if constexpr (is_vector<T>) {
        v.resize(in.length(least<typename T::value_type>()));
        for (auto& e : v) get(in, e);
    } else {
        template for (constexpr auto m : fields<T>) get(in, v.[:m:]);
    }
}

/// @brief The schema of a results file, a line per struct, `Results` first.
inline const std::string& results_schema() {
    static const std::string text = [] {
        Schema s;
        describe<Results>(s);
        std::string out;
        for (const auto& line : s.lines) out += line + '\n';
        return out;
    }();
    return text;
}

}  // namespace detail

/// @brief Writes `r` to `path`, replacing any file there. The schema first, then the values in
/// its order.
inline std::expected<void, std::string> write_results(const std::filesystem::path& path,
                                                      const Results&               r) {
    detail::Out out;
    out.put(results_magic);
    out.put(results_version);
    out.put(std::string_view(detail::results_schema()));
    detail::put(out, r);

    detail::File f{std::fopen(path.c_str(), "wb")};
    if (!f) return std::unexpected(std::format("cannot open {} to write", path.string()));
    bool ok = std::fwrite(out.bytes.data(), 1, out.bytes.size(), f.get()) == out.bytes.size();
    if (!ok || std::fclose(f.release()) != 0)
        return std::unexpected(std::format("cannot write {}", path.string()));
    return {};
}

/// @brief Reads the results at `path`, refusing a file that is damaged, of another version, or of
/// another schema than this build's.
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
    if (in.get_string() != detail::results_schema() && !in.short_)
        return std::unexpected(std::format("{}: results schema differs from this build's", where));

    Results r{};
    detail::get(in, r);
    if (!in.error.empty()) return std::unexpected(std::format("{}: {}", where, in.error));
    if (in.short_) return std::unexpected(std::format("{}: truncated", where));
    if (in.at != bytes.size()) return std::unexpected(std::format("{}: trailing bytes", where));
    return r;
}

}  // namespace plumb
