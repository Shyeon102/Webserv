/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CgiHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12 00:59:22 by jaoh              #+#    #+#             */
/*   Updated: 2026/06/12 03:31:59 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "CgiHandler.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <sstream>
#include <cctype>
#include <signal.h>
#include <vector>
#include <limits.h>

CgiHandler::CgiHandler() {}
CgiHandler::~CgiHandler() {}

static std::string makeAbsolutePath(const std::string& path) {
    if (path.empty() || path[0] == '/')
        return path;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        return path;

    std::string absolute(cwd);
    if (!absolute.empty() && absolute[absolute.size() - 1] != '/')
        absolute += "/";
    absolute += path;
    return absolute;
}

static std::string stripQueryString(const std::string& uri) {
    size_t qpos = uri.find('?');
    if (qpos == std::string::npos)
        return uri;
    return uri.substr(0, qpos);
}

static void setFdNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ================= Main Handler ================= */

bool CgiHandler::prepare(const HttpRequest& request,
                         const LocationConfig& location,
                         std::string& scriptPath,
                         std::string& interpreter,
                         std::map<std::string, std::string>& env,
                         HttpResponse& errorResponse) const {
    scriptPath = buildScriptPath(request.getURI(), location);
    if (scriptPath.empty()) {
        errorResponse.setStatus(403);
        errorResponse.setBody("<h1>403 Forbidden</h1>");
        return false;
    }

    scriptPath = makeAbsolutePath(scriptPath);

    interpreter = getInterpreter(scriptPath, location);
    if (interpreter.empty()) {
        errorResponse.setStatus(500);
        errorResponse.setBody("<h1>500 Internal Server Error</h1><p>No CGI interpreter configured</p>");
        return false;
    }
    interpreter = makeAbsolutePath(interpreter);

    env = buildEnv(request, location, scriptPath);
    return true;
}

pid_t CgiHandler::spawn(const std::string& scriptPath,
                        const std::string& interpreter,
                        const std::map<std::string, std::string>& env,
                        int& stdinFd,
                        int& stdoutFd) const {
    int pipeIn[2];
    int pipeOut[2];

    if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
        throw std::runtime_error("pipe failed");

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeIn[0]); close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        dup2(pipeIn[0], STDIN_FILENO);
        dup2(pipeOut[1], STDOUT_FILENO);

        close(pipeIn[0]); close(pipeIn[1]);
        close(pipeOut[0]); close(pipeOut[1]);

        std::vector<std::string> envStrings;
        std::vector<char*> envp;
        envStrings.reserve(env.size());
        envp.reserve(env.size() + 1);
        for (std::map<std::string, std::string>::const_iterator it = env.begin();
             it != env.end(); ++it) {
            envStrings.push_back(it->first + "=" + it->second);
        }
        for (size_t i = 0; i < envStrings.size(); ++i)
            envp.push_back(const_cast<char*>(envStrings[i].c_str()));
        envp.push_back(NULL);

        std::string childScriptPath = scriptPath;
        size_t lastSlash = scriptPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            std::string dir = scriptPath.substr(0, lastSlash);
            if (chdir(dir.c_str()) < 0)
                exit(1);
            childScriptPath = scriptPath.substr(lastSlash + 1);
        }

        char* argv[3];
        argv[0] = const_cast<char*>(interpreter.c_str());
        argv[1] = const_cast<char*>(childScriptPath.c_str());
        argv[2] = NULL;
        execve(interpreter.c_str(), argv, &envp[0]);
        exit(1);
    }

    close(pipeIn[0]);
    close(pipeOut[1]);
    setFdNonBlocking(pipeIn[1]);
    setFdNonBlocking(pipeOut[0]);
    signal(SIGPIPE, SIG_IGN);

    stdinFd = pipeIn[1];
    stdoutFd = pipeOut[0];
    return pid;
}

HttpResponse CgiHandler::buildResponse(const std::string& output) const {
    HttpResponse response;

    std::map<std::string, std::string> cgiHeaders;
    std::string body;
    parseCgiOutput(output, cgiHeaders, body);

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
    const std::string uriPath = stripQueryString(request.getURI());
    env["SCRIPT_NAME"] = uriPath;
    env["SCRIPT_FILENAME"] = scriptPath;
    env["REQUEST_URI"] = request.getURI();
    env["REDIRECT_STATUS"] = "200";

    if (location.hasRoot())
        env["DOCUMENT_ROOT"] = makeAbsolutePath(location.getRoot());
    else
        env["DOCUMENT_ROOT"] = makeAbsolutePath(".");
    
    // The 42 cgi_tester validates that PATH_INFO matches the requested URL
    // path.  Do not strip the matched location prefix here.
    env["PATH_INFO"] = uriPath;
    env["PATH_TRANSLATED"] = scriptPath;

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

std::string CgiHandler::trim(const std::string& s) const {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        start++;
    
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        end--;
    
    return s.substr(start, end - start);
}
