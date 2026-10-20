#pragma once
#include "MSDF.h"
#include "MSDFCache.h"
#include "../ShutdownCheck.h"
#include "MSDFAtlasTiles.h"

class MSDFFont {
    friend class MSDFCache;
    friend class MSDFPregen;

private:
    struct AtlasPage {
        IDirect3DTexture9* texture = nullptr;
        ~AtlasPage() {
            if (texture && !g_isProcessTerminating) texture->Release();
        }
    };

    // Per-string record used for selective invalidation after a tile eviction.
    struct StringInfo {
        uint32_t seq = 0;      // eviction sequence number the string's UVs were last known valid at
        uint32_t mask = 0;     // bitmask of tiles the string's glyphs live in
        bool complete = false; // false while some glyphs are still missing (string is being built incrementally)
    };

public:
    MSDFFont(FT_Face face, const FT_Byte* fontData, FT_Long dataSize);
    MSDFFont(FT_Face face, const FT_Byte* fontData, FT_Long dataSize, FT_Long faceIndex);
    ~MSDFFont();

    bool IsValid() const { return m_isValid; }

    AtlasPage* GetAtlasPage(size_t index) const;
    size_t GetAtlasPageCount() const { return m_atlasPages.size(); }
    size_t GetAtlasEvictionCount() const { return m_evictionCount; }

    const GlyphMetrics* GetGlyph(uint32_t codepoint);

    static MSDFFont* Get(FT_Face face);
    static void Register(FT_Face face, const FT_Byte* data, FT_Long size);
    static void Unregister(FT_Face face);
    static void ClearAllCache();
    static void Shutdown();

    // Call at the start of each font batch: runs atlas evictions deferred from the previous batch.
    static void FlushPendingEvictions();

    // True if atlas pages are created with auto-generated mipmaps.
    static bool IsAutoMipEnabled() { return s_autoMip; }

    // --- selective invalidation (see MSDFAtlasTiles.h) ---
    enum class StringState { Fresh, Valid, Stale };
    // Called every frame for each string: Stale means a tile it uses was evicted and it must be rebuilt.
    StringState CheckString(const void* key, bool processedBefore);
    // Called at the start of (re)processing a string's geometry.
    void BeginString(const void* key);
    // Called at the end of a processing pass with the tiles used by the glyphs processed in it.
    void AccumulateString(const void* key, uint32_t tileMask, bool complete);

private:
    bool CreateAtlasPage();
    // Find room for a w x h glyph (may open a tile / create a page). Never evicts.
    AtlasTiles::Result AllocateSlot(uint16_t w, uint16_t h, AtlasTiles::Slot& slot);
    // Run a deferred eviction: free the least-recently-used tile(s).
    void ExecutePendingEviction();
    void EvictTile(uint32_t tile);
    bool UploadGlyphToAtlas(GlyphMetrics& metrics, uint32_t codepoint);
    bool GenerateMSDF(std::vector<uint8_t>& outData, uint32_t codepoint, int sdfW, int sdfH) const;

    static msdfgen::FontHandle* CreateMSDFHandle(const FT_Byte* data, FT_Long size);

    FT_Face m_ftFace;
    msdfgen::FontHandle* m_msdfFont;
    bool m_isValid;
	uint32_t m_evictionCount;
    bool m_evictionPending = false; // atlas full: eviction is deferred to the start of the next batch

    std::unique_ptr<MSDFCache> m_cache;
    std::vector<std::unique_ptr<AtlasPage>> m_atlasPages;

    ankerl::unordered_dense::map<uint32_t, GlyphMetrics> m_glyphPool;

    AtlasTiles m_tiles{ MSDF::ATLAS_SIZE, MSDF::ATLAS_GUTTER, MSDF::MAX_ATLAS_PAGES };
    EvictionLog m_evictLog;
    uint64_t m_clock = 0;
    ankerl::unordered_dense::map<const void*, StringInfo> m_strings;

    inline static ankerl::unordered_dense::map<FT_Face, std::unique_ptr<MSDFFont>> s_fontHandles;

    inline static thread_local VectorPool<float> m_msdfPool;

    // Whether atlas pages use auto-generated mipmaps (falls back to false if creation fails)
    inline static bool s_autoMip = true;
};
