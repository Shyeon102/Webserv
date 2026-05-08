/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/20 16:27:19 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/13 12:28:32 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>
#include <map>

// 향상된 HTTP 응답 클래스
// - Keep-Alive 지원
// - Chunked transfer encoding 지원
// - 자동 헤더 관리 (Date, Server, Content-Length 등)
class HttpResponse {
public:
    HttpResponse();

    // 상태 코드 설정
    void setStatus(int code);
    
    // 헤더 설정
    void setHeader(const std::string& key, const std::string& value);
    void setContentType(const std::string& type);
    
    // 바디 설정
    void setBody(const std::string& body);
    
    // Keep-Alive 설정
    // timeout: 초 단위, max: 최대 요청 수
    void setKeepAlive(bool enable, int timeout = 60, int max = 100);
    
    // Chunked transfer encoding 설정
    void setChunked(bool enable);
    
    // 응답 문자열 생성
    std::string toString();
    std::string toChunkedString();
    
    // Keep-Alive 상태 확인
    bool isKeepAlive() const;
    int getStatusCode() const;

private:
    std::string getStatusMessage(int code) const;
    void ensureHeaders();

private:
    int statusCode;
    std::string statusMessage;
    std::map<std::string, std::string> headers;
    std::string body;
    
    bool keepAlive;
    bool chunked;
};

#endif
