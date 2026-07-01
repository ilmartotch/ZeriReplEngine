package ui

import (
	"fmt"
	"sort"
	"strings"
	"yuumi/pkg/catalog"
)

type ErrorCatalogEntry struct {
	Code string
	Message string
	Trigger string
	Hint string
}

func RenderErrorCatalog(filter string) string {
	needle := strings.ToUpper(strings.TrimSpace(filter))
	entries := make([]ErrorCatalogEntry, 0)

	for _, entry := range catalog.Errors() {
		if needle != "" && !strings.HasPrefix(entry.Code, needle) {
			continue
		}
		entries = append(entries, ErrorCatalogEntry{
			Code: entry.Code,
			Message: entry.Message,
			Trigger: entry.Trigger,
			Hint: entry.Hint,
		})
	}

	if len(entries) == 0 {
		return fmt.Sprintf("No error codes match %q.\nTry /errors (all codes) or a family like /errors ai.", strings.TrimSpace(filter))
	}

	sort.SliceStable(entries, func(i, j int) bool {
		return entries[i].Code < entries[j].Code
	})

	var b strings.Builder
	if needle == "" {
		b.WriteString("Zeri error catalog (")
		b.WriteString(fmt.Sprintf("%d codes", len(entries)))
		b.WriteString("). Filter with /errors <family> (example: /errors ai).\n")
	} else {
		b.WriteString(fmt.Sprintf("Zeri error catalog — %s (%d codes).\n", needle, len(entries)))
	}

	for _, entry := range entries {
		b.WriteString("\n")
		b.WriteString(entry.Code)
		b.WriteString(" — ")
		b.WriteString(entry.Message)
		b.WriteString("\nWhen: ")
		b.WriteString(entry.Trigger)
		b.WriteString("\nFix: ")
		b.WriteString(entry.Hint)
		b.WriteString("\n")
	}
	return strings.TrimRight(b.String(), "\n")
}
