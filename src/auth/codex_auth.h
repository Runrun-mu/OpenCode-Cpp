#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace opencodecpp {

class CodexAuth {
public:
    // AC-21: Device code flow endpoint
    static constexpr const char* DEVICE_CODE_URL = "https://auth0.openai.com/oauth/device/code";
    // AC-23: Token endpoint
    static constexpr const char* TOKEN_URL = "https://auth0.openai.com/oauth/token";
    // Client ID for the Codex CLI
    static constexpr const char* CLIENT_ID = "DRivsnm2Mu42T3KOpqdtwB3NYviHYzwD";

    // AC-23: Token cache path (~/.opencode/codex_token.json)
    static std::string tokenCachePath();

    // AC-24: Load cached token
    static std::string loadCachedToken();

    // Save token to cache
    static void saveCachedToken(const std::string& accessToken, const std::string& refreshToken = "");

    // AC-21, AC-22, AC-23: Run the full device code auth flow
    // Returns the access token, or empty string on failure
    static std::string authenticate();

    struct DeviceCodeResponse {
        std::string device_code;
        std::string user_code;
        std::string verification_uri;
        int interval = 5;
        int expires_in = 900;
    };

    // AC-21: Get device code
    static DeviceCodeResponse getDeviceCode();

    // AC-23: Poll for token
    static std::string pollForToken(const std::string& deviceCode, int interval, int expiresIn);
};

} // namespace opencodecpp
