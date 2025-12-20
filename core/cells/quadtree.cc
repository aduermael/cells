#include "core/cells/quadtree.h"

#include <algorithm>

namespace cells {

// QuadtreeNode implementation

QuadtreeNode::QuadtreeNode(const Rect& bounds, size_t depth) : _bounds(bounds), _depth(depth) {}

bool QuadtreeNode::insert(Cell* cell, uint32_t x, uint32_t y) {
    // Check bounds
    if (!_bounds.contains(x, y)) {
        return false;
    }

    // If we're a leaf and under capacity, store here
    if (isLeaf()) {
        if (_entries.size() < MAX_ENTRIES || _depth >= MAX_DEPTH) {
            _entries.emplace_back(cell, x, y);
            return true;
        }

        // Subdivide and redistribute
        subdivide();

        // Move existing entries to children
        for (const auto& entry : _entries) {
            QuadtreeNode* child = childContaining(entry.x, entry.y);
            if (child != nullptr) {
                child->insert(entry.cell, entry.x, entry.y);
            }
        }
        _entries.clear();
    }

    // Insert into appropriate child
    QuadtreeNode* child = childContaining(x, y);
    if (child != nullptr) {
        return child->insert(cell, x, y);
    }

    return false;
}

bool QuadtreeNode::remove(Cell* cell, uint32_t x, uint32_t y) {
    if (!_bounds.contains(x, y)) {
        return false;
    }

    if (isLeaf()) {
        for (auto it = _entries.begin(); it != _entries.end(); ++it) {
            if (it->cell == cell && it->x == x && it->y == y) {
                _entries.erase(it);
                return true;
            }
        }
        return false;
    }

    // Try to remove from appropriate child
    QuadtreeNode* child = childContaining(x, y);
    return child != nullptr && child->remove(cell, x, y);
}

void QuadtreeNode::query(const Rect& rect, std::vector<QuadtreeEntry>& results) const {
    if (!_bounds.intersects(rect)) {
        return;
    }

    if (isLeaf()) {
        for (const auto& entry : _entries) {
            if (rect.contains(entry.x, entry.y)) {
                results.push_back(entry);
            }
        }
        return;
    }

    // Query all children
    if (_nw != nullptr) {
        _nw->query(rect, results);
    }
    if (_ne != nullptr) {
        _ne->query(rect, results);
    }
    if (_sw != nullptr) {
        _sw->query(rect, results);
    }
    if (_se != nullptr) {
        _se->query(rect, results);
    }
}

void QuadtreeNode::all(std::vector<QuadtreeEntry>& results) const {
    if (isLeaf()) {
        results.insert(results.end(), _entries.begin(), _entries.end());
        return;
    }

    if (_nw != nullptr) {
        _nw->all(results);
    }
    if (_ne != nullptr) {
        _ne->all(results);
    }
    if (_sw != nullptr) {
        _sw->all(results);
    }
    if (_se != nullptr) {
        _se->all(results);
    }
}

size_t QuadtreeNode::count() const {
    if (isLeaf()) {
        return _entries.size();
    }

    size_t total = 0;
    if (_nw != nullptr) {
        total += _nw->count();
    }
    if (_ne != nullptr) {
        total += _ne->count();
    }
    if (_sw != nullptr) {
        total += _sw->count();
    }
    if (_se != nullptr) {
        total += _se->count();
    }
    return total;
}

void QuadtreeNode::subdivide() {
    const uint32_t midX = _bounds.x1 + _bounds.width() / 2;
    const uint32_t midY = _bounds.y1 + _bounds.height() / 2;

    // NW: top-left
    _nw = std::make_unique<QuadtreeNode>(Rect(_bounds.x1, _bounds.y1, midX, midY), _depth + 1);
    // NE: top-right
    _ne = std::make_unique<QuadtreeNode>(Rect(midX, _bounds.y1, _bounds.x2, midY), _depth + 1);
    // SW: bottom-left
    _sw = std::make_unique<QuadtreeNode>(Rect(_bounds.x1, midY, midX, _bounds.y2), _depth + 1);
    // SE: bottom-right
    _se = std::make_unique<QuadtreeNode>(Rect(midX, midY, _bounds.x2, _bounds.y2), _depth + 1);
}

QuadtreeNode* QuadtreeNode::childContaining(uint32_t x, uint32_t y) {
    const uint32_t midX = _bounds.x1 + _bounds.width() / 2;
    const uint32_t midY = _bounds.y1 + _bounds.height() / 2;

    if (x < midX) {
        return (y < midY) ? _nw.get() : _sw.get();
    }
    return (y < midY) ? _ne.get() : _se.get();
}

const QuadtreeNode* QuadtreeNode::childContaining(uint32_t x, uint32_t y) const {
    const uint32_t midX = _bounds.x1 + _bounds.width() / 2;
    const uint32_t midY = _bounds.y1 + _bounds.height() / 2;

    if (x < midX) {
        return (y < midY) ? _nw.get() : _sw.get();
    }
    return (y < midY) ? _ne.get() : _se.get();
}

// Quadtree implementation

Quadtree::Quadtree() : _root(Rect(0, 0, 16384, 1048576)) {}

Quadtree::Quadtree(const Rect& bounds) : _root(bounds) {}

void Quadtree::build(const Sheet& sheet) {
    clear();

    for (const auto& [cellId, cellPtr] : sheet.cells) {
        Cell* cell = cellPtr.get();
        if (cell == nullptr) {
            continue;
        }

        // Look up axis positions
        auto colIt = sheet.columns.find(cell->colId);
        auto rowIt = sheet.rows.find(cell->rowId);

        if (colIt != sheet.columns.end() && rowIt != sheet.rows.end()) {
            insert(cell, colIt->second->position, rowIt->second->position);
        }
    }
}

bool Quadtree::insert(Cell* cell, const Sheet& sheet) {
    auto colIt = sheet.columns.find(cell->colId);
    auto rowIt = sheet.rows.find(cell->rowId);

    if (colIt == sheet.columns.end() || rowIt == sheet.rows.end()) {
        return false;
    }

    return insert(cell, colIt->second->position, rowIt->second->position);
}

bool Quadtree::insert(Cell* cell, uint32_t col, uint32_t row) {
    return _root.insert(cell, col, row);
}

bool Quadtree::remove(Cell* cell, const Sheet& sheet) {
    auto colIt = sheet.columns.find(cell->colId);
    auto rowIt = sheet.rows.find(cell->rowId);

    if (colIt == sheet.columns.end() || rowIt == sheet.rows.end()) {
        return false;
    }

    return remove(cell, colIt->second->position, rowIt->second->position);
}

bool Quadtree::remove(Cell* cell, uint32_t col, uint32_t row) {
    return _root.remove(cell, col, row);
}

std::vector<QuadtreeEntry> Quadtree::query(const Rect& viewport) const {
    std::vector<QuadtreeEntry> results;
    _root.query(viewport, results);
    return results;
}

std::vector<QuadtreeEntry> Quadtree::query(uint32_t x1, uint32_t y1, uint32_t x2,
                                           uint32_t y2) const {
    return query(Rect(x1, y1, x2, y2));
}

std::vector<QuadtreeEntry> Quadtree::all() const {
    std::vector<QuadtreeEntry> results;
    _root.all(results);
    return results;
}

size_t Quadtree::count() const {
    return _root.count();
}

void Quadtree::clear() {
    _root = QuadtreeNode(Rect(0, 0, 16384, 1048576));
}

}  // namespace cells
