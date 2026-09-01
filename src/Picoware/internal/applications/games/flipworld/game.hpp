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
    // Ignite the flammable world icon (tree/plant/flower/house) under (x,y), if any and
    // not already burning/charred. Returns true if something caught fire.
    bool icon_ignite_at(Level *level, float x, float y);
    void dragon_spawn(Level *level); // boss for the final world
    void ogre_boss_spawn(Level *level);  // Shadow Keep boss: big ogre that throws rocks
    void ghost_boss_spawn(Level *level); // The Hollow boss: big ghost that throws freezing ice-balls
    void flyby_dragon_spawn(Level *level, int passes, int mode, const float *targets, int nTargets); // cameo dragon (0=fire at player, 1=burn targets)
    void player_spawn(Level *level, const char *name, Vector position);
    void set_player_name(const char *name); // name floated above the player
    void fw_set_health_bar(bool on);        // show/hide the floating player health bar (persisted by launcher)
    bool fw_get_health_bar();               // current health-bar visibility (for persistence)
}