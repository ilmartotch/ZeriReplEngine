#pragma once

namespace Zeri::Ui {

inline constexpr int ZERI_PROTOCOL_VERSION = 1;

inline constexpr const char* kBridgeTypeHandshake = "handshake";
inline constexpr const char* kBridgeTypeStreamBatchEnd = "stream_batch_end";

inline constexpr const char* kBatchEndExecutionComplete = "execution_complete";
inline constexpr const char* kBatchEndBeforeInputRequest = "before_input_request";
inline constexpr const char* kBatchEndContextTransition = "context_transition";
inline constexpr const char* kBatchEndRuntimeIdle = "runtime_idle";
inline constexpr const char* kBatchEndEngineShutdown = "engine_shutdown";

}

/*
BridgeProtocol.h centralizes app-level bridge protocol constants shared across
the engine-side bridge emitters.

ZERI_PROTOCOL_VERSION is the JSON-level protocol version exchanged in the
handshake frame, independent from the low-level yuumi transport handshake.

Batch-end reason literals are centralized to keep C++ emit points consistent
with Go-side message routing.
*/
