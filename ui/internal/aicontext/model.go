package aicontext

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"strings"
	"time"
	"yuumi/pkg/catalog"

	"charm.land/bubbles/v2/spinner"
	tea "charm.land/bubbletea/v2"
)

const (
	DefaultEndpoint = "http://localhost:11434"
	DefaultModel    = "codellama:latest"
)

type AiContextModel struct {
	endpoint string
	modelName string
	apiKey string
	history []ChatMessage
	inputBuf string
	streaming bool
	streamBuf strings.Builder
	codeBlocks []CodeBlock
	showActions bool
	connected bool
	width int
	height int
	spinner spinner.Model
	checking bool
	lastError string
	systemPromptOverride string
	streamEvents <-chan tea.Msg
	cancel context.CancelFunc
}

type ChatMessage struct {
	Role string `json:"role"`
	Content string `json:"content"`
}

type CodeBlock struct {
	Lang string
	Content string
}

type AiConnectedMsg struct{}
type AiErrorMsg struct {
	Err string
}
type TokenMsg struct {
	Token string
}
type StreamDoneMsg struct{}
type StreamCancelledMsg struct{}
type AiStreamStartedMsg struct {
	Events <-chan tea.Msg
}

type streamChunk struct {
	Choices []struct {
		Delta struct {
			Content string `json:"content"`
		} `json:"delta"`
	} `json:"choices"`
}

func New(endpoint string, modelName string) AiContextModel {
	normalizedEndpoint := strings.TrimSpace(endpoint)
	if normalizedEndpoint == "" {
		normalizedEndpoint = DefaultEndpoint
	}
	normalizedModel := strings.TrimSpace(modelName)
	if normalizedModel == "" {
		normalizedModel = DefaultModel
	}
	return AiContextModel{
		endpoint:  normalizedEndpoint,
		modelName: normalizedModel,
		spinner:   spinner.New(),
	}
}

func (m *AiContextModel) Endpoint() string {
	return strings.TrimSpace(m.endpoint)
}

func (m *AiContextModel) ModelName() string {
	return strings.TrimSpace(m.modelName)
}

func (m *AiContextModel) ApiKey() string {
	return strings.TrimSpace(m.apiKey)
}

func (m *AiContextModel) HasApiKey() bool {
	return strings.TrimSpace(m.apiKey) != ""
}

func (m *AiContextModel) Connected() bool {
	return m.connected
}

func (m *AiContextModel) Checking() bool {
	return m.checking
}

func (m *AiContextModel) LastError() string {
	return strings.TrimSpace(m.lastError)
}

func (m *AiContextModel) IsStreaming() bool {
	return m.streaming
}

func (m *AiContextModel) ShowActions() bool {
	return m.showActions && len(m.codeBlocks) > 0
}

func (m *AiContextModel) CodeBlocks() []CodeBlock {
	if len(m.codeBlocks) == 0 {
		return nil
	}
	result := make([]CodeBlock, len(m.codeBlocks))
	copy(result, m.codeBlocks)
	return result
}

func (m *AiContextModel) SetEndpoint(value string) {
	normalized := strings.TrimSpace(value)
	if normalized != "" {
		m.endpoint = normalized
	}
}

func (m *AiContextModel) SetModelName(value string) {
	normalized := strings.TrimSpace(value)
	if normalized != "" {
		m.modelName = normalized
	}
}

func (m *AiContextModel) SetApiKey(value string) {
	m.apiKey = strings.TrimSpace(value)
}

func (m *AiContextModel) SetSystemPromptOverride(value string) {
	m.systemPromptOverride = strings.TrimSpace(value)
}

func (m *AiContextModel) SystemPromptOverride() string {
	return strings.TrimSpace(m.systemPromptOverride)
}

func (m *AiContextModel) Init() tea.Cmd {
	m.checking = true
	m.connected = false
	m.lastError = ""
	return ConnectivityCheckCmd(m.endpoint, m.apiKey)
}

func (m *AiContextModel) StartConnectivityCheck() tea.Cmd {
	return m.Init()
}

func (m *AiContextModel) ApplyMsg(msg tea.Msg) tea.Cmd {
	switch typed := msg.(type) {
	case AiConnectedMsg:
		m.checking = false
		m.connected = true
		m.lastError = ""
		return nil
	case AiErrorMsg:
		m.checking = false
		m.connected = false
		m.lastError = strings.TrimSpace(typed.Err)
		if m.streaming {
			m.streaming = false
			m.cancelStream()
		}
		return nil
	case AiStreamStartedMsg:
		m.streamEvents = typed.Events
		return WaitStreamEventCmd(m.streamEvents)
	case TokenMsg:
		m.streamBuf.WriteString(typed.Token)
		return WaitStreamEventCmd(m.streamEvents)
	case StreamDoneMsg:
		m.streaming = false
		m.cancel = nil
		m.streamEvents = nil
		m.connected = true
		m.lastError = ""
		content := m.streamBuf.String()
		m.codeBlocks = DetectCodeBlocks(content)
		m.showActions = len(m.codeBlocks) > 0
		m.history = append(m.history, ChatMessage{Role: "assistant", Content: content})
		return nil
	case StreamCancelledMsg:
		m.streaming = false
		m.cancel = nil
		m.streamEvents = nil
		m.codeBlocks = DetectCodeBlocks(m.streamBuf.String())
		m.showActions = len(m.codeBlocks) > 0
		return nil
	default:
		return nil
	}
}

func (m *AiContextModel) BeginRequest(userInput string, systemPrompt string) tea.Cmd {
	trimmed := strings.TrimSpace(userInput)
	if trimmed == "" || m.streaming {
		return nil
	}
	m.inputBuf = trimmed
	m.streamBuf.Reset()
	m.codeBlocks = nil
	m.showActions = false
	m.streaming = true
	m.lastError = ""
	m.history = append(m.history, ChatMessage{Role: "user", Content: trimmed})
	ctx, cancel := context.WithCancel(context.Background())
	m.cancel = cancel
	return StreamCompletionCmd(ctx, m.endpoint, m.apiKey, m.modelName, append([]ChatMessage{{Role: "system", Content: strings.TrimSpace(systemPrompt)}}, m.history...))
}

func (m *AiContextModel) CancelStreaming() {
	if m.streaming {
		m.cancelStream()
	}
}

func (m *AiContextModel) cancelStream() {
	if m.cancel != nil {
		m.cancel()
	}
	m.cancel = nil
}

func (m *AiContextModel) CurrentResponse() string {
	return m.streamBuf.String()
}

func (m *AiContextModel) ClearActions() {
	m.showActions = false
}

func ConnectivityCheckCmd(endpoint string, apiKey string) tea.Cmd {
	target := strings.TrimRight(strings.TrimSpace(endpoint), "/")
	token := strings.TrimSpace(apiKey)
	return func() tea.Msg {
		if target == "" {
			return AiErrorMsg{Err: "[ZERI][AI-001] AI endpoint unreachable at <empty>. Hint: run /setup in $ai, or start Ollama with 'ollama serve' and set '/set endpoint <url>'"}
		}
		client := &http.Client{Timeout: 4 * time.Second}
		req, err := http.NewRequest(http.MethodGet, target+"/v1/models", nil)
		if err != nil {
			return AiErrorMsg{Err: "[ZERI][AI-003] Failed to create AI request."}
		}
		if token != "" {
			req.Header.Set("Authorization", "Bearer "+token)
		}
		resp, err := client.Do(req)
		if err != nil {
			return AiErrorMsg{Err: fmt.Sprintf("[ZERI][AI-001] AI endpoint unreachable at %s. Hint: run /setup in $ai, or start Ollama with 'ollama serve' and set '/set endpoint <url>'", target)}
		}
		defer resp.Body.Close()
		if resp.StatusCode < 200 || resp.StatusCode >= 300 {
			return AiErrorMsg{Err: fmt.Sprintf("[ZERI][AI-001] AI endpoint unreachable at %s. Hint: run /setup in $ai, or start Ollama with 'ollama serve' and set '/set endpoint <url>'", target)}
		}
		return AiConnectedMsg{}
	}
}

func StreamCompletionCmd(ctx context.Context, endpoint string, apiKey string, modelName string, messages []ChatMessage) tea.Cmd {
	target := strings.TrimRight(strings.TrimSpace(endpoint), "/")
	token := strings.TrimSpace(apiKey)
	normalizedModel := strings.TrimSpace(modelName)
	if normalizedModel == "" {
		normalizedModel = DefaultModel
	}
	normalizedMessages := make([]ChatMessage, 0, len(messages))
	for _, message := range messages {
		role := strings.TrimSpace(strings.ToLower(message.Role))
		content := strings.TrimSpace(message.Content)
		if role == "" || content == "" {
			continue
		}
		normalizedMessages = append(normalizedMessages, ChatMessage{Role: role, Content: content})
	}
	return func() tea.Msg {
		ch := make(chan tea.Msg, 256)
		go func() {
			defer close(ch)
			payload := map[string]interface{}{
				"model":    normalizedModel,
				"messages": normalizedMessages,
				"stream":   true,
			}
			body, err := json.Marshal(payload)
			if err != nil {
				ch <- AiErrorMsg{Err: "[ZERI][AI-002] Failed to encode AI request payload."}
				return
			}
			req, err := http.NewRequestWithContext(ctx, http.MethodPost, target+"/v1/chat/completions", bytes.NewReader(body))
			if err != nil {
				ch <- AiErrorMsg{Err: "[ZERI][AI-003] Failed to create AI request."}
				return
			}
			req.Header.Set("Content-Type", "application/json")
			if token != "" {
				req.Header.Set("Authorization", "Bearer "+token)
			}
			resp, err := (&http.Client{Timeout: 0}).Do(req)
			if err != nil {
				if ctx.Err() != nil {
					ch <- StreamCancelledMsg{}
					return
				}
				ch <- AiErrorMsg{Err: fmt.Sprintf("[ZERI][AI-001] AI endpoint unreachable at %s. Hint: start Ollama with 'ollama serve' or configure with '/set endpoint <url>'", target)}
				return
			}
			defer resp.Body.Close()
			if resp.StatusCode < 200 || resp.StatusCode >= 300 {
				ch <- AiErrorMsg{Err: fmt.Sprintf("[ZERI][AI-004] AI request failed with status %d.", resp.StatusCode)}
				return
			}
			reader := bufio.NewReader(resp.Body)
			for {
				line, readErr := reader.ReadString('\n')
				if strings.TrimSpace(line) != "" {
					trimmed := strings.TrimSpace(line)
					if strings.HasPrefix(trimmed, "data:") {
						raw := strings.TrimSpace(strings.TrimPrefix(trimmed, "data:"))
						if raw == "[DONE]" {
							ch <- StreamDoneMsg{}
							return
						}
						var chunk streamChunk
						if err = json.Unmarshal([]byte(raw), &chunk); err == nil && len(chunk.Choices) > 0 {
							token := chunk.Choices[0].Delta.Content
							if token != "" {
								ch <- TokenMsg{Token: token}
							}
						}
					}
				}
				if readErr != nil {
					if errorsIsCancelled(ctx.Err()) {
						ch <- StreamCancelledMsg{}
						return
					}
					if readErr == io.EOF {
						ch <- StreamDoneMsg{}
						return
					}
					ch <- AiErrorMsg{Err: "[ZERI][AI-005] AI stream interrupted unexpectedly."}
					return
				}
			}
		}()
		return AiStreamStartedMsg{Events: ch}
	}
}

func WaitStreamEventCmd(events <-chan tea.Msg) tea.Cmd {
	return func() tea.Msg {
		if events == nil {
			return StreamDoneMsg{}
		}
		msg, ok := <-events
		if !ok {
			return StreamDoneMsg{}
		}
		return msg
	}
}

func DetectCodeBlocks(content string) []CodeBlock {
	re := regexp.MustCompile("(?s)```([a-zA-Z0-9_+-]*)\\n(.*?)```")
	matches := re.FindAllStringSubmatch(content, -1)
	if len(matches) == 0 {
		return nil
	}
	blocks := make([]CodeBlock, 0, len(matches))
	for _, match := range matches {
		lang := strings.ToLower(strings.TrimSpace(match[1]))
		code := strings.TrimSpace(match[2])
		if code == "" {
			continue
		}
		blocks = append(blocks, CodeBlock{Lang: lang, Content: code})
	}
	return blocks
}

func NormalizeCodeLang(lang string, fallback string) string {
	if resolved, ok := catalog.ResolveLanguage(lang); ok {
		return resolved.ID
	}
	if resolved, ok := catalog.ResolveLanguage(fallback); ok {
		return resolved.ID
	}
	return "python"
}

func BuildSystemPrompt(activeLang string, shared map[string]interface{}, override string) string {
	if trimmed := strings.TrimSpace(override); trimmed != "" {
		return trimmed
	}
	lang := strings.TrimSpace(activeLang)
	if lang == "" {
		lang = "python"
	}
	base := fmt.Sprintf("You are a coding assistant for Zeri, a multi-language REPL. Generate executable %s code.", lang)
	if len(shared) == 0 {
		return base
	}
	raw, err := json.Marshal(shared)
	if err != nil {
		return base
	}
	return base + " Current shared variables: " + string(raw)
}

func ParseSharedScopeTable(output string) map[string]interface{} {
	lines := strings.Split(strings.ReplaceAll(output, "\r\n", "\n"), "\n")
	result := map[string]interface{}{}
	inTable := false
	foundRow := false
	for _, raw := range lines {
		line := strings.TrimSpace(raw)
		if line == "" {
			continue
		}
		if strings.EqualFold(line, "Shared Scope") {
			inTable = true
			continue
		}
		if !inTable {
			continue
		}
		if strings.HasPrefix(strings.ToLower(line), "key |") || strings.HasPrefix(strings.ToLower(line), "----|") {
			continue
		}
		if line == "(empty)" {
			return map[string]interface{}{}
		}
		parts := strings.Split(line, "|")
		if len(parts) < 3 {
			continue
		}
		key := strings.TrimSpace(parts[0])
		value := strings.TrimSpace(parts[2])
		if key == "" {
			continue
		}
		result[key] = value
		foundRow = true
	}
	if !inTable {
		return nil
	}
	if !foundRow {
		return map[string]interface{}{}
	}
	return result
}

func errorsIsCancelled(err error) bool {
	return err == context.Canceled || err == context.DeadlineExceeded
}
