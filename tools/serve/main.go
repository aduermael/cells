// Static file server for Cells WASM distribution with WebSocket signaling
// Usage: go run tools/serve/main.go [options]
//
// This server is designed to serve the WASM distribution with correct
// MIME types and CORS headers for local development and testing.
// It also provides WebSocket signaling for WebRTC peer connections.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
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
const (
	pingInterval = 30 * time.Second
	pongTimeout  = 10 * time.Second
	writeTimeout = 10 * time.Second
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true // Allow all origins for development
	},
}

var roomManager *RoomManager
var agentHandler *AgentHandler

func handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket upgrade error: %v", err)
		return
	}

	// Read room and peer ID from query params or first message
	roomID := r.URL.Query().Get("room")
	peerID := r.URL.Query().Get("peer_id")

	// If not in query params, wait for join message
	if roomID == "" || peerID == "" {
		conn.SetReadDeadline(time.Now().Add(10 * time.Second))
		_, msgBytes, err := conn.ReadMessage()
		if err != nil {
			log.Printf("Failed to read join message: %v", err)
			conn.Close()
			return
		}
		conn.SetReadDeadline(time.Time{}) // Reset deadline

		var msg SignalingMessage
		if err := json.Unmarshal(msgBytes, &msg); err != nil {
			log.Printf("Failed to parse join message: %v", err)
			sendError(conn, "invalid_message", "Failed to parse message")
			conn.Close()
			return
		}

		if msg.Type != "join" {
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
	peer, ok := room.AddPeer(peerID, conn)
	if !ok {
		sendError(conn, "room_full", "Room is full")
		conn.Close()
		return
	}

	peers := room.GetPeers()
	log.Printf("[JOIN] Peer %s joined room %s (now %d peers: %v)", peerID, roomID, len(peers), peers)

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
	notifyPeerLeft(room, peerID)
	log.Printf("Peer %s left room %s", peerID, roomID)
}

func sendError(conn *websocket.Conn, code string, message string) {
	msg := SignalingMessage{
		Type:  "error",
		Error: fmt.Sprintf("%s: %s", code, message),
	}
	msgBytes, _ := json.Marshal(msg)
	conn.WriteMessage(websocket.TextMessage, msgBytes)
}

// pingRoutine sends periodic ping messages to keep the connection alive
func pingRoutine(peer *Peer, done <-chan struct{}) {
	ticker := time.NewTicker(pingInterval)
	defer ticker.Stop()

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
				log.Printf("Ping failed for peer %s: %v", peer.ID, err)
				peer.Conn.Close()
				return
			}
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
	room.Broadcast(peerID, msgBytes)
}

func notifyPeerLeft(room *Room, peerID string) {
	msg := SignalingMessage{
		Type:   "peer-left",
		PeerID: peerID,
	}
	msgBytes, _ := json.Marshal(msg)
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
	conn.WriteMessage(websocket.TextMessage, msgBytes)
}

func handlePeerMessages(room *Room, peer *Peer) {
	for {
		_, msgBytes, err := peer.Conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("[MSG] WebSocket error for peer %s: %v", peer.ID, err)
			}
			return
		}

		peer.UpdateActivity()

		var msg SignalingMessage
		if err := json.Unmarshal(msgBytes, &msg); err != nil {
			log.Printf("[MSG] Failed to parse message from peer %s: %v", peer.ID, err)
			continue
		}

		log.Printf("[MSG] Received %s from peer %s (target: %s)", msg.Type, peer.ID, msg.Target)

		switch msg.Type {
		case "offer", "answer", "ice-candidate":
			// Relay signaling messages to target peer
			relayMessage(room, peer.ID, msg)
		case "leave":
			// Peer is leaving
			return
		default:
			log.Printf("[MSG] Unknown message type from peer %s: %s", peer.ID, msg.Type)
		}
	}
}

func relayMessage(room *Room, fromPeerID string, msg SignalingMessage) {
	if msg.Target == "" {
		log.Printf("[RELAY] Missing target in %s message from peer %s", msg.Type, fromPeerID)
		return
	}

	log.Printf("[RELAY] Relaying %s from %s to %s", msg.Type, fromPeerID, msg.Target)

	// Debug: Check if target peer exists in room
	targetPeer := room.GetPeer(msg.Target)
	if targetPeer == nil {
		log.Printf("[RELAY] WARNING: Target peer %s not found in room!", msg.Target)
		// List all peers in room
		peers := room.GetPeers()
		log.Printf("[RELAY] Peers in room: %v", peers)
	} else {
		log.Printf("[RELAY] Target peer %s found", msg.Target)
	}

	// Add sender info
	relayedMsg := SignalingMessage{
		Type:      msg.Type,
		From:      fromPeerID,
		SDP:       msg.SDP,
		Candidate: msg.Candidate,
	}
	msgBytes, _ := json.Marshal(relayedMsg)

	if err := room.SendTo(msg.Target, msgBytes); err != nil {
		log.Printf("Failed to relay %s to peer %s: %v", msg.Type, msg.Target, err)
	}
}

func main() {
	port := flag.Int("port", 8081, "Port to listen on")
	dir := flag.String("dir", "dist", "Directory to serve")
	enableCollab := flag.Bool("enable-collab", false, "Enable collaboration WebSocket endpoint")
	enableAgent := flag.Bool("enable-agent", false, "Enable AI agent endpoint (requires ANTHROPIC_API_KEY)")
	maxRoomSize := flag.Int("max-room-size", 10, "Maximum peers per room")
	roomTimeout := flag.Duration("room-timeout", time.Hour, "Timeout for empty rooms")
	flag.Parse()

	// Initialize room manager if collaboration is enabled
	if *enableCollab {
		roomManager = NewRoomManager(*maxRoomSize, *roomTimeout)
		// Start cleanup routine
		stop := make(chan struct{})
		roomManager.StartCleanupRoutine(time.Minute, stop)
		log.Println("Collaboration enabled with WebSocket signaling at /ws")
	}

	// Initialize agent handler if enabled
	if *enableAgent {
		conversationStore := NewConversationStore(50, time.Hour) // 50 messages max, 1 hour timeout
		stop := make(chan struct{})
		conversationStore.StartCleanupRoutine(5*time.Minute, stop)

		var err error
		agentHandler, err = NewAgentHandler(conversationStore)
		if err != nil {
			log.Printf("Warning: Agent not enabled: %v", err)
		} else {
			log.Println("AI agent enabled at /api/agent/*")
		}
	}

	// Resolve directory path
	absDir, err := filepath.Abs(*dir)
	if err != nil {
		log.Fatalf("Failed to resolve directory: %v", err)
	}

	// Check if directory exists
	if _, err := os.Stat(absDir); os.IsNotExist(err) {
		log.Fatalf("Directory does not exist: %s\nRun 'make wasm-dist' first.", absDir)
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
	if agentHandler != nil {
		mux.HandleFunc("/api/agent/message", agentHandler.HandleMessage)
		mux.HandleFunc("/api/agent/tool-result", agentHandler.HandleToolResult)
		mux.HandleFunc("/api/agent/clear", agentHandler.HandleClearConversation)
	}
	mux.Handle("/", fileHandler)

	addr := fmt.Sprintf(":%d", *port)
	fmt.Printf("Serving %s on http://localhost%s/\n", absDir, addr)
	if *enableCollab {
		fmt.Printf("WebSocket signaling at ws://localhost%s/ws\n", addr)
	}
	if agentHandler != nil {
		fmt.Printf("AI agent API at http://localhost%s/api/agent/*\n", addr)
	}
	fmt.Println("Press Ctrl+C to stop")

	if err := http.ListenAndServe(addr, mux); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}
