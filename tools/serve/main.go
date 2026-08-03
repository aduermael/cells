// Static file server for Cells WASM distribution with WebSocket signaling
// Usage: go run tools/serve/main.go [options]
//
// This server is designed to serve the WASM distribution with correct
// MIME types and CORS headers for local development and testing.
// It also provides WebSocket signaling for WebRTC peer connections.

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"github.com/gorilla/websocket"
)

// SignalingMessage represents a WebSocket signaling message.
type SignalingMessage struct {
	Type      string          `json:"type"`
	Room      string          `json:"room,omitempty"`
	PeerID    string          `json:"peer_id,omitempty"`
	Target    string          `json:"target,omitempty"`
	From      string          `json:"from,omitempty"`
	SDP       json.RawMessage `json:"sdp,omitempty"`
	Candidate json.RawMessage `json:"candidate,omitempty"`
	Error     string          `json:"error,omitempty"`
	Peers     []string        `json:"peers,omitempty"`
}

// WebSocket connection parameters
// Generous timeouts: browser tabs can hitch during WASM/WebRTC work and miss a
// single ping window; tearing them down mid-collab was a major reliability issue.
const (
	pingInterval = 25 * time.Second
	pongTimeout  = 60 * time.Second
	writeTimeout = 30 * time.Second
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true // Allow all origins for development
	},
}

var roomManager *RoomManager
var sigTimeline = newSignalTimeline()
var signalingVerbose bool // full SDP/ICE payloads when -signaling-verbose

func handleWebSocket(w http.ResponseWriter, r *http.Request) {
	remote := r.RemoteAddr
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("[WS] upgrade error remote=%s: %v", remote, err)
		return
	}
	log.Printf("[WS] upgraded remote=%s", remote)

	// Read room and peer ID from query params or first message
	roomID := r.URL.Query().Get("room")
	peerID := r.URL.Query().Get("peer_id")

	// If not in query params, wait for join message
	if roomID == "" || peerID == "" {
		conn.SetReadDeadline(time.Now().Add(10 * time.Second))
		_, msgBytes, err := conn.ReadMessage()
		if err != nil {
			log.Printf("[WS] join read failed remote=%s: %v", remote, err)
			conn.Close()
			return
		}
		conn.SetReadDeadline(time.Time{}) // Reset deadline

		var msg SignalingMessage
		if err := json.Unmarshal(msgBytes, &msg); err != nil {
			log.Printf("[WS] join parse failed remote=%s: %v", remote, err)
			sendError(conn, "invalid_message", "Failed to parse message")
			conn.Close()
			return
		}

		if msg.Type != "join" {
			log.Printf("[WS] first message not join remote=%s type=%s", remote, msg.Type)
			sendError(conn, "expected_join", "First message must be 'join'")
			conn.Close()
			return
		}

		roomID = msg.Room
		peerID = msg.PeerID
	}

	if roomID == "" {
		sendError(conn, "missing_room", "Room ID is required")
		conn.Close()
		return
	}

	if peerID == "" {
		sendError(conn, "missing_peer_id", "Peer ID is required")
		conn.Close()
		return
	}

	// Join room
	room := roomManager.GetOrCreateRoom(roomID)
	addRes := room.AddPeerDetailed(peerID, conn)
	if !addRes.OK {
		log.Printf("[JOIN] room_full room=%s peer=%s max=%d remote=%s", roomID, peerID, room.MaxPeers, remote)
		sendError(conn, "room_full", "Room is full")
		conn.Close()
		return
	}
	peer := addRes.Peer

	peers := room.GetPeers()
	rejoinTag := ""
	if addRes.Rejoin {
		rejoinTag = " rejoin=true"
	}
	log.Printf("[JOIN] peer=%s room=%s n=%d peers=%v remote=%s%s",
		peerID, roomID, len(peers), peers, remote, rejoinTag)

	// Notify existing peers about new peer
	notifyPeerJoined(room, peerID)

	// Send 'joined' confirmation with list of existing peers
	sendJoinedConfirmation(conn, room, peerID)

	// Set up ping/pong for connection health monitoring
	done := make(chan struct{})
	go pingRoutine(peer, done)

	// Set pong handler to extend read deadline
	conn.SetPongHandler(func(string) error {
		conn.SetReadDeadline(time.Now().Add(pingInterval + pongTimeout))
		peer.UpdateActivity()
		return nil
	})

	// Set initial read deadline
	conn.SetReadDeadline(time.Now().Add(pingInterval + pongTimeout))

	// Handle messages until disconnect
	handlePeerMessages(room, peer)

	// Stop ping routine
	close(done)

	// Cleanup on disconnect
	room.RemovePeer(peerID)
	sigTimeline.clearPeer(roomID, peerID)
	notifyPeerLeft(room, peerID)
	remaining := room.GetPeers()
	log.Printf("[LEAVE] peer=%s room=%s n=%d remaining=%v", peerID, roomID, len(remaining), remaining)
}

func sendError(conn *websocket.Conn, code string, message string) {
	msg := SignalingMessage{
		Type:  "error",
		Error: fmt.Sprintf("%s: %s", code, message),
	}
	msgBytes, _ := json.Marshal(msg)
	log.Printf("[ERR] code=%s message=%s", code, message)
	if err := conn.WriteMessage(websocket.TextMessage, msgBytes); err != nil {
		log.Printf("[ERR] write failed code=%s: %v", code, err)
	}
}

// pingRoutine sends periodic ping messages to keep the connection alive
func pingRoutine(peer *Peer, done <-chan struct{}) {
	ticker := time.NewTicker(pingInterval)
	defer ticker.Stop()
	// Tolerate a few failed pings before killing (transient main-thread stalls)
	failures := 0
	const maxFailures = 3

	for {
		select {
		case <-ticker.C:
			peer.mu.Lock()
			err := peer.Conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if err == nil {
				err = peer.Conn.WriteMessage(websocket.PingMessage, nil)
			}
			peer.mu.Unlock()

			if err != nil {
				failures++
				log.Printf("[PING] fail peer=%s n=%d/%d: %v", peer.ID, failures, maxFailures, err)
				if failures >= maxFailures {
					log.Printf("[PING] closing peer=%s after %d failures", peer.ID, maxFailures)
					peer.Conn.Close()
					return
				}
				continue
			}
			failures = 0
		case <-done:
			return
		}
	}
}

func notifyPeerJoined(room *Room, peerID string) {
	msg := SignalingMessage{
		Type:   "peer-joined",
		PeerID: peerID,
	}
	msgBytes, _ := json.Marshal(msg)
	others := room.GetOtherPeers(peerID)
	log.Printf("[NOTIFY] peer-joined peer=%s room=%s notify_count=%d", peerID, room.ID, len(others))
	room.Broadcast(peerID, msgBytes)
}

func notifyPeerLeft(room *Room, peerID string) {
	msg := SignalingMessage{
		Type:   "peer-left",
		PeerID: peerID,
	}
	msgBytes, _ := json.Marshal(msg)
	others := room.GetOtherPeers(peerID)
	log.Printf("[NOTIFY] peer-left peer=%s room=%s notify_count=%d", peerID, room.ID, len(others))
	room.Broadcast(peerID, msgBytes)
}

func sendJoinedConfirmation(conn *websocket.Conn, room *Room, peerID string) {
	peers := room.GetPeers()
	// Filter out the requesting peer
	otherPeers := make([]string, 0, len(peers)-1)
	for _, p := range peers {
		if p != peerID {
			otherPeers = append(otherPeers, p)
		}
	}

	// Send 'joined' confirmation with list of existing peers
	msg := SignalingMessage{
		Type:  "joined",
		Room:  room.ID,
		Peers: otherPeers,
	}
	msgBytes, _ := json.Marshal(msg)
	if err := conn.WriteMessage(websocket.TextMessage, msgBytes); err != nil {
		log.Printf("[JOINED] send failed peer=%s room=%s: %v", peerID, room.ID, err)
		return
	}
	log.Printf("[JOINED] peer=%s room=%s existing_peers=%v", peerID, room.ID, otherPeers)
}

func handlePeerMessages(room *Room, peer *Peer) {
	for {
		_, msgBytes, err := peer.Conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("[MSG] ws_error peer=%s room=%s: %v", peer.ID, room.ID, err)
			} else {
				log.Printf("[MSG] ws_close peer=%s room=%s: %v", peer.ID, room.ID, err)
			}
			return
		}

		peer.UpdateActivity()

		var msg SignalingMessage
		if err := json.Unmarshal(msgBytes, &msg); err != nil {
			log.Printf("[MSG] parse_error peer=%s room=%s bytes=%d: %v", peer.ID, room.ID, len(msgBytes), err)
			continue
		}

		summary := summarizeSignalPayload(msg.Type, msg.SDP, msg.Candidate)
		if summary != "" {
			log.Printf("[MSG] type=%s from=%s to=%s room=%s bytes=%d %s",
				msg.Type, peer.ID, msg.Target, room.ID, len(msgBytes), summary)
		} else {
			log.Printf("[MSG] type=%s from=%s to=%s room=%s bytes=%d",
				msg.Type, peer.ID, msg.Target, room.ID, len(msgBytes))
		}
		if signalingVerbose {
			if len(msg.SDP) > 0 {
				log.Printf("[MSG-VERBOSE] sdp raw=%s", string(msg.SDP))
			}
			if len(msg.Candidate) > 0 {
				log.Printf("[MSG-VERBOSE] ice raw=%s", string(msg.Candidate))
			}
		}

		// Timeline: ice-before-offer is a common CLI↔browser interop failure mode.
		switch msg.Type {
		case "offer":
			if n := sigTimeline.noteOffer(room.ID, peer.ID, msg.Target); n > 0 {
				log.Printf("[ORDER] WARN ice_before_offer count=%d from=%s to=%s room=%s "+
					"(remote may drop early candidates if no PC yet)",
					n, peer.ID, msg.Target, room.ID)
			}
		case "answer":
			sigTimeline.noteAnswer(room.ID, peer.ID, msg.Target)
		case "ice-candidate":
			before, iceBefore, iceAfter := sigTimeline.noteIce(room.ID, peer.ID, msg.Target)
			if before {
				log.Printf("[ORDER] ice_before_offer n_before=%d from=%s to=%s room=%s",
					iceBefore, peer.ID, msg.Target, room.ID)
			} else if iceAfter == 1 {
				log.Printf("[ORDER] first_ice_after_offer from=%s to=%s room=%s",
					peer.ID, msg.Target, room.ID)
			}
		}

		switch msg.Type {
		case "offer", "answer", "ice-candidate":
			relayMessage(room, peer.ID, msg)
		case "leave":
			log.Printf("[MSG] leave peer=%s room=%s", peer.ID, room.ID)
			return
		default:
			log.Printf("[MSG] unknown_type peer=%s room=%s type=%s", peer.ID, room.ID, msg.Type)
		}
	}
}

func relayMessage(room *Room, fromPeerID string, msg SignalingMessage) {
	if msg.Target == "" {
		log.Printf("[RELAY] missing_target type=%s from=%s room=%s", msg.Type, fromPeerID, room.ID)
		return
	}

	// Add sender info
	relayedMsg := SignalingMessage{
		Type:      msg.Type,
		From:      fromPeerID,
		SDP:       msg.SDP,
		Candidate: msg.Candidate,
	}
	msgBytes, err := json.Marshal(relayedMsg)
	if err != nil {
		log.Printf("[RELAY] marshal_error type=%s from=%s to=%s: %v", msg.Type, fromPeerID, msg.Target, err)
		return
	}

	if err := room.SendTo(msg.Target, msgBytes); err != nil {
		log.Printf("[RELAY] FAIL type=%s from=%s to=%s room=%s peers=%v: %v",
			msg.Type, fromPeerID, msg.Target, room.ID, room.GetPeers(), err)
		return
	}
	log.Printf("[RELAY] ok type=%s from=%s to=%s room=%s out_bytes=%d",
		msg.Type, fromPeerID, msg.Target, room.ID, len(msgBytes))
}

func main() {
	port := flag.Int("port", 8081, "Port to listen on")
	dir := flag.String("dir", "dist/wasm", "Directory to serve")
	enableCollab := flag.Bool("enable-collab", false, "Enable collaboration WebSocket endpoint")
	maxRoomSize := flag.Int("max-room-size", 10, "Maximum peers per room")
	roomTimeout := flag.Duration("room-timeout", time.Hour, "Timeout for empty rooms")
	verbose := flag.Bool("signaling-verbose", false,
		"Log full SDP/ICE JSON payloads (default: compact summaries only)")
	flag.Parse()
	signalingVerbose = *verbose

	// Initialize room manager if collaboration is enabled
	if *enableCollab {
		roomManager = NewRoomManager(*maxRoomSize, *roomTimeout)
		// Start cleanup routine
		stop := make(chan struct{})
		roomManager.StartCleanupRoutine(time.Minute, stop)
		log.Println("Collaboration enabled with WebSocket signaling at /ws")
		if signalingVerbose {
			log.Println("Signaling verbose payload logging ON (-signaling-verbose)")
		} else {
			log.Println("Signaling logs: JOIN/LEAVE/MSG/RELAY/ORDER summaries (use -signaling-verbose for raw SDP/ICE)")
		}
	}


	// Resolve directory path
	absDir, err := filepath.Abs(*dir)
	if err != nil {
		log.Fatalf("Failed to resolve directory: %v", err)
	}

	// Check if directory exists
	if _, err := os.Stat(absDir); os.IsNotExist(err) {
		log.Fatalf("Directory does not exist: %s\nRun 'bazel run :wasm' first.", absDir)
	}

	// Create file server with custom MIME types
	fs := http.FileServer(http.Dir(absDir))
	fileHandler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Set correct MIME types based on file extension
		ext := filepath.Ext(r.URL.Path)
		switch ext {
		case ".wasm":
			w.Header().Set("Content-Type", "application/wasm")
		case ".js":
			w.Header().Set("Content-Type", "application/javascript")
		case ".css":
			w.Header().Set("Content-Type", "text/css")
		case ".html":
			w.Header().Set("Content-Type", "text/html")
		case ".json":
			w.Header().Set("Content-Type", "application/json")
		}

		// CORS headers for local development
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

		// Handle preflight requests
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}

		fs.ServeHTTP(w, r)
	})

	// Set up routes
	mux := http.NewServeMux()
	if *enableCollab {
		mux.HandleFunc("/ws", handleWebSocket)
	}
	mux.Handle("/", fileHandler)

	addr := fmt.Sprintf(":%d", *port)
	fmt.Printf("Serving %s on http://localhost%s/\n", absDir, addr)
	if *enableCollab {
		fmt.Printf("WebSocket signaling at ws://localhost%s/ws\n", addr)
	}
	fmt.Println("Press Ctrl+C to stop")

	// Wrap mux with logging middleware
	loggedMux := loggingMiddleware(mux)

	if err := http.ListenAndServe(addr, loggedMux); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}

// loggingMiddleware wraps an http.Handler to log all requests
func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()

		// Wrap ResponseWriter to capture status code
		lrw := &loggingResponseWriter{ResponseWriter: w, statusCode: http.StatusOK}

		// Call the next handler
		next.ServeHTTP(lrw, r)

		// Log the request (skip noisy static file requests)
		path := r.URL.Path
		if path == "/" || path == "/ws" || len(path) > 4 && path[:4] == "/api" {
			log.Printf("[HTTP] %s %s %d %v", r.Method, r.URL.Path, lrw.statusCode, time.Since(start))
		}
	})
}

// loggingResponseWriter wraps http.ResponseWriter to capture the status code
type loggingResponseWriter struct {
	http.ResponseWriter
	statusCode int
}

func (lrw *loggingResponseWriter) WriteHeader(code int) {
	lrw.statusCode = code
	lrw.ResponseWriter.WriteHeader(code)
}

// Flush implements http.Flusher (needed for SSE streaming)
func (lrw *loggingResponseWriter) Flush() {
	if flusher, ok := lrw.ResponseWriter.(http.Flusher); ok {
		flusher.Flush()
	}
}

// Hijack implements http.Hijacker (needed for WebSocket upgrades)
func (lrw *loggingResponseWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	if hijacker, ok := lrw.ResponseWriter.(http.Hijacker); ok {
		return hijacker.Hijack()
	}
	return nil, nil, fmt.Errorf("response does not implement http.Hijacker")
}
