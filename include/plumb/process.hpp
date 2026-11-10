#pragma once

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace plumb::detail {

/// @brief How a child process ended. `code` is its exit status, its signal, or the spawn error.
struct Exit {
    enum class Kind { ok, nonzero, signal, not_started };

    Kind kind;
    int  code;

    bool operator==(const Exit&) const = default;
};

/// @brief Runs `program` with `args` in a new process with this one's environment, and waits.
inline Exit spawn_wait(const std::filesystem::path& program, std::span<const std::string> args) {
    std::string        name = program.string();
    std::vector<char*> argv{name.data()};
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    if (int err = posix_spawn(&pid, name.c_str(), nullptr, nullptr, argv.data(), environ); err != 0)
        return {Exit::Kind::not_started, err};

    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return {Exit::Kind::not_started, errno};
    if (WIFSIGNALED(status)) return {Exit::Kind::signal, WTERMSIG(status)};
    int code = WEXITSTATUS(status);
    return {code == 0 ? Exit::Kind::ok : Exit::Kind::nonzero, code};
}

/// @brief The path of this process's own binary.
inline std::filesystem::path self() { return std::filesystem::read_symlink("/proc/self/exe"); }

}  // namespace plumb::detail
