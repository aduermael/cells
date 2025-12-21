package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

// Helper to create a test WebSocket connection pair
func createTestConnection(t *testing.T) (*websocket.Conn, *websocket.Conn, func()) {
	t.Helper()

	upgrader := websocket.Upgrader{}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			t.Fatalf("Failed to upgrade: %v", err)
		}
		// Keep connection alive for test
		for {
			_, _, err := conn.ReadMessage()
			if err != nil {
				return
			}
		}
	}))

	wsURL := "ws" + strings.TrimPrefix(server.URL, "http")
	clientConn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		t.Fatalf("Failed to connect: %v", err)
	}

	cleanup := func() {
		clientConn.Close()
		server.Close()
	}

	return clientConn, nil, cleanup
}

func TestNewRoom(t *testing.T) {
	room := NewRoom("test-room", 10)

	if room.ID != "test-room" {
		t.Errorf("Expected room ID 'test-room', got '%s'", room.ID)
	}
	if room.MaxPeers != 10 {
		t.Errorf("Expected MaxPeers 10, got %d", room.MaxPeers)
	}
	if room.PeerCount() != 0 {
		t.Errorf("Expected empty room, got %d peers", room.PeerCount())
	}
	if !room.IsEmpty() {
		t.Error("Expected room to be empty")
	}
}

func TestRoomAddRemovePeer(t *testing.T) {
	conn, _, cleanup := createTestConnection(t)
	defer cleanup()

	room := NewRoom("test-room", 10)

	// Add peer
	peer, ok := room.AddPeer("peer1", conn)
	if !ok {
		t.Fatal("Failed to add peer")
	}
	if peer == nil {
		t.Fatal("Expected non-nil peer")
	}
	if peer.ID != "peer1" {
		t.Errorf("Expected peer ID 'peer1', got '%s'", peer.ID)
	}
	if room.PeerCount() != 1 {
		t.Errorf("Expected 1 peer, got %d", room.PeerCount())
	}

	// Get peer
	gotPeer := room.GetPeer("peer1")
	if gotPeer == nil {
		t.Fatal("Expected to find peer")
	}
	if gotPeer.ID != "peer1" {
		t.Errorf("Expected peer ID 'peer1', got '%s'", gotPeer.ID)
	}

	// Remove peer
	room.RemovePeer("peer1")
	if room.PeerCount() != 0 {
		t.Errorf("Expected 0 peers, got %d", room.PeerCount())
	}
	if !room.IsEmpty() {
		t.Error("Expected room to be empty")
	}
}

func TestRoomMaxPeers(t *testing.T) {
	room := NewRoom("test-room", 2)

	conn1, _, cleanup1 := createTestConnection(t)
	defer cleanup1()
	conn2, _, cleanup2 := createTestConnection(t)
	defer cleanup2()
	conn3, _, cleanup3 := createTestConnection(t)
	defer cleanup3()

	// Add first peer
	_, ok := room.AddPeer("peer1", conn1)
	if !ok {
		t.Fatal("Failed to add first peer")
	}

	// Add second peer
	_, ok = room.AddPeer("peer2", conn2)
	if !ok {
		t.Fatal("Failed to add second peer")
	}

	// Try to add third peer - should fail
	_, ok = room.AddPeer("peer3", conn3)
	if ok {
		t.Fatal("Should not be able to add third peer when room is full")
	}

	if room.PeerCount() != 2 {
		t.Errorf("Expected 2 peers, got %d", room.PeerCount())
	}
}

func TestRoomGetPeers(t *testing.T) {
	room := NewRoom("test-room", 10)

	conn1, _, cleanup1 := createTestConnection(t)
	defer cleanup1()
	conn2, _, cleanup2 := createTestConnection(t)
	defer cleanup2()

	room.AddPeer("peer1", conn1)
	room.AddPeer("peer2", conn2)

	peers := room.GetPeers()
	if len(peers) != 2 {
		t.Errorf("Expected 2 peer IDs, got %d", len(peers))
	}

	// Check that both IDs are present
	found := make(map[string]bool)
	for _, id := range peers {
		found[id] = true
	}
	if !found["peer1"] || !found["peer2"] {
		t.Error("Expected to find both peer1 and peer2")
	}
}

func TestRoomGetOtherPeers(t *testing.T) {
	room := NewRoom("test-room", 10)

	conn1, _, cleanup1 := createTestConnection(t)
	defer cleanup1()
	conn2, _, cleanup2 := createTestConnection(t)
	defer cleanup2()
	conn3, _, cleanup3 := createTestConnection(t)
	defer cleanup3()

	room.AddPeer("peer1", conn1)
	room.AddPeer("peer2", conn2)
	room.AddPeer("peer3", conn3)

	others := room.GetOtherPeers("peer1")
	if len(others) != 2 {
		t.Errorf("Expected 2 other peers, got %d", len(others))
	}

	// Check that peer1 is not included
	for _, peer := range others {
		if peer.ID == "peer1" {
			t.Error("peer1 should not be in other peers list")
		}
	}
}

func TestRoomReJoin(t *testing.T) {
	room := NewRoom("test-room", 10)

	conn1, _, cleanup1 := createTestConnection(t)
	defer cleanup1()
	conn2, _, cleanup2 := createTestConnection(t)
	defer cleanup2()

	// Add peer with first connection
	_, ok := room.AddPeer("peer1", conn1)
	if !ok {
		t.Fatal("Failed to add peer")
	}
	if room.PeerCount() != 1 {
		t.Errorf("Expected 1 peer, got %d", room.PeerCount())
	}

	// Rejoin with new connection (same peer ID)
	_, ok = room.AddPeer("peer1", conn2)
	if !ok {
		t.Fatal("Failed to rejoin")
	}
	// Should still have 1 peer
	if room.PeerCount() != 1 {
		t.Errorf("Expected 1 peer after rejoin, got %d", room.PeerCount())
	}
}

func TestRoomManagerGetOrCreate(t *testing.T) {
	rm := NewRoomManager(10, time.Hour)

	// Create first room
	room1 := rm.GetOrCreateRoom("room1")
	if room1 == nil {
		t.Fatal("Expected non-nil room")
	}
	if room1.ID != "room1" {
		t.Errorf("Expected room ID 'room1', got '%s'", room1.ID)
	}
	if rm.RoomCount() != 1 {
		t.Errorf("Expected 1 room, got %d", rm.RoomCount())
	}

	// Get same room again
	room1Again := rm.GetOrCreateRoom("room1")
	if room1Again != room1 {
		t.Error("Expected same room instance")
	}
	if rm.RoomCount() != 1 {
		t.Errorf("Expected still 1 room, got %d", rm.RoomCount())
	}

	// Create second room
	room2 := rm.GetOrCreateRoom("room2")
	if room2 == nil {
		t.Fatal("Expected non-nil room")
	}
	if rm.RoomCount() != 2 {
		t.Errorf("Expected 2 rooms, got %d", rm.RoomCount())
	}
}

func TestRoomManagerGetRoom(t *testing.T) {
	rm := NewRoomManager(10, time.Hour)

	// Non-existent room
	room := rm.GetRoom("nonexistent")
	if room != nil {
		t.Error("Expected nil for non-existent room")
	}

	// Create and get
	rm.GetOrCreateRoom("room1")
	room = rm.GetRoom("room1")
	if room == nil {
		t.Fatal("Expected to find room1")
	}
	if room.ID != "room1" {
		t.Errorf("Expected room ID 'room1', got '%s'", room.ID)
	}
}

func TestRoomManagerRemoveRoom(t *testing.T) {
	rm := NewRoomManager(10, time.Hour)

	rm.GetOrCreateRoom("room1")
	rm.GetOrCreateRoom("room2")

	if rm.RoomCount() != 2 {
		t.Errorf("Expected 2 rooms, got %d", rm.RoomCount())
	}

	rm.RemoveRoom("room1")

	if rm.RoomCount() != 1 {
		t.Errorf("Expected 1 room, got %d", rm.RoomCount())
	}

	if rm.GetRoom("room1") != nil {
		t.Error("Expected room1 to be removed")
	}
	if rm.GetRoom("room2") == nil {
		t.Error("Expected room2 to still exist")
	}
}

func TestRoomManagerCleanupEmptyRooms(t *testing.T) {
	// Use very short timeout for test
	rm := NewRoomManager(10, 10*time.Millisecond)

	// Create rooms
	room1 := rm.GetOrCreateRoom("room1")
	rm.GetOrCreateRoom("room2")

	// Add a peer to room1 to make it non-empty
	conn, _, cleanup := createTestConnection(t)
	defer cleanup()
	room1.AddPeer("peer1", conn)

	// Wait for timeout
	time.Sleep(20 * time.Millisecond)

	// Cleanup should only remove room2 (empty and timed out)
	removed := rm.CleanupEmptyRooms()
	if removed != 1 {
		t.Errorf("Expected 1 room removed, got %d", removed)
	}

	if rm.GetRoom("room1") == nil {
		t.Error("Expected room1 to still exist (has peer)")
	}
	if rm.GetRoom("room2") != nil {
		t.Error("Expected room2 to be removed")
	}
}

func TestRoomManagerGetRoomIDs(t *testing.T) {
	rm := NewRoomManager(10, time.Hour)

	rm.GetOrCreateRoom("room1")
	rm.GetOrCreateRoom("room2")
	rm.GetOrCreateRoom("room3")

	ids := rm.GetRoomIDs()
	if len(ids) != 3 {
		t.Errorf("Expected 3 room IDs, got %d", len(ids))
	}

	found := make(map[string]bool)
	for _, id := range ids {
		found[id] = true
	}
	if !found["room1"] || !found["room2"] || !found["room3"] {
		t.Error("Expected to find all room IDs")
	}
}

func TestRoomConcurrency(t *testing.T) {
	room := NewRoom("test-room", 100)

	var wg sync.WaitGroup
	errors := make(chan error, 100)

	// Concurrent adds
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			conn, _, cleanup := createTestConnection(t)
			defer cleanup()
			peerID := string(rune('A' + id%26))
			room.AddPeer(peerID, conn)
		}(i)
	}

	// Concurrent reads
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_ = room.GetPeers()
			_ = room.PeerCount()
			_ = room.IsEmpty()
		}()
	}

	wg.Wait()
	close(errors)

	for err := range errors {
		t.Error(err)
	}
}

func TestRoomManagerConcurrency(t *testing.T) {
	rm := NewRoomManager(10, time.Hour)

	var wg sync.WaitGroup

	// Concurrent creates
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			roomID := string(rune('A' + id%26))
			rm.GetOrCreateRoom(roomID)
		}(i)
	}

	// Concurrent reads
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_ = rm.RoomCount()
			_ = rm.GetRoomIDs()
		}()
	}

	wg.Wait()
}
