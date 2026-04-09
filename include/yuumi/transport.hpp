#pragma once
#include <yuumi/types.hpp>
#include <asio.hpp>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <asio/windows/stream_handle.hpp>
#endif

namespace yuumi {

    class Transport {
    public:
#if defined(_WIN32)
        using SocketType = asio::windows::stream_handle;
#else
        using SocketType = asio::local::stream_protocol::socket;
#endif

        explicit Transport(asio::io_context& ctx) : _socket(ctx) {}

        Result<> listen(const std::string& pipe_name) {
            try {
#if defined(_WIN32)
                std::string full_pipe_path = "\\\\.\\pipe\\" + pipe_name;
                HANDLE hPipe = CreateNamedPipeA(
                    full_pipe_path.c_str(),
                    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                    1, 1024 * 16, 1024 * 16, 0, NULL);

                if (hPipe == INVALID_HANDLE_VALUE) return std::unexpected(Error::ConnectionFailed);

                OVERLAPPED ov{};
                ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                if (ov.hEvent == NULL) {
                    CloseHandle(hPipe);
                    return std::unexpected(Error::ConnectionFailed);
                }

                BOOL ok = ConnectNamedPipe(hPipe, &ov);
                if (!ok) {
                    DWORD err = GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        WaitForSingleObject(ov.hEvent, INFINITE);
                    } else if (err != ERROR_PIPE_CONNECTED) {
                        CloseHandle(ov.hEvent);
                        CloseHandle(hPipe);
                        return std::unexpected(Error::ConnectionFailed);
                    }
                }
                CloseHandle(ov.hEvent);

                _socket.assign(hPipe);
#else
                std::string socket_path = resolve_socket_path(pipe_name);
                std::filesystem::remove(socket_path);
                asio::local::stream_protocol::endpoint ep(socket_path);
                asio::local::stream_protocol::acceptor acceptor(_socket.get_executor(), ep);
                acceptor.accept(_socket);
#endif
                return {};
            } catch (...) {
                return std::unexpected(Error::ConnectionFailed);
            }
        }

        void close() {
            asio::error_code ec;
            _socket.close(ec);
        }

        SocketType& socket() { return _socket; }

    private:
        SocketType _socket;

#if !defined(_WIN32)
        static std::string resolve_socket_path(const std::string& pipe_name) {
            std::filesystem::path p(pipe_name);
            if (p.is_absolute()) return pipe_name;
            std::error_code ec;
            auto temp_dir = std::filesystem::temp_directory_path(ec);
            if (ec || temp_dir.empty()) {
                ec.clear();
                temp_dir = std::filesystem::current_path(ec);
                if (ec || temp_dir.empty()) {
                    temp_dir = ".";
                }
            }
            p = temp_dir / pipe_name;
            if (!p.has_extension()) p += ".sock";
            return p.string();
        }
#endif
    };
}

/*
 * Transport: Platform-specific IPC layer.
 *
 * Windows path:
 *   Uses Windows Named Pipes (\\.\pipe\<name>) via CreateNamedPipeA and
 *   assigns the connected HANDLE to asio::windows::stream_handle.
 *
 * Unix path:
 *   Creates a Unix domain socket at <temp_dir>/<pipe_name>.sock (or at the
 *   absolute path if already absolute), removing stale socket files before bind.
 *
 * close():
 *   Exposed for Bridge::stop() to cancel any blocking asio::read/asio::write
 *   in the I/O threads before joining them. Without this, stop() would deadlock
 *   if a thread is stuck inside a synchronous socket operation.
 */
