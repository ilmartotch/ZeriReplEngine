package bridge

import (
	"yuumi/pkg/yuumi"

	tea "charm.land/bubbletea/v2"
)

type RealYuumiClient struct {
	client  *yuumi.Client
	program *tea.Program
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

func (r *RealYuumiClient) ConnectCmd() tea.Cmd {
	return func() tea.Msg {
		if r.client != nil {
			return ConnectedMsg{}
		}
		return DisconnectedMsg{Reason: "no client configured"}
	}
}

func (r *RealYuumiClient) SendDataCmd(s string) tea.Cmd {
	client := r.client
	return func() tea.Msg {
		if client == nil {
			return nil
		}
		payload := map[string]interface{}{
			"type":    "command",
			"payload": s,
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
	if r.client == nil || r.program == nil {
		return
	}
	r.client.OnMessage(func(data map[string]interface{}, ch yuumi.Channel) {
		msgType, _ := data["type"].(string)
		switch msgType {
		case "ready":
			r.program.Send(ConnectedMsg{})
		case "output", "info", "success":
			payload, _ := data["payload"].(string)
			if payload != "" {
				r.program.Send(DataMsg{Content: payload})
			}
		case "error":
			payload, _ := data["payload"].(string)
			if payload != "" {
				r.program.Send(ErrorMsg{Content: payload})
			}
		case "context_changed":
          name, _ := data["context"].(string)
			active, ok := data["active"].(bool)
			if !ok {
				active = true
			}
			r.program.Send(ContextChangedMsg{ContextName: name, Active: active})
		case "code_mode":
            name, _ := data["context"].(string)
			if name == "" {
				name = "sandbox"
			}
			active, ok := data["active"].(bool)
			if !ok {
				active = false
			}
			r.program.Send(ContextChangedMsg{ContextName: name, Active: active})
		case "":
			return
		default:
			payload, _ := data["payload"].(string)
			if payload == "" {
				return
			}
			r.program.Send(DataMsg{Content: msgType + ": " + payload})
		}
	})
	r.client.OnDisconnect(func(err error) {
		reason := "connection lost"
		if err != nil {
			reason = err.Error()
		}
		r.program.Send(DisconnectedMsg{Reason: reason})
	})
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [yuumi_client.go]
 *
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
