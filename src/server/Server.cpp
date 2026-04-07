#include "Server.hpp"
#include "HttpParseResult.hpp"
#include "HttpRequestValidator.hpp"
#include "Router.hpp"
#include "GetHandler.hpp"
#include "PostHandler.hpp"
#include "DeleteHandler.hpp"
#include "CgiHandler.hpp"
#include "ErrorHandler.hpp"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cerrno>
#include <sstream>
#include <cctype>

#include <stdio.h> // for perror but we can change to strerror
// perror를 사용해도 되는지 확인
static void fatal(const char* msg) {
    perror(msg);
    std::exit(1);
}

static std::string stripQueryString(const std::string& uri) {
    size_t qpos = uri.find('?');
    if (qpos == std::string::npos)
        return uri;
    return uri.substr(0, qpos);
}

static std::string toLowerAscii(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}

static bool hasMethod(const std::vector<std::string>& methods, const std::string& method) {
    for (size_t i = 0; i < methods.size(); ++i) {
        if (methods[i] == method)
            return true;
    }
    return false;
}

static std::vector<std::string> normalizeConfiguredMethods(const std::vector<std::string>& configured) {
    std::vector<std::string> allowed;
    // Project policy: GET is always allowed.
    allowed.push_back("GET");
    allowed.push_back("HEAD");
    if (hasMethod(configured, "POST"))
        allowed.push_back("POST");
    if (hasMethod(configured, "DELETE"))
        allowed.push_back("DELETE");
    return allowed;
}

static std::vector<std::string> resolveAllowedMethods(const LocationConfig* loc, const ServerConfig& cfg) {
    if (loc != NULL) {
        if (loc->hasAllowMethods())
            return normalizeConfiguredMethods(loc->getAllowMethods());
        if (loc->hasMethods())
            return normalizeConfiguredMethods(loc->getMethods());
    }
    if (cfg.hasAllowMethods())
        return normalizeConfiguredMethods(cfg.getAllowMethods());
    if (cfg.hasMethods())
        return normalizeConfiguredMethods(cfg.getMethods());

    std::vector<std::string> defaults;
    defaults.push_back("GET");
    defaults.push_back("HEAD");
    return defaults;
}

static std::string buildAllowHeaderValue(const std::vector<std::string>& allowed) {
    std::string value;
    if (hasMethod(allowed, "GET"))
        value += "GET";
    if (hasMethod(allowed, "POST")) {
        if (!value.empty()) value += ", ";
        value += "POST";
    }
    if (hasMethod(allowed, "DELETE")) {
        if (!value.empty()) value += ", ";
        value += "DELETE";
    }
    return value;
}

static bool locationHasCgiForUri(const LocationConfig& loc, const std::string& uri) {
    if (!loc.hasCgiPass())
        return false;
    const std::string path = stripQueryString(uri);
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    const std::string ext = path.substr(dot);
    const std::map<std::string, std::string>& pass = loc.getCgiPass();
    return pass.find(ext) != pass.end();
}

static bool exceedsClientMaxBodySize(const HttpRequest& req, size_t maxBodySize) {
    const std::map<std::string, std::string>& headers = req.getHeaders();
    std::map<std::string, std::string>::const_iterator clIt = headers.find("content-length");
    if (clIt != headers.end()) {
        std::istringstream iss(clIt->second);
        size_t contentLen = 0;
        iss >> contentLen;
        if (!iss.fail() && contentLen > maxBodySize)
            return true;
    }
    return req.getBody().size() > maxBodySize;
}

Server::Server(const std::vector<ServerConfig>& cfgs)
: _configs(cfgs), _sidSeq(1) {
    if (_configs.empty())
        throw std::runtime_error("No server config provided");

    // listen 포트 수집 (중복 제거)
    std::set<int> ports;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const std::vector<int>& ps = _configs[i].getListenPorts();
        ports.insert(ps.begin(), ps.end());
    }
    if (ports.empty())
        throw std::runtime_error("No listen port configured");

    // server-level runtime tuning values (use first server block as global runtime policy)
    _maxConnections = _configs[0].getMaxConnections();
    _idleTimeoutSec = _configs[0].getIdleTimeout();
    _writeTimeoutSec = _configs[0].getWriteTimeout();
    _maxKeepAlive = _configs[0].getKeepAliveMax();

    // 각 포트마다 리스너 생성 및 poll 등록
    for (std::set<int>::const_iterator it = ports.begin(); it != ports.end(); ++it) {
        int port = *it;
        int fd = createListenSocket(port);
        setNonBlocking(fd);
        _listenFds.push_back(fd);
        _listenFdSet.insert(fd);
        _listenFdToPort[fd] = port;

        pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        _pfds.push_back(p);

        // 첫 포트는 기존 코드 호환성/로깅용으로 저장
        if (it == ports.begin())
            _port = port;

        std::cout << "Listening on port " << port << "\n";
    }
}

Server::~Server() {
    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it)
        delete it->second;
    _conns.clear();

    for (size_t i = 0; i < _listenFds.size(); ++i) {
        if (_listenFds[i] != -1) ::close(_listenFds[i]);
    }
}

int Server::createListenSocket(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) fatal("socket");

    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        fatal("setsockopt");

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) fatal("bind");
    if (::listen(fd, 128) < 0) fatal("listen");
    return fd;
}

void Server::setNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) fatal("fcntl(F_GETFL)");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) fatal("fcntl(F_SETFL)");
}

void Server::run() {
    while (true) {
        sweepTimeouts();

        int ready = ::poll(&_pfds[0], _pfds.size(), 1000); // 1s tick
        if (ready < 0) {
            if (errno == EINTR) continue;
            fatal("poll");
        }
        if (ready == 0) continue;

        for (size_t i = 0; i < _pfds.size(); /* increment inside */) {
            if (_pfds[i].revents == 0) { ++i; continue; }

        if (isListenFd(_pfds[i].fd)) {
            if (_pfds[i].revents & POLLIN) acceptLoop(_pfds[i].fd);
            _pfds[i].revents = 0;
            ++i;
            continue;
        }

            handleClientEvent(i);
            // handleClientEvent may remove current index; safest is to just continue without ++i if removed
            // We'll detect by checking i within bounds and revents reset.
            if (i < _pfds.size() && _pfds[i].revents == 0) ++i;
        }

        for (size_t k = 0; k < _pfds.size(); ++k) _pfds[k].revents = 0;
    }
}

void Server::acceptLoop(int listenFd) {
    while (true) {
        int cfd = ::accept(listenFd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            perror("accept");
            return;
        }

        if ((int)_conns.size() >= _maxConnections) {
            ::close(cfd);
            continue;
        }

        setNonBlocking(cfd);

        Connection* conn = new Connection(cfd);
        _conns[cfd] = conn;
        _clientPort[cfd] = _listenFdToPort[listenFd];

        pollfd p;
        p.fd = cfd;
        p.events = POLLIN; // start with read
        p.revents = 0;
        _pfds.push_back(p);

        std::cout << "Accepted fd=" << cfd << "\n";
    }
}

// CHECK)
void    Server::handleClientEvent(size_t idx)
{
    int fd = _pfds[idx].fd;
    Connection* conn = 0;

    std::map<int, Connection*>::iterator it = _conns.find(fd);
    if (it == _conns.end()) {
        removeConn(fd);
        return;
    }
    conn = it->second;

    short ev = _pfds[idx].revents;

    if (ev & (POLLERR | POLLHUP | POLLNVAL)) {
        removeConn(fd);
        return;
    }

    if (ev & POLLIN) {
        if (!conn->onReadable()) { removeConn(fd); return; }

        // =====================================================
        // [HTTP/1.1 Validator 기반 보강 로직]
        // - 명세 기반 상태코드 분기
        // - Host 필수 검증
        // - Version 검사
        // - Method 구현 여부 검사
        // =====================================================

        while (true)
        {
            HttpRequest req;
            std::string& in = conn->inBuf();

            // raw buffer 기반 파싱 시도
            req.appendData(in);

            // 명세 기반 검증 수행
            HttpParseResult result = HttpRequestValidator::validate(req);

            // 1) 아직 데이터 부족 (partial read)
            if (result.getStatus() == HttpParseResult::PARSE_NEED_MORE)
            {
                break ;
            }

            // 2) 파싱 에러 (정확한 HTTP 상태코드 사용)
            if (result.getStatus() == HttpParseResult::PARSE_ERROR)
            {
                const ServerConfig& cfg = pickDefaultServerConfigForFd(fd);
                HttpResponse resp = buildErrorResponse(result.getHttpStatusCode(), cfg);

                std::string bytes = resp.toString();
                conn->queueWrite(bytes);
                conn->closeAfterWrite();
                break ;
            }

            // 3) 정상 파싱 완료
            if (result.getStatus() == HttpParseResult::PARSE_COMPLETE)
            {
                // 두번째 요청 있을 경우, 첫번째 요청(consumedLength)까지 지우기 -> 다음 요청을 남겨둬야함
                in.erase(0, result.getConsumedLength());

                const ServerConfig& cfg = pickServerConfig(fd, req);
                if (exceedsClientMaxBodySize(req, cfg.getClientMaxBodySize())) {
                    HttpResponse resp = buildErrorResponse(413, cfg);
                    std::string bytes = resp.toString();
                    conn->queueWrite(bytes);
                    conn->closeAfterWrite();
                    break ;
                }

                conn->incRequestCount();
                onRequest(fd, req);

                if (conn->shouldCloseAfterWrite())
                    break ;
                if (conn->requestCount() >= _maxKeepAlive)
                {
                    conn->closeAfterWrite();
                    break ;
                }
            }
        }
    }

    if (ev & POLLOUT) {
        if (!conn->onWritable()) { removeConn(fd); return ; }
    }

    updatePollEventsFor(fd);
    _pfds[idx].revents = 0;
}


// =========================
// [기존 코드 - 유지용]
// =========================
/*void Server::handleClientEvent(size_t idx) {
    int fd = _pfds[idx].fd;
    Connection* conn = 0;

    std::map<int, Connection*>::iterator it = _conns.find(fd);
    if (it == _conns.end()) {
        removeConn(fd);
        return;
    }
    conn = it->second;

    short ev = _pfds[idx].revents;

    if (ev & (POLLERR | POLLHUP | POLLNVAL)) {
        removeConn(fd);
        return;
    }

    if (ev & POLLIN) {
        if (!conn->onReadable()) { removeConn(fd); return; }

        // Try parse requests as long as possible (pipelining-friendly)
        while (true) {
            HttpRequest req;
            std::string& in = conn->inBuf();

            // HttpRequest::appendData를 사용하여 파싱 시도
            bool complete = req.appendData(in);
            if (!complete) break; // need more bytes

            if (req.hasError()) {
                HttpResponse resp;
                resp.setStatus(400);
                resp.setBody("400 Bad Request\n");
                std::string bytes = resp.toString();
                conn->queueWrite(bytes);
                conn->closeAfterWrite();
                break;
            }

            // 파싱 성공 - 처리된 데이터는 버퍼에서 제거해야 함
            // 간단하게 전체 버퍼를 클리어 (단일 요청 처리)
            in.clear();

            conn->incRequestCount();
            onRequest(fd, req);
            // if handler queued write and wants close, we may stop parsing more
            if (conn->shouldCloseAfterWrite()) break;
            if (conn->requestCount() >= _maxKeepAlive) {
                conn->closeAfterWrite();
                break;
            }
        }
    }

    if (ev & POLLOUT) {
        if (!conn->onWritable()) { removeConn(fd); return; }
    }

    updatePollEventsFor(fd);
    _pfds[idx].revents = 0;
}*/

void Server::updatePollEventsFor(int fd) {
    Connection* conn = _conns[fd];
    for (size_t i = 0; i < _pfds.size(); ++i) {
        if (isListenFd(_pfds[i].fd)) continue; // 리스너는 항상 POLLIN 유지
        if (_pfds[i].fd == fd) {
            short e = POLLIN;
            if (conn->hasPendingWrite()) e |= POLLOUT;
            _pfds[i].events = e;
            return;
        }
    }
}

void Server::removeConn(int fd) {
    std::map<int, Connection*>::iterator it = _conns.find(fd);
    if (it != _conns.end()) {
        delete it->second;
        _conns.erase(it);
    }
    _clientPort.erase(fd);
    for (size_t i = 0; i < _pfds.size(); ++i) {
        if (isListenFd(_pfds[i].fd)) continue; // 리스너는 제거 대상 아님
        if (_pfds[i].fd == fd) {
            _pfds.erase(_pfds.begin() + i);
            break;
        }
    }
    std::cout << "Closed fd=" << fd << "\n";
}

void Server::sweepTimeouts() {
    std::time_t now = std::time(NULL);
    std::vector<int> toClose;

    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it) {
        Connection* c = it->second;
        int idle = static_cast<int>(now - c->lastActive());
        if (!c->hasPendingWrite()) {
            if (idle > _idleTimeoutSec) toClose.push_back(it->first);
        } else {
            if (idle > _writeTimeoutSec) toClose.push_back(it->first);
        }
    }
    for (size_t i = 0; i < toClose.size(); ++i) removeConn(toClose[i]);

    // (optional) session cleanup (very simple)
    for (std::map<std::string, Session>::iterator sit = _sessions.begin(); sit != _sessions.end(); ) {
        if ((int)(now - sit->second.lastSeen) > 300) { // 5 minutes
            std::map<std::string, Session>::iterator kill = sit++;
            _sessions.erase(kill);
        } else {
            ++sit;
        }
    }
}

std::string Server::newSessionId() {
    // Simple, non-crypto SID (bonus demo용)
    std::ostringstream oss;
    oss << std::time(NULL) << "-" << (_sidSeq++);
    return oss.str();
}

Server::Session& Server::getOrCreateSession(const HttpRequest& req, HttpResponse& resp) {
    // HttpRequest에 cookie 메서드가 없으므로 헤더에서 직접 파싱
    std::string cookieHeader;
    const std::map<std::string, std::string>& headers = req.getHeaders();
    std::map<std::string, std::string>::const_iterator it = headers.find("cookie");
    if (it != headers.end()) {
        cookieHeader = it->second;
    }
    
    // 간단한 쿠키 파싱 (sid=value 형태)
    std::string sid;
    if (!cookieHeader.empty()) {
        size_t pos = cookieHeader.find("sid=");
        if (pos != std::string::npos) {
            size_t start = pos + 4;
            size_t end = cookieHeader.find(";", start);
            if (end == std::string::npos) end = cookieHeader.length();
            sid = cookieHeader.substr(start, end - start);
        }
    }
    
    if (sid.empty() || _sessions.find(sid) == _sessions.end()) {
        sid = newSessionId();
        _sessions[sid] = Session();
        resp.setHeader("Set-Cookie", "sid=" + sid + "; Path=/; HttpOnly");
    }
    Session& s = _sessions[sid];
    s.lastSeen = std::time(NULL);
    return s;
}

bool Server::isListenFd(int fd) const {
    return _listenFdSet.find(fd) != _listenFdSet.end();
}

std::string Server::extractHostName(const HttpRequest& req) const {
    const std::map<std::string, std::string>& headers = req.getHeaders();
    std::map<std::string, std::string>::const_iterator it = headers.find("host");
    if (it == headers.end())
        return "";

    std::string host = it->second;
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host[0])))
        host.erase(0, 1);
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host[host.size() - 1])))
        host.erase(host.size() - 1);

    if (host.empty())
        return "";

    if (host[0] == '[') {
        size_t end = host.find(']');
        if (end != std::string::npos)
            return toLowerAscii(host.substr(0, end + 1));
        return toLowerAscii(host);
    }

    size_t colon = host.find(':');
    if (colon != std::string::npos)
        return toLowerAscii(host.substr(0, colon));
    return toLowerAscii(host);
}

const ServerConfig& Server::pickServerConfig(int fd, const HttpRequest& req) const {
    std::map<int, int>::const_iterator pIt = _clientPort.find(fd);
    int port = (pIt != _clientPort.end()) ? pIt->second : _port;
    const std::string host = extractHostName(req);

    const ServerConfig* fallback = NULL;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];
        const std::vector<int> ports = cfg.getListenPorts();
        bool matchPort = false;
        for (size_t j = 0; j < ports.size(); ++j) {
            if (ports[j] == port) {
                matchPort = true;
                break;
            }
        }
        if (!matchPort)
            continue;

        if (!fallback)
            fallback = &cfg;

        if (host.empty())
            continue;

        if (cfg.hasServerNames()) {
            const std::vector<std::string>& names = cfg.getServerNames();
            for (size_t k = 0; k < names.size(); ++k) {
                if (toLowerAscii(names[k]) == host)
                    return cfg;
            }
        }
    }

    if (fallback)
        return *fallback;
    return _configs[0];
}

const ServerConfig& Server::pickDefaultServerConfigForFd(int fd) const {
    std::map<int, int>::const_iterator pIt = _clientPort.find(fd);
    int port = (pIt != _clientPort.end()) ? pIt->second : _port;

    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];
        const std::vector<int> ports = cfg.getListenPorts();
        for (size_t j = 0; j < ports.size(); ++j) {
            if (ports[j] == port)
                return cfg;
        }
    }
    return _configs[0];
}

HttpResponse Server::buildErrorResponse(int code, const ServerConfig& cfg) const {
    std::map<int, std::string> errorPages;
    errorPages[code] = cfg.getErrorPage();
    return ErrorHandler::buildError(code, errorPages);
}

bool Server::isMethodAllowed(const ServerConfig& cfg, const std::string& method) const {
    if (!cfg.hasMethods())
        return true;
    const std::vector<std::string>& methods = cfg.getMethods();
    for (size_t i = 0; i < methods.size(); ++i) {
        if (methods[i] == method)
            return true;
    }
    return false;
}

void Server::onRequest(int fd, const HttpRequest& req) {
    Connection* conn = _conns[fd];

    const ServerConfig& cfg = pickServerConfig(fd, req);

    // keep-alive decision
    bool keepAlive = (req.getVersion() == "HTTP/1.1");
    const std::map<std::string, std::string>& headers = req.getHeaders();
    std::map<std::string, std::string>::const_iterator connIt = headers.find("connection");
    if (connIt != headers.end()) {
        std::string connHdr = connIt->second;
        if (connHdr == "close" || connHdr == "Close") {
            keepAlive = false;
        } else if (connHdr == "keep-alive" || connHdr == "Keep-Alive") {
            keepAlive = true;
        }
    }

    if (conn->requestCount() >= (_maxKeepAlive - 1))
        keepAlive = false;

    HttpResponse resp;
    const std::string uriPath = stripQueryString(req.getURI());

    Router router;
    const std::vector<LocationConfig>& locations = cfg.getLocations();
    for (size_t i = 0; i < locations.size(); ++i)
        router.addLocation(locations[i]);
    const LocationConfig* location = router.match(uriPath);
    const std::vector<std::string> allowedMethods = resolveAllowedMethods(location, cfg);
    const std::string allowHeader = buildAllowHeaderValue(allowedMethods);
    
    if (location == NULL) {
        resp = buildErrorResponse(404, cfg);
    } else {
        bool allowed = hasMethod(allowedMethods, req.getMethod());

        if (location->hasRedirect()) {
            const Redirect& redir = location->getRedirect();
            resp.setStatus(redir.status);
            resp.setHeader("Location", redir.target);
            resp.setBody("");
        } else if (!allowed) {
            resp = buildErrorResponse(405, cfg);
            resp.setHeader("Allow", allowHeader);
        } else if (locationHasCgiForUri(*location, req.getURI())) {
            CgiHandler cgiHandler;
            resp = cgiHandler.handle(req, *location);
        } else if (req.getMethod() == "GET" || req.getMethod() == "HEAD") {
            GETHandler getHandler;
            resp = getHandler.handle(req, *location);
            if (req.getMethod() == "HEAD")
                resp.setBody(""); // HEAD 요청은 바디 없이 헤더만 전송
        } else if (req.getMethod() == "POST") {
            size_t maxBody = cfg.hasClientMaxBodySize() ? cfg.getClientMaxBodySize() : 0;
            POSTHandler postHandler(maxBody);
            resp = postHandler.handle(req, *location);
        } else if (req.getMethod() == "DELETE") {
            DELETEHandler deleteHandler;
            resp = deleteHandler.handle(req, *location);
        } else {
            resp = buildErrorResponse(501, cfg);
        }

        if (resp.getStatusCode() >= 400) {
            int code = resp.getStatusCode();
            HttpResponse errResp = buildErrorResponse(code, cfg);
            if (code == 405)
                errResp.setHeader("Allow", allowHeader);
            resp = errResp;
        }

    }

    // Connection 헤더 설정
    resp.setKeepAlive(keepAlive, _idleTimeoutSec, _maxKeepAlive);

    std::string bytes = resp.toString();
    conn->queueWrite(bytes);

    if (!keepAlive) conn->closeAfterWrite();
}
