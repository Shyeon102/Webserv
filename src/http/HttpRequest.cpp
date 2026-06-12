/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/20 16:24:43 by jaoh              #+#    #+#             */
/*   Updated: 2026/06/12 04:14:49 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpRequest.hpp"
#include <sstream>
#include <cstdlib>
#include <cctype>

// 기본 생성자: 각 플래그와 상태값을 초기화한다.
HttpRequest::HttpRequest()
    : headersParsed(false),
      bodyParsed(false),
      complete(false),
      error(false),
      errorType(ERROR_NONE),
      contentLength(0),
      chunked(false),
      bodyStart(0),
      lastActivityTime(std::time(NULL)),
      consumedLength(0) {}

// 소켓에서 새로 읽은 데이터를 rawBuffer에 이어 붙이고,
// 헤더/바디를 순차적으로 파싱한다.
// complete가 true가 되면 하나의 HTTP 요청이 완성된 것.
bool HttpRequest::appendData(const std::string& data) {
    rawBuffer += data;

    // CHECK) 전체 요청 크기 제한 검사
    if (rawBuffer.size() > MAX_REQUEST_SIZE)
    {
        error = true;
        setError(ERROR_REQUEST_TOO_LARGE);
        return false;
    }

    // 아직 헤더를 다 읽지 못했다면, 헤더 끝("\r\n\r\n")을 찾는다.
    if (!headersParsed) {
        size_t pos = rawBuffer.find("\r\n\r\n");
        
        // CHECK)
        if (pos != std::string::npos)
        {
            if (pos > MAX_HEADER_SIZE)
            {
                error = true;
                setError(ERROR_HEADER_TOO_LARGE);
                return false;
            }

            parseHeaders(rawBuffer.substr(0, pos));

            if (error)
                return false;
            
            bodyStart = pos + 4;
            headersParsed = true;
        }
        else
            return false;

        //if (pos == std::string::npos)
            //return false;

        // 헤더 블록(첫 줄 + 헤더들)을 잘라서 파싱
        //parseHeaders(rawBuffer.substr(0, pos));
        //bodyStart = pos + 4;
        //headersParsed = true;
    }

    // 헤더 파싱이 끝난 이후에는 바디를 파싱
    if (!bodyParsed) {
        if (!parseBody())
            return false;
    }

    return complete;
}

/* ================= Request Line + Headers ================= */

// "GET /index.html HTTP/1.1" 형식의 첫 줄을 method, uri, version 으로 분리
void HttpRequest::parseRequestLine(const std::string& line) {
    std::stringstream ss(line);
    //ss >> method >> uri >> version;

    // CHECK)
    std::string extra; // 토큰이 3개 초과인지 확인하는 장치 : 잘못된 요청 ex) GET / HTTP/1.1 EXTRA
    ss >> method >> uri >> version >> extra;

    if (method.empty() || uri.empty() || version.empty() || !extra.empty())
    {
        error = true;
        return ;
    }
    
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
    {
        error = true;
        return ;
    }
    
    if (uri.length() > MAX_URI_LENGTH)
    {
        error = true;
        return ;
    }
}

// 첫 줄(요청 라인) 이후의 헤더들을 한 줄씩 읽어 파싱
void HttpRequest::parseHeaders(const std::string& block) {
    std::istringstream iss(block);
    std::string line;

    // 첫 줄은 요청 라인
    std::getline(iss, line);
    if (line.size() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    
    // CHECK) ex)GET /aaaaaaa...(10000자)... HTTP/1.1
    if (line.length() > MAX_LINE_LENGTH)
    {
        error = true;
        return ;
    }

    parseRequestLine(line);

    // 나머지 줄들은 "Key: Value" 형식의 헤더
    while (std::getline(iss, line)) {
        if (line.size() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        
        // CHECK) Header line 길이 제한 : ex) Host: aaaaaaaaaaaaa...(10000글자)
        if (line.length() > MAX_LINE_LENGTH)
        {
            error = true;
            return ;
        }
        
        if (line.empty())
            break;

        size_t pos = line.find(":");
        if (pos == std::string::npos) {
            error = true;
            return;
        }

        std::string key = toLower(trim(line.substr(0, pos)));
        std::string value = trim(line.substr(pos + 1));

        /* Content-Length가 여러 개 있을 경우: 값이 같아야 한다, 다르면 400 처리 */
        /* Content-Length가 여러개 있을 수 있음 (단, 값이 모두 동일해야함) */
        /* CEHCK) 하지만 우리는 nginx 수준의 완전한 RFC 구현을 요구하지 않기 때문에 그냥 중복 Content-Length는 그냥 400 처리로 진행: 중복 Content-Length 값까지 비교 검사는 진행 안함 */
        if (key == "content-length" && headers.find(key) != headers.end()) // find()가 못 찾으면 end()를 반환: 중복 검사
        {
            error = true;
            return ;
        }

        headers[key] = value;
        // CHECK) header 개수 제한
        if (headers.size() > MAX_HEADERS_COUNT)
        {
            error = true;
            return ;
        }
    }

    // Content-Length 가 있으면 바디 길이 설정
    if (headers.count("content-length")) {
        const std::string& cl = headers["content-length"];
        if (cl.empty()) {
            error = true;
            return;
        }

        for (size_t i = 0; i < cl.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(cl[i]))) {
                error = true;
                return;
            }
        }

        std::istringstream iss(cl);
        size_t parsed = 0;
        iss >> parsed;
        if (iss.fail() || !iss.eof()) {
            error = true;
            return;
        }
        contentLength = parsed;
    }

    // Transfer-Encoding: chunked 인 경우 chunked 플래그 설정
    if (headers.count("transfer-encoding")
        && toLower(headers["transfer-encoding"]) == "chunked")
        chunked = true;
}

/* ================= Body ================= */

// Content-Length 또는 chunked 여부에 따라 body를 파싱
bool HttpRequest::parseBody() {
    if (chunked)
        return parseChunkedBody();

    if (contentLength > 0) {
        if (rawBuffer.size() < bodyStart + contentLength)
            return false;

        body = rawBuffer.substr(bodyStart, contentLength);
        // CHECK)
        consumedLength = bodyStart + contentLength;
    }
    // CHECK)
    else
        consumedLength = bodyStart;

    bodyParsed = true;
    complete = true;
    return true;
}

/* ================= Chunked ================= */

bool HttpRequest::parseChunkedBody() {
    size_t pos = bodyStart;

    while (true) {
        size_t crlf = rawBuffer.find("\r\n", pos);
        if (crlf == std::string::npos)
            return false;

        std::string hexSize = rawBuffer.substr(pos, crlf - pos);

        // CHECK) 빈 size는 에러
        if (hexSize.empty())
        {
            error = true;
            return false;
        }

        // CHECK) 16진수 검사
        for (size_t i = 0; i < hexSize.size(); ++i)
        {
            if (!std::isxdigit(hexSize[i]))
            {
                error = true;
                return false;
            }
        }
        
        // CEHCK) hex -> size_t 변환
        std::istringstream  iss(hexSize);
        size_t  chunkSize;
        iss >> std::hex >> chunkSize;

        if (iss.fail())
        {
            error = true;
            return false;
        }

        //size_t chunkSize = std::strtoul(hexSize.c_str(), NULL, 16);

        pos = crlf + 2;

        if (chunkSize == 0) {
            // CHECK) 반드시 마지막 CRLF 존재해야 함
            if (rawBuffer.size() < (pos + 2))
                return false; // 아직 다 안 들어옴

            if (rawBuffer.substr(pos, 2) != "\r\n")
            {
                error = true;
                return false;
            }

            consumedLength = pos + 2; // 마지막 CRLF 포함 (0\r\n 까지 읽은 위치 -> 하지만 chunked는 0\r\n\r\n으로 끝나므로 마지막 \r\n 2바이트를 더 포함)
            bodyParsed = true;
            complete = true;
            return true;
        }

        if (rawBuffer.size() < pos + chunkSize + 2)
            return false;

        // CHECK) chunk 데이터 뒤 CRLF 검사
        if (rawBuffer.substr(pos + chunkSize, 2) != "\r\n")
        {
            error = true;
            return false;
        }
            
        body.append(rawBuffer.substr(pos, chunkSize));
        pos += chunkSize + 2; // skip data + CRLF
    }
}

/* ================= Utils ================= */

std::string HttpRequest::trim(const std::string& s) const {
    size_t start = 0;
    while (start < s.size() && std::isspace(s[start]))
        start++;
    size_t end = s.size();
    while (end > start && std::isspace(s[end - 1]))
        end--;
    return s.substr(start, end - start);
}

std::string HttpRequest::toLower(const std::string& s) const {
    std::string r;
    for (size_t i = 0; i < s.size(); i++)
        r += std::tolower(s[i]);
    return r;
}

/* ================= Getters ================= */

bool HttpRequest::isComplete() const { return complete; }
bool HttpRequest::hasError() const { return error; }
bool HttpRequest::isTimedOut() const { return errorType == ERROR_TIMEOUT; }

const std::string& HttpRequest::getMethod() const { return method; }
const std::string& HttpRequest::getURI() const { return uri; }
const std::string& HttpRequest::getVersion() const { return version; }
const std::map<std::string, std::string>& HttpRequest::getHeaders() const { return headers; }
const std::string& HttpRequest::getBody() const { return body; }
size_t HttpRequest::getConsumedLength() const { return consumedLength; }

HttpRequest::ErrorType HttpRequest::getErrorType() const { return errorType; }

std::string HttpRequest::getErrorMessage() const {
    switch (errorType) {
        case ERROR_REQUEST_TOO_LARGE: return "request too large";
        case ERROR_HEADER_TOO_LARGE: return "header too large";
        case ERROR_LINE_TOO_LONG: return "line too long";
        case ERROR_INVALID_REQUEST_LINE: return "invalid request line";
        case ERROR_INVALID_HEADER: return "invalid header";
        case ERROR_TIMEOUT: return "request timeout";
        case ERROR_MALFORMED_CHUNKED: return "malformed chunked body";
        case ERROR_NONE:
        default:
            return "";
    }
}

void HttpRequest::updateLastActivity() { lastActivityTime = std::time(NULL); }

bool HttpRequest::checkTimeout(int timeoutSeconds) const {
    return (std::time(NULL) - lastActivityTime) > timeoutSeconds;
}

void HttpRequest::reset() {
    rawBuffer.clear();
    method.clear();
    uri.clear();
    version.clear();
    headers.clear();
    body.clear();
    headersParsed = false;
    bodyParsed = false;
    complete = false;
    error = false;
    errorType = ERROR_NONE;
    contentLength = 0;
    chunked = false;
    bodyStart = 0;
    consumedLength = 0;
    lastActivityTime = std::time(NULL);
}

void HttpRequest::setError(ErrorType type) {
    error = true;
    errorType = type;
}
