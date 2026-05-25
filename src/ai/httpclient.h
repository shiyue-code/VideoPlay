#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <string>
#include <map>
#include <functional>
#include <cstdint>

namespace VideoPlay {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success() const { return statusCode >= 200 && statusCode < 300; }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    void setBaseUrl(const std::string& url);
    void setApiKey(const std::string& key);
    void setAuthHeader(const std::string& headerName, const std::string& valuePrefix);
    void setTimeout(int seconds);

    HttpResponse get(const std::string& path,
                     const std::map<std::string, std::string>& headers = {});

    HttpResponse post(const std::string& path, const std::string& jsonBody,
                      const std::map<std::string, std::string>& headers = {});

    HttpResponse uploadFile(const std::string& path,
                            const std::string& filePath,
                            const std::string& fieldName,
                            const std::map<std::string, std::string>& fields = {},
                            const std::map<std::string, std::string>& headers = {});

private:
    std::string m_baseUrl;
    std::string m_apiKey;
    std::string m_authHeaderName = "Authorization";
    std::string m_authHeaderPrefix = "Bearer ";
    int m_timeout = 60;

    std::map<std::string, std::string> defaultHeaders() const;
    std::string buildUrl(const std::string& path) const;
};

} // namespace VideoPlay

#endif // HTTPCLIENT_H
