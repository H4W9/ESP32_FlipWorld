#include <ArduinoJson.h>
#include "../../../applications/games/flipworld/game.hpp"
#include "../../../applications/games/flipworld/sprites.hpp"
namespace FlipWorld
{
#undef ENTITY_UP
#undef ENTITY_DOWN
#undef ENTITY_LEFT
#undef ENTITY_RIGHT
#define ENTITY_UP Vector(0, -1)
#define ENTITY_DOWN Vector(0, 1)
#define ENTITY_LEFT Vector(-1, 0)
#define ENTITY_RIGHT Vector(1, 0)

    // Name shown floating above the player (set to the signed-in username by the
    // launcher; defaults to "Player" for offline/guest play).
    static char fw_player_name[24] = "Player";
    void set_player_name(const char *name)
    {
        if (name == nullptr || name[0] == '\0')
        {
            strncpy(fw_player_name, "Player", sizeof(fw_player_name) - 1);
        }
        else
        {
            strncpy(fw_player_name, name, sizeof(fw_player_name) - 1);
        }
        fw_player_name[sizeof(fw_player_name) - 1] = '\0';
    }
    typedef struct
    {
        const char *name;
        const uint8_t *data;
        Vector size;
    } PlayerContext;

    static PlayerContext player_context_get(const char *name, bool is_left)
    {
        // players
        if (strcmp(name, "naked") == 0)
            return {name, is_left ? player_left_naked_10x10px : player_right_naked_10x10px, Vector(10, 10)};
        if (strcmp(name, "sword") == 0)
            return {name, is_left ? player_left_sword_15x11px : player_right_sword_15x11px, Vector(15, 11)};
        if (strcmp(name, "axe") == 0)
            return {name, is_left ? player_left_axe_15x11px : player_right_axe_15x11px, Vector(15, 11)};
        if (strcmp(name, "bow") == 0)
            return {name, is_left ? player_left_bow_13x11px : player_right_bow_13x11px, Vector(13, 11)};
        // enemies
        if (strcmp(name, "cyclops") == 0)
            return {name, is_left ? enemy_left_cyclops_10x11px : enemy_right_cyclops_10x11px, Vector(10, 11)};
        if (strcmp(name, "ghost") == 0)
            return {name, is_left ? enemy_left_ghost_15x15px : enemy_right_ghost_15x15px, Vector(15, 15)};
        if (strcmp(name, "ogre") == 0)
            return {name, is_left ? enemy_left_ogre_10x13px : enemy_right_ogre_10x13px, Vector(10, 13)};

        return {NULL, NULL, Vector(0, 0)};
    }

    static void enemy_update(Entity *self, Game *game)
    {
        // check if enemy is dead
        if (self->state == ENTITY_DEAD)
        {
            return;
        }

        // float delta_time = 1.0 / game->fps;
        float delta_time = 1.0 / 30; // 30 frames per second

        // Increment the elapsed_attack_timer for the enemy
        self->elapsed_attack_timer += delta_time;

        switch (self->state)
        {
        case ENTITY_IDLE:
            // Increment the elapsed_move_timer
            self->elapsed_move_timer += delta_time;
            self->position_set(self->position);
            // Check if it's time to move again
            if (self->elapsed_move_timer >= self->move_timer)
            {
                // Determine the next state based on the current position
                if (fabs(self->position.x - self->start_position.x) < 1 && fabs(self->position.y - self->start_position.y) < 1)
                {
                    self->state = ENTITY_MOVING_TO_END;
                }
                else if (fabs(self->position.x - self->end_position.x) < 1 && fabs(self->position.y - self->end_position.y) < 1)
                {
                    self->state = ENTITY_MOVING_TO_START;
                }
                // Reset the elapsed_move_timer
                self->elapsed_move_timer = 0;
            }
            break;
        case ENTITY_MOVING_TO_END:
        case ENTITY_MOVING_TO_START:
        case ENTITY_ATTACKED:
            // determine the direction vector
            Vector direction_vector = {0, 0};

            // if attacked, change state to moving based on the direction
            if (self->state == ENTITY_ATTACKED)
            {
                self->state = self->position.x < self->old_position.x ? ENTITY_MOVING_TO_END : ENTITY_MOVING_TO_START;
            }

            // Determine the target position based on the current state
            Vector target_position = self->state == ENTITY_MOVING_TO_END ? self->end_position : self->start_position;

            // Calculate direction towards the target
            if (self->position.x < target_position.x)
            {
                direction_vector.x = 1;
                self->direction = ENTITY_RIGHT;
            }
            else if (self->position.x > target_position.x)
            {
                direction_vector.x = -1;
                self->direction = ENTITY_LEFT;
            }
            else if (self->position.y < target_position.y)
            {
                direction_vector.y = 1;
                self->direction = ENTITY_DOWN;
            }
            else if (self->position.y > target_position.y)
            {
                direction_vector.y = -1;
                self->direction = ENTITY_UP;
            }

            // Normalize direction vector
            float length = sqrt(direction_vector.x * direction_vector.x + direction_vector.y * direction_vector.y);
            if (length != 0)
            {
                direction_vector.x /= length;
                direction_vector.y /= length;
            }

            // Update position based on direction and speed
            Vector new_pos = self->position;
            new_pos.x += direction_vector.x * self->speed * delta_time;
            new_pos.y += direction_vector.y * self->speed * delta_time;

            // Clamp the position to the target to prevent overshooting
            if ((direction_vector.x > 0 && new_pos.x > target_position.x) || (direction_vector.x < 0 && new_pos.x < target_position.x))
            {
                new_pos.x = target_position.x;
            }

            if ((direction_vector.y > 0 && new_pos.y > target_position.y) || (direction_vector.y < 0 && new_pos.y < target_position.y))
            {
                new_pos.y = target_position.y;
            }

            // Set the new position
            self->position_set(new_pos);

            // Check if the enemy has reached or surpassed the target_position
            bool reached_x = fabs(new_pos.x - target_position.x) < 1;
            bool reached_y = fabs(new_pos.y - target_position.y) < 1;

            if (reached_x && reached_y)
            {
                // Set the state to idle
                self->state = ENTITY_IDLE;
                self->elapsed_move_timer = 0;

                self->position_changed = true;
            }
            break;
        }
    }

    // Draw a label (name / health) above an entity. yOffset lifts it above the
    // sprite; the player uses a larger offset so its name doesn't cover a nearby
    // enemy's health readout while attacking.
    static void draw_username(Game *game, Vector pos, const char *username, int yOffset = 10, uint16_t color = TFT_RED)
    {
        float sx = pos.x - game->pos.x - (strlen(username) * 2);
        float sy = pos.y - game->pos.y - yOffset;

        // skip if drawing the label is off-screen
        if (pos.x - game->pos.x - (strlen(username) * 2 + 8) < 0 || pos.x - game->pos.x + (strlen(username) * 2 + 8) > game->size.x ||
            sy < 0 || sy > game->size.y)
        {
            return;
        }

        // black backing box, then the text on top
        game->draw->display->fillRect(sx, sy, strlen(username) * 5 + 4, 10, TFT_BLACK);
        game->draw->text(Vector(sx, sy), username, color);
    }

    static void enemy_render(Entity *self, Draw *draw, Game *game)
    {
        if (self->state == ENTITY_DEAD)
        {
            return;
        }
        char health_str[32];
        snprintf(health_str, sizeof(health_str), "%.0f", (double)self->health);

        // skip if enemy is out of the screen
        if (self->position.x + self->size.x < game->pos.x || self->position.x > game->pos.x + game->size.x ||
            self->position.y + self->size.y < game->pos.y || self->position.y > game->pos.y + game->size.y)
        {
            return;
        }

        // draw enemy health just above the enemy (sprite facing is handled in the
        // level's render pass now, so this overlay can run after all sprites).
        draw_username(game, self->position, health_str, 12);
    }

    int last_button = -1;
    // Enemy collision function: when this is called, the enemy has collided with another entity
    static void enemy_collision(Entity *self, Entity *other, Game *game)
    {
        if (strcmp(other->name, "Player") == 0)
        {
            // Get positions of the enemy and the player
            Vector enemy_pos = self->position;
            Vector player_pos = other->position;

            // Determine if the enemy is facing the player or player is facing the enemy
            bool enemy_is_facing_player = false;
            bool player_is_facing_enemy = false;

            if (self->direction == ENTITY_LEFT && player_pos.x < enemy_pos.x ||
                self->direction == ENTITY_RIGHT && player_pos.x > enemy_pos.x ||
                self->direction == ENTITY_UP && player_pos.y < enemy_pos.y ||
                self->direction == ENTITY_DOWN && player_pos.y > enemy_pos.y)
            {
                enemy_is_facing_player = true;
            }
            if (other->direction == ENTITY_LEFT && enemy_pos.x < player_pos.x ||
                other->direction == ENTITY_RIGHT && enemy_pos.x > player_pos.x ||
                other->direction == ENTITY_UP && enemy_pos.y < player_pos.y ||
                other->direction == ENTITY_DOWN && enemy_pos.y > player_pos.y)
            {
                player_is_facing_enemy = true;
            }

            // Handle Player Attacking Enemy (Press OK, facing enemy, and enemy not facing player)
            // we need to store the last button pressed to prevent multiple attacks
            if (player_is_facing_enemy && last_button == BUTTON_CENTER && !enemy_is_facing_player)
            {
                // Reset last button
                last_button = -1;

                // check if enough time has passed since the last attack
                if (other->elapsed_attack_timer >= other->attack_timer)
                {
                    // Reset player's elapsed attack timer
                    other->elapsed_attack_timer = 0;
                    self->elapsed_attack_timer = 0; // Reset enemy's attack timer to block enemy attack

                    // Increase XP by the enemy's strength
                    other->xp += self->strength;

                    // Increase health by 10% of the enemy's strength
                    other->health += self->strength * 0.1;

                    // check max health
                    if (other->health > 100)
                    {
                        other->health = 100;
                    }

                    // Decrease enemy health by player strength
                    self->health -= other->strength;

                    // check if enemy is dead
                    if (self->health > 0)
                    {
                        self->state = ENTITY_ATTACKED;
                        self->elapsed_move_timer = 0;
                        self->position_changed = true;
                        self->position_set(self->old_position);
                    }
                }
            }
            // Handle Enemy Attacking Player (enemy facing player)
            else if (enemy_is_facing_player)
            {
                // check if enough time has passed since the last attack
                if (self->elapsed_attack_timer >= self->attack_timer)
                {
                    // Reset enemy's elapsed attack timer
                    self->elapsed_attack_timer = 0;

                    // Decrease player health by enemy strength
                    other->health -= self->strength;

                    // check if player is dead
                    if (other->health > 0)
                    {
                        other->state = ENTITY_ATTACKED;
                        other->position_set(other->old_position);
                    }
                }
            }

            // check if player is dead
            if (other->health <= 0)
            {
                other->state = ENTITY_DEAD;
                other->position = other->start_position;
                other->health = other->max_health;
                other->position_set(other->start_position);
            }

            // check if enemy is dead
            if (self->health <= 0)
            {
                self->state = ENTITY_DEAD;
                self->position = Vector(-100, -100);
                self->health = 0;
                self->elapsed_move_timer = 0;
                self->position_set(self->position);
            }
        }
    }

    static void enemy_spawn(
        Level *level,
        const char *name,
        Vector direction,
        Vector start_position,
        Vector end_position,
        float move_timer,
        float elapsed_move_timer,
        float speed,
        float attack_timer,
        float elapsed_attack_timer,
        float strength,
        float health)
    {
        // Get the enemy context
        PlayerContext enemy_left = player_context_get(name, true);
        PlayerContext enemy_right = player_context_get(name, false);

        // check if enemy context is valid
        if (enemy_left.data != NULL && enemy_right.data != NULL)
        {
            // Create the enemy entity
            Entity *entity = new Entity(level->getBoard(), name, ENTITY_ENEMY, start_position, enemy_left.size, enemy_left.data, enemy_left.data, enemy_right.data, NULL, NULL, enemy_update, enemy_render, enemy_collision, true, true);
            entity->ink_color = 0xF800; // FlipWorld colour port: red foes
            entity->direction = direction;
            entity->start_position = start_position;
            entity->end_position = end_position;
            entity->move_timer = move_timer;
            entity->elapsed_move_timer = elapsed_move_timer;
            entity->speed = speed;
            entity->attack_timer = attack_timer;
            entity->elapsed_attack_timer = elapsed_attack_timer;
            entity->strength = strength;
            entity->health = health;
            entity->max_health = health;

            // Add the enemy entity to the level
            level->entity_add(entity);
        }
    }

    void enemy_spawn_json(Level *level, const char *json)
    {
        // Parse the json
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);

        // Check for errors
        if (error)
        {
            return;
        }

        // Loop through the json data
        int index = 0;
        while (doc["enemy_data"][index])
        {
            // Get the enemy data
            const char *id = doc["enemy_data"][index]["id"];
            Vector start_position = Vector(doc["enemy_data"][index]["start_position"]["x"], doc["enemy_data"][index]["start_position"]["y"]);
            Vector end_position = Vector(doc["enemy_data"][index]["end_position"]["x"], doc["enemy_data"][index]["end_position"]["y"]);
            float move_timer = doc["enemy_data"][index]["move_timer"];
            float speed = doc["enemy_data"][index]["speed"];
            float attack_timer = doc["enemy_data"][index]["attack_timer"];
            float strength = doc["enemy_data"][index]["strength"];
            float health = doc["enemy_data"][index]["health"];

            // Spawn the enemy entity
            enemy_spawn(level, id, ENTITY_LEFT, start_position, end_position, move_timer, 0, speed, attack_timer, 0, strength, health);

            // Increment the index
            index++;
        }
    }

    // Update player stats based on XP using iterative method
    static int get_player_level_iterative(uint32_t xp)
    {
        int level = 1;
        uint32_t xp_required = 100; // Base XP for level 2

        while (level < 100 && xp >= xp_required) // Maximum level supported
        {
            level++;
            xp_required = (uint32_t)(xp_required * 1.5); // 1.5 growth factor per level
        }

        return level;
    }

    static void update_stats(Entity *player)
    {
        // Determine the player's level based on XP
        player->level = get_player_level_iterative(player->xp);

        // Update strength and max health based on the new level
        player->strength = 10 + (player->level * 1);           // 1 strength per level
        player->max_health = 100 + ((player->level - 1) * 10); // 10 health per level
    }

    static void player_update(Entity *self, Game *game)
    {
        // Apply health regeneration
        self->elapsed_health_regen += 1.0 / 30; // 30 frames per second
        if (self->elapsed_health_regen >= 1 && self->health < self->max_health)
        {
            self->health += self->health_regen;
            self->elapsed_health_regen = 0;
            if (self->health > self->max_health)
            {
                self->health = self->max_health;
            }
        }

        // Increment the elapsed_attack_timer for the player
        self->elapsed_attack_timer += 1.0 / 30; // 30 frames per second

        // update plyer traits
        update_stats(self);

        Vector oldPos = self->position;
        Vector newPos = oldPos;

        // Edge-zone nav (the original layout): the touch's screen position picks the
        // move. Top/bottom fifth = up/down, left/right quarter = left/right, and a
        // CORNER is in two zones at once → diagonal (e.g. bottom+left = down-left).
        // The central rectangle (no edge) is the attack zone (its original size).
        const float STEP = 6;
        TouchInput *touch = game->input_manager ? game->input_manager->getTouch() : nullptr;
        if (touch && touch->isPressed())
        {
            float w = game->size.x, h = game->size.y;
            float px = touch->x(), py = touch->y();
            float mx = 0, my = 0;
            if (py < h / 5) my -= 1;          // top edge    → up
            else if (py > h * 4 / 5) my += 1; // bottom edge → down
            if (px < w / 4) mx -= 1;          // left edge   → left
            else if (px > w * 3 / 4) mx += 1; // right edge  → right

            if (mx != 0 || my != 0)
            {
                float mag = sqrtf(mx * mx + my * my); // normalise so diagonals aren't faster
                newPos.x += (mx / mag) * STEP;
                newPos.y += (my / mag) * STEP;
                if (mx < 0) { self->direction = ENTITY_LEFT;  last_button = BUTTON_LEFT; }
                else if (mx > 0) { self->direction = ENTITY_RIGHT; last_button = BUTTON_RIGHT; }
                else if (my < 0) { self->direction = ENTITY_UP; last_button = BUTTON_UP; }
                else { self->direction = ENTITY_DOWN; last_button = BUTTON_DOWN; }
            }
            else
            {
                last_button = BUTTON_CENTER; // central rectangle = attack
            }
        }

        // reset input
        game->input = -1;

        // Tentatively set new position
        self->position_set(newPos);

        // check if new position is within the level boundaries
        if (newPos.x < 0 || newPos.x + self->size.x > game->current_level->size.x ||
            newPos.y < 0 || newPos.y + self->size.y > game->current_level->size.y)
        {
            // restore old position
            self->position_set(oldPos);
        }

        // Store the current camera position before updating
        game->old_pos = game->pos;

        // Camera follows the player horizontally only; it is LOCKED vertically so the
        // map never scrolls up/down. The fixed y aligns the BOTTOM of the map with the
        // bottom of the display (camera_y = world height - screen height).
        float camera_x = self->position.x - (game->size.x / 2);
        float max_cam_x = game->current_level->size.x - game->size.x;
        if (max_cam_x < 0) max_cam_x = 0;
        camera_x = constrain(camera_x, 0, max_cam_x);

        float camera_y = game->current_level->size.y - game->size.y; // bottom-aligned, fixed
        game->pos = Vector(camera_x, camera_y);

        // (Facing sprite is chosen at render time in Level::render, into a local, so
        // ent->sprite / sprite_left / sprite_right stay distinct and ~Entity can free
        // each exactly once — reassigning here would double-free on teardown.)
    }

    // Draw the user stats (health, xp, and level) as a fixed HUD.
    static void draw_user_stats(Entity *self, Vector pos, Game *game)
    {
        const int ROW = 18; // spacing between rows

        // black backing box so the text stays readable over the world
        game->draw->display->fillRect(pos.x - 2, pos.y - 3, 60, ROW * 3 + 6, TFT_BLACK);

        char health[32];
        char xp[32];
        char level[32];

        snprintf(health, sizeof(health), "HP : %.0f", (double)self->health);
        snprintf(level, sizeof(level), "LVL: %.0f", (double)self->level);

        if (self->xp < 10000)
            snprintf(xp, sizeof(xp), "XP : %.0f", (double)self->xp);
        else
            snprintf(xp, sizeof(xp), "XP : %.0fK", (double)self->xp / 1000);

        // draw rows with generous spacing (green HUD — distinct from red/white labels)
        const uint16_t STAT_COL = 0x07E0; // green
        game->draw->text(Vector(pos.x, pos.y), health, STAT_COL);
        game->draw->text(Vector(pos.x, pos.y + ROW), xp, STAT_COL);
        game->draw->text(Vector(pos.x, pos.y + ROW * 2), level, STAT_COL);
    }

    static void player_render(Entity *self, Draw *draw, Game *game)
    {
        // Player name floated well above the sprite (28px) so it clears a nearby
        // enemy's health label while attacking. White so it's distinct from the red
        // enemy-health labels and the green stat HUD.
        draw_username(game, self->position, fw_player_name, 28, 0xFFFF);
        draw_user_stats(self, Vector(5, 34), game);    // fixed HUD at top (below the header)
    }

    void player_spawn(Level *level, const char *name, Vector position)
    {
        // Get the player context
        PlayerContext player_left = player_context_get(name, true);
        PlayerContext player_right = player_context_get(name, false);

        // check if player context is valid
        if (player_left.data != NULL && player_right.data != NULL)
        {
            // Create the player entity
            Entity *player = new Entity(level->getBoard(), "Player", ENTITY_PLAYER, position, player_left.size, player_left.data, player_left.data, player_right.data, NULL, NULL, player_update, player_render, NULL, true, true);
            player->ink_color = 0x1C9F; // FlipWorld colour port: bright blue hero (DodgerBlue, pops on black)
            player->is_player = true;   // shared across levels; Level::clear must not delete it
            player->level = 1;
            player->health = 100;
            player->max_health = 100;
            player->strength = 10;
            player->attack_timer = 0.2; // short cooldown so tapping/holding centre attacks repeatedly
            player->health_regen = 1;
            level->entity_add(player);
        }
    }
    void game_stop()
    {
        // nothing to do here
    }
} // namespace FlipWorld