// Signaling log helpers: compact, always-on summaries for WebRTC debug.
// Avoid dumping full SDP/ICE payloads by default (noise + PII in host addrs).

package main

import (
	"encoding/json"
	"fmt"
	"strings"
	"sync"
)

// pairKey identifies a directed signaling pair within a room.
func pairKey(roomID, from, to string) string {
	return roomID + "|" + from + "->" + to
}

// signalTimeline tracks coarse offer/answer/ice order per pair for debug logs.
type signalTimeline struct {
	mu    sync.Mutex
	pairs map[string]*pairState
}

type pairState struct {
	offerSeen  bool
	answerSeen bool
	iceBefore  int // ice-candidate count before first offer on this directed pair
	iceAfter   int
}

func newSignalTimeline() *signalTimeline {
	return &signalTimeline{pairs: make(map[string]*pairState)}
}

func (t *signalTimeline) get(key string) *pairState {
	if t.pairs[key] == nil {
		t.pairs[key] = &pairState{}
	}
	return t.pairs[key]
}

// noteOffer records an offer on from→to. Returns ice-before-offer count if any.
func (t *signalTimeline) noteOffer(roomID, from, to string) (iceBeforeOffer int) {
	t.mu.Lock()
	defer t.mu.Unlock()
	st := t.get(pairKey(roomID, from, to))
	iceBeforeOffer = st.iceBefore
	st.offerSeen = true
	return iceBeforeOffer
}

// noteAnswer records an answer on from→to.
func (t *signalTimeline) noteAnswer(roomID, from, to string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	st := t.get(pairKey(roomID, from, to))
	st.answerSeen = true
}

// noteIce records an ice-candidate on from→to. Returns (beforeOffer, totalIce).
func (t *signalTimeline) noteIce(roomID, from, to string) (beforeOffer bool, iceBefore, iceAfter int) {
	t.mu.Lock()
	defer t.mu.Unlock()
	st := t.get(pairKey(roomID, from, to))
	if !st.offerSeen {
		st.iceBefore++
		return true, st.iceBefore, st.iceAfter
	}
	st.iceAfter++
	return false, st.iceBefore, st.iceAfter
}

// clearPeer drops all pairs involving peerID in roomID (on leave).
func (t *signalTimeline) clearPeer(roomID, peerID string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	prefix := roomID + "|"
	for k := range t.pairs {
		if !strings.HasPrefix(k, prefix) {
			continue
		}
		// keys: room|from->to
		rest := strings.TrimPrefix(k, prefix)
		parts := strings.SplitN(rest, "->", 2)
		if len(parts) != 2 {
			continue
		}
		if parts[0] == peerID || parts[1] == peerID {
			delete(t.pairs, k)
		}
	}
}

// summarizeSDP returns a one-line description of an SDP blob.
func summarizeSDP(raw json.RawMessage) string {
	if len(raw) == 0 {
		return "sdp=<empty>"
	}
	var obj struct {
		Type string `json:"type"`
		SDP  string `json:"sdp"`
	}
	if err := json.Unmarshal(raw, &obj); err != nil {
		return fmt.Sprintf("sdp=raw_len=%d (parse_err)", len(raw))
	}
	lines := 0
	hasApp := false
	hasFingerprint := false
	hasIceUfrag := false
	for _, line := range strings.Split(obj.SDP, "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		lines++
		if strings.HasPrefix(line, "m=application") {
			hasApp = true
		}
		if strings.HasPrefix(line, "a=fingerprint:") {
			hasFingerprint = true
		}
		if strings.HasPrefix(line, "a=ice-ufrag:") {
			hasIceUfrag = true
		}
	}
	typ := obj.Type
	if typ == "" {
		typ = "?"
	}
	return fmt.Sprintf("sdp_type=%s sdp_bytes=%d sdp_lines=%d app=%t fingerprint=%t ice_ufrag=%t",
		typ, len(obj.SDP), lines, hasApp, hasFingerprint, hasIceUfrag)
}

// summarizeICE returns a one-line description of an ICE candidate payload.
// Highlights browser-interop issues (a= prefix) and candidate type.
func summarizeICE(raw json.RawMessage) string {
	if len(raw) == 0 {
		return "ice=<empty>"
	}
	var obj struct {
		Candidate     string `json:"candidate"`
		SDPMid        string `json:"sdpMid"`
		SDPMLineIndex *int   `json:"sdpMLineIndex"`
	}
	if err := json.Unmarshal(raw, &obj); err != nil {
		return fmt.Sprintf("ice=raw_len=%d (parse_err)", len(raw))
	}
	if obj.Candidate == "" {
		return "ice=end-of-candidates"
	}
	c := obj.Candidate
	hasAPrefix := strings.HasPrefix(c, "a=")
	// Strip a= for type parse
	body := c
	if hasAPrefix {
		body = c[2:]
	}
	// candidate:foundation component protocol priority addr port typ TYPE ...
	typ := "?"
	if i := strings.Index(body, " typ "); i >= 0 {
		rest := body[i+5:]
		fields := strings.Fields(rest)
		if len(fields) > 0 {
			typ = fields[0]
		}
	}
	proto := "?"
	fields := strings.Fields(body)
	// candidate:x N protocol ...
	if len(fields) >= 3 {
		// fields[0] is candidate:foundation
		proto = fields[2]
	}
	mid := obj.SDPMid
	if mid == "" {
		mid = "-"
	}
	mline := -1
	if obj.SDPMLineIndex != nil {
		mline = *obj.SDPMLineIndex
	}
	return fmt.Sprintf("ice_typ=%s proto=%s mid=%s mline=%d a_prefix=%t cand_bytes=%d",
		typ, proto, mid, mline, hasAPrefix, len(c))
}

// summarizeSignalPayload picks SDP or ICE summary based on message type.
func summarizeSignalPayload(msgType string, sdp, candidate json.RawMessage) string {
	switch msgType {
	case "offer", "answer":
		return summarizeSDP(sdp)
	case "ice-candidate":
		return summarizeICE(candidate)
	default:
		return ""
	}
}
