#include "ai/httpclient.h"
#include "utils/logger.h"
#include <sstream>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("ai.http");
    return *logger;
}
}


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

    std::string base = m_baseUrl;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    std::string adjustedPath = path;
    if (base.size() >= 3 && base.substr(base.size() - 3) == "/v1") {
        if (adjustedPath.substr(0, 4) == "/v1/") {
            adjustedPath = adjustedPath.substr(3);
        } else if (adjustedPath == "/v1") {
            adjustedPath = "";
        }
    }

    std::string url = base;
    if (!adjustedPath.empty() && adjustedPath[0] != '/') {
        url += "/";
    }
    url += adjustedPath;
    return url;
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    int port = 0;
    bool isHttps = false;
};

static ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl result;
    
    if (url.substr(0, 8) == "https://") {
        result.isHttps = true;
        result.port = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (url.substr(0, 7) == "http://") {
        result.isHttps = false;
        result.port = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        result.isHttps = true;
        result.port = INTERNET_DEFAULT_HTTPS_PORT;
    }
    
    size_t start = result.isHttps ? 8 : 7;
    size_t pathStart = url.find('/', start);
    
    std::string hostPart;
    if (pathStart != std::string::npos) {
        hostPart = url.substr(start, pathStart - start);
        result.path = std::wstring(url.begin() + pathStart, url.end());
    } else {
        hostPart = url.substr(start);
        result.path = L"/";
    }
    
    size_t colonPos = hostPart.find(':');
    if (colonPos != std::string::npos) {
        std::string portStr = hostPart.substr(colonPos + 1);
        result.port = std::stoi(portStr);
        hostPart = hostPart.substr(0, colonPos);
    }
    
    result.host = std::wstring(hostPart.begin(), hostPart.end());
    
    return result;
}

static std::wstring buildHeadersString(const std::map<std::string, std::string>& headers) {
    std::wstring result;
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value + "\r\n";
        result += std::wstring(header.begin(), header.end());
    }
    return result;
}

static HttpResponse winHttpRequest(const std::string& url, const std::wstring& method,
                                    const std::string& body, const std::map<std::string, std::string>& headers,
                                    int timeout) {
    HttpResponse response;
    
    auto parsed = parseUrl(url);
    
    HINTERNET hSession = WinHttpOpen(L"VideoPlay AI/1.0",  
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        response.statusCode = -1;
        response.body = "WinHttpOpen failed: " + std::to_string(GetLastError());
        return response;
    }
    
    // 设置超时
    DWORD connectTimeout = timeout * 1000;
    DWORD receiveTimeout = timeout * 3000;
    DWORD sendTimeout = timeout * 1000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &sendTimeout, sizeof(sendTimeout));
    
    HINTERNET hConnect = WinHttpConnect(hSession, parsed.host.c_str(), parsed.port, 0);
    if (!hConnect) {
        response.statusCode = -1;
        response.body = "WinHttpConnect failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    DWORD flags = parsed.isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), parsed.path.c_str(),
                                            NULL, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        response.statusCode = -1;
        response.body = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    if (parsed.isHttps) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    }
    
    std::wstring headersStr = buildHeadersString(headers);
    
    LPVOID requestBody = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str();
    DWORD requestBodySize = body.empty() ? 0 : body.size();
    
    BOOL result = WinHttpSendRequest(hRequest, headersStr.c_str(), -1,
                                     requestBody, requestBodySize, requestBodySize, 0);
    if (!result) {
        response.statusCode = -1;
        response.body = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        response.statusCode = -1;
        response.body = "WinHttpReceiveResponse failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    response.statusCode = statusCode;
    
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
        responseBody.append(buffer.data(), bytesRead);
    }
    response.body = responseBody;
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}

HttpResponse HttpClient::get(const std::string& path,
                             const std::map<std::string, std::string>& headers) {
    std::string url = buildUrl(path);
    logger().debug("[HTTP] GET " + url);

    auto mergedHeaders = defaultHeaders();
    mergedHeaders.insert(headers.begin(), headers.end());

    return winHttpRequest(url, L"GET", "", mergedHeaders, m_timeout);
}

HttpResponse HttpClient::post(const std::string& path, const std::string& jsonBody,
                              const std::map<std::string, std::string>& headers) {
    std::string url = buildUrl(path);
    logger().debug("[HTTP] POST " + url);

    auto mergedHeaders = defaultHeaders();
    mergedHeaders["Content-Type"] = "application/json";
    mergedHeaders.insert(headers.begin(), headers.end());

    return winHttpRequest(url, L"POST", jsonBody, mergedHeaders, m_timeout);
}

HttpResponse HttpClient::uploadFile(const std::string& path,
                                    const std::string& filePath,
                                    const std::string& fieldName,
                                    const std::map<std::string, std::string>& fields,
                                    const std::map<std::string, std::string>& headers) {
    std::string url = buildUrl(path);
    logger().debug("[HTTP] UPLOAD " + url + " file=" + filePath);

    auto mergedHeaders = defaultHeaders();
    mergedHeaders.insert(headers.begin(), headers.end());

    auto parsed = parseUrl(url);
    
    // 读取文件
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        logger().error("[HTTP] Cannot open file: " + filePath);
        HttpResponse response;
        response.statusCode = -1;
        response.body = "Cannot open file: " + filePath;
        return response;
    }
    std::string fileContent((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    file.close();
    
    // 构建 multipart/form-data
    std::string boundary = "----VideoPlayBoundary" + std::to_string(GetTickCount64());
    std::string body;
    
    for (const auto& [key, value] : fields) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n";
        body += value + "\r\n";
    }
    
    std::string filename = std::filesystem::path(filePath).filename().string();
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + fieldName + "\"; filename=\"" + filename + "\"\r\n";
    body += "Content-Type: audio/wav\r\n\r\n";
    body += fileContent;
    body += "\r\n--" + boundary + "--\r\n";
    
    // 构建请求头
    std::string contentType = "Content-Type: multipart/form-data; boundary=" + boundary;
    std::wstring headersStr = std::wstring(contentType.begin(), contentType.end()) + L"\r\n";
    for (const auto& [key, value] : mergedHeaders) {
        std::string header = key + ": " + value + "\r\n";
        headersStr += std::wstring(header.begin(), header.end());
    }
    
    HINTERNET hSession = WinHttpOpen(L"VideoPlay AI/1.0",  
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        HttpResponse response;
        response.statusCode = -1;
        response.body = "WinHttpOpen failed";
        return response;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, parsed.host.c_str(), parsed.port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        HttpResponse response;
        response.statusCode = -1;
        response.body = "WinHttpConnect failed";
        return response;
    }
    
    DWORD flags = parsed.isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", parsed.path.c_str(),
                                            NULL, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        HttpResponse response;
        response.statusCode = -1;
        response.body = "WinHttpOpenRequest failed";
        return response;
    }
    
    if (parsed.isHttps) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    }
    
    BOOL result = WinHttpSendRequest(hRequest, headersStr.c_str(), -1,
                                     (LPVOID)body.c_str(), body.size(), body.size(), 0);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        HttpResponse response;
        response.statusCode = -1;
        response.body = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        return response;
    }
    
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        HttpResponse response;
        response.statusCode = -1;
        response.body = "WinHttpReceiveResponse failed: " + std::to_string(GetLastError());
        return response;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
        responseBody.append(buffer.data(), bytesRead);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    HttpResponse response;
    response.statusCode = statusCode;
    response.body = responseBody;
    return response;
}

} // namespace VideoPlay
