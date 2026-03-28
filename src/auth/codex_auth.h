#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace opencodecpp {

class CodexAuth {
public:
    // Updated OAuth constants (Authorization Code + PKCE flow)
    static constexpr const char* CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann";
    static constexpr const char* AUTHORIZE_URL = "https://auth.openai.com/oauth/authorize";
    static constexpr const char* TOKEN_URL = "https://auth.openai.com/oauth/token";
    static constexpr const char* REDIRECT_URI = "http://localhost:1455/auth/callback";
    static constexpr const char* SCOPE = "openid profile email offline_access";
    static constexpr const char* CODEX_BASE_URL = "https://chatgpt.com/backend-api";

    // PKCE support
    struct PKCEPair {
        std::string verifier;
        std::string challenge;
    };

    // Generate PKCE code_verifier (32 random bytes, base64url) and
    // code_challenge (SHA256 of verifier, base64url)
    static PKCEPair generatePKCE();

    // Generate random state parameter (16 random bytes, hex-encoded)
    static std::string generateState();

    // Build the full authorization URL for browser-based OAuth
    static std::string buildAuthorizationUrl(const std::string& codeChallenge, const std::string& state);

    // Extract chatgpt_account_id from JWT access token
    static std::string extractAccountId(const std::string& jwt);

    // Token cache path (~/.opencode/codex_token.json)
    static std::string tokenCachePath();

    // Load cached access token string (empty if not cached or expired)
    static std::string loadCachedToken();

    // Load full cached token data as JSON
    static nlohmann::json loadCachedTokenData();

    // Save token to cache with all fields
    static void saveCachedToken(const std::string& accessToken,
                                const std::string& refreshToken = "",
                                long long expiresAt = 0,
                                const std::string& accountId = "");

    // Check if token should be refreshed
    static bool shouldRefresh(long long expiresAt);

    // Refresh an expired token using refresh_token
    static std::string refreshToken(const std::string& refreshToken);

    // Start local HTTP server on 127.0.0.1:1455, wait for OAuth callback
    // Returns the authorization code, or empty on timeout/error
    static std::string waitForCallback(const std::string& expectedState, int timeoutSeconds = 120);

    // Run the full Authorization Code + PKCE auth flow
    // Returns the access token, or empty string on failure
    static std::string authenticate();

    // Token exchange: exchange authorization code for tokens
    static nlohmann::json exchangeCode(const std::string& code, const std::string& codeVerifier);

private:
    // Base64url encode/decode helpers
    static std::string base64urlEncode(const unsigned char* data, size_t len);
    static std::string base64urlEncode(const std::string& data);
    static std::string base64urlDecode(const std::string& input);

    // URL-encode a string
    static std::string urlEncode(const std::string& str);
};

} // namespace opencodecpp
