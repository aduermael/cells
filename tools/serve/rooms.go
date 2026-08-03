// Signaling room and peer management for WebRTC collaboration.
// Tracks peers per room, broadcasts presence, and cleans up idle rooms.

package main

import (
	"fmt"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// Peer represents a connected peer in a room.
type Peer struct {
	ID         string
	Conn       *websocket.Conn
	JoinedAt   time.Time
	LastActive time.Time
	mu         sync.Mutex
}

// Send sends a message to the peer, thread-safe.
func (p *Peer) Send(message []byte) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.Conn.WriteMessage(websocket.TextMessage, message)
}

// Close closes the peer connection.
func (p *Peer) Close() error {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.Conn.Close()
}

// UpdateActivity updates the last active time.
func (p *Peer) UpdateActivity() {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.LastActive = time.Now()
}

// Room represents a collaboration room with connected peers.
type Room struct {
	ID          string
	peers       map[string]*Peer // peer ID -> Peer
	mu          sync.RWMutex
	CreatedAt   time.Time
	LastActive  time.Time
	MaxPeers    int
}

// NewRoom creates a new room with the given ID.
func NewRoom(id string, maxPeers int) *Room {
	now := time.Now()
	return &Room{
		ID:         id,
		peers:      make(map[string]*Peer),
		CreatedAt:  now,
		LastActive: now,
		MaxPeers:   maxPeers,
	}
}

// AddPeerResult is returned from AddPeerDetailed for logging (rejoin vs new).
type AddPeerResult struct {
	Peer   *Peer
	OK     bool
	Rejoin bool // true if same peer_id replaced an existing connection
}

// AddPeer adds a peer to the room. Returns false if room is full.
func (r *Room) AddPeer(peerID string, conn *websocket.Conn) (*Peer, bool) {
	res := r.AddPeerDetailed(peerID, conn)
	return res.Peer, res.OK
}

// AddPeerDetailed is like AddPeer but reports rejoin for logging.
func (r *Room) AddPeerDetailed(peerID string, conn *websocket.Conn) AddPeerResult {
	r.mu.Lock()
	defer r.mu.Unlock()

	// Rejoin same peer id: replace connection (allowed even when room is "full").
	if existing, ok := r.peers[peerID]; ok {
		existing.Close()
		now := time.Now()
		peer := &Peer{
			ID:         peerID,
			Conn:       conn,
			JoinedAt:   now,
			LastActive: now,
		}
		r.peers[peerID] = peer
		r.LastActive = now
		return AddPeerResult{Peer: peer, OK: true, Rejoin: true}
	}

	if len(r.peers) >= r.MaxPeers {
		return AddPeerResult{OK: false}
	}

	now := time.Now()
	peer := &Peer{
		ID:         peerID,
		Conn:       conn,
		JoinedAt:   now,
		LastActive: now,
	}
	r.peers[peerID] = peer
	r.LastActive = now
	return AddPeerResult{Peer: peer, OK: true, Rejoin: false}
}

// RemovePeer removes a peer from the room.
func (r *Room) RemovePeer(peerID string) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if peer, ok := r.peers[peerID]; ok {
		peer.Close()
		delete(r.peers, peerID)
		r.LastActive = time.Now()
	}
}

// GetPeer returns a peer by ID.
func (r *Room) GetPeer(peerID string) *Peer {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.peers[peerID]
}

// GetPeers returns a copy of all peer IDs in the room.
func (r *Room) GetPeers() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()

	peerIDs := make([]string, 0, len(r.peers))
	for id := range r.peers {
		peerIDs = append(peerIDs, id)
	}
	return peerIDs
}

// GetOtherPeers returns all peers except the one specified.
func (r *Room) GetOtherPeers(excludePeerID string) []*Peer {
	r.mu.RLock()
	defer r.mu.RUnlock()

	peers := make([]*Peer, 0, len(r.peers)-1)
	for id, peer := range r.peers {
		if id != excludePeerID {
			peers = append(peers, peer)
		}
	}
	return peers
}

// Broadcast sends a message to all peers except the sender.
func (r *Room) Broadcast(senderID string, message []byte) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	for id, peer := range r.peers {
		if id != senderID {
			// Don't block on failed sends
			go peer.Send(message)
		}
	}
	r.LastActive = time.Now()
}

// SendTo sends a message to a specific peer.
func (r *Room) SendTo(targetPeerID string, message []byte) error {
	r.mu.RLock()
	peer := r.peers[targetPeerID]
	r.mu.RUnlock()

	if peer == nil {
		return fmt.Errorf("peer %s not in room %s", targetPeerID, r.ID)
	}
	return peer.Send(message)
}

// PeerCount returns the number of peers in the room.
func (r *Room) PeerCount() int {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return len(r.peers)
}

// IsEmpty returns true if the room has no peers.
func (r *Room) IsEmpty() bool {
	return r.PeerCount() == 0
}

// UpdateActivity updates the room's last active time.
func (r *Room) UpdateActivity() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.LastActive = time.Now()
}

// RoomManager manages multiple rooms.
type RoomManager struct {
	rooms      map[string]*Room // room ID -> Room
	mu         sync.RWMutex
	maxPeers   int
	roomTimeout time.Duration
}

// NewRoomManager creates a new room manager.
func NewRoomManager(maxPeers int, roomTimeout time.Duration) *RoomManager {
	return &RoomManager{
		rooms:       make(map[string]*Room),
		maxPeers:    maxPeers,
		roomTimeout: roomTimeout,
	}
}

// GetOrCreateRoom gets an existing room or creates a new one.
func (rm *RoomManager) GetOrCreateRoom(roomID string) *Room {
	rm.mu.Lock()
	defer rm.mu.Unlock()

	if room, ok := rm.rooms[roomID]; ok {
		return room
	}

	room := NewRoom(roomID, rm.maxPeers)
	rm.rooms[roomID] = room
	return room
}

// GetRoom returns a room by ID, or nil if not found.
func (rm *RoomManager) GetRoom(roomID string) *Room {
	rm.mu.RLock()
	defer rm.mu.RUnlock()
	return rm.rooms[roomID]
}

// RemoveRoom removes a room from the manager.
func (rm *RoomManager) RemoveRoom(roomID string) {
	rm.mu.Lock()
	defer rm.mu.Unlock()
	delete(rm.rooms, roomID)
}

// CleanupEmptyRooms removes rooms that have been empty for longer than timeout.
func (rm *RoomManager) CleanupEmptyRooms() int {
	rm.mu.Lock()
	defer rm.mu.Unlock()

	removed := 0
	now := time.Now()

	for id, room := range rm.rooms {
		if room.IsEmpty() && now.Sub(room.LastActive) > rm.roomTimeout {
			delete(rm.rooms, id)
			removed++
		}
	}

	return removed
}

// RoomCount returns the number of rooms.
func (rm *RoomManager) RoomCount() int {
	rm.mu.RLock()
	defer rm.mu.RUnlock()
	return len(rm.rooms)
}

// GetRoomIDs returns a list of all room IDs.
func (rm *RoomManager) GetRoomIDs() []string {
	rm.mu.RLock()
	defer rm.mu.RUnlock()

	ids := make([]string, 0, len(rm.rooms))
	for id := range rm.rooms {
		ids = append(ids, id)
	}
	return ids
}

// StartCleanupRoutine starts a background goroutine that periodically cleans up empty rooms.
func (rm *RoomManager) StartCleanupRoutine(interval time.Duration, stop <-chan struct{}) {
	go func() {
		ticker := time.NewTicker(interval)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				rm.CleanupEmptyRooms()
			case <-stop:
				return
			}
		}
	}()
}
