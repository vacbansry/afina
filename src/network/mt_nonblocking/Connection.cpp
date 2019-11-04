#include "Connection.h"

#include <iostream>
#include <sys/uio.h>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start() {
    _logger->debug("Start the acceptor on {}", _socket);
    running.store(true);
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
}

// See Connection.h
void Connection::OnError() {
    _logger->debug("Error on {}", _socket);
    running.store(false);
}

// See Connection.h
void Connection::OnClose() {
    _logger->debug("Closing {}", _socket);
    running.store(false);
}

// See Connection.h
void Connection::DoRead() {
    _logger->debug("Reading on {}", _socket);
    int client_socket = _socket;

    // Here is connection state
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    try {
        int readed_bytes = -1;
        char client_buffer[4096];
        while ((readed_bytes = read(client_socket, client_buffer, sizeof(client_buffer))) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);

            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (readed_bytes > 0) {
                _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Send response
                    result += "\r\n";
                    _results.push_back(result);
                    _event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR;

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if (readed_bytes > 0) {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", client_socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {
    _logger->debug("Writing on {}", _socket);
    std::size_t idx = 0;
    struct iovec buffers[_results.size()];
    auto _results_it = _results.begin();

    for (auto i = 0; i < _results.size(); ++i, ++_results_it) {
        buffers[i].iov_base = &(*_results_it)[0];
        buffers[i].iov_len = (*_results_it).size();
    }

    buffers[0].iov_base = (char *) buffers[0].iov_base + _first_byte;
    buffers[0].iov_len -= _first_byte;

    auto amount_placed_bytes = writev(_socket, buffers, _results.size());
    if (amount_placed_bytes == -1) {
        throw std::runtime_error(std::string(strerror(errno)));
    }
    _first_byte += amount_placed_bytes;

    _results_it = _results.begin();
    for (auto result : _results) {
        if (_first_byte < result.size()) {
            break;
        }
        _first_byte -= result.size();
        _results_it++;
    }

    _results.erase(_results.begin(), _results_it);

    if(_results.size() == 0) {
        _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
    } else {
        _event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR;
    }
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
