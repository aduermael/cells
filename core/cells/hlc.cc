#include "core/cells/hlc.h"

#include <chrono>
#include <cstdlib>
#include <sstream>

namespace cells {

HLC::HLC() : wall_time(0), logical(0), node_id() {}

HLC::HLC(int64_t wall, uint32_t logic, const ID& node)
    : wall_time(wall), logical(logic), node_id(node) {}

bool HLC::isZero() const {
    return wall_time == 0 && logical == 0 && node_id.isNull();
}

int HLC::compare(const HLC& other) const {
    // First compare wall_time
    if (wall_time < other.wall_time) {
        return -1;
    }
    if (wall_time > other.wall_time) {
        return 1;
    }

    // Wall times equal, compare logical counter
    if (logical < other.logical) {
        return -1;
    }
    if (logical > other.logical) {
        return 1;
    }

    // Both equal, compare node_id as tiebreaker
    if (node_id < other.node_id) {
        return -1;
    }
    if (other.node_id < node_id) {
        return 1;
    }

    return 0;  // Completely equal
}

bool HLC::operator==(const HLC& other) const {
    return compare(other) == 0;
}

bool HLC::operator!=(const HLC& other) const {
    return compare(other) != 0;
}

bool HLC::operator<(const HLC& other) const {
    return compare(other) < 0;
}

bool HLC::operator>(const HLC& other) const {
    return compare(other) > 0;
}

bool HLC::operator<=(const HLC& other) const {
    return compare(other) <= 0;
}

bool HLC::operator>=(const HLC& other) const {
    return compare(other) >= 0;
}

std::string HLC::toString() const {
    std::ostringstream oss;
    oss << wall_time << "." << logical << "." << node_id.toString();
    return oss.str();
}

HLC HLC::fromString(const std::string& str) {
    // Format: wall_time.logical.node_id
    // Example: 1705312200000.0.N3f8hJ2w

    size_t first_dot = str.find('.');
    if (first_dot == std::string::npos) {
        return HLC();  // Invalid format
    }

    size_t second_dot = str.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return HLC();  // Invalid format
    }

    // Parse wall_time
    std::string wall_str = str.substr(0, first_dot);
    char* end = nullptr;
    int64_t wall = std::strtoll(wall_str.c_str(), &end, 10);
    if (end == wall_str.c_str()) {
        return HLC();  // Failed to parse
    }

    // Parse logical
    std::string logical_str = str.substr(first_dot + 1, second_dot - first_dot - 1);
    unsigned long logical_val = std::strtoul(logical_str.c_str(), &end, 10);
    if (end == logical_str.c_str()) {
        return HLC();  // Failed to parse
    }

    // Parse node_id
    std::string node_str = str.substr(second_dot + 1);
    ID node(node_str);

    return HLC(wall, static_cast<uint32_t>(logical_val), node);
}

int64_t current_time_ms() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

HLC generate_hlc(const HLC& last_hlc, const ID& node_id) {
    int64_t now = current_time_ms();

    // If wall clock has advanced, use new time with logical 0
    if (now > last_hlc.wall_time) {
        return HLC(now, 0, node_id);
    }

    // Wall clock hasn't advanced (or went backwards), increment logical
    return HLC(last_hlc.wall_time, last_hlc.logical + 1, node_id);
}

HLC generate_initial_hlc(const ID& node_id) {
    return HLC(current_time_ms(), 0, node_id);
}

HLC update_hlc(const HLC& last_local, const HLC& received, const ID& node_id) {
    int64_t now = current_time_ms();

    // Choose the maximum wall_time among now, last_local, and received
    int64_t max_wall = now;
    if (last_local.wall_time > max_wall) {
        max_wall = last_local.wall_time;
    }
    if (received.wall_time > max_wall) {
        max_wall = received.wall_time;
    }

    uint32_t new_logical = 0;

    if (max_wall == now && max_wall > last_local.wall_time && max_wall > received.wall_time) {
        // Physical time advanced past both, reset logical to 0
        new_logical = 0;
    } else if (max_wall == last_local.wall_time && max_wall == received.wall_time) {
        // All three are at the same wall_time
        new_logical = (last_local.logical > received.logical ? last_local.logical : received.logical)
                      + 1;
    } else if (max_wall == last_local.wall_time) {
        // last_local has the highest wall_time
        new_logical = last_local.logical + 1;
    } else if (max_wall == received.wall_time) {
        // received has the highest wall_time
        new_logical = received.logical + 1;
    } else {
        // now is highest but equal to one of them
        if (now == last_local.wall_time) {
            new_logical = last_local.logical + 1;
        } else if (now == received.wall_time) {
            new_logical = received.logical + 1;
        }
    }

    return HLC(max_wall, new_logical, node_id);
}

}  // namespace cells
