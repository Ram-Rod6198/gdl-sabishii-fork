#pragma once

#include <filesystem>
#include <optional>

#include "engine/render/Image.h"

namespace gdl::game {

/** Native menu marker fitted to a desktop pointer; independent of virtual HUD scaling. */
struct GameCursor {
    static constexpr u32 kSize = 40;    ///< the pointer's own size, a scale of one
    static constexpr u32 kSmallest = 8; ///< the sides a picture may be asked at
    static constexpr u32 kLargest = 256;
    Image image;
    u32 hotX = 0;
    u32 hotY = 0;

    /** The side of the pointer's picture at `scale` times its own size, within the limits
     * the settings keep; anything that is no number is its own size. */
    static u32 sizeAt(f32 scale);
    static std::optional<GameCursor> load(const std::filesystem::path& assetRoot, f32 scale = 1.0f);
    /** Crop visible art, turn its pointed right end up-left, and antialias its reduction to
     * a picture `size` on a side. One larger than the pointer's own is smoothed between the
     * marker's texels rather than showing them as blocks. */
    static std::optional<GameCursor> fromMarker(const Image& marker, u32 size = kSize);
};

} // namespace gdl::game
