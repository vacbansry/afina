#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <afina/execute/Command.h>
#include <protocol/Parser.h>
#include <afina/logging/Service.h>
#include <cstring>
#include <list>
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace STnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<spdlog::logger> log, std::shared_ptr<Afina::Storage> ps)
        : _socket(s), _logger(log), pStorage(ps) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
    }

    ~Connection() {}

    inline bool isAlive() const { return running.load(); }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    std::atomic<bool> running;

    std::shared_ptr<spdlog::logger> _logger;
    std::shared_ptr<Afina::Storage> pStorage;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    std::size_t arg_remains = 0;

    char client_buffer[4096];
    int _first_byte = 0;
    int _read_bytes = 0;
    std::list<std::string> _results;
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
