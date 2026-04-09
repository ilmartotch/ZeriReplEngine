package ui

import (
	"regexp"
	"strings"
)

var multiSpaceRe = regexp.MustCompile(`  +`)
var multiNewlineRe = regexp.MustCompile(`\n{3,}`)

func NormaliseContent(raw string) string {
	s := strings.ReplaceAll(raw, "\r\n", "\n")
	s = strings.ReplaceAll(s, "\r", "\n")
	s = strings.ReplaceAll(s, "\t", " ")
	s = multiSpaceRe.ReplaceAllString(s, " ")
	lines := strings.Split(s, "\n")
	for i, l := range lines {
		lines[i] = strings.TrimSpace(l)
	}
	s = multiNewlineRe.ReplaceAllString(strings.Join(lines, "\n"), "\n\n")
	return strings.TrimSpace(s)
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [normalise.go]
 *
 * What changed:
 *   - Implements NormaliseContent() for message content sanitisation.
 *   - Pre-compiled regexps avoid repeated compilation per call.
 *
 * Why:
 *   - Every string entering ChatMessage.Content must pass through this
 *     function to guarantee consistent rendering in the viewport.
 *   - Normalises line endings, collapses whitespace, trims per-line,
 *     and limits consecutive blank lines to one.
 *
 * Impact on other components:
 *   - update.go calls NormaliseContent before storing any message.
 *   - bridge/yuumi_client.go calls it on incoming DATA frames.
 *
 * Future maintenance notes:
 *   - If code blocks need preserved indentation, add a bypass flag.
 */
