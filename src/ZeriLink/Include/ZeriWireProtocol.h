#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Zeri::Link {

    enum class MsgType : uint8_t {
        EXEC_CODE   = 0x01,
        EXEC_RESULT = 0x02,
        REQ_INPUT   = 0x03,
        RES_INPUT   = 0x04,
        SYS_EVENT   = 0x05
    };

    struct ZeriFrame {
        MsgType type;
        std::string payload;
    };

    inline constexpr size_t kHeaderSize     = 4;
    inline constexpr size_t kTypeSize       = 1;
    inline constexpr size_t kMaxPayload    = 16u * 1024u * 1024u;

    [[nodiscard]] std::vector<std::byte> EncodeFrame(const ZeriFrame& frame);

    class FrameDecoder {
    public:
        FrameDecoder();

        [[nodiscard]] std::optional<ZeriFrame> Feed(std::span<const std::byte> data);
        void Reset();

    private:
        static constexpr size_t kInitialReserve = 8192;

        enum class State : uint8_t {
            READ_HEADER,
            READ_TYPE,
            READ_PAYLOAD
        };

        void ResetState();
        void SavePending(const std::byte* src, size_t len, size_t pos);

        State m_state{ State::READ_HEADER };
        std::array<std::byte, kHeaderSize> m_headerBuf{};
        size_t m_headerPos{ 0 };
        uint32_t m_payloadLen{ 0 };
        MsgType m_frameType{};
        std::string m_payload;
        size_t m_payloadPos{ 0 };
        std::vector<std::byte> m_pending;
    };

}

/*
Wire format for a single frame:
  [4 bytes] uint32_t little-endian — payload length (N)
  [1 byte]  MsgType — message type
  [N bytes] UTF-8 JSON string — payload (opaque to this layer)

MsgType values:
  0x01 EXEC_CODE Host -> Sidecar Source code to execute
  0x02 EXEC_RESULT Sidecar -> Host Execution output, error, exitCode
  0x03 REQ_INPUT Sidecar -> Host Sidecar needs user input (e.g. input()/prompt())
  0x04 RES_INPUT Host -> Sidecar User-provided input response
  0x05 SYS_EVENT Bidirectional Handshake (READY), errors, TERMINATE

EncodeFrame serializes a ZeriFrame into the wire format as a contiguous byte vector.

FrameDecoder is an incremental state machine that consumes raw bytes from the pipe
and produces complete ZeriFrame objects.
  - Partial reads (pipe fragmentation across multiple ReceiveData calls)
  - Frame boundaries within a single read (leftover bytes saved in m_pending)
  - Safety cap at 16MB payload to reject malformed or malicious frames
  - Pre-allocated buffers (m_payload reserves capacity, m_pending reserves 8KB)

The decoder does not interpret or validate the JSON payload; it is treated as an
opaque string. JSON parsing is the responsibility of the ProcessBridge dispatch layer.
*/
