#pragma once
#include <string>
#include <map>
#include <functional>

namespace opencodecpp {

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::string error;
};

using StreamCallback = std::function<void(const std::string& data)>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpResponse post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers
    );

    HttpResponse postStream(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers,
        StreamCallback callback,
        std::function<bool()> cancelCheck = nullptr
    );
};

} // namespace opencodecpp
