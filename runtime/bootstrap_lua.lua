local bit = require("bit")

local EXEC_CODE = 0x01
local EXEC_RESULT = 0x02
local REQ_INPUT = 0x03
local RES_INPUT = 0x04
local SYS_EVENT = 0x05

local HEADER_SIZE = 4
local TYPE_SIZE = 1
local MAX_PAYLOAD = 16 * 1024 * 1024

local stdin = io.stdin
local stdout = io.stdout

stdin:setvbuf("no")
stdout:setvbuf("no")

local shared_env = {
    math = math,
    string = string,
    table = table,
    io = io,
    os = os,
    tonumber = tonumber,
    tostring = tostring,
    type = type,
    pairs = pairs,
    ipairs = ipairs,
    next = next,
    select = select,
    pcall = pcall,
    xpcall = xpcall,
    error = error,
    assert = assert,
    print = print,
    _VERSION = _VERSION
}
shared_env._G = shared_env

local captured_output = {}
local captured_errors = {}

local function u32_to_le(n)
    local b1 = bit.band(n, 0xFF)
    local b2 = bit.band(bit.rshift(n, 8), 0xFF)
    local b3 = bit.band(bit.rshift(n, 16), 0xFF)
    local b4 = bit.band(bit.rshift(n, 24), 0xFF)
    return string.char(b1, b2, b3, b4)
end

local function le_to_u32(s)
    local b1, b2, b3, b4 = s:byte(1, 4)
    return b1 + bit.lshift(b2, 8) + bit.lshift(b3, 16) + bit.lshift(b4, 24)
end

local function json_escape(value)
    local out = value:gsub("\\", "\\\\")
    out = out:gsub('"', '\\"')
    out = out:gsub("\b", "\\b")
    out = out:gsub("\f", "\\f")
    out = out:gsub("\n", "\\n")
    out = out:gsub("\r", "\\r")
    out = out:gsub("\t", "\\t")
    out = out:gsub("[%z\1-\31]", function(c)
        return string.format("\\u%04x", c:byte())
    end)
    return out
end

local function json_unescape(value)
    local i = 1
    local len = #value
    local out = {}

    while i <= len do
        local ch = value:sub(i, i)
        if ch ~= "\\" then
            out[#out + 1] = ch
            i = i + 1
        else
            local nxt = value:sub(i + 1, i + 1)
            if nxt == '"' then
                out[#out + 1] = '"'
                i = i + 2
            elseif nxt == "\\" then
                out[#out + 1] = "\\"
                i = i + 2
            elseif nxt == "/" then
                out[#out + 1] = "/"
                i = i + 2
            elseif nxt == "b" then
                out[#out + 1] = "\b"
                i = i + 2
            elseif nxt == "f" then
                out[#out + 1] = "\f"
                i = i + 2
            elseif nxt == "n" then
                out[#out + 1] = "\n"
                i = i + 2
            elseif nxt == "r" then
                out[#out + 1] = "\r"
                i = i + 2
            elseif nxt == "t" then
                out[#out + 1] = "\t"
                i = i + 2
            elseif nxt == "u" then
                local hex = value:sub(i + 2, i + 5)
                local num = tonumber(hex, 16)
                if num and num <= 255 then
                    out[#out + 1] = string.char(num)
                end
                i = i + 6
            else
                i = i + 2
            end
        end
    end

    return table.concat(out)
end

local function json_extract_string_field(payload, key)
    local pattern = '"' .. key .. '"%s*:%s*"'
    local _, end_pos = payload:find(pattern)
    if not end_pos then
        return nil
    end

    local i = end_pos + 1
    local out = {}
    local escaped = false

    while i <= #payload do
        local ch = payload:sub(i, i)

        if escaped then
            out[#out + 1] = "\\" .. ch
            escaped = false
        elseif ch == "\\" then
            escaped = true
        elseif ch == '"' then
            break
        else
            out[#out + 1] = ch
        end

        i = i + 1
    end

    return json_unescape(table.concat(out))
end

local function write_frame(msg_type, payload)
    local payload_len = #payload
    local frame = u32_to_le(payload_len) .. string.char(msg_type) .. payload
    stdout:write(frame)
    stdout:flush()
end

local function send_sys_event(status, extra_json)
    local payload
    if extra_json and #extra_json > 0 then
        payload = '{"status":"' .. json_escape(status) .. '",' .. extra_json .. '}'
    else
        payload = '{"status":"' .. json_escape(status) .. '"}'
    end
    write_frame(SYS_EVENT, payload)
end

local function send_exec_result(output, err, exit_code)
    local err_json = "null"
    if err and #err > 0 then
        err_json = '"' .. json_escape(err) .. '"'
    end

    local payload = '{"output":"' .. json_escape(output or "") .. '","error":' .. err_json .. ',"exitCode":' .. tostring(exit_code or -1) .. '}'
    write_frame(EXEC_RESULT, payload)
end

local function read_exact(size)
    if size == 0 then
        return ""
    end

    local chunks = {}
    local total = 0

    while total < size do
        local remaining = size - total
        local chunk = stdin:read(remaining)

        if not chunk or #chunk == 0 then
            return nil
        end

        total = total + #chunk
        chunks[#chunks + 1] = chunk
    end

    if #chunks == 1 then
        return chunks[1]
    end

    return table.concat(chunks)
end

local function read_frame()
    local header = read_exact(HEADER_SIZE + TYPE_SIZE)
    if not header then
        return nil
    end

    local payload_len = le_to_u32(header:sub(1, HEADER_SIZE))
    if payload_len > MAX_PAYLOAD then
        return nil
    end

    local msg_type = header:byte(HEADER_SIZE + 1)
    local payload = ""
    if payload_len > 0 then
        payload = read_exact(payload_len)
        if not payload then
            return nil
        end
    end

    return {
        type = msg_type,
        payload = payload
    }
end

local function handle_sys_event(payload)
    local status = json_extract_string_field(payload, "status")
    if status == "TERMINATE" then
        os.exit(0)
    end
end

local function wait_for_input_response()
    while true do
        local frame = read_frame()
        if not frame then
            return ""
        end

        if frame.type == RES_INPUT then
            local value = json_extract_string_field(frame.payload, "value")
            if value == nil then
                return frame.payload
            end
            return value
        end

        if frame.type == SYS_EVENT then
            handle_sys_event(frame.payload)
        end
    end
end

local function install_io_hooks()
    shared_env.print = function(...)
        local parts = {}
        local n = select("#", ...)
        for i = 1, n do
            parts[#parts + 1] = tostring(select(i, ...))
        end
        captured_output[#captured_output + 1] = table.concat(parts, "\t")
    end

    local original_io = io
    local proxy_io = {}

    proxy_io.read = function(...)
        local prompt = ""
        local first = select(1, ...)
        if type(first) == "string" then
            prompt = first
        end

        write_frame(REQ_INPUT, '{"prompt":"' .. json_escape(prompt) .. '"}')
        return wait_for_input_response()
    end

    proxy_io.write = function(...)
        local n = select("#", ...)
        local parts = {}
        for i = 1, n do
            parts[#parts + 1] = tostring(select(i, ...))
        end
        captured_output[#captured_output + 1] = table.concat(parts, "")
        return true
    end

    proxy_io.stderr = {
        write = function(_, ...)
            local n = select("#", ...)
            local parts = {}
            for i = 1, n do
                parts[#parts + 1] = tostring(select(i, ...))
            end
            captured_errors[#captured_errors + 1] = table.concat(parts, "")
            return true
        end
    }

    proxy_io.stdout = {
        write = proxy_io.write
    }

    proxy_io.stdin = original_io.stdin
    proxy_io.open = original_io.open
    proxy_io.close = original_io.close
    proxy_io.flush = original_io.flush
    proxy_io.lines = original_io.lines
    proxy_io.input = original_io.input
    proxy_io.output = original_io.output
    proxy_io.popen = original_io.popen
    proxy_io.tmpfile = original_io.tmpfile
    proxy_io.type = original_io.type

    shared_env.io = proxy_io
end

local function execute_code(payload)
    local code = json_extract_string_field(payload, "code")
    if not code or #code == 0 then
        code = json_extract_string_field(payload, "source") or ""
    end

    if code == nil then
        send_exec_result("", "Invalid JSON in EXEC_CODE payload", -1)
        return
    end

    captured_output = {}
    captured_errors = {}

    local fn, load_err = loadstring(code)
    if not fn then
        send_exec_result("", tostring(load_err), 1)
        return
    end

    setfenv(fn, shared_env)

    local function traceback_handler(err)
        return debug.traceback(tostring(err), 2)
    end

    local ok, result = xpcall(fn, traceback_handler)
    if not ok then
        captured_errors[#captured_errors + 1] = tostring(result)
        send_exec_result(table.concat(captured_output, "\n"), table.concat(captured_errors, "\n"), 1)
        return
    end

    if result ~= nil then
        captured_output[#captured_output + 1] = tostring(result)
    end

    local err_text = nil
    if #captured_errors > 0 then
        err_text = table.concat(captured_errors, "\n")
    end

    send_exec_result(table.concat(captured_output, "\n"), err_text, 0)
end

local function main()
    install_io_hooks()
    send_sys_event("READY")

    while true do
        local frame = read_frame()
        if not frame then
            break
        end

        if frame.type == EXEC_CODE then
            execute_code(frame.payload)
        elseif frame.type == SYS_EVENT then
            handle_sys_event(frame.payload)
        end
    end
end

local ok, err = pcall(main)
if not ok then
    send_sys_event("CRASHED", '"error":"' .. json_escape(tostring(err)) .. '"')
    os.exit(1)
end

--[[
bootstrap_lua.lua implements ZeriWire protocol over stdin/stdout for LuaJIT.

Capabilities:
- Sends SYS_EVENT READY at startup.
- Handles EXEC_CODE in a persistent shared environment (state reuse across calls).
- Captures print/io.write output and returns EXEC_RESULT JSON payloads.
- Supports interactive input via io.read mapped to REQ_INPUT/RES_INPUT.
- Handles SYS_EVENT TERMINATE for controlled shutdown.

Wire framing:
- 4-byte little-endian payload length
- 1-byte message type
- UTF-8 JSON payload
]]
