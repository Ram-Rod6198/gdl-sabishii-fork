#include <array>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/core/Types.h"
#include "engine/math/Math.h"
#include "engine/ui/Canvas.h"
#include "engine/ui/TextPainter.h"

#include "FakeRenderDevice.h"
#include "game/characters/CharacterSave.h"
#include "game/characters/LevelResults.h"
#include "game/config/ScoreboardMode.h"
#include "game/hud/Scoreboard.h"
#include "game/party/PlayerRuntime.h"

namespace {
using namespace gdl;
using namespace gdl::game;
using Row = Scoreboard::Row;

/** Four seats in their own colours, named for their seats. */
struct Party {
    std::array<PlayerRuntime, 4> players;
    Party() {
        for (usize i = 0; i < players.size(); ++i) {
            CharacterSave save;
            save.name = std::string("SEAT0") + static_cast<char>('0' + i);
            save.color = static_cast<s32>(i);
            players[i].actor.spawn(static_cast<s32>(i), save, nullptr, Vec3{0.0f}, 0.0f);
        }
    }
};

/** A level's gains for `player`: gold, kills, experience. */
LevelResults gains(s32 player, s32 gold, s32 kills, s32 experience) {
    return {player, {gold, kills, experience}};
}

TEST_CASE("the scoreboard ranks the party by kills or by experience, best first", "[scoreboard]") {
    const Party party;
    const std::array<LevelResults, 4> results{gains(0, 900, 5, 40), gains(1, 10, 12, 300),
                                              gains(2, 0, 12, 10), gains(3, 50, 0, 900)};
    CHECK(Scoreboard::rank(results, party.players, ScoreboardMode::Off).empty());

    // Equal scores share a place, in seat order, and the next takes the place after them.
    CHECK(Scoreboard::rank(results, party.players, ScoreboardMode::Kills) ==
          std::vector<Row>{{1, 1, 1, "SEAT01", 12},
                           {1, 2, 2, "SEAT02", 12},
                           {3, 0, 0, "SEAT00", 5},
                           {4, 3, 3, "SEAT03", 0}});
    CHECK(Scoreboard::rank(results, party.players, ScoreboardMode::Experience) ==
          std::vector<Row>{{1, 3, 3, "SEAT03", 900},
                           {2, 1, 1, "SEAT01", 300},
                           {3, 0, 0, "SEAT00", 40},
                           {4, 2, 2, "SEAT02", 10}});
    // Gold is never the score.
    CHECK(Scoreboard::scoreOf(results[0], ScoreboardMode::Kills) == 5);
    CHECK(Scoreboard::scoreOf(results[0], ScoreboardMode::Experience) == 40);
    CHECK(Scoreboard::scoreOf(results[0], ScoreboardMode::Off) == 0);
}

TEST_CASE("the scoreboard needs two players still in the game", "[scoreboard]") {
    Party party;
    const std::array<LevelResults, 3> results{gains(0, 0, 5, 40), gains(1, 0, 12, 300),
                                              gains(7, 0, 99, 999)}; // a seat nobody holds
    // One player alone has nobody to be ranked against.
    CHECK(Scoreboard::rank(std::span{results}.first(1), party.players, ScoreboardMode::Kills)
              .empty());
    CHECK(Scoreboard::rank({}, party.players, ScoreboardMode::Kills).empty());
    CHECK(Scoreboard::rank(results, party.players, ScoreboardMode::Kills) ==
          std::vector<Row>{{1, 1, 1, "SEAT01", 12}, {2, 0, 0, "SEAT00", 5}});
    // One who has quit the game is ranked no longer, which here leaves one alone.
    party.players[1].departed = true;
    CHECK(Scoreboard::rank(results, party.players, ScoreboardMode::Kills).empty());
}

TEST_CASE("the scoreboard draws a heading and a line for each row, or nothing", "[scoreboard]") {
    test::FakeRenderDevice device;
    Canvas canvas;
    Scoreboard board;
    const std::vector<Row> rows{{1, 1, 1, "SEAT01", 12}, {2, 0, 0, "SEAT00", 5}};
    const auto drawn = [&](const TextPainter& text, std::span<const Row> shown) {
        device.draws.clear();
        canvas.begin(device, Mat4{1.0f});
        board.draw(canvas, text, "Kills", shown);
        canvas.end();
        return device.draws.size();
    };
    // Without a font, or without rows, nothing is drawn.
    const TextPainter none;
    CHECK(drawn(none, rows) == 0);
    CHECK(board.shown() == 0);
    CHECK(Scoreboard::kLeft + Scoreboard::kScoreX < 128); // clear of the level's title
}
} // namespace
