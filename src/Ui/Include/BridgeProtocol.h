#pragma once

#include <string_view>

namespace Zeri::Ui {

inline constexpr int ZERI_PROTOCOL_VERSION = 1;

inline constexpr const char* kBridgeTypeIdHandshake = "HANDSHAKE";
inline constexpr const char* kBridgeTypeIdCommand = "COMMAND";
inline constexpr const char* kBridgeTypeIdInputResponse = "INPUT_RESPONSE";
inline constexpr const char* kBridgeTypeIdReady = "READY";
inline constexpr const char* kBridgeTypeIdOutput = "OUTPUT";
inline constexpr const char* kBridgeTypeIdError = "ERROR";
inline constexpr const char* kBridgeTypeIdInfo = "INFO";
inline constexpr const char* kBridgeTypeIdSuccess = "SUCCESS";
inline constexpr const char* kBridgeTypeIdContextChanged = "CONTEXT_CHANGED";
inline constexpr const char* kBridgeTypeIdReqInput = "REQ_INPUT";
inline constexpr const char* kBridgeTypeIdSelRequest = "SEL_REQUEST";
inline constexpr const char* kBridgeTypeIdListScriptsWithContent = "LIST_SCRIPTS_WITH_CONTENT";
inline constexpr const char* kBridgeTypeIdSettingsSnapshot = "SETTINGS_SNAPSHOT";
inline constexpr const char* kBridgeTypeIdSettingsUpdate = "SETTINGS_UPDATE";
inline constexpr const char* kBridgeTypeIdSessionSaveState = "SESSION_SAVE_STATE";
inline constexpr const char* kBridgeTypeIdSessionLoadState = "SESSION_LOAD_STATE";
inline constexpr const char* kBridgeTypeIdSaveScript = "SAVE_SCRIPT";
inline constexpr const char* kBridgeTypeIdDeleteScript = "DELETE_SCRIPT";
inline constexpr const char* kBridgeTypeIdRunScript = "RUN_SCRIPT";
inline constexpr const char* kBridgeTypeIdCancelExecution = "CANCEL_EXECUTION";
inline constexpr const char* kBridgeTypeIdSharedScopeSnapshot = "SHARED_SCOPE_SNAPSHOT";
inline constexpr const char* kBridgeTypeIdSharedGet = "SHARED_GET";
inline constexpr const char* kBridgeTypeIdSharedSet = "SHARED_SET";
inline constexpr const char* kBridgeTypeIdSharedList = "SHARED_LIST";
inline constexpr const char* kBridgeTypeIdSharedValue = "SHARED_VALUE";
inline constexpr const char* kBridgeTypeIdSharedAck = "SHARED_ACK";
inline constexpr const char* kBridgeTypeIdSharedListResponse = "SHARED_LIST_RESPONSE";
inline constexpr const char* kBridgeTypeIdScriptActionResponse = "SCRIPT_ACTION_RESPONSE";
inline constexpr const char* kBridgeTypeIdSessionStateResponse = "SESSION_STATE_RESPONSE";
inline constexpr const char* kBridgeTypeIdSettingsSnapshotResponse = "SETTINGS_SNAPSHOT_RESPONSE";
inline constexpr const char* kBridgeTypeIdSettingsUpdateResponse = "SETTINGS_UPDATE_RESPONSE";
inline constexpr const char* kBridgeTypeIdScriptListResponse = "SCRIPT_LIST_RESPONSE";
inline constexpr const char* kBridgeTypeIdSharedScopeSnapshotResponse = "SHARED_SCOPE_SNAPSHOT_RESPONSE";
inline constexpr const char* kBridgeTypeIdStreamBatchEnd = "STREAM_BATCH_END";
inline constexpr const char* kBridgeTypeIdShutdown = "SHUTDOWN";
inline constexpr const char* kBridgeTypeIdQuit = "QUIT";

[[nodiscard]] std::string_view BridgeTypeValue(std::string_view id);

inline constexpr const char* kBatchEndExecutionComplete = "execution_complete";
inline constexpr const char* kBatchEndBeforeInputRequest = "before_input_request";
inline constexpr const char* kBatchEndContextTransition = "context_transition";
inline constexpr const char* kBatchEndRuntimeIdle = "runtime_idle";
inline constexpr const char* kBatchEndEngineShutdown = "engine_shutdown";
inline constexpr const char* kBatchEndExecutionCancelled = "execution_cancelled";

}

/*
BridgeProtocol.h centralizes app-level bridge protocol constants shared across
the engine-side bridge emitters.

ZERI_PROTOCOL_VERSION is the JSON-level protocol version exchanged in the
handshake frame, independent from the low-level yuumi transport handshake.

Batch-end reason literals are centralized to keep emit points consistent with
Go-side message routing.
*/
