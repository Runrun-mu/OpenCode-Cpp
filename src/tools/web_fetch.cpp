#include "web_fetch.h"
#include <regex>
#include <curl/curl.h>

namespace opencodecpp {

nlohmann::json WebFetchTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"url", {
                {"type", "string"},
                {"description", "The URL to fetch content from"}
            }},
            {"prompt", {
                {"type", "string"},
                {"description", "A prompt describing what information to extract from the page"}
            }}
        }},
        {"required", nlohmann::json::array({"url", "prompt"})}
    };
}

nlohmann::json WebFetchTool::execute(const nlohmann::json& params) {
    if (!params.contains("url") || !params["url"].is_string()) {
        return {{"error", "Missing required parameter: url"}};
    }
    if (!params.contains("prompt") || !params["prompt"].is_string()) {
        return {{"error", "Missing required parameter: prompt"}};
    }

    std::string url = params["url"].get<std::string>();
    std::string prompt = params["prompt"].get<std::string>();

    // AC-10: Validate URL
    if (url.find("://") == std::string::npos) {
        return {{"error", "Invalid URL: " + url}};
    }

    // Fetch the URL using libcurl directly (need GET, not POST)
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {{"error", "Failed to initialize curl"}};
    }

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            size_t totalSize = size * nmemb;
            std::string* str = static_cast<std::string*>(userp);
            str->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, 5 * 1024 * 1024L); // 5MB limit

    CURLcode res = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {{"error", std::string("Fetch failed: ") + curl_easy_strerror(res)}};
    }

    if (statusCode >= 400) {
        return {{"error", "HTTP error " + std::to_string(statusCode)}};
    }

    // AC-8: Strip HTML tags
    std::string text = stripHtml(responseBody);

    // AC-9: Truncate to 10000 characters
    text = truncateText(text, MAX_TEXT_LENGTH);

    return {
        {"url", url},
        {"prompt", prompt},
        {"content", text},
        {"status_code", statusCode}
    };
}

std::string WebFetchTool::stripHtml(const std::string& html) {
    std::string result = html;

    // Remove script and style blocks entirely
    result = std::regex_replace(result, std::regex("<script[^>]*>[\\s\\S]*?</script>", std::regex::icase), " ");
    result = std::regex_replace(result, std::regex("<style[^>]*>[\\s\\S]*?</style>", std::regex::icase), " ");

    // Remove HTML comments
    result = std::regex_replace(result, std::regex("<!--[\\s\\S]*?-->"), " ");

    // Replace block-level tags with newlines
    result = std::regex_replace(result, std::regex("<(br|p|div|h[1-6]|li|tr)[^>]*>", std::regex::icase), "\n");

    // Remove all remaining HTML tags
    result = std::regex_replace(result, std::regex("<[^>]*>"), "");

    // Decode common HTML entities
    std::vector<std::pair<std::string, std::string>> entities = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "},
        {"&apos;", "'"}
    };
    for (auto& [entity, replacement] : entities) {
        size_t pos = 0;
        while ((pos = result.find(entity, pos)) != std::string::npos) {
            result.replace(pos, entity.length(), replacement);
            pos += replacement.length();
        }
    }

    // Collapse multiple whitespace/newlines
    result = std::regex_replace(result, std::regex("[ \\t]+"), " ");
    result = std::regex_replace(result, std::regex("\\n{3,}"), "\n\n");

    // Trim leading/trailing whitespace
    size_t start = result.find_first_not_of(" \t\n\r");
    size_t end = result.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        result = result.substr(start, end - start + 1);
    } else {
        result.clear();
    }

    return result;
}

std::string WebFetchTool::truncateText(const std::string& text, size_t maxLen) {
    if (text.size() <= maxLen) return text;
    return text.substr(0, maxLen);
}

} // namespace opencodecpp
