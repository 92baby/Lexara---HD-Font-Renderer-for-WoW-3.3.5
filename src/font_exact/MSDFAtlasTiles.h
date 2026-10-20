#pragma once
// Pure logic (no D3D / game dependencies) for the tiled glyph atlas:
//  - AtlasTiles: shelf-packs glyphs inside fixed tiles (4 per atlas page) and does LRU eviction per tile.
//  - EvictionLog: remembers which tiles were evicted so strings can be invalidated selectively.
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <vector>
#include <algorithm>

class AtlasTiles {
public:
    static constexpr uint32_t TILES_PER_AXIS = 2;
    static constexpr uint32_t TILES_PER_PAGE = TILES_PER_AXIS * TILES_PER_AXIS;
    static constexpr uint32_t MAX_TILES = 32; // masks are uint32_t

    struct Tile {
        int nextX = 0, nextY = 0, rowHeight = 0;
        bool inUse = false;
        uint64_t lastUse = 0;
        std::vector<uint32_t> codepoints;
    };

    struct Slot {
        uint32_t tile = 0;
        uint32_t page = 0;
        uint32_t x = 0, y = 0; // pixel position inside the page texture
    };

    enum class Result { Ok, Full, PageFailed };

    AtlasTiles(uint32_t atlasSize, uint32_t gutter, uint32_t maxPages)
        : m_atlasSize(atlasSize), m_gutter(gutter),
          m_tileSize(atlasSize / TILES_PER_AXIS),
          m_tileCount(std::min<uint32_t>(maxPages * TILES_PER_PAGE, MAX_TILES)),
          m_tiles(m_tileCount) {}

    uint32_t TileCount() const { return m_tileCount; }
    uint32_t TileSize() const { return m_tileSize; }
    const Tile& GetTile(uint32_t i) const { return m_tiles[i]; }

    // Find room for a w x h glyph. Never evicts. Opening a fresh tile calls ensurePage(pageIndex).
    // Idempotent until Commit(): repeated calls return the same slot.
    Result Find(uint32_t w, uint32_t h, Slot& out, const std::function<bool(uint32_t)>& ensurePage) {
        // A glyph that can never fit a tile is not a reason to evict anything.
        if (w + 2 * m_gutter > m_tileSize || h + 2 * m_gutter > m_tileSize) return Result::PageFailed;

        if (m_current >= 0 && TryPlace(m_tiles[m_current], w, h, out)) {
            Fill(out, static_cast<uint32_t>(m_current));
            return Result::Ok;
        }
        for (uint32_t i = 0; i < m_tileCount; ++i) {
            if (m_tiles[i].inUse) continue;
            uint32_t page = i / TILES_PER_PAGE;
            if (!ensurePage(page)) return Result::PageFailed;
            Tile& t = m_tiles[i];
            t.inUse = true;
            t.nextX = static_cast<int>(m_gutter);
            t.nextY = static_cast<int>(m_gutter);
            t.rowHeight = 0;
            t.lastUse = m_clock;
            t.codepoints.clear();
            m_current = static_cast<int>(i);
            if (TryPlace(t, w, h, out)) {
                Fill(out, i);
                return Result::Ok;
            }
        }
        return Result::Full;
    }

    // Reserve the slot returned by Find() for a glyph.
    void Commit(const Slot& s, uint32_t w, uint32_t h, uint32_t codepoint) {
        Tile& t = m_tiles[s.tile];
        t.nextX = static_cast<int>(s.x - TileOriginX(s.tile) + w + m_gutter);
        t.rowHeight = std::max(t.rowHeight, static_cast<int>(h));
        t.codepoints.push_back(codepoint);
        t.lastUse = m_clock;
    }

    void Touch(uint32_t tile) {
        if (tile < m_tileCount) m_tiles[tile].lastUse = m_clock;
    }
    void SetClock(uint64_t c) { m_clock = c; }
    uint64_t Clock() const { return m_clock; }

    // Least-recently-used tile in use. Prefers tiles not touched this clock tick. -1 if none.
    int PickVictim() const {
        int best = -1;
        for (uint32_t i = 0; i < m_tileCount; ++i) {
            if (!m_tiles[i].inUse) continue;
            if (best < 0) { best = static_cast<int>(i); continue; }
            const Tile& b = m_tiles[best];
            const Tile& c = m_tiles[i];
            // tie-break: prefer evicting a tile other than the one currently being filled
            if (c.lastUse < b.lastUse ||
                (c.lastUse == b.lastUse && static_cast<int>(i) != m_current && best == m_current)) {
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    // Free a tile; returns the codepoints that lived in it.
    std::vector<uint32_t> Evict(uint32_t tile) {
        Tile& t = m_tiles[tile];
        std::vector<uint32_t> cps = std::move(t.codepoints);
        t.codepoints.clear();
        t.inUse = false;
        t.nextX = t.nextY = t.rowHeight = 0;
        if (m_current == static_cast<int>(tile)) m_current = -1;
        return cps;
    }

    void Reset() {
        for (auto& t : m_tiles) { t = Tile{}; }
        m_current = -1;
    }

    // Pixel rectangle of a tile inside its page.
    uint32_t TileOriginX(uint32_t tile) const { return ((tile % TILES_PER_PAGE) % TILES_PER_AXIS) * m_tileSize; }
    uint32_t TileOriginY(uint32_t tile) const { return ((tile % TILES_PER_PAGE) / TILES_PER_AXIS) * m_tileSize; }

private:
    bool TryPlace(Tile& t, uint32_t w, uint32_t h, Slot& out) {
        const int G = static_cast<int>(m_gutter);
        const int S = static_cast<int>(m_tileSize);
        const int W = static_cast<int>(w), H = static_cast<int>(h);
        if (t.nextX + W + G <= S && t.nextY + H + G <= S) {
            out.x = static_cast<uint32_t>(t.nextX);
            out.y = static_cast<uint32_t>(t.nextY);
            return true;
        }
        int ny = t.nextY + t.rowHeight + G;
        if (ny + H + G <= S && G + W + G <= S) {
            t.nextX = G; t.nextY = ny; t.rowHeight = 0;
            out.x = static_cast<uint32_t>(t.nextX);
            out.y = static_cast<uint32_t>(t.nextY);
            return true;
        }
        return false;
    }
    void Fill(Slot& s, uint32_t tile) const {
        s.tile = tile;
        s.page = tile / TILES_PER_PAGE;
        s.x += TileOriginX(tile); // convert tile-local to page coordinates
        s.y += TileOriginY(tile);
    }

    uint32_t m_atlasSize, m_gutter, m_tileSize, m_tileCount;
    std::vector<Tile> m_tiles;
    int m_current = -1;
    uint64_t m_clock = 0;
};

// Remembers which tiles were evicted at each step, so a string that recorded
// (seq, tileMask) when it was built can be checked cheaply: it is still valid
// unless a tile it uses was evicted since.
class EvictionLog {
public:
    uint32_t Seq() const { return m_seq; }
    void Record(uint32_t tileMask) { ++m_seq; m_log[m_seq & 63] = tileMask; }
    bool StillValid(uint32_t fromSeq, uint32_t stringMask) const {
        uint32_t d = m_seq - fromSeq;
        if (d == 0) return true;
        if (d > 63) return false; // too old to know: rebuild
        uint32_t acc = 0;
        for (uint32_t s = fromSeq + 1; s != m_seq + 1; ++s) acc |= m_log[s & 63];
        return (acc & stringMask) == 0;
    }
private:
    uint32_t m_seq = 0;
    std::array<uint32_t, 64> m_log{};
};
