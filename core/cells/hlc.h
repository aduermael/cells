#ifndef CELLS_HLC_H_
#define CELLS_HLC_H_

#include <cstdint>

#include <string>

#include "core/cells/types.h"

namespace cells {

// Hybrid Logical Clock (HLC) timestamp for CRDT operation ordering.
// Combines wall clock time with logical counter for total ordering.
// Format: wall_time.logical.node_id
// Example: 1705312200000.0.N3f8hJ2w
struct HLC {
    int64_t wall_time;  // Unix timestamp in milliseconds
    uint32_t logical;   // Logical counter for causality
    ID node_id;         // Node that generated this timestamp

    // Default constructor creates a zero HLC
    HLC();

    // Construct with specific values
    HLC(int64_t wall, uint32_t logic, const ID& node);

    // Check if this is a zero/null HLC
    [[nodiscard]] bool isZero() const;

    // Three-way comparison for total ordering
    // Returns: -1 if this < other, 0 if equal, 1 if this > other
    // Order: wall_time -> logical -> node_id
    [[nodiscard]] int compare(const HLC& other) const;

    // Comparison operators
    bool operator==(const HLC& other) const;
    bool operator!=(const HLC& other) const;
    bool operator<(const HLC& other) const;
    bool operator>(const HLC& other) const;
    bool operator<=(const HLC& other) const;
    bool operator>=(const HLC& other) const;

    // Serialize to string format: "wall_time.logical.node_id"
    [[nodiscard]] std::string toString() const;

    // Deserialize from string format
    // Returns zero HLC if parsing fails
    static HLC fromString(const std::string& str);
};

// Get current system time in milliseconds since Unix epoch
int64_t current_time_ms();

// Generate a new HLC timestamp.
// Updates based on the local node's last HLC and current system time.
// Ensures the new HLC is always greater than last_hlc.
// Parameters:
//   last_hlc: The most recent HLC timestamp seen (local or remote)
//   node_id: The local node's ID
HLC generate_hlc(const HLC& last_hlc, const ID& node_id);

// Initialize HLC with current system time and logical counter 0.
HLC generate_initial_hlc(const ID& node_id);

// Update local HLC when receiving a remote HLC.
// Returns a new HLC that is greater than both last_local and received.
// Used to maintain HLC consistency when receiving operations from peers.
HLC update_hlc(const HLC& last_local, const HLC& received, const ID& node_id);

}  // namespace cells

#endif  // CELLS_HLC_H_
