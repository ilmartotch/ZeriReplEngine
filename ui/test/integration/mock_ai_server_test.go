package integration

import (
	"encoding/json"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"
	"yuumi/internal/aicontext"

	tea "charm.land/bubbletea/v2"
)

type capturedChatRequest struct {
	Model string `json:"model"`
	Stream bool `json:"stream"`
	Messages []struct {
		Role string `json:"role"`
		Content string `json:"content"`
	} `json:"messages"`
}

func newMockAiServer(t *testing.T, delayPerChunk time.Duration) (*httptest.Server, func() capturedChatRequest) {
	t.Helper()
	var mu sync.Mutex
	lastRequest := capturedChatRequest{}

	handler := http.NewServeMux()
	handler.HandleFunc("/v1/models", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"data":[{"id":"codellama:latest"}]}`))
	})
	handler.HandleFunc("/v1/chat/completions", func(w http.ResponseWriter, r *http.Request) {
		defer r.Body.Close()
		var req capturedChatRequest
		_ = json.NewDecoder(r.Body).Decode(&req)
		mu.Lock()
		lastRequest = req
		mu.Unlock()

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}

		chunks := []string{
			"data: {\"choices\":[{\"delta\":{\"content\":\"Here is a Python function:\\n\"}}]}\n\n",
			"data: {\"choices\":[{\"delta\":{\"content\":\"```python\\ndef reverse(s):\\n    return s[::-1]\\n```\"}}]}\n\n",
			"data: [DONE]\n\n",
		}
		for _, chunk := range chunks {
			_, _ = w.Write([]byte(chunk))
			flusher.Flush()
			if delayPerChunk > 0 {
				time.Sleep(delayPerChunk)
			}
		}
	})

	server := httptest.NewServer(handler)
	getReq := func() capturedChatRequest {
		mu.Lock()
		defer mu.Unlock()
		return lastRequest
	}
	return server, getReq
}

func runAiCmdLoop(m *aicontext.AiContextModel, cmd tea.Cmd, limit int) {
	for i := 0; i < limit && cmd != nil; i++ {
		msg := cmd()
		cmd = m.ApplyMsg(msg)
	}
}

func TestAiConnectivity(t *testing.T) {
	server, _ := newMockAiServer(t, 0)
	defer server.Close()

	m := aicontext.New(server.URL, "codellama:latest")
	cmd := m.Init()
	runAiCmdLoop(&m, cmd, 3)
	if !m.Connected() {
		t.Fatalf("expected connected=true")
	}
}

func TestAiEndpointUnreachable(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("failed to create temp listener: %v", err)
	}
	addr := listener.Addr().String()
	_ = listener.Close()

	m := aicontext.New("http://"+addr, "codellama:latest")
	cmd := m.Init()
	runAiCmdLoop(&m, cmd, 3)
	if !strings.Contains(strings.ToUpper(m.LastError()), "AI-001") {
		t.Fatalf("expected AI-001 in error, got %q", m.LastError())
	}
}

func TestAiStreaming(t *testing.T) {
	server, _ := newMockAiServer(t, 0)
	defer server.Close()

	m := aicontext.New(server.URL, "codellama:latest")
	cmd := m.BeginRequest("write reverse function", "You are a coding assistant for Zeri, a multi-language REPL. Generate executable python code.")
	runAiCmdLoop(&m, cmd, 256)
	if !strings.Contains(m.CurrentResponse(), "def reverse(s):") {
		t.Fatalf("expected streamed function content, got %q", m.CurrentResponse())
	}
}

func TestAiCodeBlockDetection(t *testing.T) {
	server, _ := newMockAiServer(t, 0)
	defer server.Close()

	m := aicontext.New(server.URL, "codellama:latest")
	cmd := m.BeginRequest("write reverse function", "You are a coding assistant for Zeri, a multi-language REPL. Generate executable python code.")
	runAiCmdLoop(&m, cmd, 256)
	blocks := m.CodeBlocks()
	if len(blocks) != 1 {
		t.Fatalf("expected 1 code block, got %d", len(blocks))
	}
	if blocks[0].Lang != "python" {
		t.Fatalf("expected python code block, got %q", blocks[0].Lang)
	}
	if !strings.Contains(blocks[0].Content, "return s[::-1]") {
		t.Fatalf("unexpected code block content: %q", blocks[0].Content)
	}
}

func TestAiStreamCancel(t *testing.T) {
	server, _ := newMockAiServer(t, 150*time.Millisecond)
	defer server.Close()

	m := aicontext.New(server.URL, "codellama:latest")
	cmd := m.BeginRequest("stream slowly", "You are a coding assistant for Zeri, a multi-language REPL. Generate executable python code.")
	cancelled := false
	for i := 0; i < 256 && cmd != nil; i++ {
		msg := cmd()
		if _, ok := msg.(aicontext.TokenMsg); ok {
			m.CancelStreaming()
		}
		if _, ok := msg.(aicontext.StreamCancelledMsg); ok {
			cancelled = true
		}
		cmd = m.ApplyMsg(msg)
		if cancelled {
			break
		}
	}
	if !cancelled && !strings.Contains(strings.ToLower(m.CurrentResponse()), "here is a python function") {
		t.Fatalf("expected partial output to be preserved on cancel")
	}
	if m.IsStreaming() {
		t.Fatalf("expected streaming=false after cancel")
	}
}

func TestAiContextAwarePrompt(t *testing.T) {
	server, getReq := newMockAiServer(t, 0)
	defer server.Close()

	m := aicontext.New(server.URL, "codellama:latest")
	shared := map[string]interface{}{"x": 5}
	systemPrompt := aicontext.BuildSystemPrompt("lua", shared, "")
	cmd := m.BeginRequest("write reverse function", systemPrompt)
	runAiCmdLoop(&m, cmd, 256)
	req := getReq()
	if !req.Stream {
		t.Fatalf("expected stream=true")
	}
	if len(req.Messages) == 0 {
		t.Fatalf("expected non-empty messages")
	}
	if req.Messages[0].Role != "system" {
		t.Fatalf("expected first message role=system, got %q", req.Messages[0].Role)
	}
	if !strings.Contains(strings.ToLower(req.Messages[0].Content), "lua") {
		t.Fatalf("expected lua in system prompt, got %q", req.Messages[0].Content)
	}
}
