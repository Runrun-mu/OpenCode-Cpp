#include "codex_auth.h"
#include "../utils/http.h"
#include "../config/config.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <CommonCrypto/CommonDigest.h>

namespace opencodecpp {

// ============================================================
// Base64url helpers
// ============================================================

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string CodexAuth::base64urlEncode(const unsigned char* data, size_t len) {
    std::string result;
    int i = 0;
    unsigned char arr3[3], arr4[4];

    while (len--) {
        arr3[i++] = *(data++);
        if (i == 3) {
            arr4[0] = (arr3[0] & 0xfc) >> 2;
            arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
            arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
            arr4[3] = arr3[2] & 0x3f;
            for (i = 0; i < 4; i++)
                result += base64_chars[arr4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) arr3[j] = '\0';
        arr4[0] = (arr3[0] & 0xfc) >> 2;
        arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
        arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
        arr4[3] = arr3[2] & 0x3f;
        for (int j = 0; j < i + 1; j++)
            result += base64_chars[arr4[j]];
    }

    // Convert to base64url: + → -, / → _, strip =
    for (auto& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!result.empty() && result.back() == '=') result.pop_back();

    return result;
}

std::string CodexAuth::base64urlEncode(const std::string& data) {
    return base64urlEncode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

std::string CodexAuth::base64urlDecode(const std::string& input) {
    std::string b64 = input;
    // Convert base64url to standard base64
    for (auto& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // Add padding
    while (b64.size() % 4 != 0) b64 += '=';

    std::string result;
    int i = 0;
    unsigned char arr4[4], arr3[3];

    for (char c : b64) {
        if (c == '=') break;
        const char* p = strchr(base64_chars, c);
        if (!p) continue;
        arr4[i++] = static_cast<unsigned char>(p - base64_chars);
        if (i == 4) {
            arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
            arr3[1] = ((arr4[1] & 0xf) << 4) + ((arr4[2] & 0x3c) >> 2);
            arr3[2] = ((arr4[2] & 0x3) << 6) + arr4[3];
            for (i = 0; i < 3; i++) result += static_cast<char>(arr3[i]);
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 4; j++) arr4[j] = 0;
        arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
        arr3[1] = ((arr4[1] & 0xf) << 4) + ((arr4[2] & 0x3c) >> 2);
        arr3[2] = ((arr4[2] & 0x3) << 6) + arr4[3];
        for (int j = 0; j < i - 1; j++) result += static_cast<char>(arr3[j]);
    }
    return result;
}

std::string CodexAuth::urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : str) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

// ============================================================
// PKCE
// ============================================================

CodexAuth::PKCEPair CodexAuth::generatePKCE() {
    PKCEPair pair;

    // Generate 32 random bytes
    std::random_device rd;
    unsigned char randomBytes[32];
    for (int i = 0; i < 32; i++) {
        randomBytes[i] = static_cast<unsigned char>(rd() & 0xFF);
    }

    // code_verifier = base64url(randomBytes)
    pair.verifier = base64urlEncode(randomBytes, 32);

    // code_challenge = base64url(SHA256(code_verifier))
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(pair.verifier.c_str(), static_cast<CC_LONG>(pair.verifier.length()), hash);
    pair.challenge = base64urlEncode(hash, CC_SHA256_DIGEST_LENGTH);

    return pair;
}

std::string CodexAuth::generateState() {
    std::random_device rd;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        oss << std::setw(2) << (rd() & 0xFF);
    }
    return oss.str();
}

// ============================================================
// Authorization URL
// ============================================================

std::string CodexAuth::buildAuthorizationUrl(const std::string& codeChallenge, const std::string& state) {
    std::string url = AUTHORIZE_URL;
    url += "?response_type=code";
    url += "&client_id=" + urlEncode(CLIENT_ID);
    url += "&redirect_uri=" + urlEncode(REDIRECT_URI);
    url += "&scope=" + urlEncode(SCOPE);
    url += "&code_challenge=" + urlEncode(codeChallenge);
    url += "&code_challenge_method=S256";
    url += "&state=" + urlEncode(state);
    return url;
}

// ============================================================
// JWT decode
// ============================================================

std::string CodexAuth::extractAccountId(const std::string& jwt) {
    // Split JWT on '.'
    size_t dot1 = jwt.find('.');
    if (dot1 == std::string::npos) return "";
    size_t dot2 = jwt.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return "";

    std::string payloadB64 = jwt.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string payloadJson = base64urlDecode(payloadB64);

    try {
        auto j = nlohmann::json::parse(payloadJson);
        if (j.contains("https://api.openai.com/auth")) {
            auto& authClaim = j["https://api.openai.com/auth"];
            if (authClaim.contains("chatgpt_account_id")) {
                return authClaim["chatgpt_account_id"].get<std::string>();
            }
        }
    } catch (...) {}

    return "";
}

// ============================================================
// Token cache
// ============================================================

std::string CodexAuth::tokenCachePath() {
    return Config::configDir() + "/codex_token.json";
}

std::string CodexAuth::loadCachedToken() {
    auto data = loadCachedTokenData();
    if (data.contains("access_token")) {
        // Check expiration
        if (data.contains("expires_at")) {
            long long expiresAt = data["expires_at"].get<long long>();
            if (shouldRefresh(expiresAt)) {
                return ""; // Token is expired
            }
        }
        return data["access_token"].get<std::string>();
    }
    return "";
}

nlohmann::json CodexAuth::loadCachedTokenData() {
    std::string path = tokenCachePath();
    std::ifstream f(path);
    if (!f.good()) return nlohmann::json::object();

    try {
        nlohmann::json j;
        f >> j;
        return j;
    } catch (...) {}

    return nlohmann::json::object();
}

void CodexAuth::saveCachedToken(const std::string& accessToken,
                                const std::string& refreshTok,
                                long long expiresAt,
                                const std::string& accountId) {
    std::string dir = Config::configDir();
    mkdir(dir.c_str(), 0755);

    nlohmann::json j;
    j["access_token"] = accessToken;
    if (!refreshTok.empty()) {
        j["refresh_token"] = refreshTok;
    }
    if (expiresAt > 0) {
        j["expires_at"] = expiresAt;
    } else {
        // Default: 1 hour from now
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        j["expires_at"] = epoch + 3600;
    }
    if (!accountId.empty()) {
        j["account_id"] = accountId;
    }

    std::ofstream f(tokenCachePath());
    if (f.good()) {
        f << j.dump(2) << std::endl;
    }
}

bool CodexAuth::shouldRefresh(long long expiresAt) {
    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return expiresAt < epoch;
}

// ============================================================
// Token exchange & refresh
// ============================================================

nlohmann::json CodexAuth::exchangeCode(const std::string& code, const std::string& codeVerifier) {
    HttpClient http;

    std::string body;
    body += "grant_type=authorization_code";
    body += "&client_id=" + urlEncode(CLIENT_ID);
    body += "&code=" + urlEncode(code);
    body += "&code_verifier=" + urlEncode(codeVerifier);
    body += "&redirect_uri=" + urlEncode(REDIRECT_URI);

    auto resp = http.post(
        TOKEN_URL,
        body,
        {{"Content-Type", "application/x-www-form-urlencoded"}}
    );

    if (!resp.error.empty()) {
        std::cerr << "Token exchange failed: " << resp.error << std::endl;
        return nlohmann::json::object();
    }

    try {
        return nlohmann::json::parse(resp.body);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse token response: " << e.what() << std::endl;
    }

    return nlohmann::json::object();
}

std::string CodexAuth::refreshToken(const std::string& refreshTok) {
    HttpClient http;

    std::string body;
    body += "grant_type=refresh_token";
    body += "&refresh_token=" + urlEncode(refreshTok);
    body += "&client_id=" + urlEncode(CLIENT_ID);

    auto resp = http.post(
        TOKEN_URL,
        body,
        {{"Content-Type", "application/x-www-form-urlencoded"}}
    );

    if (!resp.error.empty()) {
        return "";
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("access_token")) {
            std::string newAccess = j["access_token"].get<std::string>();
            std::string newRefresh = j.value("refresh_token", refreshTok);
            long long expiresIn = j.value("expires_in", 3600);

            auto now = std::chrono::system_clock::now();
            auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            long long expiresAt = epoch + expiresIn;

            std::string accountId = extractAccountId(newAccess);
            saveCachedToken(newAccess, newRefresh, expiresAt, accountId);

            return newAccess;
        }
    } catch (...) {}

    return "";
}

// ============================================================
// Local HTTP callback server
// ============================================================

std::string CodexAuth::waitForCallback(const std::string& expectedState, int timeoutSeconds) {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return "";
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1455);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port 1455" << std::endl;
        close(serverFd);
        return "";
    }

    if (listen(serverFd, 1) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        close(serverFd);
        return "";
    }

    // Use poll for timeout
    struct pollfd pfd;
    pfd.fd = serverFd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeoutSeconds * 1000);
    if (ret <= 0) {
        std::cerr << "Auth callback timed out" << std::endl;
        close(serverFd);
        return "";
    }

    int clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0) {
        close(serverFd);
        return "";
    }

    // Read HTTP request
    char buffer[4096];
    ssize_t n = read(clientFd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(clientFd);
        close(serverFd);
        return "";
    }
    buffer[n] = '\0';
    std::string request(buffer);

    // Parse GET /auth/callback?code=...&state=...
    std::string code;
    std::string state;

    size_t qPos = request.find('?');
    size_t spacePos = request.find(' ', qPos != std::string::npos ? qPos : 0);
    if (qPos != std::string::npos && spacePos != std::string::npos) {
        std::string query = request.substr(qPos + 1, spacePos - qPos - 1);

        // Parse query parameters
        std::istringstream qs(query);
        std::string param;
        while (std::getline(qs, param, '&')) {
            size_t eq = param.find('=');
            if (eq != std::string::npos) {
                std::string key = param.substr(0, eq);
                std::string val = param.substr(eq + 1);
                if (key == "code") code = val;
                else if (key == "state") state = val;
            }
        }
    }

    // Send success response
    std::string successHtml =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<html><body><h1>Authentication Successful!</h1>"
        "<p>You can close this tab and return to the terminal.</p></body></html>";
    write(clientFd, successHtml.c_str(), successHtml.size());

    close(clientFd);
    close(serverFd);

    // Validate state
    if (state != expectedState) {
        std::cerr << "State mismatch in OAuth callback" << std::endl;
        return "";
    }

    return code;
}

// ============================================================
// Full authentication flow
// ============================================================

std::string CodexAuth::authenticate() {
    // Try cached token first
    auto tokenData = loadCachedTokenData();
    if (tokenData.contains("access_token")) {
        long long expiresAt = tokenData.value("expires_at", (long long)0);
        if (!shouldRefresh(expiresAt)) {
            return tokenData["access_token"].get<std::string>();
        }
        // Try refresh (AC-12)
        if (tokenData.contains("refresh_token")) {
            std::string newToken = refreshToken(tokenData["refresh_token"].get<std::string>());
            if (!newToken.empty()) {
                return newToken;
            }
        }
        // AC-13: Refresh failed, clear cache and re-authenticate
        remove(tokenCachePath().c_str());
    }

    // Generate PKCE and state
    auto pkce = generatePKCE();
    std::string state = generateState();

    // Build authorization URL
    std::string authUrl = buildAuthorizationUrl(pkce.challenge, state);

    // Display URL (AC-5)
    std::cout << "\n=== OpenAI Codex Authentication ===\n";
    std::cout << "Opening browser for authentication...\n";
    std::cout << "If browser doesn't open, visit:\n";
    std::cout << authUrl << "\n";
    std::cout << "Waiting for authentication...\n" << std::endl;

    // Try to open browser (AC-5)
#ifdef __APPLE__
    std::string cmd = "open \"" + authUrl + "\" 2>/dev/null &";
#else
    std::string cmd = "xdg-open \"" + authUrl + "\" 2>/dev/null &";
#endif
    system(cmd.c_str());

    // Wait for callback (AC-4)
    std::string code = waitForCallback(state);
    if (code.empty()) {
        std::cerr << "Failed to receive authorization code." << std::endl;
        return "";
    }

    // Exchange code for tokens (AC-6)
    auto tokenResp = exchangeCode(code, pkce.verifier);
    if (!tokenResp.contains("access_token")) {
        std::cerr << "Token exchange failed." << std::endl;
        if (tokenResp.contains("error_description")) {
            std::cerr << tokenResp["error_description"].get<std::string>() << std::endl;
        }
        return "";
    }

    std::string accessToken = tokenResp["access_token"].get<std::string>();
    std::string refreshTok = tokenResp.value("refresh_token", "");
    long long expiresIn = tokenResp.value("expires_in", 3600);

    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    long long expiresAt = epoch + expiresIn;

    // AC-7: Extract account ID from JWT
    std::string accountId = extractAccountId(accessToken);

    // AC-11: Cache with all fields
    saveCachedToken(accessToken, refreshTok, expiresAt, accountId);

    std::cout << "Authentication successful!" << std::endl;
    return accessToken;
}

} // namespace opencodecpp
