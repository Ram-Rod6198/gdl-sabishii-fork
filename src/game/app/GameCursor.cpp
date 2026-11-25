#include "game/app/GameCursor.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

#include "engine/assets/TextureSet.h"
#include "engine/core/Log.h"

#include "game/config/GameConfig.h"

namespace gdl::game {

u32 GameCursor::sizeAt(f32 scale) {
    if (!std::isfinite(scale)) {
        return kSize;
    }
    const f32 held =
        std::clamp(scale, DisplayConfig::kCursorScaleMin, DisplayConfig::kCursorScaleMax);
    return static_cast<u32>(std::lround(static_cast<f32>(kSize) * held));
}

std::optional<GameCursor> GameCursor::load(const std::filesystem::path& assetRoot, f32 scale) {
    try {
        TextureSet textures;
        if (textures.load(assetRoot / "STATIC")) {
            if (const auto marker = textures.find("MARKER_LEFT")) {
                return fromMarker(textures.image(*marker), sizeAt(scale));
            }
        }
    } catch (const std::exception& error) {
        log::warn("Mouse cursor asset unusable: {}", error.what());
    }
    return std::nullopt;
}

std::optional<GameCursor> GameCursor::fromMarker(const Image& marker, u32 size) {
    if (size < kSmallest || size > kLargest || marker.width == 0 || marker.height == 0 ||
        marker.width > static_cast<u32>(std::numeric_limits<s32>::max()) ||
        marker.height > static_cast<u32>(std::numeric_limits<s32>::max()) ||
        marker.pixels.size() != u64{marker.width} * marker.height * 4) {
        return std::nullopt;
    }
    u32 left = marker.width;
    u32 top = marker.height;
    u32 right = 0;
    u32 bottom = 0;
    for (u32 y = 0; y < marker.height; ++y) {
        for (u32 x = 0; x < marker.width; ++x) {
            if (marker.pixel(x, y).a != 0) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (left == marker.width) {
        return std::nullopt;
    }
    const f32 width = static_cast<f32>(right - left + 1);
    const f32 height = static_cast<f32>(bottom - top + 1);
    const Vec2 center{static_cast<f32>(left) + width * 0.5f, static_cast<f32>(top) + height * 0.5f};
    // In top-down image coordinates, -135 degrees turns the marker's pointed
    // right end up-left, leaving its wider ornament behind the click hotspot.
    constexpr f32 kDiagonal = 0.70710678118f;
    constexpr f32 kMargin = 1.0f;
    const f32 scale = (static_cast<f32>(size) - 2 * kMargin) / ((width + height) * kDiagonal);
    const f32 middle = static_cast<f32>(size) * 0.5f;
    const auto forward = [&](const Vec2& point) {
        const Vec2 delta = (point - center) * (scale * kDiagonal);
        return Vec2{middle - delta.x + delta.y, middle - delta.x - delta.y};
    };
    // Use the visible right tip, not the transparent corner of the rotated bitmap.
    u32 tipY = top;
    f32 nearest = std::numeric_limits<f32>::max();
    for (u32 y = top; y <= bottom; ++y) {
        const f32 distance = std::abs(static_cast<f32>(y) + 0.5f - center.y);
        if (marker.pixel(right, y).a != 0 && distance < nearest) {
            nearest = distance;
            tipY = y;
        }
    }
    const Vec2 tip = forward({static_cast<f32>(right) + 0.5f, static_cast<f32>(tipY) + 0.5f});
    GameCursor cursor;
    cursor.image = Image::filled(size, size, Color::rgba(0, 0, 0, 0));
    cursor.hotX = static_cast<u32>(std::clamp(std::floor(tip.x), 0.0f, static_cast<f32>(size - 1)));
    cursor.hotY = static_cast<u32>(std::clamp(std::floor(tip.y), 0.0f, static_cast<f32>(size - 1)));
    // Area samples keep the fine outline readable at pointer size. Accumulate
    // premultiplied colour so hidden RGB in transparent texels cannot make a halo;
    // the OS receives straight RGBA after filtering.
    constexpr s32 kSamples = 4;
    // One texel's colour, premultiplied; nothing outside the marker.
    const auto at = [&](s32 px, s32 py) {
        if (px < 0 || py < 0 || px >= static_cast<s32>(marker.width) ||
            py >= static_cast<s32>(marker.height)) {
            return Vec4{0};
        }
        const Color color = marker.pixel(static_cast<u32>(px), static_cast<u32>(py));
        const f32 alpha = static_cast<f32>(color.a);
        return Vec4{static_cast<f32>(color.r) * alpha, static_cast<f32>(color.g) * alpha,
                    static_cast<f32>(color.b) * alpha, alpha};
    };
    const auto texel = [&](const Vec2& source) {
        return at(static_cast<s32>(std::floor(source.x)), static_cast<s32>(std::floor(source.y)));
    };
    // The four texels about a point, weighed by how near each one's centre is.
    const auto blended = [&](const Vec2& source) {
        const Vec2 corner = glm::floor(source - Vec2{0.5f});
        const Vec2 part = source - Vec2{0.5f} - corner;
        const auto px = static_cast<s32>(corner.x);
        const auto py = static_cast<s32>(corner.y);
        return glm::mix(glm::mix(at(px, py), at(px + 1, py), part.x),
                        glm::mix(at(px, py + 1), at(px + 1, py + 1), part.x), part.y);
    };
    const bool smooth = size > kSize;
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            Vec4 sum{0};
            for (s32 sy = 0; sy < kSamples; ++sy) {
                for (s32 sx = 0; sx < kSamples; ++sx) {
                    const Vec2 target{
                        static_cast<f32>(x) + (static_cast<f32>(sx) + 0.5f) / kSamples - middle,
                        static_cast<f32>(y) + (static_cast<f32>(sy) + 0.5f) / kSamples - middle};
                    const Vec2 source = center + Vec2{-target.x - target.y, target.x - target.y} *
                                                     (kDiagonal / scale);
                    sum += smooth ? blended(source) : texel(source);
                }
            }
            if (sum.a > 0) {
                cursor.image.setPixel(
                    x, y,
                    Color::rgba(static_cast<u8>(std::lround(sum.r / sum.a)),
                                static_cast<u8>(std::lround(sum.g / sum.a)),
                                static_cast<u8>(std::lround(sum.b / sum.a)),
                                static_cast<u8>(std::lround(sum.a / (kSamples * kSamples)))));
            }
        }
    }
    return cursor;
}

} // namespace gdl::game
