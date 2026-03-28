#include "codex_auth.h"
#include "../utils/http.h"
#include "../config/config.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/stat.h>

namespace opencodecpp {

std::string CodexAuth::tokenCachePath() {
    return Config::configDir() + "/codex_token.json";
}

std::string CodexAuth::loadCachedToken() {
    std::string path = tokenCachePath();
    std::ifstream f(path);
    if (!f.good()) return "";

    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("access_token")) {
            return j["access_token"].get<std::string>();
        }
    } catch (...) {}

    return "";
}

void CodexAuth::saveCachedToken(const std::string& accessToken, const std::string& refreshToken) {
    std::string dir = Config::configDir();
    mkdir(dir.c_str(), 0755);

    nlohmann::json j;
    j["access_token"] = accessToken;
    if (!refreshToken.empty()) {
        j["refresh_token"] = refreshToken;
    }

    std::ofstream f(tokenCachePath());
    if (f.good()) {
        f << j.dump(2) << std::endl;
    }
}

CodexAuth::DeviceCodeResponse CodexAuth::getDeviceCode() {
    DeviceCodeResponse result;

    HttpClient http;
    nlohmann::json body;
    body["client_id"] = CLIENT_ID;
    body["scope"] = "openid profile email offline_access";
    body["audience"] = "https://api.openai.com/v1";

    auto resp = http.post(
        DEVICE_CODE_URL,
        body.dump(),
        {{"Content-Type", "application/json"}}
    );

    if (!resp.error.empty()) {
        std::cerr << "Failed to get device code: " << resp.error << std::endl;
        return result;
    }

    try {
        auto j = nlohmann::json::parse(resp.body);
        result.device_code = j.value("device_code", "");
        result.user_code = j.value("user_code", "");
        result.verification_uri = j.value("verification_uri_complete",
            j.value("verification_uri", ""));
        result.interval = j.value("interval", 5);
        result.expires_in = j.value("expires_in", 900);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse device code response: " << e.what() << std::endl;
    }

    return result;
}

std::string CodexAuth::pollForToken(const std::string& deviceCode, int interval, int expiresIn) {
    HttpClient http;

    auto startTime = std::chrono::steady_clock::now();
    auto maxDuration = std::chrono::seconds(expiresIn);

    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > maxDuration) {
            std::cerr << "Authentication timed out." << std::endl;
            return "";
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval));

        nlohmann::json body;
        body["grant_type"] = "urn:ietf:params:oauth:grant-type:device_code";
        body["device_code"] = deviceCode;
        body["client_id"] = CLIENT_ID;

        auto resp = http.post(
            TOKEN_URL,
            body.dump(),
            {{"Content-Type", "application/json"}}
        );

        if (!resp.error.empty()) continue;

        try {
            auto j = nlohmann::json::parse(resp.body);

            if (j.contains("access_token")) {
                std::string accessToken = j["access_token"].get<std::string>();
                std::string refreshToken = j.value("refresh_token", "");
                saveCachedToken(accessToken, refreshToken);
                return accessToken;
            }

            std::string error = j.value("error", "");
            if (error == "authorization_pending") {
                // Still waiting, continue polling
                continue;
            } else if (error == "slow_down") {
                interval += 5; // Back off
                continue;
            } else {
                // Other error (expired_token, access_denied, etc.)
                std::cerr << "Authentication error: " << error << std::endl;
                return "";
            }
        } catch (...) {
            continue;
        }
    }
}

std::string CodexAuth::authenticate() {
    // AC-24: Try cached token first
    std::string cached = loadCachedToken();
    if (!cached.empty()) {
        return cached;
    }

    // AC-21: Get device code
    auto deviceCode = getDeviceCode();
    if (deviceCode.device_code.empty()) {
        std::cerr << "Failed to obtain device code." << std::endl;
        return "";
    }

    // AC-22: Display user code and verification URL
    std::cout << "\n=== OpenAI Codex Authentication ===\n";
    std::cout << "Please visit: " << deviceCode.verification_uri << "\n";
    std::cout << "And enter code: " << deviceCode.user_code << "\n";
    std::cout << "Waiting for authentication...\n" << std::endl;

    // AC-23: Poll for token
    return pollForToken(deviceCode.device_code, deviceCode.interval, deviceCode.expires_in);
}

} // namespace opencodecpp
