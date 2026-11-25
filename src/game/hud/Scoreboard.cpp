#include "game/hud/Scoreboard.h"

#include <algorithm>
#include <string>

#include "engine/math/Math.h"

#include "game/characters/CharacterSave.h"
#include "game/characters/ClassData.h"

namespace gdl::game {
namespace {

constexpr usize kKillsTotal = 1; ///< LevelResults::totals: gold, kills, experience
constexpr usize kExperienceTotal = 2;

/** One line, dark behind and in `color` over it, so it reads over any level. */
void drawLine(Canvas& canvas, const TextPainter& text, s32 x, s32 y, std::string_view words,
              Color color) {
    text.draw(canvas, x + 1, y + 1, words,
              TextStyle{.scale = Scoreboard::kScale,
                        .color = Color::rgba(0, 0, 0, Scoreboard::kShadowAlpha)});
    text.draw(canvas, x, y, words, TextStyle{.scale = Scoreboard::kScale, .color = color});
}

} // namespace

s32 Scoreboard::scoreOf(const LevelResults& results, ScoreboardMode mode) {
    switch (mode) {
    case ScoreboardMode::Kills: return results.totals[kKillsTotal];
    case ScoreboardMode::Experience: return results.totals[kExperienceTotal];
    case ScoreboardMode::Off: break;
    }
    return 0;
}

std::vector<Scoreboard::Row> Scoreboard::rank(std::span<const LevelResults> results,
                                              std::span<const PlayerRuntime> players,
                                              ScoreboardMode mode) {
    std::vector<Row> rows;
    if (mode == ScoreboardMode::Off) {
        return rows;
    }
    for (const LevelResults& result : results) {
        const auto found = std::ranges::find_if(players, [&](const PlayerRuntime& runtime) {
            return runtime.actor.player() == result.player && !runtime.departed;
        });
        if (found == players.end()) {
            continue;
        }
        const CharacterSave& save = found->actor.save();
        rows.push_back({.place = 1,
                        .seat = result.player,
                        .color = save.color,
                        .name = save.name,
                        .score = scoreOf(result, mode)});
    }
    if (rows.size() < 2) {
        rows.clear();
        return rows;
    }
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b) {
        return a.score != b.score ? a.score > b.score : a.seat < b.seat;
    });
    for (usize i = 1; i < rows.size(); ++i) {
        rows[i].place =
            rows[i].score == rows[i - 1].score ? rows[i - 1].place : static_cast<s32>(i) + 1;
    }
    return rows;
}

void Scoreboard::draw(Canvas& canvas, const TextPainter& text, std::string_view heading,
                      std::span<const Row> rows) {
    m_shown = 0;
    if (rows.empty() || !text.ready()) {
        return;
    }
    s32 y = kTop;
    if (!heading.empty()) {
        drawLine(canvas, text, kLeft, y, heading, Color::white());
        y += kRowStep;
    }
    for (const Row& row : rows) {
        const Color color = playerColor(row.color);
        drawLine(canvas, text, kLeft, y, std::to_string(row.place), color);
        drawLine(canvas, text, kLeft + kNameX, y, row.name, color);
        drawLine(canvas, text, kLeft + kScoreX, y, std::to_string(row.score), color);
        y += kRowStep;
        ++m_shown;
    }
}

} // namespace gdl::game
