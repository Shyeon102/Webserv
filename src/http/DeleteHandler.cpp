/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   DeleteHandler.cpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/03 17:53:29 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/12 01:20:00 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "DeleteHandler.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

/* ================= Main Handler ================= */

HttpResponse DELETEHandler::handle(const HttpRequest& request,
                                   const LocationConfig& location) {
    HttpResponse res;

    // 1. 경로 빌드 및 기본 검증
    std::string path = buildPath(request.getURI(), location);
    
    if (path.empty()) {
        res.setStatus(403);
        res.setBody("<h1>403 Forbidden</h1><p>Invalid path</p>");
        return res;
    }

    // 2. 경로 보안 검증
    if (!isPathSafe(path)) {
        res.setStatus(403);
        res.setBody("<h1>403 Forbidden</h1><p>Path traversal attempt detected</p>");
        return res;
    }

    // 3. 파일/디렉토리 존재 확인
    if (!pathExists(path)) {
        res.setStatus(404);
        res.setBody("<h1>404 Not Found</h1><p>Resource does not exist</p>");
        return res;
    }

    // 4. 디렉토리인지 확인 (디렉토리 삭제는 거부)
    if (isDirectory(path)) {
        res.setStatus(403);
        res.setBody("<h1>403 Forbidden</h1><p>Cannot delete directories</p>");
        return res;
    }

    // 5. 일반 파일인지 확인
    if (!isRegularFile(path)) {
        res.setStatus(403);
        res.setBody("<h1>403 Forbidden</h1><p>Can only delete regular files</p>");
        return res;
    }

    // 6. 삭제 가능 여부 확인 (권한 등)
    if (!canDelete(path)) {
        res.setStatus(403);
        res.setBody("<h1>403 Forbidden</h1><p>Permission denied</p>");
        return res;
    }

    res.setStatus(501);
    res.setBody("<h1>501 Not Implemented</h1><p>DELETE is disabled in this build</p>");
    return res;
}

/* ================= Path Building ================= */

std::string DELETEHandler::buildPath(const std::string& uri,
                                     const LocationConfig& location) const {
    std::string rel = uri;
    const std::string& locPath = location.getPath();
    
    // location.path prefix 제거
    if (uri.compare(0, locPath.size(), locPath) == 0)
        rel = uri.substr(locPath.size());
    
    if (!rel.empty() && rel[0] == '/')
        rel.erase(0, 1);
    
    // Query string 제거
    size_t qPos = rel.find('?');
    if (qPos != std::string::npos)
        rel = rel.substr(0, qPos);
    
    // Fragment 제거
    size_t fragPos = rel.find('#');
    if (fragPos != std::string::npos)
        rel = rel.substr(0, fragPos);
    
    // Path traversal 기본 검증
    if (rel.find("..") != std::string::npos)
        return "";
    
    // 절대 경로 시도 방지
    if (!rel.empty() && rel[0] == '/')
        return "";
    
    // 최종 경로 조합
    std::string result = location.hasRoot() ? location.getRoot() : "";
    if (!result.empty() && result[result.size() - 1] != '/')
        result += "/";
    if (!rel.empty())
        result += rel;
    
    return result;
}

/* ================= File Type Checks ================= */

bool DELETEHandler::pathExists(const std::string& path) const {
    struct stat st;
    return (stat(path.c_str(), &st) == 0);
}

bool DELETEHandler::isRegularFile(const std::string& path) const {
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode);
}

bool DELETEHandler::isDirectory(const std::string& path) const {
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

/* ================= Permission & Security ================= */

bool DELETEHandler::canDelete(const std::string& path) const {
    // access() 함수로 쓰기 권한 확인
    // W_OK: 쓰기 권한 (삭제는 부모 디렉토리에 대한 쓰기 권한 필요)
    
    // 1. 파일 자체에 대한 접근 권한 확인
    if (access(path.c_str(), F_OK) != 0)
        return false;
    
    // 2. 부모 디렉토리 쓰기 권한 확인 (실제 삭제 권한)
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos)
        return false;
    
    std::string parentDir = path.substr(0, lastSlash);
    if (parentDir.empty())
        parentDir = "/";
    
    return (access(parentDir.c_str(), W_OK | X_OK) == 0);
}

bool DELETEHandler::isPathSafe(const std::string& path) const {
    if (path.find("..") != std::string::npos)
        return false;
    if (path.find('\0') != std::string::npos)
        return false;
    if (path.find("//") != std::string::npos)
        return false;
    return true;
}

/* ================= Delete Operation ================= */

bool DELETEHandler::deleteFile(const std::string& path) const {
    (void)path;
    return false;
}
