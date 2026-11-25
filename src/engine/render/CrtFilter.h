#pragma once

#include <algorithm>

#include "engine/core/Types.h"
#include "engine/math/Math.h"
#include "engine/render/RenderTypes.h"

namespace gdl {

/**
 * An optional picture-tube look over the whole frame, HUD and menus included: scan lines at
 * the composed frame's own rows, a faint aperture grille and darker corners. A port option,
 * not the original's output; nothing is bent, so a pointer stays where it is drawn. The bars
 * beside a letterboxed frame are left as they are.
 */
struct CrtFilter {
    /** The composed frame's place in the framebuffer, in its pixels. */
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 columns = 640.0f; ///< the composed frame's size, whose rows are the scan lines
    f32 rows = 448.0f;

    static constexpr f32 kScanlines = 0.35f; ///< how far a line's edge falls below its middle
    static constexpr f32 kGrille = 0.12f;    ///< how far two of three subpixel columns fall
    static constexpr f32 kCorners = 0.15f;   ///< the corners' darkening, as an exponent
    /** Framebuffer rows to a scan line under which the lines are left out, and from which
     * they are whole: between a line and its gap there is nothing to draw them with. */
    static constexpr f32 kFewestRowsToALine = 1.5f;
    static constexpr f32 kRowsToAWholeLine = 2.0f;

    /** The filter for a `frame` letterboxed into `target`, as the game composes it. */
    static constexpr CrtFilter of(Extent2D frame, Extent2D target) {
        const auto columns = static_cast<f32>(frame.width);
        const auto rows = static_cast<f32>(frame.height);
        const Letterbox fit = fitFrame(columns, rows, static_cast<f32>(target.width),
                                       static_cast<f32>(target.height));
        return {fit.offsetX, fit.offsetY, columns * fit.scale, rows * fit.scale, columns, rows};
    }

    /** False for a frame or a target with no size, which nothing can be drawn through. */
    constexpr bool drawable() const {
        return width > 0.0f && height > 0.0f && columns > 0.0f && rows > 0.0f;
    }

    /** The scan lines' strength at this size: kScanlines from two framebuffer rows a line,
     * fading to none at one and a half. */
    constexpr f32 scanlineStrength() const {
        if (!drawable()) {
            return 0.0f;
        }
        const f32 rowsToALine = height / rows;
        const f32 whole = std::clamp((rowsToALine - kFewestRowsToALine) /
                                         (kRowsToAWholeLine - kFewestRowsToALine),
                                     0.0f, 1.0f);
        return kScanlines * whole;
    }
};

} // namespace gdl
