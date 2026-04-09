package yuumi

import (
	"encoding/binary"
	"encoding/json"
	"io"
)

const (
	frameHeaderSize = 6
	frameMaxMessageSize uint32 = 16 * 1024 * 1024
)

func encodePayloadJSON(data interface{}) ([]byte, error) {
	return json.Marshal(data)
}

func decodePayloadJSON(payload []byte) (map[string]interface{}, error) {
	var data map[string]interface{}
	if err := json.Unmarshal(payload, &data); err != nil {
		return nil, err
	}
	return data, nil
}

func encodeFrame(data interface{}, ch Channel) ([]byte, error) {
	payload, err := encodePayloadJSON(data)
	if err != nil {
		return nil, err
	}

	frame := make([]byte, frameHeaderSize+len(payload))
	binary.LittleEndian.PutUint32(frame[0:4], uint32(len(payload)))
	frame[4] = byte(ch)
	frame[5] = 0
	copy(frame[frameHeaderSize:], payload)
	return frame, nil
}

func decodeFrame(r io.Reader) (map[string]interface{}, Channel, error) {
	header := make([]byte, frameHeaderSize)
	if _, err := io.ReadFull(r, header); err != nil {
		return nil, 0, err
	}

	length := binary.LittleEndian.Uint32(header[0:4])
	if length > frameMaxMessageSize {
		return nil, 0, NewError(ErrProtocolViolation, "message too large")
	}

	ch := Channel(header[4])
	body := make([]byte, length)
	if _, err := io.ReadFull(r, body); err != nil {
		return nil, 0, err
	}

	data, err := decodePayloadJSON(body)
	if err != nil {
		return nil, 0, err
	}

	return data, ch, nil
}

/*
 * protocol.go: Modern REPL frame codec for the Go client.
 * - Header format is fixed to 6 bytes: [4B Length (Little Endian)][1B Channel][1B Flags].
 * - Payload format is UTF-8 JSON for all channels.
 * - encodeFrame/decodeFrame centralize framing logic to keep Client transport code minimal.
 */
