import sys
import os
import json
import struct
import io
import builtins
import traceback

EXEC_CODE = 0x01
EXEC_RESULT = 0x02
REQ_INPUT = 0x03
RES_INPUT = 0x04
SYS_EVENT = 0x05

HEADER_SIZE = 4
TYPE_SIZE = 1
MAX_PAYLOAD = 16 * 1024 * 1024

_stdin_fd = sys.stdin.fileno()
_stdout_fd = sys.stdout.fileno()

_decoder = None

_global_ns = {"__builtins__": builtins}


def _write_all(fd, data):
    offset = 0
    length = len(data)
    while offset < length:
        written = os.write(fd, data[offset:])
        offset += written


def write_frame(msg_type, payload):
    payload_bytes = payload.encode("utf-8")
    header = struct.pack("<IB", len(payload_bytes), msg_type)
    _write_all(_stdout_fd, header + payload_bytes)


def send_sys_event(status, extra=None):
    obj = {"status": status}
    if extra:
        obj.update(extra)
    write_frame(SYS_EVENT, json.dumps(obj))


def send_exec_result(output, error, exit_code):
    write_frame(EXEC_RESULT, json.dumps({
        "output": output,
        "error": error,
        "exitCode": exit_code
    }))


def _zeri_input(prompt_msg=""):
    write_frame(REQ_INPUT, json.dumps({"prompt": str(prompt_msg)}))
    while True:
        chunk = os.read(_stdin_fd, 4096)
        if not chunk:
            return ""
        frames = _decoder.feed(chunk)
        for msg_type, payload in frames:
            if msg_type == RES_INPUT:
                try:
                    parsed = json.loads(payload)
                    return parsed.get("value", "")
                except (json.JSONDecodeError, ValueError):
                    return payload
            elif msg_type == SYS_EVENT:
                _handle_sys_event(payload)


builtins.input = _zeri_input


class FrameDecoder:
    def __init__(self):
        self._buf = b""

    def feed(self, chunk):
        self._buf += chunk
        frames = []

        while len(self._buf) >= HEADER_SIZE + TYPE_SIZE:
            payload_len = struct.unpack_from("<I", self._buf, 0)[0]

            if payload_len > MAX_PAYLOAD:
                self._buf = b""
                break

            total = HEADER_SIZE + TYPE_SIZE + payload_len
            if len(self._buf) < total:
                break

            msg_type = self._buf[HEADER_SIZE]
            payload = self._buf[HEADER_SIZE + TYPE_SIZE:total].decode("utf-8")

            frames.append((msg_type, payload))
            self._buf = self._buf[total:]

        return frames


def _handle_exec_code(payload):
    try:
        parsed = json.loads(payload)
    except (json.JSONDecodeError, ValueError):
        send_exec_result("", "Invalid JSON in EXEC_CODE payload", -1)
        return

    code = parsed.get("code") or parsed.get("source") or ""

    stdout_cap = io.StringIO()
    stderr_cap = io.StringIO()

    saved_out, saved_err = sys.stdout, sys.stderr
    sys.stdout = stdout_cap
    sys.stderr = stderr_cap

    try:
        try:
            compiled = compile(code, "<zeri>", "eval")
            result = eval(compiled, _global_ns)
            if result is not None:
                print(repr(result))
        except SyntaxError:
            compiled = compile(code, "<zeri>", "exec")
            exec(compiled, _global_ns)
    except SystemExit:
        pass
    except Exception:
        sys.stdout, sys.stderr = saved_out, saved_err
        output = stdout_cap.getvalue()
        if output.endswith("\n"):
            output = output[:-1]
        err_text = stderr_cap.getvalue()
        tb_text = traceback.format_exc()
        error = (err_text + tb_text).strip() if err_text else tb_text.strip()
        send_exec_result(output, error, 1)
        return

    sys.stdout, sys.stderr = saved_out, saved_err

    output = stdout_cap.getvalue()
    if output.endswith("\n"):
        output = output[:-1]

    send_exec_result(output, None, 0)


def _handle_res_input(payload):
    pass


def _handle_sys_event(payload):
    try:
        parsed = json.loads(payload)
    except (json.JSONDecodeError, ValueError):
        return

    if parsed.get("status") == "TERMINATE":
        os._exit(0)


def main():
    global _decoder
    _decoder = FrameDecoder()

    send_sys_event("READY")

    while True:
        try:
            chunk = os.read(_stdin_fd, 4096)
        except OSError:
            break

        if not chunk:
            break

        frames = _decoder.feed(chunk)

        for msg_type, payload in frames:
            if msg_type == EXEC_CODE:
                _handle_exec_code(payload)
            elif msg_type == RES_INPUT:
                _handle_res_input(payload)
            elif msg_type == SYS_EVENT:
                _handle_sys_event(payload)


if __name__ == "__main__":
    try:
        main()
    except Exception as err:
        send_sys_event("CRASHED", {"error": str(err)})
        sys.exit(1)


"""
Implements the Zeri-Wire binary protocol over raw file descriptors
(stdin fd 0, stdout fd 1) for bidirectional communication with the
C++ ProcessBridge core. Launched by ProcessBridge as:

    python -u bootstrap_python.py

The -u flag disables Python's internal I/O buffering so that frames
written to stdout reach the pipe immediately without stalling.

Wire format (matches ZeriWireProtocol.h and bootstrap_bun.js):
  [4 bytes] uint32 LE  — payload length (N)
  [1 byte ] MsgType — message type
  [N bytes] UTF-8 JSON — payload (opaque to this layer)

MsgType constants:
  0x01 EXEC_CODE Host  -> Sidecar   Source code to execute
  0x02 EXEC_RESULT Sidecar -> Host    Output, error, exitCode
  0x03 REQ_INPUT Sidecar -> Host    Sidecar needs user input
  0x04 RES_INPUT Host  -> Sidecar   User-provided input value
  0x05 SYS_EVENT Bidirectional      READY, TERMINATE, CRASHED

Startup:
  Sends SYS_EVENT {"status":"READY"} on stdout to unblock the C++
  ProcessBridge::Launch() handshake, identical to bootstrap_bun.js.

Execution model:
  User code is compiled with compile() in a two-pass strategy:
    1. "eval" mode — single expressions (2+3, len("hi"), x).
       If successful, the result is printed via repr() to replicate
       Python's interactive REPL behaviour (None is suppressed).
    2. "exec" mode — fallback for statements, assignments, loops,
       multi-line blocks. No implicit result printing.
  A persistent namespace dict (_global_ns) is shared across all
  EXEC_CODE invocations so that variables survive between calls,
  mirroring the globalScope object in the JS sidecar.
  SystemExit raised by user code is caught and suppressed so that
  sys.exit() inside user snippets does not kill the sidecar.

Output capture:
  sys.stdout and sys.stderr are temporarily replaced with StringIO
  instances during execution. All print() and sys.stderr.write()
  output is captured and returned in the EXEC_RESULT JSON payload.
  Protocol I/O bypasses the redirected streams entirely: write_frame
  calls os.write(_stdout_fd, ...) on the original stdout file
  descriptor, which is saved at module-load time before any redirect.
  This prevents user code from corrupting the wire protocol.
  A single trailing newline is stripped from captured stdout to match
  the JS sidecar behaviour (console.log entries joined with "\n").

Input handling (builtins.input override):
  builtins.input is replaced with _zeri_input at module-load time.
  When user code calls input("prompt"), the override:
    1. Sends a REQ_INPUT frame with the prompt message.
    2. Enters a blocking os.read loop on stdin, feeding chunks to
       the shared FrameDecoder until a RES_INPUT frame arrives.
    3. Returns the "value" field from the RES_INPUT JSON payload.
  SYS_EVENT frames received while waiting are dispatched immediately
  (e.g. TERMINATE triggers os._exit). Because execution is single-
  threaded and synchronous, the main read loop is naturally paused
  while _zeri_input blocks — no threading or locks are required, and
  there is no concurrent access to the FrameDecoder.

FrameDecoder:
  Python port of the C++ and JS decoders. Accumulates raw bytes in
  self._buf via concatenation, parses [header][type][payload] frames,
  and returns a list of (msg_type, payload_str) tuples per feed()
  call. Includes the 16 MB safety cap to reject malformed frames.
  Leftover bytes from partial frames remain in self._buf across
  consecutive feed() calls, handling pipe fragmentation.

Shutdown:
  SYS_EVENT {"status":"TERMINATE"} triggers os._exit(0) for instant
  process termination, bypassing exception handlers and matching the
  process.exit(0) call in the JS sidecar.
  If the main loop throws an unhandled exception, a SYS_EVENT CRASHED
  frame is sent before sys.exit(1) so the C++ side can detect and
  report the failure.

I/O strategy:
  Raw file descriptors (os.read / os.write) bypass Python's buffered
  I/O layer entirely, giving deterministic single-syscall semantics.
  _write_all loops until every byte is flushed, guarding against
  partial writes on large frames. struct.pack("<IB", length, type)
  encodes the 5-byte header (4 LE uint32 + 1 uint8) in one call.

_handle_res_input in the main dispatch is a no-op by design.
  RES_INPUT is only meaningful inside the _zeri_input blocking loop
  (which reads directly from stdin during exec). If a RES_INPUT
  arrives outside of an active input() call (protocol violation),
  it is safely discarded.

Dependencies: Python 3.10+ standard library only. No pip packages.
"""