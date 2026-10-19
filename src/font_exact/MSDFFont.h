#pragma once
#include "MSDF.h"
#include "MSDFCache.h"
#include "../ShutdownCheck.h"

class MSDFFont {
    friend class MSDFCache;
    friend class MSDFPregen;

private:
    struct AtlasPage {
        IDirect3DTexture9* texture = nullptr;
        int nextX = 0, nextY = 0;
        int rowHeight = 0;
        int g = 0;
        std::vector<uint32_t> codepoints;

        AtlasPage(int gutter) : nextX(gutter), nextY(gutter), g(gutter) {}
        ~AtlasPage() { 
            if (texture && !g_isProcessTerminating) texture->Release(); 
        }

        void Clear() {
            nextX = g;
            nextY = g;
            rowHeight = 0;
            codepoints.clear();
        }
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

private:
    bool CreateAtlasPage();
    // Find room for a w x h glyph (may wrap to a new row or create a page). Never evicts. False if full.
    bool AllocateSlot(uint16_t w, uint16_t h, int16_t& outPage);
    // Run a deferred eviction: clear the oldest page and bump the eviction counter.
    void ExecutePendingEviction();
    bool UploadGlyphToAtlas(GlyphMetrics& metrics, uint32_t codepoint);
    bool GenerateMSDF(std::vector<uint8_t>& outData, uint32_t codepoint, int sdfW, int sdfH) const;

    static msdfgen::FontHandle* CreateMSDFHandle(const FT_Byte* data, FT_Long size);

    FT_Face m_ftFace;
    msdfgen::FontHandle* m_msdfFont;
    bool m_isValid;
    uint16_t m_oldestPage;
	uint32_t m_evictionCount;
    bool m_evictionPending = false; // atlas full: eviction is deferred to the start of the next batch

    std::unique_ptr<MSDFCache> m_cache;
    std::vector<std::unique_ptr<AtlasPage>> m_atlasPages;

    ankerl::unordered_dense::map<uint32_t, GlyphMetrics> m_glyphPool;

    inline static ankerl::unordered_dense::map<FT_Face, std::unique_ptr<MSDFFont>> s_fontHandles;

    inline static thread_local VectorPool<float> m_msdfPool;

    // Whether atlas pages use auto-generated mipmaps (falls back to false if creation fails)
    inline static bool s_autoMip = true;
};
