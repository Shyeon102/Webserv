/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12 00:59:22 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/12 00:59:33 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "CgiHandler.hpp"
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cctype>
#include <iostream>
#include <errno.h>
#include <signal.h>
#include <vector>

CgiHandler::CgiHandler() {}
CgiHandler::~CgiHandler() {}

/* ================= Main Handler ================= */

HttpResponse CgiHandler::handle(const HttpRequest& request,
                                const LocationConfig& location) {
    HttpResponse response;

    // 1. 스크립트 경로 빌드
    std::string scriptPath = buildScriptPath(request.getURI(), location);
    if (scriptPath.empty()) {
        response.setStatus(403);
        response.setBody("<h1>403 Forbidden</h1>");
        return response;
    }

    // 2. 파일 존재 및 실행 권한 확인
    struct stat st;
    if (stat(scriptPath.c_str(), &st) != 0) {
        response.setStatus(404);
        response.setBody("<h1>404 Not Found</h1>");
        return response;
    }

    // 3. 인터프리터 결정 (php-cgi, python 등)
    std::string interpreter = getInterpreter(scriptPath, location);
    if (interpreter.empty()) {
        response.setStatus(500);
        response.setBody("<h1>500 Internal Server Error</h1><p>No CGI interpreter configured</p>");
        return response;
    }

    // 4. CGI 환경 변수 준비
    std::map<std::string, std::string> env = buildEnv(request, location, scriptPath);

    // 5. CGI 실행
    std::string output;
    try {
        output = executeCgi(scriptPath, interpreter, env, request.getBody());
    } catch (const std::exception&) {
        response.setStatus(502);
        response.setBody("<h1>502 Bad Gateway</h1><p>CGI execution failed</p>");
        return response;
    }

    // 6. CGI 출력 파싱
    std::map<std::string, std::string> cgiHeaders;
    std::string body;
    parseCgiOutput(output, cgiHeaders, body);

    // 7. 응답 구성
    // Status 헤더가 있으면 사용, 없으면 200
    if (cgiHeaders.count("status")) {
        int code = std::atoi(cgiHeaders["status"].c_str());
        response.setStatus(code > 0 ? code : 200);
    } else {
        response.setStatus(200);
    }

    // CGI 헤더를 응답에 복사
    for (std::map<std::string, std::string>::iterator it = cgiHeaders.begin();
         it != cgiHeaders.end(); ++it) {
        if (it->first != "status") {
            response.setHeader(it->first, it->second);
        }
    }

    response.setBody(body);
    return response;
}

/* ================= CGI 환경 변수 ================= */

std::map<std::string, std::string> CgiHandler::buildEnv(
    const HttpRequest& request,
    const LocationConfig& location,
    const std::string& scriptPath) const {
    
    std::map<std::string, std::string> env;
    const std::map<std::string, std::string>& headers = request.getHeaders();

    // RFC 3875 - CGI/1.1 필수 메타변수
    env["GATEWAY_INTERFACE"] = "CGI/1.1";
    env["SERVER_PROTOCOL"] = request.getVersion();
    env["SERVER_SOFTWARE"] = "webserv/1.0";
    env["REQUEST_METHOD"] = request.getMethod();
    env["SCRIPT_NAME"] = request.getURI();
    env["SCRIPT_FILENAME"] = scriptPath;
    
    // PATH_INFO와 PATH_TRANSLATED 설정
    std::string pathInfo = request.getURI();
    const std::string& locPath = location.getPath();
    size_t scriptNamePos = pathInfo.find(locPath);
    if (scriptNamePos != std::string::npos) {
        pathInfo = pathInfo.substr(scriptNamePos + locPath.size());
    }
    env["PATH_INFO"] = pathInfo;
    env["PATH_TRANSLATED"] = (location.hasRoot() ? location.getRoot() : "") + pathInfo;

    // Query string (URI에 ? 이후)
    size_t qPos = request.getURI().find('?');
    if (qPos != std::string::npos) {
        env["QUERY_STRING"] = request.getURI().substr(qPos + 1);
    } else {
        env["QUERY_STRING"] = "";
    }

    // Content 관련
    if (headers.count("content-type"))
        env["CONTENT_TYPE"] = headers.find("content-type")->second;
    if (headers.count("content-length"))
        env["CONTENT_LENGTH"] = headers.find("content-length")->second;

    // HTTP 헤더를 HTTP_* 형식으로 변환
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        std::string key = "HTTP_";
        for (size_t i = 0; i < it->first.size(); ++i) {
            char c = it->first[i];
            if (c == '-')
                key += '_';
            else
                key += std::toupper(static_cast<unsigned char>(c));
        }
        env[key] = it->second;
    }

    // 서버 정보 (실제 구현에서는 ServerConfig에서 가져와야 함)
    env["SERVER_NAME"] = "localhost";
    env["SERVER_PORT"] = "8080";
    
    // Remote 정보 (실제로는 클라이언트 소켓 정보에서)
    env["REMOTE_ADDR"] = "127.0.0.1";

    return env;
}

/* ================= CGI 실행 ================= */

std::string CgiHandler::executeCgi(
    const std::string& scriptPath,
    const std::string& interpreter,
    const std::map<std::string, std::string>& env,
    const std::string& body) const {

    int pipeIn[2];   // 부모 -> 자식 (stdin)
    int pipeOut[2];  // 자식 -> 부모 (stdout)

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
        throw std::runtime_error("pipe failed");

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeIn[0]); close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        // 자식 프로세스: CGI 스크립트 실행
        
        // stdin/stdout 리다이렉트
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);
        
        close(pipeIn[0]); close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);

        // 환경 변수 구성 (execve envp)
        std::vector<std::string> envStrings;
        std::vector<char*> envp;
        envStrings.reserve(env.size());
        envp.reserve(env.size() + 1);
        for (std::map<std::string, std::string>::const_iterator it = env.begin();
             it != env.end(); ++it) {
            envStrings.push_back(it->first + "=" + it->second);
        }
        for (size_t i = 0; i < envStrings.size(); ++i) {
            envp.push_back(const_cast<char*>(envStrings[i].c_str()));
        }
        envp.push_back(NULL);

        // 스크립트 디렉토리로 chdir
        size_t lastSlash = scriptPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            std::string dir = scriptPath.substr(0, lastSlash);
            if (chdir(dir.c_str()) < 0) {
                std::cerr << "chdir failed: " << strerror(errno) << std::endl;
                exit(1);
            }
        }

        // 실행
        char* argv[3];
        argv[0] = const_cast<char*>(interpreter.c_str());
        argv[1] = const_cast<char*>(scriptPath.c_str());
        argv[2] = NULL;

        execve(interpreter.c_str(), argv, &envp[0]);
        
        // execve 실패 시
        std::cerr << "execve failed: " << strerror(errno) << std::endl;
        exit(1);
    }

    // 부모 프로세스
    close(pipeIn[0]);
    close(pipeOut[1]);

    // POST body를 CGI stdin으로 전송
    if (!body.empty()) {
        ssize_t written = write(pipeIn[1], body.c_str(), body.size());
        (void)written; // 에러 처리는 실제 구현에서 강화
    }
    close(pipeIn[1]);

    // CGI 출력 읽기
    std::string output;
    char buffer[4096];
    ssize_t bytesRead;
    
    while ((bytesRead = read(pipeOut[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, bytesRead);
    }
    close(pipeOut[0]);

    // 자식 프로세스 종료 대기 (타임아웃 처리)
    int status;
    if (!waitWithTimeout(pid, status, 30)) { // 30초 타임아웃
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        throw std::runtime_error("CGI timeout");
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        throw std::runtime_error("CGI script exited with error");
    }

    return output;
}

/* ================= CGI 출력 파싱 ================= */

void CgiHandler::parseCgiOutput(const std::string& output,
                                std::map<std::string, std::string>& headers,
                                std::string& body) const {
    // CGI 출력은 "헤더\r\n\r\n바디" 형식
    size_t pos = output.find("\r\n\r\n");
    if (pos == std::string::npos) {
        // \n\n으로 시도
        pos = output.find("\n\n");
        if (pos == std::string::npos) {
            body = output;
            return;
        }
    }

    std::string headerBlock = output.substr(0, pos);
    body = output.substr(pos + (output[pos] == '\r' ? 4 : 2));

    // 헤더 파싱
    std::istringstream iss(headerBlock);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));

        // 헤더 키를 소문자로 (표준화)
        for (size_t i = 0; i < key.size(); ++i)
            key[i] = std::tolower(static_cast<unsigned char>(key[i]));

        headers[key] = value;
    }
}

/* ================= 유틸리티 ================= */

std::string CgiHandler::buildScriptPath(const std::string& uri,
                                       const LocationConfig& location) const {
    std::string rel = uri;
    const std::string& locPath = location.getPath();
    if (uri.compare(0, locPath.size(), locPath) == 0)
        rel = uri.substr(locPath.size());
    
    if (!rel.empty() && rel[0] == '/')
        rel.erase(0, 1);

    // Query string 제거
    size_t qPos = rel.find('?');
    if (qPos != std::string::npos)
        rel = rel.substr(0, qPos);

    // Path traversal 방어
    if (rel.find("..") != std::string::npos)
        return "";

    std::string result = location.hasRoot() ? location.getRoot() : "";
    if (!result.empty() && result[result.size() - 1] != '/')
        result += "/";
    result += rel;

    return result;
}

std::string CgiHandler::getInterpreter(const std::string& path,
                                      const LocationConfig& location) const {
    // 확장자 추출
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "";

    std::string ext = path.substr(dot);

    if (location.hasCgiPass()) {
        const std::map<std::string, std::string>& pass = location.getCgiPass();
        std::map<std::string, std::string>::const_iterator it = pass.find(ext);
        if (it != pass.end())
            return it->second;
    }
    
    // fallback 하드코딩
    if (ext == ".php")
        return "/usr/bin/php-cgi";
    if (ext == ".py")
        return "/usr/bin/python3";
    if (ext == ".pl")
        return "/usr/bin/perl";

    return "";
}

bool CgiHandler::waitWithTimeout(pid_t pid, int& status, int timeoutSec) const {
    time_t start = time(NULL);
    
    while (time(NULL) - start < timeoutSec) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            return true;
        if (result < 0)
            return false;
        
        usleep(100000); // 100ms
    }
    
    return false;
}

std::string CgiHandler::trim(const std::string& s) const {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        start++;
    
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        end--;
    
    return s.substr(start, end - start);
}
