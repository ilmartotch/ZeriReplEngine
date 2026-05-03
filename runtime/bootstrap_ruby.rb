require "json"
require "stringio"

EXEC_CODE = 0x01
EXEC_RESULT = 0x02
REQ_INPUT = 0x03
RES_INPUT = 0x04
SYS_EVENT = 0x05

HEADER_SIZE = 4
TYPE_SIZE = 1
MAX_PAYLOAD = 16 * 1024 * 1024

$stdin.binmode
$stdout.binmode

ORIG_STDIN = STDIN
ORIG_STDOUT = STDOUT.dup

class FrameDecoder
  def initialize
    @buffer = +""
    @buffer.force_encoding(Encoding::BINARY)
  end

  def feed(chunk)
    @buffer << chunk if chunk && !chunk.empty?

    frames = []

    while @buffer.bytesize >= HEADER_SIZE + TYPE_SIZE
      payload_len = @buffer.byteslice(0, HEADER_SIZE).unpack1("V")

      if payload_len > MAX_PAYLOAD
        @buffer.clear
        break
      end

      total_len = HEADER_SIZE + TYPE_SIZE + payload_len
      break if @buffer.bytesize < total_len

      type = @buffer.getbyte(HEADER_SIZE)
      payload = @buffer.byteslice(HEADER_SIZE + TYPE_SIZE, payload_len)
      payload = payload.force_encoding(Encoding::UTF_8)

      frames << [type, payload]
      @buffer = @buffer.byteslice(total_len, @buffer.bytesize - total_len) || +""
      @buffer.force_encoding(Encoding::BINARY)
    end

    frames
  end
end

def write_all(io, data)
  offset = 0
  while offset < data.bytesize
    written = io.write(data.byteslice(offset, data.bytesize - offset))
    break if written.nil? || written <= 0
    offset += written
  end
  io.flush
end

def write_frame(msg_type, payload)
  payload_bytes = payload.to_s.encode(Encoding::UTF_8)
  header = [payload_bytes.bytesize, msg_type].pack("VC")
  write_all(ORIG_STDOUT, header + payload_bytes)
end

def send_sys_event(status, extra = nil)
  obj = { status: status }
  obj.merge!(extra) if extra.is_a?(Hash)
  write_frame(SYS_EVENT, JSON.generate(obj))
end

def send_exec_result(output, error, exit_code)
  write_frame(
    EXEC_RESULT,
    JSON.generate(
      {
        output: output,
        error: error,
        exitCode: exit_code
      }
    )
  )
end

def handle_sys_event(payload)
  parsed = JSON.parse(payload)
  status = parsed["status"]
  exit(0) if status == "TERMINATE"
rescue JSON::ParserError
  nil
end

def wait_for_input_response(decoder)
  loop do
    chunk = ORIG_STDIN.readpartial(4096)
    return "" if chunk.nil? || chunk.empty?

    frames = decoder.feed(chunk)
    frames.each do |type, payload|
      if type == RES_INPUT
        begin
          parsed = JSON.parse(payload)
          return parsed["value"].to_s
        rescue JSON::ParserError
          return payload.to_s
        end
      elsif type == SYS_EVENT
        handle_sys_event(payload)
      end
    end
  end
rescue EOFError
  ""
end

def install_input_hook(decoder)
  Kernel.module_eval do
    define_method(:gets) do |*args|
      prompt_message = args.first.to_s
      write_frame(REQ_INPUT, JSON.generate({ prompt: prompt_message }))
      value = wait_for_input_response(decoder)
      value + "\n"
    end
  end
end

def extract_code(payload)
  parsed = JSON.parse(payload)
  code = parsed["code"]
  code = parsed["source"] if code.nil? || code.empty?
  code.to_s
rescue JSON::ParserError
  nil
end

def execute_code(payload, bind)
  code = extract_code(payload)
  if code.nil?
    send_exec_result("", "Invalid JSON in EXEC_CODE payload", -1)
    return
  end

  stdout_cap = StringIO.new
  stderr_cap = StringIO.new

  saved_stdout = $stdout
  saved_stderr = $stderr
  $stdout = stdout_cap
  $stderr = stderr_cap

  begin
    result = eval(code, bind, "<zeri>")
    stdout_cap.puts(result.inspect) unless result.nil?

    output = stdout_cap.string
    output = output.chomp

    err_text = stderr_cap.string
    err_text = nil if err_text.empty?

    send_exec_result(output, err_text, 0)
  rescue SystemExit
    send_exec_result("", nil, 0)
  rescue StandardError => e
    output = stdout_cap.string
    output = output.chomp

    err_text = stderr_cap.string
    bt = e.full_message(highlight: false, order: :top)
    message = err_text.empty? ? bt : (err_text + bt)
    send_exec_result(output, message.strip, 1)
  ensure
    $stdout = saved_stdout
    $stderr = saved_stderr
  end
end

def main
  decoder = FrameDecoder.new
  bind = TOPLEVEL_BINDING

  install_input_hook(decoder)
  send_sys_event("READY")

  loop do
    chunk = ORIG_STDIN.readpartial(4096)
    break if chunk.nil? || chunk.empty?

    frames = decoder.feed(chunk)
    frames.each do |type, payload|
      case type
      when EXEC_CODE
        execute_code(payload, bind)
      when RES_INPUT
        nil
      when SYS_EVENT
        handle_sys_event(payload)
      end
    end
  end
rescue EOFError
  nil
end

begin
  main
rescue StandardError => e
  send_sys_event("CRASHED", { error: e.message })
  exit(1)
end

=begin
bootstrap_ruby.rb implements ZeriWire protocol over stdin/stdout.

Capabilities:
- Sends SYS_EVENT READY at startup.
- Handles EXEC_CODE by evaluating Ruby code in TOPLEVEL_BINDING to persist state.
- Captures stdout/stderr and returns EXEC_RESULT JSON with output/error/exitCode.
- Handles input requests by overriding Kernel#gets and exchanging REQ_INPUT/RES_INPUT.
- Handles SYS_EVENT TERMINATE for controlled shutdown.

Wire framing:
- 4 bytes little-endian payload length
- 1 byte message type
- UTF-8 JSON payload
=end
