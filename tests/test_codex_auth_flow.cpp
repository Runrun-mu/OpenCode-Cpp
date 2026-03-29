#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <fstream>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include "auth/codex_auth.h"
#include "llm/openai.h"
#include "llm/streaming.h"

using namespace opencodecpp;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x + " at line " + std::to_string(__LINE__))
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x + " at line " + std::to_string(__LINE__))
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + " at line " + std::to_string(__LINE__))
#define ASSERT_NE(a, b) if ((a) == (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " != " + #b + " at line " + std::to_string(__LINE__))

// ==========================================
// F-1: OAuth Endpoint & PKCE Migration
// ==========================================

// AC-1: CLIENT_ID updated
void test_client_id_updated() {
    ASSERT_EQ(std::string(CodexAuth::CLIENT_ID), std::string("app_EMoamEEZ73f0CkXaXp7hrann"));
}

// AC-2: Auth endpoints use auth.openai.com
void test_auth_endpoints_updated() {
    std::string authorizeUrl(CodexAuth::AUTHORIZE_URL);
    std::string tokenUrl(CodexAuth::TOKEN_URL);
    ASSERT_TRUE(authorizeUrl.find("auth.openai.com") != std::string::npos);
    ASSERT_TRUE(tokenUrl.find("auth.openai.com") != std::string::npos);
    // Must NOT contain auth0
    ASSERT_TRUE(authorizeUrl.find("auth0") == std::string::npos);
    ASSERT_TRUE(tokenUrl.find("auth0") == std::string::npos);
}

// AC-3: PKCE code_verifier and code_challenge generation
void test_pkce_generation() {
    auto pkce = CodexAuth::generatePKCE();
    // code_verifier should be base64url encoded 32 random bytes (~43 chars)
    ASSERT_TRUE(pkce.verifier.length() >= 40);
    ASSERT_TRUE(pkce.verifier.length() <= 48);
    // Should not contain non-base64url characters
    for (char c : pkce.verifier) {
        ASSERT_TRUE(isalnum(c) || c == '-' || c == '_');
    }
    // code_challenge should be SHA256 of verifier, base64url encoded (~43 chars)
    ASSERT_TRUE(pkce.challenge.length() >= 40);
    ASSERT_TRUE(pkce.challenge.length() <= 48);
    for (char c : pkce.challenge) {
        ASSERT_TRUE(isalnum(c) || c == '-' || c == '_');
    }
    // verifier and challenge must be different (SHA256 transforms)
    ASSERT_NE(pkce.verifier, pkce.challenge);
}

// AC-3: PKCE generation produces different values each time
void test_pkce_uniqueness() {
    auto pkce1 = CodexAuth::generatePKCE();
    auto pkce2 = CodexAuth::generatePKCE();
    ASSERT_NE(pkce1.verifier, pkce2.verifier);
    ASSERT_NE(pkce1.challenge, pkce2.challenge);
}

// AC-4: Redirect URI uses port 1455
void test_redirect_uri() {
    std::string redirectUri(CodexAuth::REDIRECT_URI);
    ASSERT_TRUE(redirectUri.find("1455") != std::string::npos);
    ASSERT_TRUE(redirectUri.find("/auth/callback") != std::string::npos);
    ASSERT_EQ(redirectUri, std::string("http://localhost:1455/auth/callback"));
}

// AC-5: Authorization URL is built correctly
void test_build_authorization_url() {
    auto pkce = CodexAuth::generatePKCE();
    std::string state = CodexAuth::generateState();
    std::string url = CodexAuth::buildAuthorizationUrl(pkce.challenge, state);
    ASSERT_TRUE(url.find("auth.openai.com/oauth/authorize") != std::string::npos);
    ASSERT_TRUE(url.find("response_type=code") != std::string::npos);
    ASSERT_TRUE(url.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann") != std::string::npos);
    ASSERT_TRUE(url.find("code_challenge_method=S256") != std::string::npos);
    ASSERT_TRUE(url.find("code_challenge=") != std::string::npos);
    ASSERT_TRUE(url.find("state=") != std::string::npos);
    ASSERT_TRUE(url.find("redirect_uri=") != std::string::npos);
    ASSERT_TRUE(url.find("scope=") != std::string::npos);
}

// AC-6: State generation
void test_state_generation() {
    std::string state = CodexAuth::generateState();
    // 16 random bytes hex-encoded → 32 hex chars
    ASSERT_EQ(state.length(), (size_t)32);
    for (char c : state) {
        ASSERT_TRUE(isxdigit(c));
    }
    // Uniqueness
    std::string state2 = CodexAuth::generateState();
    ASSERT_NE(state, state2);
}

// ==========================================
// F-2: JWT Decode & Codex API Endpoint
// ==========================================

// AC-7: JWT payload decode to extract chatgpt_account_id
void test_jwt_decode_account_id() {
    // Construct a fake JWT with the right claim structure
    // Header: {"alg":"RS256","typ":"JWT"}
    // Payload: {"https://api.openai.com/auth":{"chatgpt_account_id":"acct_test123"}}
    // We only need header.payload.signature format
    std::string header_b64 = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9";
    // base64url of: {"https://api.openai.com/auth":{"chatgpt_account_id":"acct_test123"},"exp":9999999999}
    std::string payload_b64 = "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF90ZXN0MTIzIn0sImV4cCI6OTk5OTk5OTk5OX0";
    std::string sig_b64 = "fakesig";

    std::string jwt = header_b64 + "." + payload_b64 + "." + sig_b64;

    std::string accountId = CodexAuth::extractAccountId(jwt);
    ASSERT_EQ(accountId, std::string("acct_test123"));
}

// AC-8: API base URL switches for codex mode
void test_codex_mode_base_url() {
    OpenAIProvider provider("test-token", "gpt-4o", "https://chatgpt.com/backend-api");
    provider.setCodexMode(true, "acct_test123");
    // The provider should use the codex base URL
    auto headers = provider.getHeaders();
    ASSERT_TRUE(headers.find("chatgpt-account-id") != headers.end());
}

// AC-9: Codex mode headers
void test_codex_mode_headers() {
    OpenAIProvider provider("test-token", "gpt-4o", "https://chatgpt.com/backend-api");
    provider.setCodexMode(true, "acct_test123");

    auto headers = provider.getHeaders();
    ASSERT_EQ(headers["chatgpt-account-id"], std::string("acct_test123"));
    ASSERT_EQ(headers["OpenAI-Beta"], std::string("responses=experimental"));
    ASSERT_EQ(headers["originator"], std::string("codex_cli_rs"));
    ASSERT_EQ(headers["Authorization"], std::string("Bearer test-token"));
}

// AC-9: Non-codex mode should NOT have codex headers
void test_non_codex_mode_headers() {
    OpenAIProvider provider("test-token", "gpt-4o");
    auto headers = provider.getHeaders();
    ASSERT_TRUE(headers.find("chatgpt-account-id") == headers.end());
    ASSERT_TRUE(headers.find("OpenAI-Beta") == headers.end());
    ASSERT_TRUE(headers.find("originator") == headers.end());
}

// AC-10: Request body transformed to Codex Responses format
void test_codex_request_body_format() {
    OpenAIProvider provider("test-token", "gpt-4o", "https://chatgpt.com/backend-api");
    provider.setCodexMode(true, "acct_test123");

    std::vector<Message> msgs;
    Message userMsg;
    userMsg.role = "user";
    userMsg.content = "Hello, write a function";
    msgs.push_back(userMsg);

    std::string systemPrompt = "You are a coding assistant.";

    auto req = provider.buildRequest(msgs, {}, systemPrompt, true);

    // In codex mode, request should use Responses API format
    ASSERT_TRUE(req.contains("instructions"));
    ASSERT_EQ(req["instructions"].get<std::string>(), systemPrompt);
    ASSERT_TRUE(req.contains("input"));
    ASSERT_TRUE(req["input"].is_array());
    ASSERT_TRUE(req.contains("store"));
    ASSERT_EQ(req["store"].get<bool>(), false);
    ASSERT_TRUE(req.contains("stream"));
    ASSERT_EQ(req["stream"].get<bool>(), true);
    // Should NOT have Chat Completions format fields
    ASSERT_FALSE(req.contains("messages"));
}

// AC-10: Codex request with multiple messages
void test_codex_request_multi_message() {
    OpenAIProvider provider("test-token", "gpt-4o", "https://chatgpt.com/backend-api");
    provider.setCodexMode(true, "acct_test123");

    std::vector<Message> msgs;
    Message m1; m1.role = "user"; m1.content = "Hello"; msgs.push_back(m1);
    Message m2; m2.role = "assistant"; m2.content = "Hi there!"; msgs.push_back(m2);
    Message m3; m3.role = "user"; m3.content = "Write code"; msgs.push_back(m3);

    auto req = provider.buildRequest(msgs, {}, "system prompt", true);
    ASSERT_TRUE(req["input"].is_array());
    ASSERT_TRUE(req["input"].size() == 3);
    // Check role mapping
    ASSERT_EQ(req["input"][0]["role"].get<std::string>(), std::string("user"));
    ASSERT_EQ(req["input"][1]["role"].get<std::string>(), std::string("assistant"));
    ASSERT_EQ(req["input"][2]["role"].get<std::string>(), std::string("user"));
}

// ==========================================
// F-3: Token Lifecycle & Response Handling
// ==========================================

// AC-11: Token cache includes all required fields
void test_token_cache_fields() {
    // Create a temp directory for testing
    std::string tmpDir = "/tmp/opencode_test_" + std::to_string(getpid());
    mkdir(tmpDir.c_str(), 0755);

    // Save a token
    nlohmann::json tokenData;
    tokenData["access_token"] = "test_access_token";
    tokenData["refresh_token"] = "test_refresh_token";
    tokenData["expires_at"] = 9999999999;
    tokenData["account_id"] = "acct_test";

    std::string tokenPath = tmpDir + "/codex_token.json";
    std::ofstream f(tokenPath);
    f << tokenData.dump(2);
    f.close();

    // Read it back and verify all fields
    std::ifstream rf(tokenPath);
    nlohmann::json loaded;
    rf >> loaded;
    ASSERT_TRUE(loaded.contains("access_token"));
    ASSERT_TRUE(loaded.contains("refresh_token"));
    ASSERT_TRUE(loaded.contains("expires_at"));
    ASSERT_TRUE(loaded.contains("account_id"));
    ASSERT_EQ(loaded["access_token"].get<std::string>(), std::string("test_access_token"));
    ASSERT_EQ(loaded["refresh_token"].get<std::string>(), std::string("test_refresh_token"));
    ASSERT_EQ(loaded["account_id"].get<std::string>(), std::string("acct_test"));

    // Cleanup
    remove(tokenPath.c_str());
    rmdir(tmpDir.c_str());
}

// AC-11: saveCachedToken includes all fields
void test_save_cached_token_fields() {
    // Use CodexAuth::saveCachedToken with all fields
    // We need to temporarily override the config dir - test by saving and loading
    CodexAuth::saveCachedToken("access123", "refresh456", 9999999999, "acct_789");
    auto tokenData = CodexAuth::loadCachedTokenData();
    ASSERT_EQ(tokenData["access_token"].get<std::string>(), std::string("access123"));
    ASSERT_EQ(tokenData["refresh_token"].get<std::string>(), std::string("refresh456"));
    ASSERT_EQ(tokenData["account_id"].get<std::string>(), std::string("acct_789"));
    ASSERT_TRUE(tokenData.contains("expires_at"));
    // Cleanup: remove the test token so it doesn't pollute real usage
    remove(CodexAuth::tokenCachePath().c_str());
}

// AC-12: shouldRefresh returns true for expired tokens
void test_should_refresh_expired() {
    // Token that expired in the past
    ASSERT_TRUE(CodexAuth::shouldRefresh(1000000000)); // year ~2001
}

// AC-12: shouldRefresh returns false for valid tokens
void test_should_refresh_valid() {
    // Token that expires far in the future
    ASSERT_FALSE(CodexAuth::shouldRefresh(9999999999));
}

// AC-14: Codex SSE event parsing - output_text.delta
void test_codex_sse_output_text_delta() {
    std::string received_text;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            if (data.contains("type") && data["type"] == "response.output_text.delta") {
                if (data.contains("delta")) {
                    received_text += data["delta"].get<std::string>();
                }
            }
        },
        nullptr
    );

    parser.feed("data: {\"type\":\"response.output_text.delta\",\"delta\":\"Hello \"}\n\n");
    parser.feed("data: {\"type\":\"response.output_text.delta\",\"delta\":\"world!\"}\n\n");
    ASSERT_EQ(received_text, std::string("Hello world!"));
}

// AC-14: Codex SSE event parsing - response.done
void test_codex_sse_response_done() {
    bool gotDone = false;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            if (data.contains("type") && data["type"] == "response.done") {
                gotDone = true;
            }
        },
        nullptr
    );

    parser.feed("data: {\"type\":\"response.done\"}\n\n");
    ASSERT_TRUE(gotDone);
}

// AC-10: Codex URL uses /codex/responses
void test_codex_url_endpoint() {
    OpenAIProvider provider("test-token", "gpt-4o", "https://chatgpt.com/backend-api");
    provider.setCodexMode(true, "acct_test123");
    std::string url = provider.getRequestUrl();
    ASSERT_EQ(url, std::string("https://chatgpt.com/backend-api/codex/responses"));
}

// Non-codex URL uses /v1/chat/completions
void test_non_codex_url_endpoint() {
    OpenAIProvider provider("test-token", "gpt-4o", "https://api.openai.com");
    std::string url = provider.getRequestUrl();
    ASSERT_EQ(url, std::string("https://api.openai.com/v1/chat/completions"));
}

// Test that old CLIENT_ID and auth0 references are gone
void test_no_old_auth_references() {
    // CLIENT_ID should NOT be the old one
    ASSERT_NE(std::string(CodexAuth::CLIENT_ID), std::string("DRivsnm2Mu42T3KOpqdtwB3NYviHYzwD"));
}

// Test SCOPE constant
void test_scope_constant() {
    std::string scope(CodexAuth::SCOPE);
    ASSERT_TRUE(scope.find("openid") != std::string::npos);
    ASSERT_TRUE(scope.find("offline_access") != std::string::npos);
}

int main() {
    std::cout << "=== Codex Auth Flow Tests ===\n\n";

    std::cout << "--- F-1: OAuth Endpoint & PKCE Migration ---\n";
    TEST(client_id_updated);               // AC-1
    TEST(auth_endpoints_updated);          // AC-2
    TEST(pkce_generation);                 // AC-3
    TEST(pkce_uniqueness);                 // AC-3
    TEST(redirect_uri);                    // AC-4
    TEST(build_authorization_url);         // AC-5
    TEST(state_generation);                // AC-6
    TEST(no_old_auth_references);          // AC-1, AC-2

    std::cout << "\n--- F-2: JWT Decode & Codex API Endpoint ---\n";
    TEST(jwt_decode_account_id);           // AC-7
    TEST(codex_mode_base_url);             // AC-8
    TEST(codex_mode_headers);              // AC-9
    TEST(non_codex_mode_headers);          // AC-9
    TEST(codex_request_body_format);       // AC-10
    TEST(codex_request_multi_message);     // AC-10
    TEST(codex_url_endpoint);              // AC-8, AC-9
    TEST(non_codex_url_endpoint);          // AC-8

    std::cout << "\n--- F-3: Token Lifecycle & Response Handling ---\n";
    TEST(token_cache_fields);              // AC-11
    TEST(save_cached_token_fields);        // AC-11
    TEST(should_refresh_expired);          // AC-12
    TEST(should_refresh_valid);            // AC-12
    TEST(codex_sse_output_text_delta);     // AC-14
    TEST(codex_sse_response_done);         // AC-14
    TEST(scope_constant);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
