#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <string>
#include <ctime>

class Connection {
public:
    enum State {
        READING,
        WRITING
    };

    explicit Connection(int fd);
    ~Connection();

    int fd() const;
    State state() const;

    // I/O (non-blocking)
    // returns false if connection should be closed now
    bool onReadable();
    bool onWritable();

    std::string& inBuf();
    const std::string& inBuf() const;

    void queueWrite(const std::string& bytes);
    bool hasPendingWrite() const;

    void closeAfterWrite();
    bool shouldCloseAfterWrite() const;

    // keep-alive 관리용
    void incRequestCount();
    int requestCount() const;

    void touch();
    std::time_t lastActive() const;

private:
    int _fd;
    State _state;

    std::string _in;
    std::string _out; // 클라이언트에게 보내야할 데이터.
    size_t _outPos;  // write offset to avoid repeated erase
    // _out의 현재 포지션(얼마나 보냈는가)

    bool _closeAfterWrite;
    std::time_t _lastActive;
    int _requestsHandled;

    Connection(const Connection&);
    Connection& operator=(const Connection&);
};

#endif
