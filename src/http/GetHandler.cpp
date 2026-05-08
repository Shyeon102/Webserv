/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   GetHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/25 16:31:09 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/13 16:03:39 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "GetHandler.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <limits>

/* ================= Main ================= */

HttpResponse GETHandler::handle(const HttpRequest& request,
                                const LocationConfig& location) {
    HttpResponse response;

    std::string path = buildPath(request.getURI(), location);
    
    // Path Traversal 검증 실패
    if (path.empty()) {
        response.setStatus(403);
        response.setBody("<h1>403 Forbidden</h1>");
        return response;
    }

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        response.setStatus(404);
        response.setBody("<h1>404 Not Found</h1>");
        return response;
    }

    // 디렉토리인 경우
    if (S_ISDIR(st.st_mode)) {
        return handleDirectory(path, request.getURI(), location);
    }

    // 일반 파일인 경우
    return handleFile(path, st);
}

/* ================= Directory Handling ================= */

HttpResponse GETHandler::handleDirectory(const std::string& path,
                                        const std::string& uri,
                                        const LocationConfig& location) {
    HttpResponse response;
    const std::vector<std::string> emptyIndex;
    const std::vector<std::string>& indexFiles =
        location.hasIndex() ? location.getIndex() : emptyIndex;

    // location.indexFiles 순회하며 존재하는 첫 index 파일 찾기
    for (size_t i = 0; i < indexFiles.size(); ++i) {
        std::string indexPath = path;
        if (indexPath[indexPath.size() - 1] != '/')
            indexPath += "/";
        indexPath += indexFiles[i];

        struct stat st;
        if (stat(indexPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return handleFile(indexPath, st);
        }
    }

    // index 파일이 없고 autoindex가 활성화되어 있으면
    if (location.hasAutoindex() && location.getAutoindex()) {
        std::string body = generateAutoIndex(path, uri);
        response.setBody(body);
        response.setContentType("text/html");
        return response;
    }

    // index도 없고 autoindex도 비활성화
    response.setStatus(403);
    response.setBody("<h1>403 Forbidden</h1>");
    return response;
}

/* ================= File Handling ================= */

HttpResponse GETHandler::handleFile(const std::string& path,
                                   const struct stat& st) {
    HttpResponse response;

    // 파일 크기 제한 (예: 100MB)
    const size_t MAX_FILE_SIZE = 100 * 1024 * 1024;
    if (static_cast<size_t>(st.st_size) > MAX_FILE_SIZE) {
        response.setStatus(413);
        response.setBody("<h1>413 Payload Too Large</h1>");
        return response;
    }

    // 파일 읽기
    std::string body = readFile(path);
    if (body.empty() && st.st_size > 0) {
        // 파일이 존재하지만 읽을 수 없음 (권한 문제)
        response.setStatus(403);
        response.setBody("<h1>403 Forbidden</h1>");
        return response;
    }

    response.setBody(body);
    response.setContentType(getMimeType(path));
    
    // Last-Modified 헤더 추가 (캐싱 지원)
    char timeBuf[100];
    struct tm* tm = gmtime(&st.st_mtime);
    strftime(timeBuf, sizeof(timeBuf), "%a, %d %b %Y %H:%M:%S GMT", tm);
    response.setHeader("Last-Modified", std::string(timeBuf));
    
    return response;
}

/* ================= Utils ================= */

std::string GETHandler::buildPath(const std::string& uri,
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
    
    // Fragment 제거
    size_t fragPos = rel.find('#');
    if (fragPos != std::string::npos)
        rel = rel.substr(0, fragPos);
    
    // URL 디코딩
    rel = urlDecode(rel);
    
    // Path Traversal 강화 검증
    if (!isPathSafe(rel))
        return "";
    
    std::string result = location.hasRoot() ? location.getRoot() : "";
    if (!result.empty() && result[result.size() - 1] != '/')
        result += "/";
    if (!rel.empty())
        result += rel;
    
    return result;
}

bool GETHandler::isPathSafe(const std::string& path) const {
    // ".." 체크
    if (path.find("..") != std::string::npos)
        return false;
    
    // 절대 경로 시도 방지
    if (!path.empty() && path[0] == '/')
        return false;
    
    // null byte 체크
    if (path.find('\0') != std::string::npos)
        return false;
    
    // 연속된 슬래시 정규화
    if (path.find("//") != std::string::npos)
        return false;
    
    return true;
}

std::string GETHandler::urlDecode(const std::string& s) const {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            char* end;
            long val = strtol(hex, &end, 16);
            if (*end == '\0') {
                result += static_cast<char>(val);
                i += 2;
            } else {
                result += s[i];
            }
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string GETHandler::readFile(const std::string& path) const {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open() || !file.good())
        return "";
    
    // 파일 끝으로 이동해서 크기 확인
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    if (size < 0)
        return "";
    
    // 버퍼 할당 및 읽기
    std::string content;
    content.reserve(static_cast<size_t>(size));
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        content.append(buffer, file.gcount());
    }
    
    return content;
}

/* ================= Autoindex ================= */

std::string GETHandler::generateAutoIndex(const std::string& path,
                                          const std::string& uri) const {
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return "<h1>403 Forbidden</h1>";

    std::ostringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html>\n<head>\n";
    html << "<meta charset=\"UTF-8\">\n";
    html << "<title>Index of " << uri << "</title>\n";
    html << "<style>\n";
    html << "body { font-family: monospace; padding: 20px; }\n";
    html << "table { border-collapse: collapse; width: 100%; }\n";
    html << "th, td { text-align: left; padding: 8px; border-bottom: 1px solid #ddd; }\n";
    html << "a { text-decoration: none; color: #0066cc; }\n";
    html << "a:hover { text-decoration: underline; }\n";
    html << "</style>\n";
    html << "</head>\n<body>\n";
    html << "<h1>Index of " << uri << "</h1>\n";
    html << "<table>\n";
    html << "<tr><th>Name</th><th>Size</th><th>Modified</th></tr>\n";

    // 상위 디렉토리 링크
    if (uri != "/" && !uri.empty()) {
        html << "<tr><td><a href=\"..\">..</a></td><td>-</td><td>-</td></tr>\n";
    }

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        std::string name(entry->d_name);
        if (name == ".")
            continue;

        std::string fullPath = path;
        if (fullPath[fullPath.size() - 1] != '/')
            fullPath += "/";
        fullPath += name;

        struct stat st;
        std::string size = "-";
        std::string modified = "-";

        if (stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                name += "/";
            } else {
                // 파일 크기
                std::ostringstream oss;
                if (st.st_size < 1024)
                    oss << st.st_size << "B";
                else if (st.st_size < 1024 * 1024)
                    oss << (st.st_size / 1024) << "K";
                else
                    oss << (st.st_size / (1024 * 1024)) << "M";
                size = oss.str();
            }

            // 수정 시간
            char timeBuf[100];
            struct tm* tm = localtime(&st.st_mtime);
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", tm);
            modified = timeBuf;
        }

        std::string href = uri;
        if (href[href.size() - 1] != '/')
            href += "/";
        href += name;

        html << "<tr><td><a href=\"" << href << "\">" << name << "</a></td>";
        html << "<td>" << size << "</td>";
        html << "<td>" << modified << "</td></tr>\n";
    }

    html << "</table>\n</body>\n</html>";
    closedir(dir);
    return html.str();
}

/* ================= MIME ================= */

std::string GETHandler::getMimeType(const std::string& path) const {
    size_t dot = path.find_last_of(".");
    if (dot == std::string::npos)
        return "application/octet-stream";

    std::string ext = path.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); i++)
        ext[i] = std::tolower(static_cast<unsigned char>(ext[i]));

    // 일반 웹 파일
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "xml") return "application/xml";
    if (ext == "txt") return "text/plain";
    
    // 이미지
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "webp") return "image/webp";
    
    // 폰트
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf") return "font/ttf";
    if (ext == "otf") return "font/otf";
    
    // 비디오/오디오
    if (ext == "mp4") return "video/mp4";
    if (ext == "webm") return "video/webm";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "ogg") return "audio/ogg";
    
    // 문서
    if (ext == "pdf") return "application/pdf";
    if (ext == "zip") return "application/zip";
    if (ext == "tar") return "application/x-tar";
    if (ext == "gz") return "application/gzip";

    return "application/octet-stream";
}
