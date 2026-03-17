const EXEC_CODE = 0x01;
const EXEC_RESULT = 0x02;
const REQ_INPUT = 0x03;
const RES_INPUT = 0x04;
const SYS_EVENT = 0x05;

const HEADER_SIZE = 4;
const TYPE_SIZE = 1;

let inputResolve = null;

const globalScope = {};

const capturedOutput = [];
const capturedErrors = [];

const originalLog = console.log;
const originalError = console.error;

function writeFrame(type, payload) {
    const payloadBuf = Buffer.from(payload, "utf-8");
    const frame = Buffer.alloc(HEADER_SIZE + TYPE_SIZE + payloadBuf.length);
    frame.writeUInt32LE(payloadBuf.length, 0);
    frame.writeUInt8(type, HEADER_SIZE);
    payloadBuf.copy(frame, HEADER_SIZE + TYPE_SIZE);
    Bun.write(Bun.stdout, frame);
}

function sendSysEvent(status, extra) {
    const obj = { status, ...extra };
    writeFrame(SYS_EVENT, JSON.stringify(obj));
}

function sendExecResult(output, error, exitCode) {
    writeFrame(EXEC_RESULT, JSON.stringify({
        output: output,
        error: error,
        exitCode: exitCode
    }));
}

function overrideConsole() {
    capturedOutput.length = 0;
    capturedErrors.length = 0;

    console.log = (...args) => {
        capturedOutput.push(args.map(String).join(" "));
    };
    console.error = (...args) => {
        capturedErrors.push(args.map(String).join(" "));
    };
}

function restoreConsole() {
    console.log   = originalLog;
    console.error = originalError;
}

globalThis.prompt = function(message) {
    throw new Error("__ZERI_INPUT_REQUEST__:" + (message || ""));
};

async function handleExecCode(payload) {
    let parsed;
    try {
        parsed = JSON.parse(payload);
    } catch {
        sendExecResult("", "Invalid JSON in EXEC_CODE payload", -1);
        return;
    }

    const code = parsed.code || parsed.source || "";

    overrideConsole();

    try {
        const fn = new Function(
            ...Object.keys(globalScope),
            `"use strict";\n${code}`
        );
        const result = fn(...Object.values(globalScope));

        if (result !== undefined) {
            capturedOutput.push(String(result));
        }
    } catch (err) {
        if (typeof err.message === "string" && err.message.startsWith("__ZERI_INPUT_REQUEST__:")) {
            restoreConsole();
            const promptMsg = err.message.slice("__ZERI_INPUT_REQUEST__:".length);
            await handleInputRequest(promptMsg);
            return;
        }

        const errStr = capturedErrors.length > 0
            ? capturedErrors.join("\n") + "\n" + String(err)
            : String(err);

        restoreConsole();
        sendExecResult(capturedOutput.join("\n"), errStr, 1);
        return;
    }

    restoreConsole();
    sendExecResult(capturedOutput.join("\n"), null, 0);
}

async function handleInputRequest(promptMessage) {
    writeFrame(REQ_INPUT, JSON.stringify({ prompt: promptMessage }));

    const response = await waitForInputResponse();

    let value = "";
    try {
        const parsed = JSON.parse(response);
        value = parsed.value || "";
    } catch {
        value = response;
    }

    globalScope["__zeri_last_input"] = value;

    const resumeCode = `var __input_result = __zeri_last_input;`;
    await handleExecCode(JSON.stringify({ code: resumeCode }));
}

function waitForInputResponse() {
    return new Promise((resolve) => {
        inputResolve = resolve;
    });
}

function handleResInput(payload) {
    if (inputResolve) {
        const cb = inputResolve;
        inputResolve = null;
        cb(payload);
    }
}

function handleSysEvent(payload) {
    let parsed;
    try {
        parsed = JSON.parse(payload);
    } catch {
        return;
    }

    if (parsed.status === "TERMINATE") {
        process.exit(0);
    }
}

class FrameDecoder {
    constructor() {
        this.buffer = Buffer.alloc(0);
    }

    feed(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        const frames = [];

        while (this.buffer.length >= HEADER_SIZE + TYPE_SIZE) {
            const payloadLen = this.buffer.readUInt32LE(0);

            if (payloadLen > 16 * 1024 * 1024) {
                this.buffer = Buffer.alloc(0);
                break;
            }

            const totalLen = HEADER_SIZE + TYPE_SIZE + payloadLen;
            if (this.buffer.length < totalLen) break;

            const type = this.buffer.readUInt8(HEADER_SIZE);
            const payload = this.buffer.slice(HEADER_SIZE + TYPE_SIZE, totalLen).toString("utf-8");

            frames.push({ type, payload });
            this.buffer = this.buffer.slice(totalLen);
        }

        return frames;
    }
}

async function main() {
    const decoder = new FrameDecoder();

    sendSysEvent("READY");

    const stream = Bun.stdin.stream();
    const reader = stream.getReader();

    while (true) {
        const { value, done } = await reader.read();
        if (done) break;

        const frames = decoder.feed(Buffer.from(value));

        for (const frame of frames) {
            switch (frame.type) {
                case EXEC_CODE:
                    await handleExecCode(frame.payload);
                    break;
                case RES_INPUT:
                    handleResInput(frame.payload);
                    break;
                case SYS_EVENT:
                    handleSysEvent(frame.payload);
                    break;
            }
        }
    }
}

main().catch((err) => {
    sendSysEvent("CRASHED", { error: String(err) });
    process.exit(1);
});

/*
Implements the Zeri-Wire binary protocol over stdin/stdout for bidirectional
communication with the C++ ProcessBridge core.

Wire format (matches ZeriWireProtocol.h):
  [4 bytes] uint32 LE = payload length (N)
  [1 byte] MsgType
  [N bytes] JSON payload (UTF-8)

Startup:
  Sends SYS_EVENT {"status":"READY"} immediately on stdout to unblock
  the ProcessBridge::Launch() handshake. The C++ side waits for this
  frame before marking the sidecar as operational.

Execution model:
  Code received via EXEC_CODE is executed using new Function() with the
  globalScope object destructured as parameters. This provides:
  - Persistent state: variables added to globalScope survive across executions.
  - Isolation: "use strict" prevents accidental global pollution.
  - Safety: new Function() is preferred over raw eval() because it creates
    a new scope while still allowing access to shared state via globalScope.

Console capture:
  console.log and console.error are temporarily overridden during execution
  to capture all output into arrays. After execution, the originals are restored.
  This ensures sidecar diagnostic output (if any) doesn't corrupt the wire
  protocol on stdout.

Input handling (prompt):
  globalThis.prompt is overridden to throw a sentinel error
  (__ZERI_INPUT_REQUEST__). When caught in handleExecCode:
  1. A REQ_INPUT frame is sent to the C++ host with the prompt message.
  2. The sidecar awaits a RES_INPUT frame via a Promise (waitForInputResponse).
  3. The user's response is stored in globalScope.__zeri_last_input.
  4. Execution resumes with a synthetic code snippet that reads the value.
  This mechanism avoids blocking stdin (which carries the wire protocol)
  and keeps the async read loop running during the input wait.

FrameDecoder:
  JavaScript port of the C++ FrameDecoder state machine. Uses Buffer.concat
  for accumulation and readUInt32LE/readUInt8 for header parsing. Includes
  the same 16MB safety cap to reject malformed frames.

Shutdown:
  On SYS_EVENT {"status":"TERMINATE"}, process.exit(0) is called immediately.
  If the main loop throws, a SYS_EVENT CRASHED is sent before exiting with
  code 1, allowing the C++ side to detect the failure.

Dependencies: Bun native APIs only (Bun.stdin.stream, Bun.write, Buffer).
No npm packages required.
*/
