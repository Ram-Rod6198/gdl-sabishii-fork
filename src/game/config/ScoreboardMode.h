#pragma once

#include <array>
#include <string_view>

#include "engine/core/Types.h"

namespace gdl::game {

/** What the optional scoreboard ranks the party by in a level: nothing (it is not shown), the
 * enemies and generators each has destroyed there, or the experience each has won there. */
enum class ScoreboardMode : u8 { Off, Kills, Experience };
inline constexpr std::array<std::string_view, 3> kScoreboardModeNames{"off", "kills", "experience"};

} // namespace gdl::game
