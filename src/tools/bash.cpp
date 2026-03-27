#include "bash.h"
#include <cstdio>
#include <array>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <chrono>

namespace opencodecpp {

nlohmann::json BashTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"command", {{"type", "string"}, {"description", "The shell command to execute"}}},
            {"timeout", {{"type", "integer"}, {"description", "Timeout in seconds (default 30)"}, {"default", 30}}}
        }},
        {"required", {"command"}}
    };
}

nlohmann::json BashTool::execute(const nlohmann::json& params) {
    std::string command = params.value("command", "");
    int timeout = params.value("timeout", 30);

    if (command.empty()) {
        return {{"error", "command parameter is required"}};
    }

    // Use pipe for output with timeout
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {{"error", "Failed to create pipe"}};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {{"error", "Failed to fork process"}};
    }

    if (pid == 0) {
        // Child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // Parent
    close(pipefd[1]);

    // Set pipe to non-blocking
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    std::string output;
    std::array<char, 4096> buffer;
    bool timedOut = false;

    auto startTime = std::chrono::steady_clock::now();
    auto timeoutMs = std::chrono::milliseconds(timeout * 1000);

    // Read output with poll-based timeout
    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed >= timeoutMs) {
            timedOut = true;
            break;
        }

        auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeoutMs - elapsed).count();

        struct pollfd pfd;
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;

        int pollResult = poll(&pfd, 1, std::min(remainingMs, (long long)500));
        if (pollResult > 0 && (pfd.revents & POLLIN)) {
            ssize_t bytesRead = read(pipefd[0], buffer.data(), buffer.size());
            if (bytesRead > 0) {
                output.append(buffer.data(), bytesRead);
                if (output.size() > 1024 * 1024) break; // 1MB limit
            } else if (bytesRead == 0) {
                break; // EOF
            }
        } else if (pollResult == 0) {
            // Poll timeout, check if child has exited
            int status;
            int wr = waitpid(pid, &status, WNOHANG);
            if (wr > 0) {
                // Child exited, drain remaining output
                while (true) {
                    ssize_t bytesRead = read(pipefd[0], buffer.data(), buffer.size());
                    if (bytesRead <= 0) break;
                    output.append(buffer.data(), bytesRead);
                }
                close(pipefd[0]);
                int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                nlohmann::json result;
                result["stdout"] = output;
                result["exit_code"] = exitCode;
                return result;
            }
        } else {
            break; // Error
        }
    }
    close(pipefd[0]);

    // Wait or kill
    int status = 0;
    if (timedOut) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    } else {
        waitpid(pid, &status, 0);
    }

    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    nlohmann::json result;
    result["stdout"] = output;
    result["exit_code"] = exitCode;
    if (timedOut) {
        result["error"] = "Command timed out after " + std::to_string(timeout) + " seconds";
    }
    return result;
}

} // namespace opencodecpp
