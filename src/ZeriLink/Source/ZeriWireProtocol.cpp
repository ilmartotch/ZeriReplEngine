#include "../Include/ZeriWireProtocol.h"

namespace Zeri::Link {

    std::vector<std::byte> EncodeFrame(const ZeriFrame& frame) {
        const auto payloadLen = static_cast<uint32_t>(frame.payload.size());
        const size_t totalSize = kHeaderSize + kTypeSize + payloadLen;

        std::vector<std::byte> out(totalSize);

        out[0] = static_cast<std::byte>(payloadLen & 0xFFu);
        out[1] = static_cast<std::byte>((payloadLen >> 8) & 0xFFu);
        out[2] = static_cast<std::byte>((payloadLen >> 16) & 0xFFu);
        out[3] = static_cast<std::byte>((payloadLen >> 24) & 0xFFu);

        out[4] = static_cast<std::byte>(frame.type);

        if (payloadLen > 0) {
            std::memcpy(out.data() + 5, frame.payload.data(), payloadLen);
        }

        return out;
    }

    FrameDecoder::FrameDecoder() {
        m_pending.reserve(kInitialReserve);
        m_payload.reserve(kInitialReserve);
    }

    std::optional<ZeriFrame> FrameDecoder::Feed(std::span<const std::byte> data) {
        const std::byte* ptr = data.data();
        size_t len = data.size();

        if (!m_pending.empty()) {
            m_pending.insert(m_pending.end(), data.begin(), data.end());
            ptr = m_pending.data();
            len = m_pending.size();
        }

        size_t pos = 0;

        while (pos < len) {
            switch (m_state) {

            case State::READ_HEADER: {
                const size_t need = kHeaderSize - m_headerPos;
                const size_t take = std::min(need, len - pos);
                std::memcpy(m_headerBuf.data() + m_headerPos, ptr + pos, take);
                m_headerPos += take;
                pos += take;

                if (m_headerPos < kHeaderSize) break;

                m_payloadLen =
                    static_cast<uint32_t>(m_headerBuf[0]) |
                    (static_cast<uint32_t>(m_headerBuf[1]) << 8) |
                    (static_cast<uint32_t>(m_headerBuf[2]) << 16) |
                    (static_cast<uint32_t>(m_headerBuf[3]) << 24);

                if (m_payloadLen > kMaxPayload) {
                    Reset();
                    return std::nullopt;
                }

                m_state = State::READ_TYPE;
                break;
            }

            case State::READ_TYPE: {
                m_frameType = static_cast<MsgType>(ptr[pos]);
                ++pos;

                if (m_payloadLen == 0) {
                    ZeriFrame frame{ m_frameType, {} };
                    ResetState();
                    SavePending(ptr, len, pos);
                    return frame;
                }

                m_payload.resize(m_payloadLen);
                m_payloadPos = 0;
                m_state = State::READ_PAYLOAD;
                break;
            }

            case State::READ_PAYLOAD: {
                const size_t need = m_payloadLen - m_payloadPos;
                const size_t take = std::min(need, len - pos);
                std::memcpy(m_payload.data() + m_payloadPos, ptr + pos, take);
                m_payloadPos += take;
                pos += take;

                if (m_payloadPos < m_payloadLen) break;

                ZeriFrame frame{ m_frameType, std::move(m_payload) };
                ResetState();
                SavePending(ptr, len, pos);
                return frame;
            }

            }
        }

        m_pending.clear();
        return std::nullopt;
    }

    void FrameDecoder::Reset() {
        ResetState();
        m_pending.clear();
    }

    void FrameDecoder::ResetState() {
        m_state = State::READ_HEADER;
        m_headerPos = 0;
        m_payloadLen = 0;
        m_payloadPos = 0;
    }

    void FrameDecoder::SavePending(const std::byte* src, size_t len, size_t pos) {
        m_pending.clear();
        if (pos < len) {
            m_pending.assign(src + pos, src + len);
        }
    }

}

/*
EncodeFrame:
  Allocates a single contiguous vector of exact size (4 + 1 + N bytes).
  Writes the payload length as 4-byte little-endian, followed by the MsgType byte,
  followed by the raw payload bytes. Uses memcpy for the payload copy.
  The constants kHeaderSize and kTypeSize are referenced from FrameDecoder to
  keep the magic numbers centralized.

FrameDecoder::Feed — state machine walkthrough:

  READ_HEADER:
    Accumulates bytes into m_headerBuf (4 bytes). Handles partial headers across
    multiple Feed calls via m_headerPos. When 4 bytes are collected, decodes the
    payload length as uint32 little-endian using bitwise OR (no endian assumption
    on the host). If the length exceeds kMaxPayload (16MB), the decoder resets
    and returns nullopt to reject malformed frames.

  READ_TYPE:
    Reads a single byte and casts it to MsgType. If payload length is zero,
    the frame is immediately complete (e.g., a SYS_EVENT with no data).
    Otherwise, pre-sizes m_payload to the exact length and transitions to
    READ_PAYLOAD.

  READ_PAYLOAD:
    Copies bytes into m_payload via memcpy. Handles partial payloads across
    multiple Feed calls via m_payloadPos. When all bytes are collected,
    the payload is moved into the returned ZeriFrame (zero-copy transfer).

  Pending bytes (m_pending):
    When a Feed call delivers bytes spanning two frames (e.g., end of frame A +
    start of frame B), the decoder completes frame A and saves the remaining
    bytes in m_pending. On the next Feed call, m_pending is prepended to the
    new data so the state machine processes a single contiguous span.
    This is the only allocation path after construction and occurs only when
    frame boundaries fall within a single pipe read — a rare but valid case.

  ResetState vs Reset:
    ResetState clears the state machine fields (header position, payload position,
    state enum) but preserves m_pending. This is called after successfully
    extracting a frame, so leftover bytes survive for the next extraction.
    Reset clears everything including m_pending, used for error recovery.

  Buffer reuse:
    m_payload and m_pending are reserve'd in the constructor (8KB each).
    After std::move on m_payload, the string is in a valid-but-unspecified state;
    the next resize() may reuse the existing allocation or allocate fresh.
    For typical JSON payloads (< 1KB), SSO or the reserved buffer covers most cases.
*/
