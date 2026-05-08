/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/20 16:24:57 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/17 01:03:04 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <map>
#include <ctime>

// 향상된 HTTP 요청 파서
// - 요청 크기 제한
// - 타임아웃 추적
// - 더 나은 에러 처리
// - 악의적인 요청 방어
class HttpRequest {
public:
    HttpRequest();

    // 소켓에서 읽은 raw data를 누적
    bool appendData(const std::string& data);

    // 상태 확인
    bool isComplete() const;
    bool hasError() const;
    bool isTimedOut() const;
    
    // 에러 상세 정보
    enum ErrorType {
        ERROR_NONE = 0,
        ERROR_REQUEST_TOO_LARGE,
        ERROR_HEADER_TOO_LARGE,
        ERROR_LINE_TOO_LONG,
        ERROR_INVALID_REQUEST_LINE,
        ERROR_INVALID_HEADER,
        ERROR_TIMEOUT,
        ERROR_MALFORMED_CHUNKED
    };
    
    ErrorType getErrorType() const;
    std::string getErrorMessage() const;

    // Getters
    const std::string& getMethod() const;
    const std::string& getURI() const;
    const std::string& getVersion() const;
    const std::map<std::string, std::string>& getHeaders() const;
    const std::string& getBody() const;

    // CHECK) getter 추가
    size_t  getConsumedLength() const;
    
    // 타임아웃 체크 (마지막 데이터 수신 시각 기준)
    void updateLastActivity();
    bool checkTimeout(int timeoutSeconds) const;
    
    // 리셋 (Keep-Alive 연결 재사용 시)
    void reset();

private:
    void parseRequestLine(const std::string& line);
    void parseHeaders(const std::string& block);
    bool parseBody();
    bool parseChunkedBody();
    
    // 검증 메서드
    bool validateRequestLine();
    bool validateHeaders();
    bool validateMethod();
    bool validateURI();
    bool validateVersion();
    
    // 제한 체크
    bool checkRequestSize();
    bool checkHeaderSize();
    bool checkLineLength(const std::string& line);
    
    // 유틸리티
    std::string trim(const std::string& s) const;
    std::string toLower(const std::string& s) const;
    void setError(ErrorType type);

private:
    // 요청 데이터
    std::string rawBuffer;
    std::string method;
    std::string uri;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;

    // 상태 플래그
    bool headersParsed;
    bool bodyParsed;
    bool complete;
    bool error;
    ErrorType errorType;

    // 크기 정보
    size_t contentLength;
    bool chunked;
    size_t bodyStart;
    
    // 타임아웃 추적
    time_t lastActivityTime;
    
    // 보안 제한 (설정 가능하도록 static const로)
    static const size_t MAX_REQUEST_SIZE = 10 * 1024 * 1024;  // 10MB
    static const size_t MAX_HEADER_SIZE = 8 * 1024;            // 8KB
    static const size_t MAX_URI_LENGTH = 2048;                 // 2KB
    static const size_t MAX_LINE_LENGTH = 8192;                // 8KB
    static const size_t MAX_HEADERS_COUNT = 100;               // 100개

    // CHECK)
    // ex) rawBuffer에 [요청1][요청2] 있다고 가정
    // 요청1 길이 = 52 bytes : consumedLength = 52
    // server에서 in.erase(0, 52); -> 다음 버퍼(요청 2)를 읽기 위해
    // [요청2]만 남음
    // 이 작업을 위해 멤버변수 생성
    size_t consumedLength;
};

#endif
