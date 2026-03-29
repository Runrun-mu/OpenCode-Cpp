// AC-16 Integration Test:
// Verifies that running `./build/opencode --provider openai --model gpt-4o --auth codex`
// shows browser auth URL and begins the login flow.
//
// This test spawns the binary, captures its stdout/stderr for a few seconds,
// then kills it and validates the output contains the expected auth URL pattern.

#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <climits>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x + " at line " + std::to_string(__LINE__))

// Helper: run binary, capture output for up to `timeout_secs`, then kill it.
// Returns the captured stdout+stderr output.
static std::string runBinaryWithTimeout(const std::string& binary,
                                         const std::vector<std::string>& args,
                                         int timeout_secs) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        throw std::runtime_error("pipe() failed");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error("fork() failed");
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]); // close read end

        // Redirect stdout and stderr to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Build argv
        std::vector<const char*> argv;
        argv.push_back(binary.c_str());
        for (const auto& a : args) {
            argv.push_back(a.c_str());
        }
        argv.push_back(nullptr);

        // Unset OPENAI_API_KEY to ensure codex auth path is taken
        unsetenv("OPENAI_API_KEY");

        execv(binary.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127); // exec failed
    }

    // Parent process
    close(pipefd[1]); // close write end

    // Set non-blocking on read end
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    std::string output;
    char buf[4096];

    // Poll for output with timeout
    struct pollfd pfd;
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;

    int elapsed_ms = 0;
    int poll_interval_ms = 100;
    int timeout_ms = timeout_secs * 1000;

    while (elapsed_ms < timeout_ms) {
        int ret = poll(&pfd, 1, poll_interval_ms);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                output += buf;
            }
        }
        elapsed_ms += poll_interval_ms;

        // If we already got auth URL output, no need to wait longer
        if (output.find("auth.openai.com") != std::string::npos) {
            break;
        }
    }

    // Kill the child process (it's blocking on socket accept)
    kill(pid, SIGTERM);
    usleep(100000); // 100ms grace
    kill(pid, SIGKILL);

    // Drain remaining output
    while (true) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        output += buf;
    }

    close(pipefd[0]);
    waitpid(pid, nullptr, 0);

    return output;
}

// Find the opencode binary - check several possible locations relative to CWD and executable path
static std::string findBinary() {
    const char* candidates[] = {
        "./opencode",           // ctest runs from build dir
        "./build/opencode",     // running from project root
        "../build/opencode",    // running from tests/ dir
        "build/opencode",       // another relative path
        nullptr
    };

    for (int i = 0; candidates[i] != nullptr; i++) {
        if (access(candidates[i], X_OK) == 0) {
            char resolved[PATH_MAX];
            if (realpath(candidates[i], resolved)) {
                return std::string(resolved);
            }
            return std::string(candidates[i]);
        }
    }
    return "";
}

// Helper to temporarily remove the codex token cache so the auth flow is triggered
static std::string getTokenCachePath() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.opencode/codex_token.json";
}

static std::string backupAndRemoveTokenCache() {
    std::string path = getTokenCachePath();
    if (path.empty()) return "";
    std::string backup = path + ".test_backup";
    // Rename to backup (if exists)
    rename(path.c_str(), backup.c_str());
    return backup;
}

static void restoreTokenCache(const std::string& backup) {
    if (backup.empty()) return;
    std::string path = getTokenCachePath();
    // Restore from backup
    rename(backup.c_str(), path.c_str());
}

// RAII guard for token cache backup/restore
struct TokenCacheGuard {
    std::string backup;
    TokenCacheGuard() : backup(backupAndRemoveTokenCache()) {}
    ~TokenCacheGuard() { restoreTokenCache(backup); }
};

// AC-16: Running ./build/opencode --provider openai --model gpt-4o --auth codex
//        shows browser auth URL and completes login flow
void test_ac16_codex_auth_shows_url() {
    std::string binary = findBinary();
    if (binary.empty()) {
        throw std::runtime_error("Cannot find opencode binary - build first");
    }

    // Remove cached token so auth flow is triggered
    TokenCacheGuard guard;

    std::string output = runBinaryWithTimeout(binary, {
        "--provider", "openai",
        "--model", "gpt-4o",
        "--auth", "codex"
    }, 5);

    // Verify the output contains key auth flow indicators
    // 1. Must show the auth.openai.com authorization URL
    ASSERT_TRUE(output.find("auth.openai.com/oauth/authorize") != std::string::npos);

    // 2. Must contain the correct client_id
    ASSERT_TRUE(output.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann") != std::string::npos);

    // 3. Must show PKCE code_challenge parameter
    ASSERT_TRUE(output.find("code_challenge=") != std::string::npos);

    // 4. Must use S256 method
    ASSERT_TRUE(output.find("code_challenge_method=S256") != std::string::npos);

    // 5. Must include state parameter
    ASSERT_TRUE(output.find("state=") != std::string::npos);

    // 6. Must show authentication messaging
    ASSERT_TRUE(output.find("Authentication") != std::string::npos ||
                output.find("authentication") != std::string::npos);

    // 7. Must include redirect URI pointing to port 1455
    ASSERT_TRUE(output.find("1455") != std::string::npos);

    // 8. Must show "Waiting for authentication" or similar
    ASSERT_TRUE(output.find("Waiting") != std::string::npos);
}

// AC-16: Verify the auth URL contains all required OAuth parameters
void test_ac16_auth_url_complete_params() {
    std::string binary = findBinary();
    if (binary.empty()) {
        throw std::runtime_error("Cannot find opencode binary - build first");
    }

    TokenCacheGuard guard;

    std::string output = runBinaryWithTimeout(binary, {
        "--provider", "openai",
        "--model", "gpt-4o",
        "--auth", "codex"
    }, 5);

    // Extract the URL from output
    std::string urlPrefix = "https://auth.openai.com/oauth/authorize?";
    auto pos = output.find(urlPrefix);
    ASSERT_TRUE(pos != std::string::npos);

    // Find the end of the URL (next whitespace or newline)
    auto endPos = output.find_first_of(" \n\r", pos);
    std::string url = output.substr(pos, endPos - pos);

    // Verify all required OAuth parameters are present
    ASSERT_TRUE(url.find("response_type=code") != std::string::npos);
    ASSERT_TRUE(url.find("client_id=") != std::string::npos);
    ASSERT_TRUE(url.find("redirect_uri=") != std::string::npos);
    ASSERT_TRUE(url.find("scope=") != std::string::npos);
    ASSERT_TRUE(url.find("code_challenge=") != std::string::npos);
    ASSERT_TRUE(url.find("code_challenge_method=S256") != std::string::npos);
    ASSERT_TRUE(url.find("state=") != std::string::npos);
}

// AC-16: Verify the binary starts without OPENAI_API_KEY when --auth codex is used
void test_ac16_no_api_key_needed() {
    std::string binary = findBinary();
    if (binary.empty()) {
        throw std::runtime_error("Cannot find opencode binary - build first");
    }

    TokenCacheGuard guard;

    // The binary should NOT error with "No API key found" when --auth codex is used
    std::string output = runBinaryWithTimeout(binary, {
        "--provider", "openai",
        "--model", "gpt-4o",
        "--auth", "codex"
    }, 5);

    // Must NOT contain the API key error
    ASSERT_TRUE(output.find("No API key found") == std::string::npos);
    // Must show auth flow instead
    ASSERT_TRUE(output.find("auth.openai.com") != std::string::npos);
}

int main() {
    std::cout << "=== AC-16: Codex Auth CLI Integration Tests ===\n\n";

    TEST(ac16_codex_auth_shows_url);
    TEST(ac16_auth_url_complete_params);
    TEST(ac16_no_api_key_needed);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
