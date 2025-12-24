// Presence - Ephemeral presence data for collaboration
// Handles cursor positions, selections, and user presence
// Presence data is ephemeral and never affects the Workbook
// (only Operations can mutate Workbook state)

#ifndef CELLS_NET_PRESENCE_H
#define CELLS_NET_PRESENCE_H

#include <cstdint>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cells::net {

// Forward declarations
class PresenceManager;
class RTCDataChannel;

// Color palette for user cursors - distinct colors with good contrast
// Colors are chosen to work well with white text labels
// Matches the colors in presence.js
extern const char* const USER_COLORS[];
extern const size_t USER_COLORS_COUNT;

// Adjectives and animals for random name generation
extern const char* const NAME_ADJECTIVES[];
extern const size_t NAME_ADJECTIVES_COUNT;
extern const char* const NAME_ANIMALS[];
extern const size_t NAME_ANIMALS_COUNT;

// Cursor/cell position (zero-based indices)
struct CursorPosition {
    int32_t col{-1};  // Column index (-1 = not set)
    int32_t row{-1};  // Row index (-1 = not set)

    bool operator==(const CursorPosition& other) const {
        return col == other.col && row == other.row;
    }
    bool operator!=(const CursorPosition& other) const { return !(*this == other); }
};

// Selection range (start to end)
struct SelectionRange {
    CursorPosition start;
    CursorPosition end;

    bool operator==(const SelectionRange& other) const {
        return start == other.start && end == other.end;
    }
    bool operator!=(const SelectionRange& other) const { return !(*this == other); }
};

// Mouse pointer position (relative to grid)
struct MousePosition {
    double x{0.0};
    double y{0.0};

    bool operator==(const MousePosition& other) const { return x == other.x && y == other.y; }
    bool operator!=(const MousePosition& other) const { return !(*this == other); }
};

// Complete presence data for a user
struct PresenceData {
    std::string peer_id;          // Unique peer identifier
    std::string name;             // Display name
    std::string color;            // Cursor/selection color (hex, e.g., "#E53935")
    std::string sheet_id;         // Current sheet ID
    bool has_cursor{false};       // Whether cursor position is set
    CursorPosition cursor;        // Current cursor position
    bool has_selection{false};    // Whether selection is set
    SelectionRange selection;     // Selected range
    bool has_mouse{false};        // Whether mouse position is set
    MousePosition mouse;          // Mouse pointer position
    bool is_editing{false};       // Whether user is currently editing a cell
    CursorPosition editing_cell;  // Cell being edited (if is_editing)
    std::string editing_text;     // Current text being typed (ephemeral, not committed)
    int64_t timestamp{0};         // Last update timestamp (ms since epoch)

    // Serialize to JSON for wire format
    [[nodiscard]] std::string toJSON() const;

    // Parse from JSON
    static bool fromJSON(const std::string& json, PresenceData& out);
};

// Delegate interface for presence events
class PresenceDelegate {
public:
    virtual ~PresenceDelegate() = default;

    // Called when a peer's presence is updated (including new peers)
    virtual void presenceDidUpdate(PresenceManager& manager, const std::string& peer_id,
                                   const PresenceData& presence) = 0;

    // Called when a peer's presence is removed (peer disconnected)
    virtual void presenceDidRemove(PresenceManager& manager, const std::string& peer_id) = 0;
};

// PresenceManager - tracks local and remote user presence
// Thread-safety: All methods must be called from main thread
class PresenceManager {
public:
    // Configuration
    static constexpr int UPDATE_INTERVAL_MS = 200;  // 5 Hz for cursor/selection updates
    static constexpr int LINGER_TIME_MS = 2000;     // Keep broadcasting after activity stops
    static constexpr int FADE_TIMEOUT_MS = 2000;    // Time to keep showing remote presence
    static constexpr int MAX_UPDATES_PER_SEC = 5;   // Throttle outbound updates (5 Hz max)

    PresenceManager();
    ~PresenceManager();

    // Non-copyable, non-movable
    PresenceManager(const PresenceManager&) = delete;
    PresenceManager& operator=(const PresenceManager&) = delete;
    PresenceManager(PresenceManager&&) = delete;
    PresenceManager& operator=(PresenceManager&&) = delete;

    // Initialize with local peer info
    // name can be empty to auto-generate
    void initialize(const std::string& peer_id, const std::string& name = "");

    // Local peer info
    [[nodiscard]] const std::string& getLocalPeerId() const { return local_peer_id_; }
    [[nodiscard]] const std::string& getLocalName() const { return local_name_; }
    [[nodiscard]] const std::string& getLocalColor() const { return local_color_; }

    // Set local display name
    void setLocalName(const std::string& name);

    // Update local presence
    void setCurrentSheet(const std::string& sheet_id);
    void setCursor(int32_t col, int32_t row);
    void setSelection(const CursorPosition& start, const CursorPosition& end);
    void setMousePosition(double x, double y);
    void setEditing(int32_t col, int32_t row, const std::string& text);
    void clearCursor();
    void clearSelection();
    void clearMousePosition();
    void clearEditing();

    // Get local presence data
    [[nodiscard]] PresenceData getLocalPresence() const;

    // Remote peer presence
    [[nodiscard]] std::map<std::string, PresenceData> getRemotePeers() const;
    [[nodiscard]] const PresenceData* getPeerPresence(const std::string& peer_id) const;
    [[nodiscard]] std::vector<PresenceData> getPeersOnSheet(const std::string& sheet_id) const;
    [[nodiscard]] size_t getRemotePeerCount() const;

    // Presence staleness
    [[nodiscard]] bool isPresenceStale(const std::string& peer_id) const;
    [[nodiscard]] int64_t getPresenceAge(const std::string& peer_id) const;
    [[nodiscard]] double getPresenceOpacity(const std::string& peer_id) const;

    // Handle incoming presence message from a peer
    void handlePresenceMessage(const std::string& peer_id, const std::string& json_message);

    // Remove a peer's presence (called when peer disconnects)
    void removePeer(const std::string& peer_id);

    // Process pending updates (call periodically, handles throttling)
    // Returns true if there's a message to broadcast
    // If true, outMessage contains the JSON to send
    bool processPendingUpdates(std::string& out_message);

    // Check if broadcasting is active
    [[nodiscard]] bool isBroadcastingActive() const { return is_active_; }

    // Delegate
    void setDelegate(PresenceDelegate* delegate) { delegate_ = delegate; }
    [[nodiscard]] PresenceDelegate* getDelegate() const { return delegate_; }

    // Utility functions
    static std::string generateRandomName();
    static std::string getColorForPeer(const std::string& peer_id);

private:
    void markActivity();
    [[nodiscard]] int64_t currentTimeMs() const;

    // Local presence state
    std::string local_peer_id_;
    std::string local_name_;
    std::string local_color_;
    std::string local_sheet_id_;
    bool local_has_cursor_{false};
    CursorPosition local_cursor_;
    bool local_has_selection_{false};
    SelectionRange local_selection_;
    bool local_has_mouse_{false};
    MousePosition local_mouse_;
    bool local_is_editing_{false};
    CursorPosition local_editing_cell_;
    std::string local_editing_text_;

    // Remote peer presence (peer_id -> PresenceData)
    std::map<std::string, PresenceData> remote_peers_;

    // Broadcasting state
    int64_t last_activity_time_{0};
    int64_t last_broadcast_time_{0};
    bool is_active_{false};
    bool needs_broadcast_{false};

    // Delegate
    PresenceDelegate* delegate_{nullptr};
};

}  // namespace cells::net

#endif  // CELLS_NET_PRESENCE_H
