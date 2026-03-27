#include "http.h"
#include <curl/curl.h>
#include <stdexcept>

namespace opencodecpp {

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

struct StreamContext {
    StreamCallback callback;
    std::function<bool()> cancelCheck;
};

static size_t streamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    StreamContext* ctx = static_cast<StreamContext*>(userp);
    if (ctx->cancelCheck && ctx->cancelCheck()) {
        return 0; // Abort transfer
    }
    std::string data(static_cast<char*>(contents), totalSize);
    ctx->callback(data);
    return totalSize;
}

HttpClient::HttpClient() {
    static bool initialized = false;
    if (!initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        initialized = true;
    }
}

HttpClient::~HttpClient() {}

HttpResponse HttpClient::post(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers
) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
    }

    struct curl_slist* headerList = nullptr;
    for (auto& [key, value] : headers) {
        std::string h = key + ": " + value;
        headerList = curl_slist_append(headerList, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse HttpClient::postStream(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    StreamCallback callback,
    std::function<bool()> cancelCheck
) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "Failed to initialize curl";
        return response;
    }

    StreamContext ctx{callback, cancelCheck};

    struct curl_slist* headerList = nullptr;
    for (auto& [key, value] : headers) {
        std::string h = key + ": " + value;
        headerList = curl_slist_append(headerList, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

} // namespace opencodecpp
