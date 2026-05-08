/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/12/20 16:27:49 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/13 12:28:35 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpResponse.hpp"
#include <sstream>
#include <ctime>

HttpResponse::HttpResponse() 
    : statusCode(200),
      statusMessage("OK"),
      keepAlive(false),
      chunked(false),
      _suppressBody(false) {}

void HttpResponse::setStatus(int code) {
    statusCode = code;
    statusMessage = getStatusMessage(code);
}

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers[key] = value;
}

void HttpResponse::setContentType(const std::string& type) {
    headers["Content-Type"] = type;
}

void HttpResponse::setBody(const std::string& b) {
    body = b;
    
    // chunked가 아니면 Content-Length 설정
    if (!chunked) {
        std::ostringstream oss;
        oss << body.size();
        headers["Content-Length"] = oss.str();
    }
}

void HttpResponse::setKeepAlive(bool enable, int timeout, int max) {
    keepAlive = enable;
    
    if (enable) {
        std::ostringstream oss;
        oss << "timeout=" << timeout << ", max=" << max;
        headers["Keep-Alive"] = oss.str();
        headers["Connection"] = "keep-alive";
    } else {
        headers["Connection"] = "close";
        headers.erase("Keep-Alive");
    }
}

void HttpResponse::setChunked(bool enable) {
    chunked = enable;
    
    if (enable) {
        headers["Transfer-Encoding"] = "chunked";
        headers.erase("Content-Length");
    } else {
        headers.erase("Transfer-Encoding");
    }
}

std::string HttpResponse::getStatusMessage(int code) const {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown";
    }
}

void HttpResponse::suppressBody() {
    _suppressBody = true;
}

void HttpResponse::ensureHeaders() {
    // Date 헤더 추가
    if (headers.find("Date") == headers.end()) {
        time_t now = time(NULL);
        struct tm* tm = gmtime(&now);
        char buf[100];
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tm);
        headers["Date"] = std::string(buf);
    }

    // Server 헤더 추가
    if (headers.find("Server") == headers.end()) {
        headers["Server"] = "webserv/1.0";
    }

    // Content-Length 확인 (chunked가 아니고 body가 있으면)
    if (!chunked && headers.find("Content-Length") == headers.end()) {
        std::ostringstream oss;
        oss << body.size();
        headers["Content-Length"] = oss.str();
    }

    // Content-Type 기본값
    if (headers.find("Content-Type") == headers.end() && 
        headers.find("content-type") == headers.end()) {
        headers["Content-Type"] = "text/html; charset=utf-8";
    }

    // Connection 기본값
    if (headers.find("Connection") == headers.end()) {
        headers["Connection"] = keepAlive ? "keep-alive" : "close";
    }
}

std::string HttpResponse::toString() {
    ensureHeaders();

    std::ostringstream oss;
    
    // Status line
    oss << "HTTP/1.1 " << statusCode << " " << statusMessage << "\r\n";

    // Headers
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        oss << it->first << ": " << it->second << "\r\n";
    }

    // 헤더와 바디 사이 빈 줄
    oss << "\r\n";

    // Body (chunked가 아닌 경우고 suppressBody가 false인 경우에만 포함)
    if (!chunked && !_suppressBody) {
        oss << body;
    }

    return oss.str();
}

// Chunked encoding을 위한 메서드
std::string HttpResponse::toChunkedString() {
    if (!chunked)
        return toString();

    ensureHeaders();

    std::ostringstream oss;
    
    // Status line
    oss << "HTTP/1.1 " << statusCode << " " << statusMessage << "\r\n";

    // Headers
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        oss << it->first << ": " << it->second << "\r\n";
    }

    oss << "\r\n";

    // Body를 chunk로 전송
    if (!body.empty()) {
        std::ostringstream chunkSize;
        chunkSize << std::hex << body.size();
        oss << chunkSize.str() << "\r\n";
        oss << body << "\r\n";
    }

    // Last chunk (0)
    oss << "0\r\n\r\n";

    return oss.str();
}

bool HttpResponse::isKeepAlive() const {
    return keepAlive;
}

int HttpResponse::getStatusCode() const {
    return statusCode;
}
