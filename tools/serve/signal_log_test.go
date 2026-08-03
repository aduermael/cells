package main

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestSummarizeSDP(t *testing.T) {
	raw, _ := json.Marshal(map[string]string{
		"type": "offer",
		"sdp": "v=0\r\n" +
			"o=- 0 0 IN IP4 127.0.0.1\r\n" +
			"s=-\r\n" +
			"t=0 0\r\n" +
			"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n" +
			"a=ice-ufrag:abc\r\n" +
			"a=fingerprint:sha-256 00:11\r\n",
	})
	s := summarizeSDP(raw)
	if !strings.Contains(s, "sdp_type=offer") {
		t.Fatalf("expected sdp_type=offer in %q", s)
	}
	if !strings.Contains(s, "app=true") {
		t.Fatalf("expected app=true in %q", s)
	}
	if !strings.Contains(s, "fingerprint=true") {
		t.Fatalf("expected fingerprint=true in %q", s)
	}
	if !strings.Contains(s, "ice_ufrag=true") {
		t.Fatalf("expected ice_ufrag=true in %q", s)
	}
}

func TestSummarizeICE(t *testing.T) {
	raw, _ := json.Marshal(map[string]interface{}{
		"candidate":     "candidate:1 1 udp 2130706431 10.0.0.2 54321 typ host",
		"sdpMid":        "0",
		"sdpMLineIndex": 0,
	})
	s := summarizeICE(raw)
	if !strings.Contains(s, "ice_typ=host") {
		t.Fatalf("expected ice_typ=host in %q", s)
	}
	if !strings.Contains(s, "a_prefix=false") {
		t.Fatalf("expected a_prefix=false in %q", s)
	}

	rawA, _ := json.Marshal(map[string]interface{}{
		"candidate":     "a=candidate:1 1 udp 2130706431 10.0.0.2 54321 typ srflx",
		"sdpMid":        "0",
		"sdpMLineIndex": 0,
	})
	sA := summarizeICE(rawA)
	if !strings.Contains(sA, "ice_typ=srflx") {
		t.Fatalf("expected ice_typ=srflx in %q", sA)
	}
	if !strings.Contains(sA, "a_prefix=true") {
		t.Fatalf("expected a_prefix=true in %q", sA)
	}

	end, _ := json.Marshal(map[string]interface{}{
		"candidate": "",
		"sdpMid":    "0",
	})
	if summarizeICE(end) != "ice=end-of-candidates" {
		t.Fatalf("expected end-of-candidates, got %q", summarizeICE(end))
	}
}

func TestSignalTimelineIceBeforeOffer(t *testing.T) {
	tl := newSignalTimeline()
	before, nBefore, _ := tl.noteIce("room1", "A", "B")
	if !before || nBefore != 1 {
		t.Fatalf("first ice should be before offer: before=%v n=%d", before, nBefore)
	}
	before, nBefore, _ = tl.noteIce("room1", "A", "B")
	if !before || nBefore != 2 {
		t.Fatalf("second ice before offer: before=%v n=%d", before, nBefore)
	}
	if n := tl.noteOffer("room1", "A", "B"); n != 2 {
		t.Fatalf("offer should report iceBefore=2, got %d", n)
	}
	before, _, nAfter := tl.noteIce("room1", "A", "B")
	if before || nAfter != 1 {
		t.Fatalf("ice after offer: before=%v after=%d", before, nAfter)
	}
	tl.clearPeer("room1", "A")
	// After clear, ice is before offer again
	before, nBefore, _ = tl.noteIce("room1", "A", "B")
	if !before || nBefore != 1 {
		t.Fatalf("after clearPeer expected fresh before-offer ice, before=%v n=%d", before, nBefore)
	}
}

func TestRoomSendToMissingPeer(t *testing.T) {
	room := NewRoom("r", 10)
	err := room.SendTo("nobody", []byte(`{}`))
	if err == nil {
		t.Fatal("expected error for missing peer")
	}
	if !strings.Contains(err.Error(), "nobody") {
		t.Fatalf("error should mention peer id: %v", err)
	}
}
