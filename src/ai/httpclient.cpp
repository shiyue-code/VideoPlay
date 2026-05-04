#include "ai/httpclient.h"
#include "utils/logger.h"
#include <httplib.h>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace VideoPlay {

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() = default;

void HttpClient::setBaseUrl(const std::string& url) {
    m_baseUrl = url;
    if (!m_baseUrl.empty() && m_baseUrl.back() == '/') {
        m_baseUrl.pop_back();
    }
}

void HttpClient::setApiKey(const std::string& key) {
    m_apiKey = key;
}

void HttpClient::setTimeout(int seconds) {
    m_timeout = seconds;
}

std::map<std::string, std::string> HttpClient::defaultHeaders() const {
    std::map<std::string, std::string> headers;
    headers["Accept"] = "application/json";
    if (!m_apiKey.empty()) {
        headers["Authorization"] = "Bearer " + m_apiKey;
    }
    return headers;
}

std::string HttpClient::buildUrl(const std::string& path) const {
    if (path.substr(0, 7) == "http://" || path.substr(0, 8) == "https://") {
        return path;
    }
    std::string url = m_baseUrl;
    if (!path.empty() && path[0] != '/') {
        url += "/";
    }
    url += path;
    return url;
}

HttpResponse HttpClient::get(const std::string& path,
                             const std::map<std::string, std::string>& headers) {
    std::string url = buildUrl(path);
    Logger::instance().debug("[HTTP] GET " + url);

    auto mergedHeaders = defaultHeaders();
    mergedHeaders.insert(headers.begin(), headers.end());

    httplib::Client client(url);
    client.set_connection_timeout(m_timeout);
    client.set_read_timeout(m_timeout * 3);
    client.set_write_timeout(m_timeout);

    httplib::Headers httplibHeaders;
    for (const auto& [key, value] : mergedHeaders) {
        httplibHeaders.emplace(key, value);
    }

    auto result = client.Get("/", httplibHeaders);

    HttpResponse response;
    if (result) {
        response.statusCode = result->status;
        response.body = result->body;
        for (const auto& [key, value] : result->headers) {
            response.headers[key] = value;
        }
    } else {
        response.statusCode = -1;
        response.body = httplib::to_string(result.error());
        Logger::instance().error("[HTTP] GET failed: " + response.body);
    }

    return response;
}

HttpResponse HttpClient::post(const std::string& path, const std::string& jsonBody,
                              const std::map<std::string, std::string>& headers) {
    std::string url = buildUrl(path);
    Logger::instance().debug("[HTTP] POST " + url);

    auto mergedHeaders = defaultHeaders();
    mergedHeaders["Content-Type"] = "application/json";
    mergedHeaders.insert(headers.begin(), headers.end());

    httplib::Client client(url);
    client.set_connection_timeout(m_timeout);
    client.set_read_timeout(m_timeout * 3);
    client.set_write_timeout(m_timeout);

    httplib::Headers httplibHeaders;
    for (const auto& [key, value] : mergedHeaders) {
        httplibHeaders.emplace(key, value);
    }

    auto result = client.Post("/", httplibHeaders, jsonBody, "application/json");

    HttpResponse response;
    if (result) {
        response.statusCode = result->status;
        response.body = result->body;
        for (const auto& [key, value] : result->headers) {
            response.headers[key] = value;
        }
    } else {
        response.statusCode = -1;
        response.body = httplib::to_string(result.error());
        Logger::instance().error("[HTTP] POST failed: " + response.body);
    }

    return response;
}

HttpResponse HttpClient::uploadFile(const std::string& path,
                                    const std::string& filePath,
                                    const std::string& fieldName,
                                    const std::map<std::string, std::string>& fields,
                                    const std::map<std::string, std::string>& headers) {
    std::string url = buildUrl(path);
    Logger::instance().debug("[HTTP] UPLOAD " + url + " file=" + filePath);

    auto mergedHeaders = defaultHeaders();
    mergedHeaders.insert(headers.begin(), headers.end());

    httplib::MultipartFormDataItems items;

    for (const auto& [key, value] : fields) {
        items.push_back({key, value, "", ""});
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        Logger::instance().error("[HTTP] Cannot open file: " + filePath);
        HttpResponse response;
        response.statusCode = -1;
        response.body = "Cannot open file: " + filePath;
        return response;
    }

    std::string fileContent((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();

    std::string filename = std::filesystem::path(filePath).filename().string();
    std::string contentType = "application/octet-stream";
    if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".wav") {
        contentType = "audio/wav";
    } else if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".mp3") {
        contentType = "audio/mpeg";
    } else if (filePath.size() >= 5 && filePath.substr(filePath.size() - 5) == ".json") {
        contentType = "application/json";
    }

    items.push_back({fieldName, fileContent, filename, contentType});

    httplib::Client client(m_baseUrl);
    client.set_connection_timeout(m_timeout);
    client.set_read_timeout(m_timeout * 5);
    client.set_write_timeout(m_timeout * 2);

    httplib::Headers httplibHeaders;
    for (const auto& [key, value] : mergedHeaders) {
        httplibHeaders.emplace(key, value);
    }

    std::string pathStr = path;
    if (!pathStr.empty() && pathStr[0] == '/') {
        pathStr = pathStr.substr(1);
    }

    auto result = client.Post("/" + pathStr, httplibHeaders, items);

    HttpResponse response;
    if (result) {
        response.statusCode = result->status;
        response.body = result->body;
        for (const auto& [key, value] : result->headers) {
            response.headers[key] = value;
        }
    } else {
        response.statusCode = -1;
        response.body = httplib::to_string(result.error());
        Logger::instance().error("[HTTP] UPLOAD failed: " + response.body);
    }

    return response;
}

} // namespace VideoPlay
