#pragma once
#include "../../../../internal/system/colors.hpp"
#include "../../../../internal/system/view.hpp"
#include "../../../../internal/system/view_manager.hpp"
#include "../../../../internal/engine/engine.hpp"
#include "../../../../internal/engine/entity.hpp"
#include "../../../../internal/engine/game.hpp"
#include "../../../../internal/engine/level.hpp"
using namespace Picoware;
namespace FlipWorld
{
    void game_stop();
    void enemy_spawn_json(Level *level, const char *json);
    void dragon_spawn(Level *level); // boss for the final world
    void flyby_dragon_spawn(Level *level, int passes, int mode, float targetX, float targetY); // cameo dragon (0=fire at player, 1=burn house)
    void player_spawn(Level *level, const char *name, Vector position);
    void set_player_name(const char *name); // name floated above the player
}