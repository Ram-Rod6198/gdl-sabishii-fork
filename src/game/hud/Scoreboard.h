#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/Types.h"
#include "engine/ui/Canvas.h"
#include "engine/ui/TextPainter.h"

#include "game/characters/LevelResults.h"
#include "game/config/ScoreboardMode.h"
#include "game/party/PlayerRuntime.h"

namespace gdl::game {

/**
 * The optional scoreboard in the view's top left corner: the party ranked by what each has
 * done in this level so far, kills or experience as the setting says, each name in its
 * character's colour. It needs a party of two or more and is never shown in the tower.
 * Presentation only, read fresh from the party every frame; it awards and keeps nothing.
 */
class Scoreboard {
public:
    static constexpr s32 kLeft = 8;     ///< the board's left edge
    static constexpr s32 kTop = 8;      ///< the heading's top
    static constexpr s32 kRowStep = 13; ///< from one line's top to the next
    static constexpr s32 kNameX = 14;   ///< a name, right of its place
    static constexpr s32 kScoreX = 78;  ///< a score, right of the longest name
    static constexpr f32 kScale = 0.4f; ///< of the large menu font
    static constexpr u8 kShadowAlpha = 200;

    struct Row {
        s32 place = 1; ///< from one; equal scores share a place
        s32 seat = 0;
        s32 color = 0;
        std::string name;
        s32 score = 0;
        bool operator==(const Row&) const = default;
    };

    /** The party's rows for `mode`, best first and equal scores in seat order: one for each of
     * `results` whose seat is still among `players`. None when the mode is Off or fewer than two
     * are ranked. */
    static std::vector<Row> rank(std::span<const LevelResults> results,
                                 std::span<const PlayerRuntime> players, ScoreboardMode mode);
    /** The score `results` holds for `mode`; none for Off. */
    static s32 scoreOf(const LevelResults& results, ScoreboardMode mode);

    /** Draws `heading` and `rows` beneath it; nothing without rows or a font. */
    void draw(Canvas& canvas, const TextPainter& text, std::string_view heading,
              std::span<const Row> rows);
    /** How many rows the last draw showed. */
    usize shown() const { return m_shown; }

private:
    usize m_shown = 0;
};

} // namespace gdl::game
