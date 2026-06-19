#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <map>
#include <string>
#include <ctime>
#include <poll.h>
#include <set>
#include <sys/types.h>
#include <csignal>

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

    // SIGINT/SIGTERM 핸들러에서 set. run() 루프를 빠져나오게 해
    // 소멸자가 정상 실행되도록 한다 (valgrind still-reachable 방지).
    static volatile sig_atomic_t s_stop;

private:
    std::vector<ServerConfig> _configs;      // 모든 server 블록 설정을 저장
    std::vector<int> _listenFds;             // 리스닝 소켓 FD 목록
    std::set<int> _listenFdSet;              // 빠른 판단용 집합
    int _port;                               // 첫 포트 (임시 호환성)
    int _maxConnections;
    int _idleTimeoutSec;
    int _writeTimeoutSec;
    int _maxKeepAlive;
    int _cgiTimeoutSec;

    std::vector<pollfd> _pfds;               // [0..n) 리스너, 이후 클라이언트
    std::map<int, Connection*> _conns;       // fd -> Connection*
    std::map<int, int> _listenFdToPort;      // listen fd -> port
    std::map<int, int> _clientPort;          // client fd -> accepted listen port
    struct CgiTask {
        int clientFd;
        pid_t pid;
        int stdinFd;
        int stdoutFd;
        std::string requestBody;
        size_t bodySent;
        std::string output;
        std::time_t startedAt;
        bool stdinClosed;
        bool stdoutClosed;
        bool childExited;
        int exitStatus;
        bool keepAlive;
        std::string method;
        const ServerConfig* cfg;
        CgiTask() : clientFd(-1), pid(-1), stdinFd(-1), stdoutFd(-1), bodySent(0),
            startedAt(0), stdinClosed(true), stdoutClosed(true), childExited(false),
            exitStatus(0), keepAlive(false), cfg(NULL) {}
    };
    std::map<int, CgiTask*> _cgiByFd;
    std::map<int, CgiTask*> _cgiByClientFd;

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
    void handleCgiEvent(size_t idx);

    void updatePollEventsFor(int fd);
    void removeConn(int fd);
    void removePollFd(int fd);

    void sweepTimeouts();
    void sweepCgiTasks();

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
    bool isCgiFd(int fd) const;
    bool startCgiTask(int clientFd,
                      const HttpRequest& req,
                      const LocationConfig& location,
                      const ServerConfig& cfg,
                      bool keepAlive);
    void checkCgiChildExit(CgiTask* task);
    void feedCgiTask(CgiTask* task);
    void drainCgiTask(CgiTask* task);
    bool isCgiTaskFinished(CgiTask* task) const;
    void finalizeCgiTask(CgiTask* task);
    void failCgiTask(CgiTask* task, int statusCode);
    void closeCgiFd(CgiTask* task, int fd);
    void destroyCgiTask(CgiTask* task);
};

#endif
