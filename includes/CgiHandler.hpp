/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12 00:58:45 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/12 00:58:48 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "RequestHandler.hpp"
#include <string>
#include <map>
#include <sys/types.h>

// CGI 실행을 담당하는 핸들러
// RFC 3147 (CGI 1.1) 기반
class CgiHandler : public RequestHandler {
public:
    CgiHandler();
    virtual ~CgiHandler();

    virtual HttpResponse handle(const HttpRequest& request,
                                const LocationConfig& location);
    bool prepare(const HttpRequest& request,
                 const LocationConfig& location,
                 std::string& scriptPath,
                 std::string& interpreter,
                 std::map<std::string, std::string>& env,
                 HttpResponse& errorResponse) const;
    pid_t spawn(const std::string& scriptPath,
                const std::string& interpreter,
                const std::map<std::string, std::string>& env,
                int& stdinFd,
                int& stdoutFd) const;
    HttpResponse buildResponse(const std::string& output) const;

private:
    // CGI 환경 변수 생성
    std::map<std::string, std::string> buildEnv(
        const HttpRequest& request,
        const LocationConfig& location,
        const std::string& scriptPath) const;

    // CGI 스크립트 실행 및 출력 캡처
    std::string executeCgi(
        const std::string& scriptPath,
        const std::string& interpreter,
        const std::map<std::string, std::string>& env,
        const std::string& body) const;

    // CGI 출력 파싱 (헤더와 바디 분리)
    void parseCgiOutput(const std::string& output,
                        std::map<std::string, std::string>& headers,
                        std::string& body) const;

    // 파일 확장자로 인터프리터 결정
    std::string getInterpreter(const std::string& path,
                              const LocationConfig& location) const;

    // 실제 스크립트 파일 경로 빌드
    std::string buildScriptPath(const std::string& uri,
                                const LocationConfig& location) const;

    // 타임아웃 처리
    bool waitWithTimeout(pid_t pid, int& status, int timeoutSec) const;

    std::string trim(const std::string& s) const;
};

#endif
