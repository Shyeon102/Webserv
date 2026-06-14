#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <map>
#include <string>
#include <ctime>
#include <poll.h>
#include <set>

#include "Connection.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "ServerConfig.hpp"

class Server {
public:
    // explicit: 암묵적 변환을 막아 잘못된 생성 호출을 방지
    explicit Server(const std::vector<ServerConfig>& cfgs);
    ~Server();

    void run();

private:
    std::vector<ServerConfig> _configs;      // 모든 server 블록 설정을 저장
    std::vector<int> _listenFds;             // 리스닝 소켓 FD 목록
    std::set<int> _listenFdSet;              // 빠른 판단용 집합
    int _port;                               // 첫 포트 (임시 호환성)
    int _maxConnections;
    int _idleTimeoutSec;
    int _writeTimeoutSec;
    int _maxKeepAlive;

    std::vector<pollfd> _pfds;               // [0..n) 리스너, 이후 클라이언트
    std::map<int, Connection*> _conns;       // fd -> Connection*
    std::map<int, int> _listenFdToPort;      // listen fd -> port
    std::map<int, int> _clientPort;          // client fd -> accepted listen port

    // simple in-memory session store
    struct Session {
        int counter;
        std::time_t lastSeen;
        Session() : counter(0), lastSeen(std::time(NULL)) {}
    };
    std::map<std::string, Session> _sessions;
    unsigned long _sidSeq;

private:
    int createListenSocket(int port);
    void setNonBlocking(int fd);

    void acceptLoop(int listenFd);
    void handleClientEvent(size_t idx);

    void updatePollEventsFor(int fd);
    void removeConn(int fd);

    void sweepTimeouts();

    // request/response flow
    void onRequest(int fd, const HttpRequest& req);
    const ServerConfig& pickServerConfig(int fd, const HttpRequest& req) const;
    std::string extractHostName(const HttpRequest& req) const;
    const ServerConfig& pickDefaultServerConfigForFd(int fd) const;
    HttpResponse buildErrorResponse(int code, const ServerConfig& cfg) const;

    // session helpers
    std::string newSessionId();
    Session& getOrCreateSession(const HttpRequest& req, HttpResponse& resp);

    bool isListenFd(int fd) const;
};

#endif
