package catalog

import (
	"embed"
	"encoding/json"
	"fmt"
	"slices"
	"strings"
	"sync"
)

const supportedCatalogVersion = 1

const (
	BridgeTypeHandshakeID = "HANDSHAKE"
	BridgeTypeCommandID = "COMMAND"
	BridgeTypeInputResponseID = "INPUT_RESPONSE"
	BridgeTypeListScriptsWithContentID = "LIST_SCRIPTS_WITH_CONTENT"
	BridgeTypeSettingsSnapshotID = "SETTINGS_SNAPSHOT"
	BridgeTypeSettingsUpdateID = "SETTINGS_UPDATE"
	BridgeTypeSessionSaveStateID = "SESSION_SAVE_STATE"
	BridgeTypeSessionLoadStateID = "SESSION_LOAD_STATE"
	BridgeTypeSaveScriptID = "SAVE_SCRIPT"
	BridgeTypeDeleteScriptID = "DELETE_SCRIPT"
	BridgeTypeRunScriptID = "RUN_SCRIPT"
	BridgeTypeCancelExecutionID = "CANCEL_EXECUTION"
	BridgeTypeSharedScopeSnapshotID = "SHARED_SCOPE_SNAPSHOT"
	BridgeTypeSharedGetID = "SHARED_GET"
	BridgeTypeSharedSetID = "SHARED_SET"
	BridgeTypeSharedListID = "SHARED_LIST"
	BridgeTypeSharedValueID = "SHARED_VALUE"
	BridgeTypeSharedAckID = "SHARED_ACK"
	BridgeTypeSharedListResponseID = "SHARED_LIST_RESPONSE"
	BridgeTypeReadyID = "READY"
	BridgeTypeOutputID = "OUTPUT"
	BridgeTypeErrorID = "ERROR"
	BridgeTypeInfoID = "INFO"
	BridgeTypeSuccessID = "SUCCESS"
	BridgeTypeContextChangedID = "CONTEXT_CHANGED"
	BridgeTypeCodeModeID = "CODE_MODE"
	BridgeTypeReqInputID = "REQ_INPUT"
	BridgeTypeSelRequestID = "SEL_REQUEST"
	BridgeTypeScriptListResponseID = "SCRIPT_LIST_RESPONSE"
	BridgeTypeScriptActionResponseID = "SCRIPT_ACTION_RESPONSE"
	BridgeTypeSessionStateResponseID = "SESSION_STATE_RESPONSE"
	BridgeTypeSettingsSnapshotResponseID = "SETTINGS_SNAPSHOT_RESPONSE"
	BridgeTypeSettingsUpdateResponseID = "SETTINGS_UPDATE_RESPONSE"
	BridgeTypeSharedScopeSnapshotResponseID = "SHARED_SCOPE_SNAPSHOT_RESPONSE"
	BridgeTypeStreamBatchEndID = "STREAM_BATCH_END"
	BridgeTypeShutdownID = "SHUTDOWN"
	BridgeTypeQuitID = "QUIT"
)

//go:embed data/*.json
var embeddedCatalogFS embed.FS

type ContextEntry struct {
	ID string `json:"id"`
	Description string `json:"description"`
	Reachable []string `json:"reachable,omitempty"`
}

type CommandScope struct {
	Type string `json:"type"`
	Contexts []string `json:"contexts,omitempty"`
}

type CommandEntry struct {
	ID string `json:"id"`
	Command string `json:"command"`
	Synopsis string `json:"synopsis"`
	Scope CommandScope `json:"scope"`
	Owners []string `json:"owners,omitempty"`
}

type ErrorEntry struct {
	Code string `json:"code"`
	Message string `json:"message"`
	Trigger string `json:"trigger"`
	Hint string `json:"hint"`
}

type LanguageEntry struct {
	ID string `json:"id"`
	Aliases []string `json:"aliases,omitempty"`
	Extension string `json:"extension"`
	Folder string `json:"folder"`
	Runtime string `json:"runtime"`
	Context string `json:"context"`
}

type BridgeTypeEntry struct {
	ID string `json:"id"`
	Value string `json:"value"`
}

type commandsCatalog struct {
	Version int `json:"version"`
	Contexts []ContextEntry `json:"contexts"`
	Commands []CommandEntry `json:"commands"`
}

type errorsCatalog struct {
	Version int `json:"version"`
	Entries []ErrorEntry `json:"entries"`
}

type languagesCatalog struct {
	Version int `json:"version"`
	Languages []LanguageEntry `json:"languages"`
}

type bridgeTypesCatalog struct {
	Version int `json:"version"`
	Types []BridgeTypeEntry `json:"types"`
}

type catalogState struct {
	commands commandsCatalog
	errors errorsCatalog
	languages languagesCatalog
	bridgeTypes bridgeTypesCatalog
	contextByID map[string]ContextEntry
	errorByCode map[string]ErrorEntry
	languageByAlias map[string]LanguageEntry
	bridgeTypeByID map[string]string
	runtimeToFolders map[string][]string
}

var (
	loadOnce sync.Once
	loaded catalogState
	loadedErr error
)

func ensureLoaded() catalogState {
	loadOnce.Do(func() {
		loaded, loadedErr = loadCatalogState()
	})
	if loadedErr != nil {
		panic(loadedErr)
	}
	return loaded
}

func loadCatalogState() (catalogState, error) {
	state := catalogState{}
	if err := readJSON("data/commands_catalog.json", &state.commands); err != nil {
		return catalogState{}, fmt.Errorf("loading commands catalog: %w", err)
	}
	if err := readJSON("data/errors_catalog.json", &state.errors); err != nil {
		return catalogState{}, fmt.Errorf("loading errors catalog: %w", err)
	}
	if err := readJSON("data/languages_catalog.json", &state.languages); err != nil {
		return catalogState{}, fmt.Errorf("loading languages catalog: %w", err)
	}
	if err := readJSON("data/bridge_types_catalog.json", &state.bridgeTypes); err != nil {
		return catalogState{}, fmt.Errorf("loading bridge types catalog: %w", err)
	}

	if err := validateVersion("commands", state.commands.Version); err != nil {
		return catalogState{}, err
	}
	if err := validateVersion("errors", state.errors.Version); err != nil {
		return catalogState{}, err
	}
	if err := validateVersion("languages", state.languages.Version); err != nil {
		return catalogState{}, err
	}
	if err := validateVersion("bridge types", state.bridgeTypes.Version); err != nil {
		return catalogState{}, err
	}

	state.contextByID = make(map[string]ContextEntry, len(state.commands.Contexts))
	for _, context := range state.commands.Contexts {
		id := normalizeID(context.ID)
		if id == "" {
			return catalogState{}, fmt.Errorf("commands catalog: context id cannot be empty")
		}
		if _, ok := state.contextByID[id]; ok {
			return catalogState{}, fmt.Errorf("commands catalog: duplicated context id %q", id)
		}
		context.ID = id
		if len(context.Reachable) == 0 {
			context.Reachable = []string{id}
		}
		for i := range context.Reachable {
			context.Reachable[i] = normalizeID(context.Reachable[i])
		}
		state.contextByID[id] = context
	}
	if _, ok := state.contextByID["global"]; !ok {
		return catalogState{}, fmt.Errorf("commands catalog: missing required context \"global\"")
	}

	for _, context := range state.contextByID {
		for _, target := range context.Reachable {
			if _, ok := state.contextByID[target]; !ok {
				return catalogState{}, fmt.Errorf("commands catalog: context %q references unknown reachable context %q", context.ID, target)
			}
		}
	}

	for i := range state.commands.Commands {
		command := &state.commands.Commands[i]
		command.ID = strings.TrimSpace(command.ID)
		command.Command = strings.TrimSpace(command.Command)
		command.Synopsis = strings.TrimSpace(command.Synopsis)
		command.Scope.Type = normalizeID(command.Scope.Type)
		if command.ID == "" || command.Command == "" || command.Synopsis == "" {
			return catalogState{}, fmt.Errorf("commands catalog: command entries require id, command and synopsis")
		}
		if command.Scope.Type != "global" && command.Scope.Type != "context" {
			return catalogState{}, fmt.Errorf("commands catalog: command %q has invalid scope type %q", command.ID, command.Scope.Type)
		}
		for j := range command.Owners {
			command.Owners[j] = normalizeID(command.Owners[j])
		}
		if len(command.Owners) == 0 {
			command.Owners = []string{"engine", "tui"}
		}
		if command.Scope.Type == "context" {
			if len(command.Scope.Contexts) == 0 {
				return catalogState{}, fmt.Errorf("commands catalog: command %q has context scope without contexts", command.ID)
			}
			dedup := make(map[string]struct{}, len(command.Scope.Contexts))
			normalized := make([]string, 0, len(command.Scope.Contexts))
			for _, rawContext := range command.Scope.Contexts {
				ctx := normalizeID(rawContext)
				if _, ok := state.contextByID[ctx]; !ok {
					return catalogState{}, fmt.Errorf("commands catalog: command %q references unknown context %q", command.ID, ctx)
				}
				if _, ok := dedup[ctx]; ok {
					continue
				}
				dedup[ctx] = struct{}{}
				normalized = append(normalized, ctx)
			}
			command.Scope.Contexts = normalized
		}
	}

	state.errorByCode = make(map[string]ErrorEntry, len(state.errors.Entries))
	for _, entry := range state.errors.Entries {
		code := strings.ToUpper(strings.TrimSpace(entry.Code))
		if code == "" {
			return catalogState{}, fmt.Errorf("errors catalog: code cannot be empty")
		}
		entry.Code = code
		state.errorByCode[code] = entry
	}

	state.languageByAlias = make(map[string]LanguageEntry)
	state.runtimeToFolders = make(map[string][]string)
	for _, language := range state.languages.Languages {
		language.ID = normalizeID(language.ID)
		language.Extension = normalizeID(language.Extension)
		language.Folder = normalizeID(language.Folder)
		language.Runtime = normalizeID(language.Runtime)
		language.Context = normalizeID(language.Context)
		if language.ID == "" || language.Extension == "" || language.Folder == "" || language.Runtime == "" || language.Context == "" {
			return catalogState{}, fmt.Errorf("languages catalog: id, extension, folder, runtime and context are required")
		}
		if _, ok := state.contextByID[language.Context]; !ok {
			return catalogState{}, fmt.Errorf("languages catalog: language %q references unknown context %q", language.ID, language.Context)
		}
		registerLanguageAlias := func(alias string) error {
			if alias == "" {
				return nil
			}
			if existing, ok := state.languageByAlias[alias]; ok && existing.ID != language.ID {
				return fmt.Errorf("languages catalog: alias %q is shared by %q and %q", alias, existing.ID, language.ID)
			}
			state.languageByAlias[alias] = language
			return nil
		}
		if err := registerLanguageAlias(language.ID); err != nil {
			return catalogState{}, err
		}
		if err := registerLanguageAlias(language.Context); err != nil {
			return catalogState{}, err
		}
		if err := registerLanguageAlias(language.Folder); err != nil {
			return catalogState{}, err
		}
		for _, alias := range language.Aliases {
			if err := registerLanguageAlias(normalizeID(alias)); err != nil {
				return catalogState{}, err
			}
		}
		state.runtimeToFolders[language.Runtime] = append(state.runtimeToFolders[language.Runtime], language.Folder)
	}
	for runtime, folders := range state.runtimeToFolders {
		state.runtimeToFolders[runtime] = slices.Compact(slices.Clone(folders))
	}

	state.bridgeTypeByID = make(map[string]string, len(state.bridgeTypes.Types))
	for _, entry := range state.bridgeTypes.Types {
		id := strings.ToUpper(strings.TrimSpace(entry.ID))
		value := normalizeID(entry.Value)
		if id == "" || value == "" {
			return catalogState{}, fmt.Errorf("bridge types catalog: id and value are required")
		}
		if _, ok := state.bridgeTypeByID[id]; ok {
			return catalogState{}, fmt.Errorf("bridge types catalog: duplicated id %q", id)
		}
		state.bridgeTypeByID[id] = value
	}

	return state, nil
}

func readJSON(path string, destination any) error {
	raw, err := embeddedCatalogFS.ReadFile(path)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(raw, destination); err != nil {
		return err
	}
	return nil
}

func validateVersion(catalogName string, version int) error {
	if version <= 0 {
		return fmt.Errorf("%s catalog: version must be a positive integer", catalogName)
	}
	if version > supportedCatalogVersion {
		return fmt.Errorf("%s catalog: unsupported version %d (max supported %d)", catalogName, version, supportedCatalogVersion)
	}
	return nil
}

func normalizeID(value string) string {
	return strings.ToLower(strings.TrimSpace(value))
}

func normalizeContext(value string) string {
	normalized := normalizeID(strings.TrimPrefix(strings.TrimSpace(value), "$"))
	normalized = strings.TrimPrefix(normalized, "zeri::")
	parts := strings.Split(normalized, "::")
	if len(parts) == 0 {
		return normalized
	}
	return parts[len(parts)-1]
}

func normalizeSlashBase(value string) string {
	trimmed := strings.ToLower(strings.TrimSpace(value))
	if !strings.HasPrefix(trimmed, "/") {
		return trimmed
	}
	fields := strings.Fields(trimmed)
	if len(fields) == 0 {
		return ""
	}
	return fields[0]
}

func Contexts() []ContextEntry {
	state := ensureLoaded()
	return slices.Clone(state.commands.Contexts)
}

func ReachableContextIDs(activeContext string) []string {
	state := ensureLoaded()
	key := normalizeContext(activeContext)
	if key == "" {
		key = "global"
	}
	if context, ok := state.contextByID[key]; ok {
		return slices.Clone(context.Reachable)
	}
	if context, ok := state.contextByID["global"]; ok {
		return slices.Clone(context.Reachable)
	}
	return []string{"global"}
}

func ContextDescription(contextID string) (string, bool) {
	state := ensureLoaded()
	context, ok := state.contextByID[normalizeID(contextID)]
	if !ok {
		return "", false
	}
	return context.Description, true
}

func Commands() []CommandEntry {
	state := ensureLoaded()
	return slices.Clone(state.commands.Commands)
}

func CommandsForContext(activeContext string) []CommandEntry {
	state := ensureLoaded()
	contextID := normalizeContext(activeContext)
	if contextID == "" {
		contextID = "global"
	}
	result := make([]CommandEntry, 0, len(state.commands.Commands))
	for _, entry := range state.commands.Commands {
		if entry.Scope.Type == "global" {
			result = append(result, entry)
			continue
		}
		if slices.Contains(entry.Scope.Contexts, contextID) {
			result = append(result, entry)
		}
	}
	return result
}

func ScopeForSlashBase(baseCommand string) (bool, []string, bool) {
	state := ensureLoaded()
	base := normalizeSlashBase(baseCommand)
	if base == "" {
		return false, nil, false
	}
	found := false
	global := false
	contextSet := map[string]struct{}{}
	for _, entry := range state.commands.Commands {
		if normalizeSlashBase(entry.Command) != base {
			continue
		}
		found = true
		if entry.Scope.Type == "global" {
			global = true
			continue
		}
		for _, contextID := range entry.Scope.Contexts {
			contextSet[contextID] = struct{}{}
		}
	}
	if !found {
		return false, nil, false
	}
	contexts := make([]string, 0, len(contextSet))
	for contextID := range contextSet {
		contexts = append(contexts, contextID)
	}
	slices.Sort(contexts)
	return global, contexts, true
}

func IsEngineGlobalSlashCommand(baseCommand string) bool {
	state := ensureLoaded()
	base := normalizeSlashBase(baseCommand)
	if base == "" {
		return false
	}
	for _, entry := range state.commands.Commands {
		if normalizeSlashBase(entry.Command) != base {
			continue
		}
		if entry.Scope.Type != "global" {
			continue
		}
		if slices.Contains(entry.Owners, "engine") {
			return true
		}
	}
	return false
}

func Errors() []ErrorEntry {
	state := ensureLoaded()
	return slices.Clone(state.errors.Entries)
}

func ErrorByCode(code string) (ErrorEntry, bool) {
	state := ensureLoaded()
	entry, ok := state.errorByCode[strings.ToUpper(strings.TrimSpace(code))]
	return entry, ok
}

func Languages() []LanguageEntry {
	state := ensureLoaded()
	return slices.Clone(state.languages.Languages)
}

func ResolveLanguage(value string) (LanguageEntry, bool) {
	state := ensureLoaded()
	normalized := normalizeID(value)
	if normalized == "" {
		return LanguageEntry{}, false
	}
	entry, ok := state.languageByAlias[normalized]
	return entry, ok
}

func IsLanguageContext(contextID string) bool {
	_, ok := ResolveLanguage(contextID)
	return ok
}

func LanguageFolders() []string {
	state := ensureLoaded()
	folders := make([]string, 0, len(state.languages.Languages))
	for _, language := range state.languages.Languages {
		folders = append(folders, language.Folder)
	}
	return folders
}

func RuntimeLanguageFolders(runtimeName string) []string {
	state := ensureLoaded()
	folders, ok := state.runtimeToFolders[normalizeID(runtimeName)]
	if !ok {
		return nil
	}
	return slices.Clone(folders)
}

func BridgeTypeValue(typeID string) string {
	state := ensureLoaded()
	value, ok := state.bridgeTypeByID[strings.ToUpper(strings.TrimSpace(typeID))]
	if !ok {
		return ""
	}
	return value
}
