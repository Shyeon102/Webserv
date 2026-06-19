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
#include <sys/wait.h>
#include <signal.h>

static void fatal(const char* msg) {
    std::cerr << msg << ": " << strerror(errno) << "\n";
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

static std::string trimAscii(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

static bool parseBufferedContentLength(const std::string& headerBlock,
                                       bool& hasContentLength,
                                       size_t& contentLength,
                                       bool& invalidHeader,
                                       bool& isChunked) {
    std::istringstream iss(headerBlock);
    std::string line;

    hasContentLength = false;
    contentLength = 0;
    invalidHeader = false;
    isChunked = false;

    std::getline(iss, line); // request line
    while (std::getline(iss, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            break;

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            invalidHeader = true;
            return false;
        }

        std::string key = toLowerAscii(trimAscii(line.substr(0, colon)));
        std::string value = trimAscii(line.substr(colon + 1));

        if (key == "transfer-encoding" && toLowerAscii(value) == "chunked")
            isChunked = true;

        if (key == "content-length") {
            if (hasContentLength) {
                invalidHeader = true;
                return false;
            }
            if (value.empty()) {
                invalidHeader = true;
                return false;
            }
            size_t parsed = 0;
            for (size_t i = 0; i < value.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
                    invalidHeader = true;
                    return false;
                }
                size_t digit = static_cast<size_t>(value[i] - '0');
                if (parsed > (static_cast<size_t>(-1) - digit) / 10) {
                    invalidHeader = true;
                    return false;
                }
                parsed = parsed * 10 + digit;
            }
            hasContentLength = true;
            contentLength = parsed;
        }
    }
    return true;
}

static bool bufferedRequestReadyForParse(const std::string& in) {
    size_t headerEnd = in.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    bool hasContentLength;
    size_t contentLength;
    bool invalidHeader;
    bool isChunked;
    parseBufferedContentLength(in.substr(0, headerEnd),
                               hasContentLength,
                               contentLength,
                               invalidHeader,
                               isChunked);

    if (invalidHeader || isChunked || !hasContentLength)
        return true;

    size_t bodyStart = headerEnd + 4;
    if (contentLength > static_cast<size_t>(-1) - bodyStart)
        return true;
    return in.size() >= bodyStart + contentLength;
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
    // Respect configured methods strictly.
    // HEAD is handled by the GET handler only when explicitly allowed.
    if (hasMethod(configured, "GET"))
        allowed.push_back("GET");
    if (hasMethod(configured, "HEAD"))
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
: _configs(cfgs), _cgiTimeoutSec(30), _sidSeq(1) {
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
        _listenFds.push_back(fd); // _listenFds 목록에 fd를 추가함. fd(socket, setsocket, bind, listen이 완료된.)
        _listenFdSet.insert(fd); // 어떤 fd가 listen fd인지 빠르게 확인하기 위한 set에도 fd를 추가해줌.
        _listenFdToPort[fd] = port; // ListenFdToPort 각 fd가 어떤 포트에 묶였는지 저장.

        pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        // 이 listen fd에 각 poll을 만들어주는데, POLLIN은 이 fd에 읽을 이벤트가 생기는지 감시.
        p.revents = 0;
        // POLLIN이벤트가 감지되면, 0을 함.
        _pfds.push_back(p);
        // poll fds리스트에 p추가.

        // 첫 포트는 기존 코드 호환성/로깅용으로 저장
        if (it == ports.begin())
            _port = port;

        std::cout << "Listening on port " << port << "\n";
    }
}

Server::~Server() {
    std::vector<CgiTask*> tasks;
    for (std::map<int, CgiTask*>::iterator it = _cgiByClientFd.begin(); it != _cgiByClientFd.end(); ++it)
        tasks.push_back(it->second);
    for (size_t i = 0; i < tasks.size(); ++i)
        destroyCgiTask(tasks[i]);

    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it)
        delete it->second;
    _conns.clear();

    for (size_t i = 0; i < _listenFds.size(); ++i) {
        if (_listenFds[i] != -1) ::close(_listenFds[i]);
    }
}

int Server::createListenSocket(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    // IPv4 TCP 소켓 하나 만들기. AF_INET은 IPv4, SOCK_STREAM은 TCP. 0은 기본 프로토콜.
    // 성공하면 fd가 나오고 실패하면 -1인듯
    if (fd < 0) fatal("socket");
    // 서버가 클라이언트 접속을 받을 입구.
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        fatal("setsockopt");
    // setsocket으로 SO_REUSEADDR은 서버가 재시작 했을 경우 주소를 이미 사용하고 있다는 에러를 줄여줌.
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    // AF_INET은 IPv4니까 IPv4의 INADDR_ANY(어느주소나) 허용.
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    // htons는 host byte order 를 network byte order로 바꿔줌.
    // 근데 그게 뭔데 #TODO
    // 서버가 어느 주소와 포트에서 기다릴지 설정.
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) fatal("bind");
    // bind는 방금 만든 fd와 소켓을 붙임. fd3아. 너는 이제 8080포트를 받도록 해 ~
    if (::listen(fd, 128) < 0) fatal("listen");
    // listen은 이제 그 socker을 접속 대기중으로 바꿈.
    return fd;
    // 모든게 처리된 fd를 반환.
}

void Server::setNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) fatal("fcntl(F_GETFL)");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) fatal("fcntl(F_SETFL)");
    // blocking이면 accept, recv, send를 멈춰서 기다릴 수 있음.
    // 근데 우리 서버는 poll같은 함수 기반이기 때문에 멈추면 안됨. 하나의 fd에서 멈추면 큰일.
    // 그래서 non-blocking으로 설정함.
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
            const int currentFd = _pfds[i].fd;
            try {
                if (isCgiFd(currentFd))
                    handleCgiEvent(i);
                else
                    handleClientEvent(i);
            } catch (const std::exception& e) {
                std::cerr << "Error handling fd=" << currentFd
                          << ": " << e.what() << "\n";
                if (isCgiFd(currentFd)) {
                    std::map<int, CgiTask*>::iterator taskIt = _cgiByFd.find(currentFd);
                    if (taskIt != _cgiByFd.end())
                        failCgiTask(taskIt->second, 502);
                } else {
                    removeConn(currentFd);
                }
            } catch (...) {
                std::cerr << "Unknown error handling fd=" << currentFd << "\n";
                if (isCgiFd(currentFd)) {
                    std::map<int, CgiTask*>::iterator taskIt = _cgiByFd.find(currentFd);
                    if (taskIt != _cgiByFd.end())
                        failCgiTask(taskIt->second, 502);
                } else {
                    removeConn(currentFd);
                }
            }
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
            std::cerr << "accept: " << strerror(errno) << "\n";
            return;
        }

        if ((int)_conns.size() >= _maxConnections) {
            ::close(cfd);
            continue;
        } // 현재 연결 수가 최대 연결수보다 많으면 새 클라이언트 바로 닫음.

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

void Server::handleClientEvent(size_t idx) {
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
        {
            std::string& in = conn->inBuf();
            size_t headerEnd = in.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                bool hasContentLength;
                size_t contentLength;
                bool invalidHeader;
                bool isChunked;
                parseBufferedContentLength(in.substr(0, headerEnd),
                                           hasContentLength,
                                           contentLength,
                                           invalidHeader,
                                           isChunked);
                if (invalidHeader) {
                    const ServerConfig& cfg = pickDefaultServerConfigForFd(fd);
                    HttpResponse resp = buildErrorResponse(400, cfg);
                    conn->queueWrite(resp.toString());
                    conn->closeAfterWrite();
                    updatePollEventsFor(fd);
                    _pfds[idx].revents = 0;
                    return;
                }
            }
        }
        if (!conn->isWaitingForCgi()) while (true) {
            HttpRequest req;
            std::string& in = conn->inBuf();

            if (!bufferedRequestReadyForParse(in))
                break;

            req.appendData(in);
            HttpParseResult result = HttpRequestValidator::validate(req);
            if (result.getStatus() == HttpParseResult::PARSE_NEED_MORE) { break; }
            if (result.getStatus() == HttpParseResult::PARSE_ERROR) {
                const ServerConfig& cfg = pickDefaultServerConfigForFd(fd);
                HttpResponse resp = buildErrorResponse(result.getHttpStatusCode(), cfg);
                std::string bytes = resp.toString();
                conn->queueWrite(bytes);
                conn->closeAfterWrite();
                break;
            }
            if (result.getStatus() == HttpParseResult::PARSE_COMPLETE) {
                in.erase(0, result.getConsumedLength());

                const ServerConfig& cfg = pickServerConfig(fd, req);
                size_t maxBodyForRequest = cfg.getClientMaxBodySize();
                if (maxBodyForRequest > 0 && exceedsClientMaxBodySize(req, maxBodyForRequest)) {
                    HttpResponse resp = buildErrorResponse(413, cfg);
                    std::string bytes = resp.toString();
                    conn->queueWrite(bytes);
                    conn->closeAfterWrite();
                    break;
                }

                conn->incRequestCount();
                onRequest(fd, req);

                if (conn->shouldCloseAfterWrite())
                    break;
                if (conn->isWaitingForCgi())
                    break;
                if (conn->requestCount() >= _maxKeepAlive) {
                    conn->closeAfterWrite();
                    break;
                }
            }
        }
    }

    if (ev & POLLOUT) {
        if (!conn->onWritable()) { removeConn(fd); return; }
    }

    updatePollEventsFor(fd);
    _pfds[idx].revents = 0;
}

void Server::handleCgiEvent(size_t idx) {
    const int fd = _pfds[idx].fd;
    std::map<int, CgiTask*>::iterator it = _cgiByFd.find(fd);
    if (it == _cgiByFd.end()) {
        removePollFd(fd);
        return;
    }

    CgiTask* task = it->second;
    const short ev = _pfds[idx].revents;

    if (ev & POLLNVAL) {
        failCgiTask(task, 502);
        return;
    }

    if (fd == task->stdoutFd) {
        if (ev & (POLLIN | POLLHUP | POLLERR))
            drainCgiTask(task);
    } else if (fd == task->stdinFd) {
        if (ev & (POLLHUP | POLLERR)) {
            closeCgiFd(task, task->stdinFd);
        } else if (ev & POLLOUT) {
            feedCgiTask(task);
        }
    }

    checkCgiChildExit(task);
    if (isCgiTaskFinished(task))
        finalizeCgiTask(task);
    else if (idx < _pfds.size())
        _pfds[idx].revents = 0;
}

void Server::updatePollEventsFor(int fd) {
    Connection* conn = _conns[fd];
    for (size_t i = 0; i < _pfds.size(); ++i) {
        if (isListenFd(_pfds[i].fd) || isCgiFd(_pfds[i].fd)) continue;
        if (_pfds[i].fd == fd) {
            short e = conn->isWaitingForCgi() ? 0 : POLLIN;
            if (conn->hasPendingWrite()) e |= POLLOUT;
            _pfds[i].events = e;
            return;
        }
    }
}

void Server::removeConn(int fd) {
    std::map<int, CgiTask*>::iterator cgiIt = _cgiByClientFd.find(fd);
    if (cgiIt != _cgiByClientFd.end())
        destroyCgiTask(cgiIt->second);

    std::map<int, Connection*>::iterator it = _conns.find(fd);
    if (it != _conns.end()) {
        delete it->second;
        _conns.erase(it);
    }
    _clientPort.erase(fd);
    removePollFd(fd);
    std::cout << "Closed fd=" << fd << "\n";
}

void Server::sweepTimeouts() {
    sweepCgiTasks();

    std::time_t now = std::time(NULL);
    std::vector<int> toClose;

    for (std::map<int, Connection*>::iterator it = _conns.begin(); it != _conns.end(); ++it) {
        Connection* c = it->second;
        if (c->isWaitingForCgi())
            continue;
        int idle = static_cast<int>(now - c->lastActive());
        if (!c->hasPendingWrite()) {
            if (idle > _idleTimeoutSec) toClose.push_back(it->first);
        } else {
            if (idle > _writeTimeoutSec) toClose.push_back(it->first);
        }
    }

    for (size_t i = 0; i < toClose.size(); ++i) removeConn(toClose[i]);

    for (std::map<std::string, Session>::iterator sit = _sessions.begin(); sit != _sessions.end(); ) {
        if ((int)(now - sit->second.lastSeen) > 300) { // 5 minutes
            std::map<std::string, Session>::iterator kill = sit++;
            _sessions.erase(kill);
        } else {
            ++sit;
        }
    }
}

void Server::removePollFd(int fd) {
    for (size_t i = 0; i < _pfds.size(); ++i) {
        if (_pfds[i].fd == fd) {
            _pfds.erase(_pfds.begin() + i);
            return;
        }
    }
}

void Server::sweepCgiTasks() {
    std::vector<CgiTask*> tasks;
    std::vector<CgiTask*> timedOut;
    const std::time_t now = std::time(NULL);

    for (std::map<int, CgiTask*>::iterator it = _cgiByClientFd.begin(); it != _cgiByClientFd.end(); ++it)
        tasks.push_back(it->second);

    for (size_t i = 0; i < tasks.size(); ++i) {
        CgiTask* task = tasks[i];
        if (_cgiByClientFd.find(task->clientFd) == _cgiByClientFd.end())
            continue;
        checkCgiChildExit(task);
        if (isCgiTaskFinished(task))
            finalizeCgiTask(task);
        else if (now - task->startedAt > _cgiTimeoutSec)
            timedOut.push_back(task);
    }
    for (size_t i = 0; i < timedOut.size(); ++i)
        failCgiTask(timedOut[i], 504);
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

bool Server::isCgiFd(int fd) const {
    return _cgiByFd.find(fd) != _cgiByFd.end();
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

/*HttpResponse Server::buildErrorResponse(int code, const ServerConfig& cfg) const {
    std::map<int, std::string> errorPages;
    errorPages[code] = cfg.getErrorPage();
    return ErrorHandler::buildError(code, errorPages);
}*/

HttpResponse Server::buildErrorResponse(int code, const ServerConfig& cfg) const {
    std::map<int, std::string> errorPages;
    const std::map<int, std::string>& cfgPages = cfg.getErrorPages();
    std::map<int, std::string>::const_iterator it = cfgPages.find(code);
    if (it != cfgPages.end()) {
        std::string root = cfg.getRoot();
        std::string path = it->second;
        // root 기준 상대경로로 합치기 (슬래시 중복 방지)
        if (!root.empty() && root[root.size() - 1] == '/'
            && !path.empty() && path[0] == '/')
            errorPages[code] = root + path.substr(1);
        else
            errorPages[code] = root + path;
    }
    return ErrorHandler::buildError(code, errorPages);
}

void Server::onRequest(int fd, const HttpRequest& req) {
    Connection* conn = _conns[fd];
    const ServerConfig& cfg = pickServerConfig(fd, req);
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
    if (location == NULL && !uriPath.empty() && uriPath[uriPath.size() - 1] != '/') {
        const std::string withSlash = uriPath + "/";
        const LocationConfig* slashMatch = router.match(withSlash);
        if (slashMatch != NULL && slashMatch->getPath() == withSlash) {
            resp.setStatus(301);
            resp.setHeader("Location", withSlash);
            resp.setBody("");
            resp.setKeepAlive(keepAlive, _idleTimeoutSec, _maxKeepAlive);
            if (req.getMethod() == "HEAD")
                resp.suppressBody();
            std::string bytes = resp.toString();
            conn->queueWrite(bytes);
            if (!keepAlive) conn->closeAfterWrite();
            return;
        }
    }
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
        } else if ((req.getMethod() == "GET" || req.getMethod() == "POST")
                && locationHasCgiForUri(*location, req.getURI())) {
            if (startCgiTask(fd, req, *location, cfg, keepAlive))
                return;
            resp = buildErrorResponse(502, cfg);
        } else if (req.getMethod() == "GET" || req.getMethod() == "HEAD") {
            GETHandler getHandler;
            resp = getHandler.handle(req, *location);
            if (req.getMethod() == "HEAD")
                resp.suppressBody();
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

    if (req.getMethod() == "HEAD")
        resp.suppressBody();

    resp.setKeepAlive(keepAlive, _idleTimeoutSec, _maxKeepAlive);

    std::string bytes = resp.toString();
    conn->queueWrite(bytes);

    if (!keepAlive) conn->closeAfterWrite();
}

bool Server::startCgiTask(int clientFd,
                          const HttpRequest& req,
                          const LocationConfig& location,
                          const ServerConfig& cfg,
                          bool keepAlive) {
    std::map<int, Connection*>::iterator connIt = _conns.find(clientFd);
    if (connIt == _conns.end())
        return false;

    CgiHandler cgiHandler;
    HttpResponse errorResponse;
    std::string scriptPath;
    std::string interpreter;
    std::map<std::string, std::string> env;
    if (!cgiHandler.prepare(req, location, scriptPath, interpreter, env, errorResponse)) {
        errorResponse.setKeepAlive(keepAlive, _idleTimeoutSec, _maxKeepAlive);
        connIt->second->queueWrite(errorResponse.toString());
        if (!keepAlive)
            connIt->second->closeAfterWrite();
        return true;
    }

    CgiTask* task = new CgiTask();
    task->clientFd = clientFd;
    task->requestBody = req.getBody();
    task->startedAt = std::time(NULL);
    task->stdinClosed = false;
    task->stdoutClosed = false;
    task->keepAlive = keepAlive;
    task->method = req.getMethod();
    task->cfg = &cfg;

    try {
        task->pid = cgiHandler.spawn(scriptPath, interpreter, env, task->stdinFd, task->stdoutFd);
    } catch (...) {
        delete task;
        return false;
    }

    _cgiByClientFd[clientFd] = task;
    _cgiByFd[task->stdinFd] = task;
    _cgiByFd[task->stdoutFd] = task;

    pollfd p;
    p.fd = task->stdinFd;
    p.events = POLLOUT;
    p.revents = 0;
    _pfds.push_back(p);

    p.fd = task->stdoutFd;
    p.events = POLLIN;
    p.revents = 0;
    _pfds.push_back(p);

    connIt->second->setWaitingForCgi(true);
    updatePollEventsFor(clientFd);
    return true;
}

void Server::checkCgiChildExit(CgiTask* task) {
    if (task == NULL || task->childExited || task->pid < 0)
        return;

    int status = 0;
    pid_t result = waitpid(task->pid, &status, WNOHANG);
    if (result == task->pid) {
        task->childExited = true;
        task->exitStatus = status;
        task->pid = -1;
    }
}

void Server::feedCgiTask(CgiTask* task) {
    if (task == NULL || task->stdinClosed)
        return;

    if (task->bodySent >= task->requestBody.size()) {
        closeCgiFd(task, task->stdinFd);
        return;
    }

    const size_t remain = task->requestBody.size() - task->bodySent;
    const size_t chunk = remain > 16384 ? 16384 : remain;
    ssize_t written = write(task->stdinFd, task->requestBody.data() + task->bodySent, chunk);
    if (written > 0) {
        task->bodySent += static_cast<size_t>(written);
        if (task->bodySent >= task->requestBody.size())
            closeCgiFd(task, task->stdinFd);
        return;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return;
    closeCgiFd(task, task->stdinFd);
}

void Server::drainCgiTask(CgiTask* task) {
    if (task == NULL || task->stdoutClosed)
        return;

    char buffer[4096];
    ssize_t bytesRead = read(task->stdoutFd, buffer, sizeof(buffer));
    if (bytesRead > 0) {
        task->output.append(buffer, bytesRead);
        return;
    }
    if (bytesRead < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return;
    closeCgiFd(task, task->stdoutFd);
}

bool Server::isCgiTaskFinished(CgiTask* task) const {
    return task != NULL && task->stdinClosed && task->stdoutClosed && task->childExited;
}

void Server::finalizeCgiTask(CgiTask* task) {
    if (task == NULL)
        return;

    std::map<int, Connection*>::iterator connIt = _conns.find(task->clientFd);
    if (connIt == _conns.end()) {
        destroyCgiTask(task);
        return;
    }

    Connection* conn = connIt->second;
    conn->setWaitingForCgi(false);

    HttpResponse response;
    if (WIFEXITED(task->exitStatus) && WEXITSTATUS(task->exitStatus) == 0) {
        CgiHandler cgiHandler;
        response = cgiHandler.buildResponse(task->output);
    } else {
        response = buildErrorResponse(502, *task->cfg);
    }

    if (task->method == "HEAD")
        response.suppressBody();
    response.setKeepAlive(task->keepAlive, _idleTimeoutSec, _maxKeepAlive);
    conn->queueWrite(response.toString());
    if (!task->keepAlive)
        conn->closeAfterWrite();

    destroyCgiTask(task);
    updatePollEventsFor(conn->fd());
}

void Server::failCgiTask(CgiTask* task, int statusCode) {
    if (task == NULL)
        return;

    if (task->pid > 0)
        kill(task->pid, SIGKILL);
    if (!task->childExited && task->pid > 0) {
        waitpid(task->pid, &task->exitStatus, 0);
        task->childExited = true;
        task->pid = -1;
    }

    std::map<int, Connection*>::iterator connIt = _conns.find(task->clientFd);
    if (connIt != _conns.end()) {
        HttpResponse response = buildErrorResponse(statusCode, *task->cfg);
        Connection* conn = connIt->second;
        conn->setWaitingForCgi(false);
        response.setKeepAlive(task->keepAlive, _idleTimeoutSec, _maxKeepAlive);
        conn->queueWrite(response.toString());
        if (!task->keepAlive)
            conn->closeAfterWrite();
        updatePollEventsFor(conn->fd());
    }

    destroyCgiTask(task);
}

void Server::closeCgiFd(CgiTask* task, int fd) {
    if (task == NULL || fd < 0)
        return;

    removePollFd(fd);
    _cgiByFd.erase(fd);
    close(fd);

    if (fd == task->stdinFd) {
        task->stdinFd = -1;
        task->stdinClosed = true;
    } else if (fd == task->stdoutFd) {
        task->stdoutFd = -1;
        task->stdoutClosed = true;
    }
}

void Server::destroyCgiTask(CgiTask* task) {
    if (task == NULL)
        return;

    closeCgiFd(task, task->stdinFd);
    closeCgiFd(task, task->stdoutFd);
    if (task->pid > 0) {
        kill(task->pid, SIGKILL);
        waitpid(task->pid, NULL, 0);
    }
    _cgiByClientFd.erase(task->clientFd);
    delete task;
}
