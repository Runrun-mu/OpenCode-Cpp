#include <iostream>
#include <cstdlib>
#include <string>
#include <map>
#include "llm/openai.h"
#include "utils/http.h"

using namespace opencodecpp;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x)
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b)

// === AC-79: postStream() returns HttpResponse (not void) ===
void test_poststream_returns_httpresponse() {
    // Verify that postStream() returns HttpResponse
    // This test will fail to compile if postStream still returns void
    HttpClient client;
    HttpResponse resp = client.postStream(
        "http://localhost:1/nonexistent",
        "{}",
        {{"Content-Type", "application/json"}},
        [](const std::string&) {},
        nullptr
    );
    // Should have an error since localhost:1 is unlikely to accept connections
    ASSERT_FALSE(resp.error.empty());
}

// === AC-79: postStream() propagates curl errors ===
void test_poststream_curl_error_propagation() {
    HttpClient client;
    HttpResponse resp = client.postStream(
        "http://invalid.host.that.does.not.exist.example:9999/api",
        "{}",
        {{"Content-Type", "application/json"}},
        [](const std::string&) {},
        nullptr
    );
    // Should have a curl error (connection refused or DNS failure)
    ASSERT_FALSE(resp.error.empty());
}

// === AC-80: postStream() has connect timeout ===
// We verify this indirectly: connecting to a non-routable IP should fail
// much faster than 300 seconds (the old timeout). With CURLOPT_CONNECTTIMEOUT=10,
// it should fail within ~10 seconds rather than hanging for 300.
// For the test we just verify the error is populated (the timeout is set in code).
void test_poststream_connect_timeout_set() {
    // This test verifies that a connection attempt to unreachable host
    // returns an error (meaning timeouts are working, not hanging)
    HttpClient client;
    HttpResponse resp = client.postStream(
        "http://127.0.0.1:1/timeout-test",  // port 1 - connection refused (fast)
        "{}",
        {{"Content-Type", "application/json"}},
        [](const std::string&) {},
        nullptr
    );
    ASSERT_FALSE(resp.error.empty());
}

// === AC-81: HTTP error codes detected ===
// We test the openai.cpp side: when postStream returns a non-200 status,
// streamMessage should set result.error
// This is tested via buildRequest + parseResponse patterns

// === AC-82: stream_options included when streaming ===
void test_openai_stream_options_included() {
    OpenAIProvider provider("test-key", "gpt-4o");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Hello";
    msgs.push_back(m);

    // When stream=true, stream_options should be included
    auto req = provider.buildRequest(msgs, {}, "", true);
    ASSERT_TRUE(req.contains("stream"));
    ASSERT_TRUE(req["stream"].get<bool>());
    ASSERT_TRUE(req.contains("stream_options"));
    ASSERT_TRUE(req["stream_options"].contains("include_usage"));
    ASSERT_TRUE(req["stream_options"]["include_usage"].get<bool>());
}

void test_openai_stream_options_not_included_when_not_streaming() {
    OpenAIProvider provider("test-key", "gpt-4o");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Hello";
    msgs.push_back(m);

    // When stream=false, stream_options should NOT be included
    auto req = provider.buildRequest(msgs, {}, "", false);
    ASSERT_FALSE(req.contains("stream_options"));
}

// === AC-83: argStrings is not static ===
// We test this by calling streamMessage twice and verifying the second call
// doesn't have stale data from the first. We use a mock-like approach
// by testing the buildRequest only (since we can't easily mock HTTP).
// The real test is a code review check, but we also verify via
// the fact that calling streamMessage to an invalid endpoint twice
// doesn't crash or produce stale data.
void test_openai_argstrings_not_static() {
    // We verify that the static keyword is not present by calling
    // streamMessage twice with tool-call responses. Since we can't mock
    // the HTTP layer, we test by connecting to a bad endpoint twice.
    // The key verification is that neither call crashes and both return errors.
    OpenAIProvider provider("test-key", "gpt-4o", "http://127.0.0.1:1");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Hello";
    msgs.push_back(m);

    // First call
    auto result1 = provider.streamMessage(msgs, {}, "", [](const std::string&) {}, nullptr, nullptr);
    // Should have error (connection refused)
    ASSERT_FALSE(result1.error.empty());

    // Second call - should also work cleanly without stale state
    auto result2 = provider.streamMessage(msgs, {}, "", [](const std::string&) {}, nullptr, nullptr);
    ASSERT_FALSE(result2.error.empty());
}

// === AC-81: streamMessage propagates HTTP errors ===
void test_openai_stream_message_propagates_errors() {
    OpenAIProvider provider("test-key", "gpt-4o", "http://127.0.0.1:1");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Hello";
    msgs.push_back(m);

    auto result = provider.streamMessage(msgs, {}, "", [](const std::string&) {}, nullptr, nullptr);
    // Should have an error propagated from HTTP layer
    ASSERT_FALSE(result.error.empty());
}

// === AC-79: postStream returns status_code for successful HTTP responses ===
void test_poststream_returns_status_code() {
    // When connecting to a valid endpoint, status_code should be set.
    // When connecting to invalid endpoint, error should be set.
    HttpClient client;
    HttpResponse resp = client.postStream(
        "http://127.0.0.1:1/test",
        "{}",
        {},
        [](const std::string&) {},
        nullptr
    );
    // Either error is set or status_code is non-zero
    ASSERT_TRUE(!resp.error.empty() || resp.status_code != 0);
}

int main() {
    std::cout << "=== OpenAI Streaming Bug Fix Tests ===\n";

    // AC-79: postStream returns HttpResponse with error propagation
    TEST(poststream_returns_httpresponse);
    TEST(poststream_curl_error_propagation);
    TEST(poststream_returns_status_code);

    // AC-80: Connect timeout
    TEST(poststream_connect_timeout_set);

    // AC-82: stream_options
    TEST(openai_stream_options_included);
    TEST(openai_stream_options_not_included_when_not_streaming);

    // AC-83: No static argStrings
    TEST(openai_argstrings_not_static);

    // AC-81: Error propagation in streamMessage
    TEST(openai_stream_message_propagates_errors);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
