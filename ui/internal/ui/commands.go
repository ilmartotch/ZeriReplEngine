package ui

import (
	"slices"
	"strings"
	"yuumi/pkg/catalog"
)

type CompletionEntry struct {
	Command  string
	Synopsis string
}

type CommandScopeValidation struct {
	Allowed bool
	Command string
	CurrentGroup  string
	AllowedGroups []string
}

func normaliseContextName(activeContext string) string {
	name := strings.TrimSpace(strings.TrimPrefix(activeContext, "$"))
	if name == "" {
		return "global"
	}
	lower := strings.ToLower(name)
	lower = strings.TrimPrefix(lower, "zeri::")
	segments := strings.Split(lower, "::")
	if len(segments) > 0 {
		return segments[len(segments)-1]
	}
	return lower
}

func commandGroupForContext(activeContext string) string {
	contextID := normaliseContextName(activeContext)
	if contextID == "" {
		return "global"
	}
	return contextID
}

func SlashCommandsForContext(activeContext string) []CompletionEntry {
	entries := catalog.CommandsForContext(normaliseContextName(activeContext))
	result := make([]CompletionEntry, 0, len(entries))
	seen := make(map[string]struct{}, len(entries))
	for _, entry := range entries {
		key := strings.ToLower(strings.TrimSpace(entry.Command))
		if key == "" {
			continue
		}
		if _, ok := seen[key]; ok {
			continue
		}
		seen[key] = struct{}{}
		result = append(result, CompletionEntry{
			Command:  entry.Command,
			Synopsis: entry.Synopsis,
		})
	}
	return result
}

func ContextCommandsForContext(activeContext string) []CompletionEntry {
	reachable := catalog.ReachableContextIDs(normaliseContextName(activeContext))
	result := make([]CompletionEntry, 0, len(reachable))
	for _, name := range reachable {
		description, ok := catalog.ContextDescription(name)
		if !ok {
			continue
		}
		result = append(result, CompletionEntry{
			Command: "$" + strings.ToLower(name),
			Synopsis: description,
		})
	}
	return result
}

func baseSlashCommand(input string) string {
	trimmed := strings.ToLower(strings.TrimSpace(input))
	if !strings.HasPrefix(trimmed, "/") {
		return ""
	}
	fields := strings.Fields(trimmed)
	if len(fields) == 0 {
		return ""
	}
	return fields[0]
}

func ValidateSlashCommandForContext(activeContext string, input string) CommandScopeValidation {
	base := baseSlashCommand(input)
	if base == "" {
		return CommandScopeValidation{Allowed: true}
	}

	currentGroup := commandGroupForContext(activeContext)
	isGlobal, allowedGroups, known := catalog.ScopeForSlashBase(base)
	if !known {
		return CommandScopeValidation{
			Allowed: true,
			Command: base,
			CurrentGroup: currentGroup,
		}
	}
	if isGlobal {
		return CommandScopeValidation{
			Allowed: true,
			Command: base,
			CurrentGroup: currentGroup,
		}
	}
	if slices.Contains(allowedGroups, currentGroup) {
		return CommandScopeValidation{
			Allowed: true,
			Command: base,
			CurrentGroup: currentGroup,
			AllowedGroups: allowedGroups,
		}
	}
	return CommandScopeValidation{
		Allowed: false,
		Command: base,
		CurrentGroup: currentGroup,
		AllowedGroups: allowedGroups,
	}
}

func CommandScopeDescription(groups []string) string {
	if len(groups) == 0 {
		return "$global"
	}
	labels := make([]string, 0, len(groups))
	for _, group := range groups {
		labels = append(labels, "$"+group)
	}
	return strings.Join(labels, " ; ")
}
