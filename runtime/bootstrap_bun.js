const fs = require("fs");

const EXEC_CODE = 0x01;
const EXEC_RESULT = 0x02;
const REQ_INPUT = 0x03;
const RES_INPUT = 0x04;
const SYS_EVENT = 0x05;

const HEADER_SIZE = 4;
const TYPE_SIZE = 1;
const MAX_PAYLOAD = 16 * 1024 * 1024;
const STDIN_FD = 0;
const STDOUT_FD = 1;

const capturedOutput = [];
const capturedErrors = [];
const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
let sharedRequestId = 0;

const originalLog = console.log;
const originalError = console.error;

function writeAll(fd, data) {
    let offset = 0;
    while (offset < data.length) {
        const written = fs.writeSync(fd, data, offset, data.length - offset);
        if (written <= 0) {
            break;
        }
        offset += written;
    }
}

function writeFrame(type, payload) {
    const payloadBuf = Buffer.from(payload, "utf-8");
    const header = Buffer.alloc(HEADER_SIZE + TYPE_SIZE);
    header.writeUInt32LE(payloadBuf.length, 0);
    header.writeUInt8(type, HEADER_SIZE);
    writeAll(STDOUT_FD, Buffer.concat([header, payloadBuf]));
}

function readExact(size) {
    if (size <= 0) {
        return Buffer.alloc(0);
    }
    const out = Buffer.alloc(size);
    let offset = 0;
    while (offset < size) {
        const read = fs.readSync(STDIN_FD, out, offset, size - offset, null);
        if (read <= 0) {
            return null;
        }
        offset += read;
    }
    return out;
}

function readFrame() {
    const header = readExact(HEADER_SIZE + TYPE_SIZE);
    if (!header) {
        return null;
    }
    const payloadLen = header.readUInt32LE(0);
    if (payloadLen > MAX_PAYLOAD) {
        return null;
    }
    const type = header.readUInt8(HEADER_SIZE);
    const payload = payloadLen > 0 ? readExact(payloadLen) : Buffer.alloc(0);
    if (payload == null) {
        return null;
    }
    return {
        type,
        payload: payload.toString("utf-8")
    };
}

function sendSysEvent(status, extra) {
    const obj = { status, ...extra };
    writeFrame(SYS_EVENT, JSON.stringify(obj));
}

function sendExecResult(output, error, exitCode) {
    writeFrame(EXEC_RESULT, JSON.stringify({
        output,
        error,
        exitCode
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
    console.log = originalLog;
    console.error = originalError;
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

function waitForInputResponse() {
    while (true) {
        const frame = readFrame();
        if (!frame) {
            return "";
        }
        if (frame.type === RES_INPUT) {
            try {
                const parsed = JSON.parse(frame.payload);
                return typeof parsed.value === "string" ? parsed.value : String(parsed.value ?? "");
            } catch {
                return frame.payload;
            }
        }
        if (frame.type === SYS_EVENT) {
            handleSysEvent(frame.payload);
        }
    }
}

function nextSharedRequestId() {
    sharedRequestId += 1;
    return sharedRequestId;
}

function waitForSharedResponse(expectedType, requestId) {
    while (true) {
        const frame = readFrame();
        if (!frame) {
            return null;
        }
        if (frame.type !== SYS_EVENT) {
            continue;
        }
        let parsed;
        try {
            parsed = JSON.parse(frame.payload);
        } catch {
            handleSysEvent(frame.payload);
            continue;
        }
        if (parsed.type === expectedType && parsed.request_id === requestId) {
            return parsed;
        }
        handleSysEvent(frame.payload);
    }
}

globalThis.prompt = function (message) {
    writeFrame(REQ_INPUT, JSON.stringify({ prompt: String(message || "") }));
    return waitForInputResponse();
};

globalThis.zeri = {
    async get(key) {
        const requestId = nextSharedRequestId();
        writeFrame(SYS_EVENT, JSON.stringify({
            type: "shared_get",
            key: String(key),
            request_id: requestId
        }));
        const response = waitForSharedResponse("shared_value", requestId);
        if (!response || !Object.prototype.hasOwnProperty.call(response, "value")) {
            return null;
        }
        return response.value;
    },
    async set(key, value) {
        const requestId = nextSharedRequestId();
        writeFrame(SYS_EVENT, JSON.stringify({
            type: "shared_set",
            key: String(key),
            value,
            request_id: requestId
        }));
        const response = waitForSharedResponse("shared_ack", requestId);
        if (response && typeof response.error === "string" && response.error.length > 0) {
            throw new Error(response.error);
        }
    }
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
        const fn = new AsyncFunction(`"use strict";\n${code}`);
        const result = await fn();
        if (result !== undefined) {
            capturedOutput.push(String(result));
        }
    } catch (err) {
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

async function main() {
    sendSysEvent("READY");
    while (true) {
        const frame = readFrame();
        if (!frame) {
            break;
        }
        switch (frame.type) {
            case EXEC_CODE:
                await handleExecCode(frame.payload);
                break;
            case SYS_EVENT:
                handleSysEvent(frame.payload);
                break;
            default:
                break;
        }
    }
}

main().catch((err) => {
    sendSysEvent("CRASHED", { error: String(err) });
    process.exit(1);
});

/*
Implements Zeri-Wire over raw fd reads/writes for Bun sidecar execution.
Supports synchronous in-execution IPC for prompt() and cross-language shared
scope requests through SYS_EVENT frames while preserving EXEC_RESULT framing.
*/
