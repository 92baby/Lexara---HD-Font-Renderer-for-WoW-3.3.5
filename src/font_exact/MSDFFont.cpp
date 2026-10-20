#include "../Logger.h"
#include "MSDFFont.h"
#include "MSDFCache.h"
#include "MSDFValidator.h"
#include "MSDFUtils.h"
#include <ranges>

MSDFFont::MSDFFont(FT_Face face, const FT_Byte* fontData, FT_Long dataSize)
    : m_ftFace(face), m_msdfFont(nullptr), m_isValid(false), m_evictionCount(0)
{
    if (!face) return;

    m_msdfFont = CreateMSDFHandle(fontData, dataSize);
    if (!m_msdfFont) return;

    m_cache = std::make_unique<MSDFCache>(fontData, dataSize,
        face->family_name ? face->family_name : "Unknown", face->style_name ? face->style_name : "",
        MSDF::SDF_RENDER_SIZE, MSDF::SDF_SPREAD);

    m_isValid = m_cache->GetManifestSize() || MSDF::ALLOW_UNSAFE_FONTS || MSDFValidator::IsFontMSDFCompatible(m_msdfFont);
    if (m_isValid) m_glyphPool.reserve(4096);
}

MSDFFont::MSDFFont(FT_Face face, const FT_Byte* fontData, FT_Long dataSize, FT_Long)
    : MSDFFont(face, fontData, dataSize) {
}

MSDFFont::~MSDFFont() {
    m_glyphPool.clear();
    m_atlasPages.clear();
    m_cache.reset();
    if (m_msdfFont) {
        msdfgen::destroyFont(m_msdfFont);
        m_msdfFont = nullptr;
    }
}

MSDFFont* MSDFFont::Get(FT_Face face) {
    auto it = s_fontHandles.find(face);
    if (it != s_fontHandles.end() && it->second->IsValid()) {
        return it->second.get();
    }
    return nullptr;
}

void MSDFFont::Register(FT_Face face, const FT_Byte* data, FT_Long size) {
    if (s_fontHandles.find(face) != s_fontHandles.end()) return;
    auto font = std::make_unique<MSDFFont>(face, data, size);
    if (font->m_msdfFont && font->m_isValid) {
        // Runtime fonts write their disk cache on a background thread (pre-generation stays synchronous).
        font->m_cache->SetAsyncWrites(true);
        s_fontHandles[face] = std::move(font);
    }
}

void MSDFFont::Unregister(FT_Face face) {
    auto it = s_fontHandles.find(face);
    if (it != s_fontHandles.end()) {
        s_fontHandles.erase(it);
    }
}

void MSDFFont::ClearAllCache() {
    for (auto& handle : s_fontHandles | std::views::values) {
        if (handle) {
            handle->m_glyphPool.clear();
            handle->m_atlasPages.clear();
            handle->m_tiles.Reset();
            handle->m_strings.clear();
            handle->m_evictLog.Record(0xFFFFFFFFu); // everything is invalid
            handle->m_evictionPending = false;
            handle->m_evictionCount++;
        }
    }
}

void MSDFFont::Shutdown() {
    s_fontHandles.clear();
}

void MSDFFont::FlushPendingEvictions() {
    for (auto& handle : s_fontHandles | std::views::values) {
        if (!handle) continue;
        handle->m_tiles.SetClock(++handle->m_clock);
        if (handle->m_evictionPending) handle->ExecutePendingEviction();
    }
}

void MSDFFont::ExecutePendingEviction() {
    m_evictionPending = false;
    // Free at most a couple of tiles per batch; glyphs that did not fit are retried on later frames.
    constexpr int kMaxTilesPerBatch = 2;
    for (int n = 0; n < kMaxTilesPerBatch; ++n) {
        int victim = m_tiles.PickVictim();
        if (victim < 0) break;
        EvictTile(static_cast<uint32_t>(victim));
    }
}

void MSDFFont::EvictTile(uint32_t tile) {
    for (uint32_t cp : m_tiles.Evict(tile)) {
        m_glyphPool.erase(cp);
    }

    const uint32_t page = tile / AtlasTiles::TILES_PER_PAGE;
    if (page < m_atlasPages.size() && m_atlasPages[page]->texture) {
        const uint32_t ox = m_tiles.TileOriginX(tile), oy = m_tiles.TileOriginY(tile), sz = m_tiles.TileSize();
        RECT r = { static_cast<LONG>(ox), static_cast<LONG>(oy), static_cast<LONG>(ox + sz), static_cast<LONG>(oy + sz) };
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(m_atlasPages[page]->texture->LockRect(0, &lr, &r, 0))) {
            auto* row = static_cast<unsigned char*>(lr.pBits);
            for (uint32_t y = 0; y < sz; ++y, row += lr.Pitch) memset(row, 0, static_cast<size_t>(sz) * 4);
            m_atlasPages[page]->texture->UnlockRect(0);
        }
    }

    m_evictLog.Record(1u << tile);
    m_evictionCount++;

    // Diagnostics: frequent evictions mean the working set does not fit the atlas.
    if (m_evictionCount <= 10 || (m_evictionCount % 100) == 0) {
        Log("[MSDF] atlas tile eviction #%zu: tile %u freed", m_evictionCount, tile);
    }
}

MSDFFont::StringState MSDFFont::CheckString(const void* key, bool processedBefore) {
    auto it = m_strings.find(key);
    if (it == m_strings.end()) return processedBefore ? StringState::Stale : StringState::Fresh;

    StringInfo& si = it->second;
    // A complete record on a string that has not been processed yet is a leftover from a dead
    // string that used the same address: forget it.
    if (!processedBefore && si.complete) {
        m_strings.erase(it);
        return StringState::Fresh;
    }
    if (!m_evictLog.StillValid(si.seq, si.mask)) {
        m_strings.erase(it);
        return StringState::Stale;
    }
    si.seq = m_evictLog.Seq();
    for (uint32_t m = si.mask; m; m &= m - 1) {
        m_tiles.Touch(static_cast<uint32_t>(std::countr_zero(m)));
    }
    return StringState::Valid;
}

void MSDFFont::BeginString(const void* key) {
    auto it = m_strings.find(key);
    if (it != m_strings.end() && it->second.complete) m_strings.erase(it);
}

void MSDFFont::AccumulateString(const void* key, uint32_t tileMask, bool complete) {
    // Addresses of dead strings are never reported to us; bound the table and let strings rebuild if it overflows.
    if (m_strings.size() > 16384) m_strings.clear();

    StringInfo& si = m_strings[key];
    si.mask |= tileMask;
    si.seq = m_evictLog.Seq();
    si.complete = complete;
}

const GlyphMetrics* MSDFFont::GetGlyph(uint32_t codepoint) {
    auto pit = m_glyphPool.find(codepoint);
    if (pit != m_glyphPool.end()) {
        if (pit->second.atlasTile != 0xFF) m_tiles.Touch(pit->second.atlasTile);
        return &pit->second;
    }

    auto [it, inserted] = m_glyphPool.try_emplace(codepoint);
    GlyphMetrics& metrics = it->second;

    if (m_cache->TryLoadGlyph(codepoint, metrics)) {
        if (!UploadGlyphToAtlas(metrics, codepoint)) {
            // No room right now (eviction deferred) or upload failed: callers retry on later frames
            m_glyphPool.erase(it);
            return nullptr;
        }
        return &metrics;
    }

    GlyphMetricsToStore storage;
    storage.codepoint = codepoint;

    if (FT_Set_Pixel_Sizes(m_ftFace, MSDF::SDF_RENDER_SIZE, MSDF::SDF_RENDER_SIZE) != 0) {
        m_glyphPool.erase(it);
        return nullptr;
    }

    FT_UInt glyphIndex = FT_Get_Char_Index(m_ftFace, codepoint);
    if (FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) {
        m_glyphPool.erase(it);
        return nullptr;
    }

    bool uploadFailed = false;
    storage.bitmapLeft = m_ftFace->glyph->bitmap_left;
    storage.bitmapTop = m_ftFace->glyph->bitmap_top;

    const bool hasOutline = m_ftFace->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
        m_ftFace->glyph->outline.n_contours > 0;
    if (hasOutline) {
        FT_BBox bbox;
        FT_Outline_Get_BBox(&m_ftFace->glyph->outline, &bbox);

        uint16_t w = static_cast<uint16_t>(std::max(0, static_cast<int>(((bbox.xMax + 63) >> 6) - (bbox.xMin >> 6))));
        uint16_t h = static_cast<uint16_t>(std::max(0, static_cast<int>(((bbox.yMax + 63) >> 6) - (bbox.yMin >> 6))));

        if (w > 0 && h > 0) {
            uint16_t sdfW = w + 2 * MSDF::SDF_SPREAD;
            uint16_t sdfH = h + 2 * MSDF::SDF_SPREAD;

            // Check the atlas has room BEFORE generating, so no CPU is wasted on a glyph that cannot be placed.
            // Mark eviction as pending; it runs at the start of the next batch.
            AtlasTiles::Slot probe;
            const AtlasTiles::Result room = AllocateSlot(sdfW, sdfH, probe);
            if (room != AtlasTiles::Result::Ok) {
                if (room == AtlasTiles::Result::Full) m_evictionPending = true;
                m_glyphPool.erase(it);
                return nullptr;
            }

            storage.ownedPixelData.reserve(static_cast<size_t>(sdfW) * sdfH * 4);
            if (GenerateMSDF(storage.ownedPixelData, codepoint, sdfW, sdfH)) {
                storage.width = sdfW;
                storage.height = sdfH;
                storage.dataSize = static_cast<uint32_t>(storage.ownedPixelData.size());
                metrics.width = storage.width;
                metrics.height = storage.height;
                metrics.bitmapLeft = storage.bitmapLeft;
                metrics.bitmapTop = storage.bitmapTop;
                metrics.pixelData = storage.ownedPixelData.data();
                uploadFailed = !UploadGlyphToAtlas(metrics, codepoint);
            }
        }
    }
    // Always store into the disk cache, whether or not the upload succeeded
    m_cache->StoreGlyph(std::move(storage));

    if (uploadFailed) {
        m_glyphPool.erase(it);
        return nullptr;
    }
    return &metrics;
}

MSDFFont::AtlasPage* MSDFFont::GetAtlasPage(size_t index) const {
    if (index < m_atlasPages.size()) {
        return m_atlasPages[index].get();
    }
    return nullptr;
}

bool MSDFFont::CreateAtlasPage() {
    auto page = std::make_unique<AtlasPage>();

    bool created = false;
    if (s_autoMip) {
        // Auto mipmaps: reduce shimmer / dropped strokes when minified (max LOD is clamped in the pixel shader)
        created = D3D::CreateTexture(&page->texture, {
            .width = MSDF::ATLAS_SIZE,
            .height = MSDF::ATLAS_SIZE,
            .levels = 0,
            .usage = D3DUSAGE_AUTOGENMIPMAP,
            .format = MSDF::D3DFMT,
            .pool = D3DPOOL_MANAGED
            });
        if (!created) s_autoMip = false; // unsupported by driver: fall back to a single level
    }
    if (!created) {
        created = D3D::CreateTexture(&page->texture, {
            .width = MSDF::ATLAS_SIZE,
            .height = MSDF::ATLAS_SIZE,
            .format = MSDF::D3DFMT,
            .pool = D3DPOOL_MANAGED
            });
    }
    if (!created) return false;

    m_atlasPages.push_back(std::move(page));
    return true;
}

AtlasTiles::Result MSDFFont::AllocateSlot(uint16_t w, uint16_t h, AtlasTiles::Slot& slot) {
    return m_tiles.Find(w, h, slot, [this](uint32_t page) {
        while (m_atlasPages.size() <= page) {
            if (!CreateAtlasPage()) return false;
        }
        return true;
    });
}

bool MSDFFont::UploadGlyphToAtlas(GlyphMetrics& metrics, uint32_t codepoint) {
    if (!metrics.pixelData || metrics.width == 0 || metrics.height == 0) return true;

    AtlasTiles::Slot slot;
    const AtlasTiles::Result room = AllocateSlot(metrics.width, metrics.height, slot);
    if (room != AtlasTiles::Result::Ok) {
        // Never evict mid-frame: strings already processed this frame would point at wiped atlas regions.
        if (room == AtlasTiles::Result::Full) m_evictionPending = true;
        return false;
    }
    AtlasPage* targetPage = m_atlasPages[slot.page].get();
    if (!targetPage->texture) return false;

    // Lock only this glyph's sub-rectangle: avoids dirtying and re-uploading the whole 16MB page
    RECT lockRect = {
        static_cast<LONG>(slot.x), static_cast<LONG>(slot.y),
        static_cast<LONG>(slot.x + metrics.width), static_cast<LONG>(slot.y + metrics.height)
    };
    D3DLOCKED_RECT lockedRect;
    if (FAILED(targetPage->texture->LockRect(0, &lockedRect, &lockRect, 0))) {
        return false;
    }
    if (lockedRect.Pitch < metrics.width * 4) {
        targetPage->texture->UnlockRect(0);
        return false;
    }

    const unsigned char* src = metrics.pixelData;
    unsigned char* dest = static_cast<unsigned char*>(lockedRect.pBits); // already points at the sub-rect's top-left
    for (uint16_t y = 0; y < metrics.height; ++y) {
        memcpy(dest, src, metrics.width * 4);
        dest += lockedRect.Pitch;
        src += metrics.width * 4;
    }
    targetPage->texture->UnlockRect(0);

    const float atlasSize = static_cast<float>(MSDF::ATLAS_SIZE);
    metrics.u0 = static_cast<float>(slot.x) / atlasSize;
    metrics.v0 = static_cast<float>(slot.y) / atlasSize;
    metrics.u1 = static_cast<float>(slot.x + metrics.width) / atlasSize;
    metrics.v1 = static_cast<float>(slot.y + metrics.height) / atlasSize;
    metrics.atlasPageIndex = static_cast<uint16_t>(slot.page);
    metrics.atlasTile = static_cast<uint8_t>(slot.tile);

    m_tiles.Commit(slot, metrics.width, metrics.height, codepoint);
    return true;
}

bool MSDFFont::GenerateMSDF(std::vector<uint8_t>& outData, uint32_t codepoint, int sdfW, int sdfH) const {
    if (sdfW <= 0 || sdfH <= 0 || sdfW > 512 || sdfH > 512) return false;

    msdfgen::Shape shape;
    if (!msdfgen::loadGlyph(shape, m_msdfFont, codepoint)) return false;

    if (shape.contours.empty()) {
        outData.assign(sdfW * sdfH * 4, 0);
        return true;
    }

    msdfgen::resolveShapeGeometry(shape);
    msdfgen::edgeColoringInkTrap(shape, 3.0, 0);

    auto bounds = shape.getBounds();
    double shapeW = bounds.r - bounds.l;
    double shapeH = bounds.t - bounds.b;
    if (shapeW <= 0 || shapeH <= 0) return false;

    double usableW = static_cast<double>(sdfW) - 2.0 * MSDF::SDF_SPREAD;
    double usableH = static_cast<double>(sdfH) - 2.0 * MSDF::SDF_SPREAD;
    if (usableW <= 0 || usableH <= 0) return false;

    double scale = std::min(usableW / shapeW, usableH / shapeH);
    msdfgen::Projection projection(
        msdfgen::Vector2(scale, scale),
        msdfgen::Vector2(MSDF::SDF_SPREAD / scale - bounds.l, MSDF::SDF_SPREAD / scale - bounds.b)
    );

    auto msdfBuf = m_msdfPool.AcquireSized(sdfW * sdfH * 3);
    auto sdfBuf = m_msdfPool.AcquireSized(sdfW * sdfH);

    msdfgen::BitmapRef<float, 3> msdfBitmap(msdfBuf.data(), sdfW, sdfH);
    msdfgen::BitmapRef<float, 1> sdfBitmap(sdfBuf.data(), sdfW, sdfH);

    msdfgen::MSDFGeneratorConfig config;
    config.overlapSupport = true;

    msdfgen::Range msdfRange(MSDF::SDF_SPREAD / scale);
    msdfgen::generateMSDF(msdfBitmap, shape, projection, msdfRange, config);
    msdfgen::SDFTransformation msdfTransform(projection, msdfRange);
    msdfgen::distanceSignCorrection(msdfBitmap, shape, msdfTransform, msdfgen::FillRule::FILL_NONZERO);

    msdfgen::Range sdfRange(MSDF::SDF_SPREAD / scale * 5.0);
    msdfgen::generateSDF(sdfBitmap, shape, projection, sdfRange);
    msdfgen::SDFTransformation sdfTransform(projection, sdfRange);
    msdfgen::distanceSignCorrection(sdfBitmap, shape, sdfTransform, msdfgen::FillRule::FILL_NONZERO);

    outData.resize(sdfW * sdfH * 4);
    uint8_t* dest = outData.data();
    const float* srcMSDF = msdfBuf.data();
    const float* srcSDF = sdfBuf.data();

    for (int i = 0; i < sdfW * sdfH; ++i) {
        dest[i * 4 + 0] = static_cast<uint8_t>(std::clamp(srcMSDF[i * 3 + 0] * 255.f, 0.f, 255.f));
        dest[i * 4 + 1] = static_cast<uint8_t>(std::clamp(srcMSDF[i * 3 + 1] * 255.f, 0.f, 255.f));
        dest[i * 4 + 2] = static_cast<uint8_t>(std::clamp(srcMSDF[i * 3 + 2] * 255.f, 0.f, 255.f));
        dest[i * 4 + 3] = static_cast<uint8_t>(std::clamp(srcSDF[i] * 255.f, 0.f, 255.f));
    }
    m_msdfPool.Release(std::move(msdfBuf));
    m_msdfPool.Release(std::move(sdfBuf));

    return true;
}

msdfgen::FontHandle* MSDFFont::CreateMSDFHandle(const FT_Byte* data, FT_Long size) {
    return !MSDF::g_msdfFreetype ? nullptr : msdfgen::loadFontData(MSDF::g_msdfFreetype, data, size);
}
