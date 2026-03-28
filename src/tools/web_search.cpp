#include "web_search.h"
#include "../utils/http.h"
#include <curl/curl.h>
#include <regex>
#include <cstdlib>

namespace opencodecpp {

nlohmann::json WebSearchTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", {
                {"type", "string"},
                {"description", "The search query string"}
            }}
        }},
        {"required", nlohmann::json::array({"query"})}
    };
}

nlohmann::json WebSearchTool::execute(const nlohmann::json& params) {
    if (!params.contains("query") || !params["query"].is_string()) {
        return {{"error", "Missing required parameter: query"}};
    }
    std::string query = params["query"].get<std::string>();
    if (query.empty()) {
        return {{"error", "Query cannot be empty"}};
    }

    // AC-4: Check SERPAPI_KEY environment variable
    const char* serpApiKey = std::getenv("SERPAPI_KEY");
    if (serpApiKey && serpApiKey[0]) {
        return searchSerpAPI(query, std::string(serpApiKey));
    }

    // AC-5: Fall back to DuckDuckGo
    return searchDuckDuckGo(query);
}

nlohmann::json WebSearchTool::searchSerpAPI(const std::string& query, const std::string& apiKey) {
    HttpClient http;

    // URL-encode the query
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {{"error", "Failed to initialize curl for URL encoding"}};
    }
    char* encoded = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string encodedQuery = encoded ? encoded : query;
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);

    std::string url = "https://serpapi.com/search.json?q=" + encodedQuery + "&api_key=" + apiKey;

    // SerpAPI uses GET, but our HttpClient only has POST.
    // Use libcurl directly for a GET request.
    HttpResponse response;
    CURL* getCurl = curl_easy_init();
    if (!getCurl) {
        return {{"error", "Failed to initialize curl"}};
    }

    std::string responseBody;
    auto writeCallback = [](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
        size_t totalSize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    };

    curl_easy_setopt(getCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(getCurl, CURLOPT_WRITEFUNCTION,
        +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            size_t totalSize = size * nmemb;
            std::string* str = static_cast<std::string*>(userp);
            str->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        });
    curl_easy_setopt(getCurl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(getCurl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(getCurl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(getCurl);
    long statusCode = 0;
    curl_easy_getinfo(getCurl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(getCurl);

    if (res != CURLE_OK) {
        return {{"error", std::string("SerpAPI request failed: ") + curl_easy_strerror(res)}};
    }

    try {
        auto json = nlohmann::json::parse(responseBody);
        nlohmann::json results = nlohmann::json::array();

        if (json.contains("organic_results")) {
            for (auto& r : json["organic_results"]) {
                nlohmann::json item;
                item["title"] = r.value("title", "");
                item["snippet"] = r.value("snippet", "");
                item["url"] = r.value("link", "");
                results.push_back(item);
            }
        }

        return {{"results", results}};
    } catch (const std::exception& e) {
        return {{"error", std::string("Failed to parse SerpAPI response: ") + e.what()}};
    }
}

nlohmann::json WebSearchTool::searchDuckDuckGo(const std::string& query) {
    // AC-5: DuckDuckGo HTML API fallback
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {{"error", "Failed to initialize curl"}};
    }

    char* encoded = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string encodedQuery = encoded ? encoded : query;
    if (encoded) curl_free(encoded);

    std::string url = "https://html.duckduckgo.com/html/?q=" + encodedQuery;

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

    // POST with form data
    std::string postData = "q=" + encodedQuery;
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

    CURLcode res = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {{"error", std::string("DuckDuckGo request failed: ") + curl_easy_strerror(res)}};
    }

    // Parse HTML to extract results
    nlohmann::json results = nlohmann::json::array();

    // Extract results from DuckDuckGo HTML
    // Pattern: <a rel="nofollow" class="result__a" href="...">title</a>
    // and <a class="result__snippet" ...>snippet</a>
    std::string linkPattern = "<a[^>]*class=\"result__a\"[^>]*href=\"([^\"]*)\"[^>]*>([\\s\\S]*?)</a>";
    std::string snippetPattern = "<a[^>]*class=\"result__snippet\"[^>]*>([\\s\\S]*?)</a>";
    std::regex linkRegex(linkPattern);
    std::regex snippetRegex(snippetPattern);

    auto linksBegin = std::sregex_iterator(responseBody.begin(), responseBody.end(), linkRegex);
    auto snippetsBegin = std::sregex_iterator(responseBody.begin(), responseBody.end(), snippetRegex);
    auto end = std::sregex_iterator();

    std::vector<std::pair<std::string, std::string>> links; // url, title
    for (auto it = linksBegin; it != end; ++it) {
        std::string href = (*it)[1].str();
        std::string title = (*it)[2].str();
        // Strip HTML tags from title
        title = std::regex_replace(title, std::regex("<[^>]*>"), "");
        title = htmlDecode(title);
        links.push_back({href, title});
    }

    std::vector<std::string> snippets;
    for (auto it = snippetsBegin; it != end; ++it) {
        std::string snippet = (*it)[1].str();
        snippet = std::regex_replace(snippet, std::regex("<[^>]*>"), "");
        snippet = htmlDecode(snippet);
        snippets.push_back(snippet);
    }

    for (size_t i = 0; i < links.size(); i++) {
        nlohmann::json item;
        item["title"] = links[i].second;
        item["url"] = links[i].first;
        item["snippet"] = (i < snippets.size()) ? snippets[i] : "";
        results.push_back(item);
    }

    return {{"results", results}};
}

std::string WebSearchTool::htmlDecode(const std::string& input) {
    std::string result = input;
    // Common HTML entities
    std::vector<std::pair<std::string, std::string>> entities = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"},
        {"&#x27;", "'"}, {"&#x2F;", "/"}
    };
    for (auto& [entity, replacement] : entities) {
        size_t pos = 0;
        while ((pos = result.find(entity, pos)) != std::string::npos) {
            result.replace(pos, entity.length(), replacement);
            pos += replacement.length();
        }
    }
    return result;
}

} // namespace opencodecpp
