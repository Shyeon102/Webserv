/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   PostHandler.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/03 17:53:03 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/13 15:58:04 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "PostHandler.hpp"
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctime>

POSTHandler::POSTHandler(size_t maxBodySizeBytes) : maxBodySize(maxBodySizeBytes) {}

HttpResponse POSTHandler::handle(const HttpRequest& request,
                                 const LocationConfig& location) {
    HttpResponse res;

    // body size limit 체크 (413)
    if (!checkBodySize(request, res))
        return res;

    // Content-Type 분기
    std::string ct = "";
    const std::map<std::string,std::string>& h = request.getHeaders();
    std::map<std::string,std::string>::const_iterator it = h.find("content-type");
    if (it != h.end()) ct = it->second;

    std::string lower = toLower(ct);

    // multipart/form-data; boundary=----WebKitFormBoundary...
    if (lower.find("multipart/form-data") != std::string::npos) {
        size_t bpos = lower.find("boundary=");
        if (bpos == std::string::npos) {
            res.setStatus(400);
            res.setBody("<h1>400 Bad Request</h1>");
            return res;
        }
        std::string boundary = ct.substr(bpos + 9);
        boundary = trim(boundary);
        if (boundary.size() >= 2 && boundary[0] == '"' && boundary[boundary.size()-1] == '"')
            boundary = boundary.substr(1, boundary.size()-2);

        return handleMultipart(request, location, boundary);
    }

    // application/x-www-form-urlencoded
    if (lower.find("application/x-www-form-urlencoded") != std::string::npos) {
        return handleFormUrlEncoded(request, location);
    }

    // 그 외: raw body
    res.setStatus(200);
    res.setContentType("text/plain");
    res.setBody(request.getBody());
    return res;
}

bool POSTHandler::checkBodySize(const HttpRequest& request, HttpResponse& res) const {
    if (maxBodySize == 0) return true;
    if (request.getBody().size() > maxBodySize) {
        res.setStatus(413);
        res.setBody("<h1>413 Payload Too Large</h1>");
        return false;
    }
    return true;
}

/* ---------------- urlencoded ---------------- */

HttpResponse POSTHandler::handleFormUrlEncoded(const HttpRequest& request,
                                               const LocationConfig& location) {
    (void)location;
    HttpResponse res;

    std::map<std::string,std::string> kv = parseUrlEncoded(request.getBody());

    std::ostringstream oss;
    oss << "Parsed x-www-form-urlencoded:\n";
    for (std::map<std::string,std::string>::iterator it = kv.begin(); it != kv.end(); ++it)
        oss << it->first << " = " << it->second << "\n";

    res.setStatus(200);
    res.setContentType("text/plain");
    res.setBody(oss.str());
    return res;
}

std::map<std::string,std::string> POSTHandler::parseUrlEncoded(const std::string& body) const {
    std::map<std::string,std::string> out;
    size_t i = 0;
    while (i < body.size()) {
        size_t amp = body.find('&', i);
        std::string pair = (amp == std::string::npos) ? body.substr(i) : body.substr(i, amp - i);

        size_t eq = pair.find('=');
        std::string k = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        std::string v = (eq == std::string::npos) ? "" : pair.substr(eq + 1);

        out[urlDecode(k)] = urlDecode(v);

        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return out;
}

std::string POSTHandler::urlDecode(const std::string& s) const {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            out += static_cast<char>(std::strtoul(hex, 0, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

/* ---------------- multipart ---------------- */

HttpResponse POSTHandler::handleMultipart(const HttpRequest& request,
                                          const LocationConfig& location,
                                          const std::string& boundary) {
    HttpResponse res;

    // 업로드가 비활성화되어 있으면 403
    if (!location.hasUploadStore()) {
        res.setStatus(403);
        res.setBody("<h1>403 Forbidden</h1><p>File upload not allowed</p>");
        return res;
    }

    std::vector< std::map<std::string,std::string> > parts;
    if (!parseMultipart(request.getBody(), boundary, parts)) {
        res.setStatus(400);
        res.setBody("<h1>400 Bad Request</h1><p>Malformed multipart</p>");
        return res;
    }

    // 업로드 디렉토리 확인
    std::string uploadDir = location.getUploadStore();
    if (uploadDir.empty()) {
        res.setStatus(500);
        res.setBody("<h1>500 Internal Server Error</h1><p>Upload directory not configured</p>");
        return res;
    }

    struct stat st;
    if (stat(uploadDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        res.setStatus(500);
        res.setBody("<h1>500 Internal Server Error</h1><p>Upload directory not available</p>");
        return res;
    }

    std::ostringstream result;
    int filesUploaded = 0;

    // parts 순회하며 파일 저장
    for (size_t i = 0; i < parts.size(); ++i) {
        std::map<std::string,std::string>& part = parts[i];
        
        // filename이 있으면 파일 part
        if (part.count("filename") && !part["filename"].empty()) {
            std::string originalName = part["filename"];
            std::string safeName = sanitizeFilename(originalName);
            
            if (safeName.empty()) {
                result << "Skipped invalid filename: " << originalName << "\n";
                continue;
            }

            // 중복 방지를 위해 타임스탬프 추가
            std::string finalName = generateUniqueFilename(uploadDir, safeName);
            std::string fullPath = uploadDir;
            if (fullPath[fullPath.size() - 1] != '/')
                fullPath += "/";
            fullPath += finalName;

            // Path traversal 최종 검증
            if (!isPathSafe(fullPath, uploadDir)) {
                result << "Rejected unsafe path: " << originalName << "\n";
                continue;
            }

            // 파일 저장
            if (saveFile(fullPath, part["data"])) {
                result << "Uploaded: " << finalName << " (" << part["data"].size() << " bytes)\n";
                filesUploaded++;
            } else {
                result << "Failed to save: " << originalName << "\n";
            }
        } else {
            // 일반 필드
            if (part.count("name")) {
                result << "Field '" << part["name"] << "': ";
                result << part["data"].substr(0, 100);
                if (part["data"].size() > 100) result << "...";
                result << "\n";
            }
        }
    }

    if (filesUploaded > 0) {
        res.setStatus(201); // Created
        res.setContentType("text/plain");
        res.setBody(result.str());
    } else {
        res.setStatus(200);
        res.setContentType("text/plain");
        res.setBody(result.str());
    }

    return res;
}

/* ---------------- 파일 저장 & 보안 ---------------- */

bool POSTHandler::saveFile(const std::string& path, const std::string& data) const {
    std::ofstream ofs(path.c_str(), std::ios::binary);
    if (!ofs.is_open())
        return false;
    
    ofs.write(data.c_str(), data.size());
    ofs.close();
    
    return ofs.good();
}

std::string POSTHandler::sanitizeFilename(const std::string& name) const {
    std::string safe;
    
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        
        // 허용: 알파벳, 숫자, 점, 언더스코어, 하이픈
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == '.' || c == '_' || c == '-') {
            safe += c;
        } else {
            safe += '_'; // 특수문자는 언더스코어로 치환
        }
    }
    
    // ".." 제거 (path traversal 방지)
    size_t pos;
    while ((pos = safe.find("..")) != std::string::npos) {
        safe.erase(pos, 2);
    }
    
    // 앞뒤 공백/점 제거
    while (!safe.empty() && (safe[0] == '.' || safe[0] == ' '))
        safe.erase(0, 1);
    while (!safe.empty() && (safe[safe.size()-1] == '.' || safe[safe.size()-1] == ' '))
        safe.erase(safe.size()-1);
    
    // 최대 길이 제한 (255자)
    if (safe.size() > 255)
        safe = safe.substr(0, 255);
    
    return safe;
}

std::string POSTHandler::generateUniqueFilename(const std::string& dir,
                                               const std::string& name) const {
    std::string testPath = dir;
    if (testPath[testPath.size() - 1] != '/')
        testPath += "/";
    testPath += name;
    
    // 파일이 없으면 그대로 사용
    struct stat st;
    if (stat(testPath.c_str(), &st) != 0)
        return name;
    
    // 중복이면 타임스탬프 추가
    std::ostringstream oss;
    oss << std::time(NULL) << "_" << name;
    return oss.str();
}

bool POSTHandler::isPathSafe(const std::string& fullPath,
                             const std::string& baseDir) const {
    // Canonical path 비교를 통한 검증
    // fullPath가 baseDir로 시작하는지 확인
    if (fullPath.compare(0, baseDir.size(), baseDir) != 0)
        return false;
    
    // ".." 가 있으면 거부
    if (fullPath.find("..") != std::string::npos)
        return false;
    
    return true;
}

/* ---------------- multipart 파싱 ---------------- */

bool POSTHandler::parseMultipart(const std::string& body,
                                 const std::string& boundary,
                                 std::vector< std::map<std::string,std::string> >& parts) const {
    std::string delim = "--" + boundary;
    std::string close = "--" + boundary + "--";

    size_t pos = 0;

    size_t first = body.find(delim, pos);
    if (first == std::string::npos) return false;
    pos = first;

    while (true) {
        if (body.compare(pos, close.size(), close) == 0)
            return true;

        if (body.compare(pos, delim.size(), delim) != 0)
            return false;

        pos += delim.size();

        if (pos + 2 > body.size() || body.substr(pos, 2) != "\r\n")
            return false;
        pos += 2;

        size_t hdrEnd = body.find("\r\n\r\n", pos);
        if (hdrEnd == std::string::npos) return false;

        std::string hdrBlock = body.substr(pos, hdrEnd - pos);
        pos = hdrEnd + 4;

        size_t next = body.find("\r\n" + delim, pos);
        if (next == std::string::npos) {
            next = body.find("\r\n" + close, pos);
            if (next == std::string::npos) return false;
        }

        std::string data = body.substr(pos, next - pos);
        pos = next + 2;

        std::map<std::string,std::string> part;
        part["data"] = data;

        std::istringstream iss(hdrBlock);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line[line.size()-1] == '\r')
                line.erase(line.size()-1);
            size_t c = line.find(':');
            if (c == std::string::npos) continue;

            std::string k = toLower(trim(line.substr(0,c)));
            std::string v = trim(line.substr(c+1));
            part[k] = v;

            if (k == "content-disposition") {
                part["disposition"] = v;
                size_t npos = v.find("name=");
                if (npos != std::string::npos) {
                    size_t start = v.find('"', npos);
                    size_t end = (start!=std::string::npos)? v.find('"', start+1) : std::string::npos;
                    if (start!=std::string::npos && end!=std::string::npos)
                        part["name"] = v.substr(start+1, end-start-1);
                }
                size_t fpos = v.find("filename=");
                if (fpos != std::string::npos) {
                    size_t start = v.find('"', fpos);
                    size_t end = (start!=std::string::npos)? v.find('"', start+1) : std::string::npos;
                    if (start!=std::string::npos && end!=std::string::npos)
                        part["filename"] = v.substr(start+1, end-start-1);
                }
            }
        }

        parts.push_back(part);

        if (body.compare(pos, close.size(), close) == 0)
            return true;
    }
}

/* ---------------- utils ---------------- */

std::string POSTHandler::trim(const std::string& s) const {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) b--;
    return s.substr(a, b-a);
}

std::string POSTHandler::toLower(const std::string& s) const {
    std::string r;
    for (size_t i=0;i<s.size();++i) r += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return r;
}
