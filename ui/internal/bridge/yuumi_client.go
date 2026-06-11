package bridge

import (
	"context"
	"sync"
	"yuumi/pkg/yuumi"

	tea "charm.land/bubbletea/v2"
)

type RealYuumiClient struct {
	client *yuumi.Client
	program *tea.Program
	messageMutex sync.Mutex
	handlerCtx context.Context
	handlerStop context.CancelFunc
}

func NewRealYuumiClient(client *yuumi.Client) *RealYuumiClient {
	return &RealYuumiClient{client: client}
}

func (r *RealYuumiClient) SetProgram(p *tea.Program) {
	r.program = p
}

func (r *RealYuumiClient) SetClient(client *yuumi.Client) {
	r.client = client
}

func (r *RealYuumiClient) Send(msg tea.Msg) {
	if r.program == nil {
		return
	}
	r.program.Send(msg)
}

func (r *RealYuumiClient) StopMessageForwarding() {
	r.messageMutex.Lock()
	defer r.messageMutex.Unlock()
	if r.handlerStop != nil {
		r.handlerStop()
		r.handlerStop = nil
		r.handlerCtx = nil
	}
}

func (r *RealYuumiClient) ConnectCmd() tea.Cmd {
	return func() tea.Msg {
		if r.client != nil {
			return ConnectedMsg{}
		}
		return DisconnectedMsg{Reason: "no client configured"}
	}
}

func (r *RealYuumiClient) SendInputResponseCmd(value string) tea.Cmd {
	client := r.client
	return func() tea.Msg {
		if client == nil {
			return nil
		}
		payload := map[string]interface{}{
			"type": "input_response",
			"payload": value,
		}
		client.Send(payload, yuumi.ChannelCommand)
		return nil
	}
}

func (r *RealYuumiClient) SendDataCmd(s string) tea.Cmd {
	client := r.client
	return func() tea.Msg {
		if client == nil {
			return nil
		}
		payload := map[string]interface{}{
			"type": "command",
			"payload": s,
		}
		client.Send(payload, yuumi.ChannelCommand)
		return nil
	}
}

func (r *RealYuumiClient) SendCommandPayloadCmd(payload map[string]interface{}) tea.Cmd {
	client := r.client
	return func() tea.Msg {
		if client == nil {
			return nil
		}
		client.Send(payload, yuumi.ChannelCommand)
		return nil
	}
}

func (r *RealYuumiClient) SendControlCmd(cmd string) tea.Cmd {
	client := r.client
	return func() tea.Msg {
		if client == nil {
			return nil
		}
		payload := map[string]interface{}{
			"type": cmd,
		}
		client.Send(payload, yuumi.ChannelControl)
		return nil
	}
}

func (r *RealYuumiClient) SendShutdownCmd() tea.Cmd {
	client := r.client
	return func() tea.Msg {
		if client != nil {
			client.Shutdown()
		}
		return nil
	}
}

func (r *RealYuumiClient) RegisterMessageHandler() {
	r.messageMutex.Lock()
	if r.handlerStop != nil {
		r.handlerStop()
	}
	handlerCtx, cancel := context.WithCancel(context.Background())
	r.handlerCtx = handlerCtx
	r.handlerStop = cancel
	client := r.client
	program := r.program
	r.messageMutex.Unlock()

	if client == nil || program == nil {
		return
	}
	client.OnMessage(func(data map[string]interface{}, ch yuumi.Channel) {
		select {
		case <-handlerCtx.Done():
			return
		default:
		}

		msgType, _ := data["type"].(string)
		switch msgType {
		case "ready":
			program.Send(ConnectedMsg{})
		case "output", "info", "success":
			payload, _ := data["payload"].(string)
			if payload != "" {
				program.Send(DataMsg{Content: payload})
			}
		case "error":
			payload, _ := data["payload"].(string)
			if payload != "" {
				program.Send(ErrorMsg{Content: payload})
			}
		case "req_input":
			prompt, _ := data["prompt"].(string)
			program.Send(InputRequestMsg{Prompt: prompt})
		case "stream_batch_end":
			reason, _ := data["reason"].(string)
			program.Send(StreamBatchEndMsg{Reason: reason})
		case "sel_request":
			title, _ := data["title"].(string)
			options := make([]string, 0)
			if optionsRaw, ok := data["options"].([]interface{}); ok {
				for _, optionRaw := range optionsRaw {
					optionText, ok := optionRaw.(string)
					if !ok {
						continue
					}
					options = append(options, optionText)
				}
			}
			program.Send(SelectionRequestMsg{Title: title, Options: options})
		case "context_changed":
			name, _ := data["context"].(string)
			active, ok := data["active"].(bool)
			if !ok {
				active = true
			}
			program.Send(ContextChangedMsg{ContextName: name, Active: active})
		case "code_mode":
			name, _ := data["context"].(string)
			if name == "" {
				name = "sandbox"
			}
			active, ok := data["active"].(bool)
			if !ok {
				active = false
			}
			program.Send(ContextChangedMsg{ContextName: name, Active: active})
		case "script_list_response":
			records := make([]ScriptRecord, 0)
			if scriptsRaw, ok := data["scripts"].([]interface{}); ok {
				for _, itemRaw := range scriptsRaw {
					item, ok := itemRaw.(map[string]interface{})
					if !ok {
						continue
					}
					size := 0
					switch value := item["size"].(type) {
					case float64:
						size = int(value)
					case int:
						size = value
					}
					name, _ := item["name"].(string)
					lang, _ := item["lang"].(string)
					modified, _ := item["modified"].(string)
					content, _ := item["content"].(string)
					records = append(records, ScriptRecord{
						Name: name,
						Lang: lang,
						Modified: modified,
						Size: size,
						Content: content,
					})
				}
			}
			program.Send(ScriptListResponseMsg{Scripts: records})
		case "script_action_response":
			action, _ := data["action"].(string)
			ok, okType := data["ok"].(bool)
			if !okType {
				ok = false
			}
			errText, _ := data["error"].(string)
			program.Send(ScriptActionResponseMsg{
				Action: action,
				Ok: ok,
				Error: errText,
			})
		case "shared_scope_snapshot_response":
			entries := map[string]interface{}{}
			if raw, ok := data["entries"].(map[string]interface{}); ok {
				for key, value := range raw {
					entries[key] = value
				}
			}
			program.Send(SharedScopeSnapshotMsg{Entries: entries})
		case "":
			return
		default:
			payload, _ := data["payload"].(string)
			if payload == "" {
				return
			}
			program.Send(DataMsg{Content: msgType + ": " + payload})
		}
	})
	client.OnDisconnect(func(err error) {
		select {
		case <-handlerCtx.Done():
			return
		default:
		}

		reason := "connection lost"
		if err != nil {
			reason = err.Error()
		}
		program.Send(DisconnectedMsg{Reason: reason})
	})
}

/*
 * What changed:
 *   - RegisterMessageHandler now also registers an OnDisconnect callback
 *     on the underlying yuumi.Client. When the readLoop exits due to a
 *     transport error (pipe break, EOF), the callback sends a
 *     DisconnectedMsg to the Bubble Tea program.
 *   - This makes the "connected" status indicator fully reactive: it
 *     transitions to "disconnected" both on engine crash (via runner.OnCrash)
 *     and on transport-level pipe loss (via client.OnDisconnect).
 *
 * Why:
 *   - Previously, if the IPC pipe broke without the engine process dying
 *     (e.g. the pipe was closed externally), the TUI would remain stuck
 *     showing "connected". The OnDisconnect callback closes this gap.
 *
 * Impact on other components:
 *   - pkg/yuumi/client.go exposes the new OnDisconnect(func(error)) method.
 *   - model.go Update() handles DisconnectedMsg unchanged.
 *
 * Future maintenance notes:
 *   - The message dispatch in RegisterMessageHandler should be extended
 *     as the engine protocol evolves (mode_confirmed, req_input, etc.).
 *   - Consider adding a reconnect strategy triggered by OnDisconnect.
 */
