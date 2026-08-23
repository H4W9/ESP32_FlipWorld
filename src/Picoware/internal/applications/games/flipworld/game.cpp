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
        if (strcmp(name, "dragon") == 0) // sprite art faces the opposite way, so swap L/R
            return {name, is_left ? enemy_right_dragon_59x44px : enemy_left_dragon_59x44px, Vector(59, 44)};

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

    // ── Boss dragon ───────────────────────────────────────────────────────────
    // Patrols the whole map horizontally (edge buffer), drifts only slightly up/down
    // from its path, and lobs a fireball at the player from a reactable distance.
    // Single boss, so its fireball state lives in module statics.
    static float g_dragonBaseY = 0;   // the horizontal path height
    static float g_dragonPhase = 0;   // vertical-wobble phase
    static float g_dragonFireCd = 0;  // seconds until it may fire again
    static bool  g_fbActive = false;  // is a fireball in flight
    static float g_fbX = 0, g_fbY = 0, g_fbDX = 0, g_fbDY = 0;
    static int   g_dragonTurns = 0;   // how many times it has turned (max 3)
    // Fractions of max health remaining at which the dragon turns to face the player —
    // one per third of health lost, so it flips exactly 3 times over the fight.
    static const float DRAGON_TURN_AT[3] = {0.667f, 0.334f, 0.08f};

    static void dragon_update(Entity *self, Game *game)
    {
        if (self->state == ENTITY_DEAD) { g_fbActive = false; return; }
        const float dt = 1.0f / 30;
        Level *lvl = game->current_level;
        if (!lvl) return;

        // find the player
        Entity *player = nullptr;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->is_player) { player = e; break; }
        }

        // Turn to face the player only at each third of health lost (3 turns total).
        // Between turns the dragon keeps its facing, so you can safely attack it from
        // behind (where it can't throw fire — see the front-arc check below).
        while (g_dragonTurns < 3 && self->health <= self->max_health * DRAGON_TURN_AT[g_dragonTurns])
        {
            self->direction = (self->direction.x < 0) ? ENTITY_RIGHT : ENTITY_LEFT; // flip facing
            g_dragonTurns++;
        }

        // Move in the direction it FACES, travelling the whole map, then parking at the
        // edge (facing only changes on a turn, so movement follows facing — it advances
        // to an edge and waits there until its next turn).
        const float buffer = 40;
        float minX = buffer, maxX = lvl->size.x - buffer - self->size.x;
        if (maxX < minX) maxX = minX;
        float step = self->speed * dt;
        float nx = self->position.x + (self->direction.x >= 0 ? step : -step);
        if (nx < minX) nx = minX;
        else if (nx > maxX) nx = maxX;

        // only a slight vertical deviation from the path
        g_dragonPhase += dt;
        float ny = g_dragonBaseY + sinf(g_dragonPhase * 1.6f) * 16.0f;
        self->position_set(Vector(nx, ny));

        // Fireball: only toward the 180° arc IN FRONT of the dragon (its facing side),
        // and only from a reasonable distance so the player can react.
        if (g_dragonFireCd > 0) g_dragonFireCd -= dt;
        float cx = self->position.x + self->size.x / 2, cy = self->position.y + self->size.y / 2;
        if (!g_fbActive && g_dragonFireCd <= 0 && player)
        {
            float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
            float dx = px - cx, dy = py - cy, dist = sqrtf(dx * dx + dy * dy);
            bool facingRight = self->direction.x >= 0;
            bool inFront = facingRight ? (px >= cx) : (px <= cx); // 180° front half-plane
            if (inFront && dist > 90 && dist < 420)
            {
                float sp = 2.6f;
                g_fbActive = true; g_fbX = cx; g_fbY = cy;
                g_fbDX = dx / dist * sp; g_fbDY = dy / dist * sp;
                g_dragonFireCd = 2.2f;
            }
        }
        if (g_fbActive)
        {
            g_fbX += g_fbDX; g_fbY += g_fbDY;
            if (g_fbX < 0 || g_fbY < 0 || g_fbX > lvl->size.x || g_fbY > lvl->size.y)
                g_fbActive = false;
            else if (player)
            {
                float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
                float ddx = g_fbX - px, ddy = g_fbY - py;
                if (ddx * ddx + ddy * ddy < 100) // ~10px hit radius
                {
                    player->health -= 25;
                    g_fbActive = false;
                    if (player->health <= 0)
                    {
                        player->state = ENTITY_DEAD;
                        player->health = player->max_health;
                        player->position = player->start_position;
                        player->position_set(player->start_position);
                    }
                    else
                        player->state = ENTITY_ATTACKED;
                }
            }
        }
    }

    static void dragon_render(Entity *self, Draw *draw, Game *game)
    {
        if (self->state == ENTITY_DEAD) return;
        char hs[24];
        snprintf(hs, sizeof(hs), "DRAGON %.0f", (double)self->health);
        draw_username(game, self->position, hs, (int)self->size.y + 4, 0xFD20);
        if (g_fbActive)
        {
            int sx = (int)(g_fbX - game->pos.x), sy = (int)(g_fbY - game->pos.y);
            game->draw->display->fillCircle(sx, sy, 4, 0xFD20); // orange fireball
            game->draw->display->fillCircle(sx, sy, 2, 0xFFE0); // hot core
        }
    }

    void dragon_spawn(Level *level)
    {
        PlayerContext dl = player_context_get("dragon", true);
        PlayerContext dr = player_context_get("dragon", false);
        if (dl.data == NULL || dr.data == NULL)
            return;
        Vector pos = Vector(360, 100);
        Entity *d = new Entity(level->getBoard(), "Dragon", ENTITY_ENEMY, pos, dl.size,
                               dl.data, dl.data, dr.data, NULL, NULL,
                               dragon_update, dragon_render, enemy_collision, true, true);
        d->ink_color = 0xFD20;      // orange boss (distinct from red foes)
        d->direction = ENTITY_LEFT; // starts facing left; player attacks from the right
        d->speed = 55;
        d->attack_timer = 0.8f;
        d->strength = 30;           // melee if the player closes in
        d->health = 1000;
        d->max_health = 1000;
        d->start_position = pos;
        d->end_position = pos;
        g_dragonBaseY = pos.y;
        g_dragonPhase = 0;
        g_dragonFireCd = 1.5f;      // brief grace before the first fireball
        g_fbActive = false;
        g_dragonTurns = 0;          // 3 turns available over the fight
        level->entity_add(d);
    }

    // ── Flyby (cameo) dragon ──────────────────────────────────────────────────
    // An UNDEFEATABLE dragon that streaks across the map: it makes N passes, throws
    // fire (at the player, or at a house which it sets alight), then leaves. It's an
    // ENTITY_NPC with no collision, so it can't be hit and never gates the map.
    static const PROGMEM uint8_t fx_dummy1x1px[1] = {0xFF}; // invisible sprite for effects
    static int   g_flyPasses = 0, g_flyMode = 0;   // mode 0 = fire at player, 1 = burn house
    static float g_flyDir = 1, g_flyY = 0, g_flyFireCd = 0, g_flyTX = 0, g_flyTY = 0;
    static bool  g_flyFbActive = false, g_flyFired = false, g_flyDone = false;
    static float g_flyFbX = 0, g_flyFbY = 0, g_flyFbDX = 0, g_flyFbDY = 0;

    static Entity *nearest_icon(Level *lvl, float tx, float ty)
    {
        Entity *best = nullptr; float bd = 1e18f;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->type == ENTITY_ICON)
            {
                float dx = e->position.x - tx, dy = e->position.y - ty, d = dx * dx + dy * dy;
                if (d < bd) { bd = d; best = e; }
            }
        }
        return best;
    }

    // Animated flames (for a burning house). A standalone persistent entity.
    static void fire_render(Entity *self, Draw *draw, Game *game)
    {
        int bx = (int)(self->position.x - game->pos.x);
        int by = (int)(self->position.y - game->pos.y);
        uint32_t t = millis();
        for (int i = 0; i < 6; i++)
        {
            int fx = bx + i * 8 + 3;
            int flick = (int)((sinf(t * 0.02f + i * 1.3f) * 0.5f + 0.5f) * 8);
            game->draw->display->fillCircle(fx, by - flick, 4, 0xFC00);     // orange flame
            game->draw->display->fillCircle(fx, by - 4 - flick, 2, 0xFFE0); // hot yellow tip
        }
    }
    // Fire burns the player while they stand in it (proximity DoT).
    static void fire_update(Entity *self, Game *game)
    {
        const float dt = 1.0f / 30;
        Level *lvl = game->current_level;
        if (!lvl) return;
        self->elapsed_attack_timer += dt;
        Entity *player = nullptr;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->is_player) { player = e; break; }
        }
        if (!player) return;
        float fx = self->position.x + 24, fy = self->position.y; // centre of the flames
        float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
        float dx = px - fx, dy = py - fy;
        if (dx * dx + dy * dy < 26 * 26 && self->elapsed_attack_timer >= 0.5f) // too close → singe
        {
            self->elapsed_attack_timer = 0;
            player->health -= 8;
            if (player->health <= 0)
            {
                player->state = ENTITY_DEAD; player->health = player->max_health;
                player->position = player->start_position; player->position_set(player->start_position);
            }
            else
                player->state = ENTITY_ATTACKED;
        }
    }
    static void spawn_fire(Level *level, Vector pos)
    {
        Entity *f = new Entity(level->getBoard(), "fire", ENTITY_ICON, pos, Vector(1, 1),
                               fx_dummy1x1px, NULL, NULL, NULL, NULL, fire_update, fire_render, NULL, true, true);
        level->entity_add(f);
    }

    static void flyby_update(Entity *self, Game *game)
    {
        const float dt = 1.0f / 30;
        Level *lvl = game->current_level;
        if (!lvl) return;
        if (g_flyDone) { self->is_active = false; g_flyFbActive = false; return; }

        Entity *player = nullptr;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->is_player) { player = e; break; }
        }

        // streak across the map
        self->direction = (g_flyDir < 0) ? ENTITY_LEFT : ENTITY_RIGHT;
        float nx = self->position.x + g_flyDir * self->speed * dt;
        float ny = g_flyY + sinf(millis() * 0.004f) * 8.0f;
        self->position_set(Vector(nx, ny));
        float cx = nx + self->size.x / 2, cy = ny + self->size.y / 2;

        // throw fire
        if (g_flyFireCd > 0) g_flyFireCd -= dt;
        if (!g_flyFbActive)
        {
            if (g_flyMode == 0 && player && g_flyFireCd <= 0)
            {
                float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
                float dx = px - cx, dy = py - cy, dd = sqrtf(dx * dx + dy * dy);
                if (dd > 1) { g_flyFbActive = true; g_flyFbX = cx; g_flyFbY = cy; float sp = 2.6f; g_flyFbDX = dx / dd * sp; g_flyFbDY = dy / dd * sp; g_flyFireCd = 1.3f; }
            }
            else if (g_flyMode == 1 && !g_flyFired && fabsf(cx - g_flyTX) < 160)
            {
                float dx = g_flyTX - cx, dy = g_flyTY - cy, dd = sqrtf(dx * dx + dy * dy);
                if (dd > 1) { g_flyFbActive = true; g_flyFbX = cx; g_flyFbY = cy; float sp = 3.0f; g_flyFbDX = dx / dd * sp; g_flyFbDY = dy / dd * sp; g_flyFired = true; }
            }
        }
        if (g_flyFbActive)
        {
            g_flyFbX += g_flyFbDX; g_flyFbY += g_flyFbDY;
            bool off = (g_flyFbX < 0 || g_flyFbY < 0 || g_flyFbX > lvl->size.x || g_flyFbY > lvl->size.y);
            if (g_flyMode == 0 && player)
            {
                float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
                float ex = g_flyFbX - px, ey = g_flyFbY - py;
                if (ex * ex + ey * ey < 100)
                {
                    player->health -= 25; g_flyFbActive = false;
                    if (player->health <= 0)
                    {
                        player->state = ENTITY_DEAD; player->health = player->max_health;
                        player->position = player->start_position; player->position_set(player->start_position);
                    }
                    else player->state = ENTITY_ATTACKED;
                }
                else if (off) g_flyFbActive = false;
            }
            else if (g_flyMode == 1)
            {
                float ex = g_flyFbX - g_flyTX, ey = g_flyFbY - g_flyTY;
                if (ex * ex + ey * ey < 220) // reached the house → set it ablaze
                {
                    g_flyFbActive = false;
                    Entity *h = nearest_icon(lvl, g_flyTX, g_flyTY);
                    if (h) h->ink_color = 0xF800;              // charred/burning red
                    spawn_fire(lvl, Vector(g_flyTX - 20, g_flyTY + 8));
                }
                else if (off) g_flyFbActive = false;
            }
        }

        // count passes; leave undefeated after the last one
        float margin = self->size.x + 20;
        if ((g_flyDir > 0 && nx > lvl->size.x + 20) || (g_flyDir < 0 && nx < -margin))
        {
            g_flyPasses--;
            if (g_flyPasses <= 0) { g_flyDone = true; self->is_active = false; g_flyFbActive = false; }
            else { g_flyDir = -g_flyDir; g_flyFired = false; }
        }
    }

    static void flyby_render(Entity *self, Draw *draw, Game *game)
    {
        if (g_flyFbActive)
        {
            int sx = (int)(g_flyFbX - game->pos.x), sy = (int)(g_flyFbY - game->pos.y);
            game->draw->display->fillCircle(sx, sy, 4, 0xFD20);
            game->draw->display->fillCircle(sx, sy, 2, 0xFFE0);
        }
    }

    void flyby_dragon_spawn(Level *level, int passes, int mode, float targetX, float targetY)
    {
        PlayerContext dl = player_context_get("dragon", true);
        PlayerContext dr = player_context_get("dragon", false);
        if (dl.data == NULL || dr.data == NULL)
            return;
        Vector pos = Vector(-60, 70);   // start off the left edge, fly in
        Entity *d = new Entity(level->getBoard(), "FlybyDragon", ENTITY_NPC, pos, dl.size,
                               dl.data, dl.data, dr.data, NULL, NULL,
                               flyby_update, flyby_render, NULL, true, true); // no collision → undefeatable
        d->ink_color = 0xFD20;
        d->direction = ENTITY_RIGHT;
        d->speed = 90;                  // fast streak
        d->health = 1; d->max_health = 1;
        g_flyPasses = passes; g_flyMode = mode; g_flyDir = 1; g_flyY = pos.y;
        g_flyFireCd = 0.6f; g_flyFbActive = false; g_flyFired = false; g_flyDone = false;
        g_flyTX = targetX; g_flyTY = targetY;
        level->entity_add(d);
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
        float mx = 0, my = 0;
        bool centreTap = false;
        TouchInput *touch = game->input_manager ? game->input_manager->getTouch() : nullptr;
        if (touch && touch->isPressed())
        {
            float w = game->size.x, h = game->size.y;
            float px = touch->x(), py = touch->y();
            if (py < h / 5) my -= 1;          // top edge    → up
            else if (py > h * 4 / 5) my += 1; // bottom edge → down
            if (px < w / 4) mx -= 1;          // left edge   → left
            else if (px > w * 3 / 4) mx += 1; // right edge  → right
            if (mx == 0 && my == 0) centreTap = true; // central rectangle
        }

        // The Frozen Lake map is slippery: input accelerates a carried velocity and
        // friction lets the player glide to a stop, instead of moving instantly.
        bool icy = game->current_level && strcmp(game->current_level->name, "Frozen Lake") == 0;
        if (icy)
        {
            const float accel = 1.6f, friction = 0.90f;
            if (mx != 0 || my != 0)
            {
                float mag = sqrtf(mx * mx + my * my);
                self->slide_vx += (mx / mag) * accel;
                self->slide_vy += (my / mag) * accel;
            }
            self->slide_vx *= friction;
            self->slide_vy *= friction;
            float sp = sqrtf(self->slide_vx * self->slide_vx + self->slide_vy * self->slide_vy);
            if (sp > STEP) { self->slide_vx = self->slide_vx / sp * STEP; self->slide_vy = self->slide_vy / sp * STEP; }
            else if (sp < 0.05f) { self->slide_vx = 0; self->slide_vy = 0; }
            newPos.x += self->slide_vx;
            newPos.y += self->slide_vy;
        }
        else
        {
            self->slide_vx = 0;
            self->slide_vy = 0;
            if (mx != 0 || my != 0)
            {
                float mag = sqrtf(mx * mx + my * my); // normalise so diagonals aren't faster
                newPos.x += (mx / mag) * STEP;
                newPos.y += (my / mag) * STEP;
            }
        }

        // Facing follows actual motion (velocity on ice, input otherwise); a centre
        // tap is the attack.
        float fvx = icy ? self->slide_vx : mx;
        float fvy = icy ? self->slide_vy : my;
        if (fabsf(fvx) > 0.05f || fabsf(fvy) > 0.05f)
        {
            if (fabsf(fvx) >= fabsf(fvy)) { self->direction = fvx < 0 ? ENTITY_LEFT : ENTITY_RIGHT; last_button = fvx < 0 ? BUTTON_LEFT : BUTTON_RIGHT; }
            else { self->direction = fvy < 0 ? ENTITY_UP : ENTITY_DOWN; last_button = fvy < 0 ? BUTTON_UP : BUTTON_DOWN; }
        }
        if (centreTap) last_button = BUTTON_CENTER;

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

        // Camera follows the player, centring on them and clamping to the map bounds.
        float max_cam_x = game->current_level->size.x - game->size.x;
        if (max_cam_x < 0) max_cam_x = 0;
        float camera_x = constrain(self->position.x - game->size.x / 2, 0, max_cam_x);

        // Vertical: if the map is TALLER than the viewport (e.g. the V8's 320px panel
        // vs a 384px map), follow the player up/down smoothly, exactly like the
        // horizontal axis. If the map is shorter than the viewport (e.g. the Pancake's
        // 480px panel), there's nothing to scroll — lock the camera so the map's bottom
        // aligns with the display bottom (camera_y = map height - screen height, which
        // is negative and never jumps).
        float max_cam_y = game->current_level->size.y - game->size.y;
        float camera_y;
        if (max_cam_y <= 0)
            camera_y = max_cam_y; // map fits vertically → fixed, bottom-aligned (Pancake)
        else
            camera_y = constrain(self->position.y - game->size.y / 2, 0, max_cam_y); // follow (V8)

        game->pos = Vector(camera_x, camera_y);

        // (Facing sprite is chosen at render time in Level::render, into a local, so
        // ent->sprite / sprite_left / sprite_right stay distinct and ~Entity can free
        // each exactly once — reassigning here would double-free on teardown.)
    }

    // Draw the user stats (health, xp, and level) as a fixed HUD.
    static void draw_user_stats(Entity *self, Vector pos, Game *game)
    {
        const uint16_t STAT_COL = 0x07E0; // green
        const int ROW = 16;

        // XP progress within the current level: find the XP threshold the current level
        // started at (base) and the one for the next level (next) — same 100 * 1.5^n
        // curve the game levels on — so we can show "into / needed" and a bar.
        float xp = self->xp;
        int lvl = 1;
        float req = 100.0f, base = 0.0f;
        while (lvl < 100 && xp >= req) { lvl++; base = req; req *= 1.5f; }
        float into = xp - base, span = req - base;
        float frac = (span > 0) ? into / span : 0.0f;
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;

        const int boxW = 104, boxH = ROW * 3 + 20;
        game->draw->display->fillRect(pos.x - 2, pos.y - 3, boxW, boxH, TFT_BLACK);

        char health[24], level[24], xpS[32];
        snprintf(health, sizeof(health), "HP %.0f/%.0f", (double)self->health, (double)self->max_health);
        snprintf(level, sizeof(level), "LVL %d", lvl);
        snprintf(xpS, sizeof(xpS), "XP %.0f/%.0f", (double)into, (double)span);

        game->draw->text(Vector(pos.x, pos.y), health, STAT_COL);
        game->draw->text(Vector(pos.x, pos.y + ROW), level, STAT_COL);

        // XP bar toward the next level, with the into/needed value under it.
        int barX = pos.x, barY = pos.y + ROW * 2 + 2, barW = boxW - 6, barH = 6;
        game->draw->display->drawRect(barX, barY, barW, barH, STAT_COL);
        int fillW = (int)((barW - 2) * frac);
        if (fillW > 0) game->draw->display->fillRect(barX + 1, barY + 1, fillW, barH - 2, STAT_COL);
        game->draw->text(Vector(pos.x, barY + barH + 1), xpS, STAT_COL);
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