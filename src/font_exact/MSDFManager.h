#pragma once
#include "MSDF.h"
#include "MSDFCache.h"
#include "ankerl/unordered_dense.h"
#include <filesystem>

class MSDFCache;

class MSDFManager {
    friend class MSDFCache;

public:
    MSDFManager();
    ~MSDFManager();
    MSDFManager(const MSDFManager&) = delete;
    MSDFManager& operator=(const MSDFManager&) = delete;
    MSDFManager(MSDFManager&&) = delete;
    MSDFManager& operator=(MSDFManager&&) = delete;

private:
    // Maximum number of block files kept mapped simultaneously.
    // Each slot now uses the real file size (~1-2.5 MB) via plain MapViewOfFile
    // instead of a worst-case placeholder (~28 MB via VirtualAlloc2 arena).
    // 16 slots therefore costs ~13-40 MB of address space instead of ~451 MB.
    static constexpr size_t MAX_MAPPED_BLOCKS = 16;
    static_assert(MAX_MAPPED_BLOCKS <= 64);
    static_assert(MSDFCache::BLOCK_SIZE > 0 && (MSDFCache::BLOCK_SIZE & (MSDFCache::BLOCK_SIZE - 1)) == 0,
        "BLOCK_SIZE must be a power of 2");

    struct alignas(64) MappedBlock {
        FileGuard    file;
        MappingGuard mapping;
        ViewGuard    view;
        uint64_t     fileSize   = 0;
        uint64_t     lruStamp   = 0;  // monotonically increasing; lowest = LRU candidate
        const MSDFCache::BlockFileHeader* header    = nullptr;
        const MSDFCache::GlyphEntry*      entries   = nullptr;
        const uint32_t*                   hashTable = nullptr;
        const uint8_t*                    payload   = nullptr;
        uint32_t     entryCount = 0;
        MSDFCache::BlockKey key;

        bool IsLoaded() const { return view.ptr != nullptr; }

        void Close() {
            view.Close();
            mapping.Close();
            file.Close();
            header    = nullptr;
            entries   = nullptr;
            hashTable = nullptr;
            payload   = nullptr;
            entryCount = 0;
            fileSize   = 0;
        }

        void Reset() {
            Close();
            lruStamp = 0;
            key = {};
        }

        MappedBlock() = default;
        MappedBlock(const MappedBlock&) = delete;
        MappedBlock& operator=(const MappedBlock&) = delete;
    };

    static bool LoadGlyph(const MSDFCache::BlockWrap& wrap, uint32_t codepoint, GlyphMetrics& outMetrics);

    // Plain MapViewOfFile — OS chooses address, mapping sized to real file.
    static bool LoadMappedBlock(const MSDFCache::BlockWrap& wrap, MappedBlock& outBlock);
    static MappedBlock* GetOrLoadMappedBlock(const MSDFCache::BlockWrap& wrap);

    // Returns the index of the least-recently-used loaded slot.
    static uint32_t FindLRUSlot();

    static void FreeBlock(uint32_t blockIndex);
    static void FreeBlockByKey(MSDFCache::BlockKey key);
    static void FlushAll();

    static uint32_t RegisterFont(FontHash hash);
    static FontHash  GetFontHash(uint32_t fontId);

    inline static std::array<MappedBlock, MAX_MAPPED_BLOCKS> s_mappedBlocks;
    inline static ankerl::unordered_dense::map<MSDFCache::BlockKey, uint32_t> s_blockCache;

    // One-entry fast path: avoids a hash lookup on repeated access to the same block.
    inline static uint32_t s_lastBlockIndex = 0xFFFFFFFF;
    inline static MSDFCache::BlockKey s_lastBlockKey;

    // Bitmask of free (unloaded) slots; bit set = free.
    inline static uint64_t s_freeMask = (1ULL << MAX_MAPPED_BLOCKS) - 1;

    // LRU clock — stamped on every GetOrLoad hit/load.
    inline static uint64_t s_lruClock = 0;

    inline static SYSTEM_INFO s_si;

    inline static ankerl::unordered_dense::map<FontHash, uint32_t> s_fontHashToId;
    inline static ankerl::unordered_dense::map<uint32_t, FontHash> s_fontIdToHash;
    inline static uint32_t s_nextFontId = 0;
};
