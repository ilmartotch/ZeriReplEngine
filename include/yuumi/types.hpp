#pragma once
#include <expected>
#include <string_view>
#include <cstdint>

namespace yuumi {

    inline constexpr uint32_t PROTOCOL_VERSION = 2;

    enum class StatusCode : uint32_t {
        HANDSHAKE_START = 100,
        CONNECTING = 101,
        
        OK_CONNECTED = 200,
        OK_MESSAGE_RECEIVED = 201,
        OK_HEARTBEAT = 202,

        ERR_MAGIC_MISMATCH = 400,
        ERR_VERSION_MISMATCH = 401,
        ERR_PID_MISMATCH = 402,
        ERR_PROTOCOL_VIOLATION = 403,

        ERR_PIPE_FAILED = 500,
        ERR_READ_TIMEOUT = 501,
        ERR_WRITE_FAILED = 502,
        ERR_CONNECTION_LOST = 503,
        ERR_INTERNAL = 599
    };

    enum class Error {
        ConnectionFailed = static_cast<int>(StatusCode::ERR_PIPE_FAILED),
        ConnectionLost = static_cast<int>(StatusCode::ERR_CONNECTION_LOST),
        PidMismatch = static_cast<int>(StatusCode::ERR_PID_MISMATCH),
        ProtocolViolation = static_cast<int>(StatusCode::ERR_PROTOCOL_VIOLATION),
        SendError = static_cast<int>(StatusCode::ERR_WRITE_FAILED),
        ReadError = static_cast<int>(StatusCode::ERR_READ_TIMEOUT),
        HandshakeFailed = static_cast<int>(StatusCode::ERR_MAGIC_MISMATCH),
        Timeout = static_cast<int>(StatusCode::ERR_READ_TIMEOUT)
    };

    template <typename T = void>
    using Result = std::expected<T, Error>;

    struct Handshake {
        uint32_t magic = 0x59554d49; 
        uint32_t version = PROTOCOL_VERSION;
        uint32_t pid = 0;
    };

    enum class Channel : uint8_t {
        Control = 0,
        Command = 1,
        Log = 2,
        Data = 3
    };

    constexpr std::string_view to_string(Error e) {
        switch (e) {
            case Error::PidMismatch: return "Security: UI Process ID does not match expectation";
            case Error::HandshakeFailed: return "Handshake: Invalid version or magic number";
            case Error::ProtocolViolation: return "Protocol: Invalid framing or JSON payload";
            case Error::ConnectionLost: return "Transport: Connection lost unexpectedly";
            default: return "Internal Error";
        }
    }
}

/*
 * Yuumi Protocol Definitions:
 * - StatusCode: Univocal status codes for diagnostics (1xx: Info, 2xx: Success, 4xx/5xx: Errors).
 * - Error: Mapping of status codes to bridge error types.
 * - Handshake: Binary structure for initial connection verification (Magic, Version, PID).
 * - Channel: Multiplexing identifiers for different data streams (Control, Command, Log, Data).
 */
