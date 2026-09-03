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

    // Whether the floating health bar under the player's name is shown. Toggled by
    // tapping the HP line of the stats HUD in-game; the launcher persists it across
    // sessions. The detailed stats HUD itself is always shown.
    static bool g_healthBar = true;
    static bool g_statsTapHeld = false; // edge-detect the toggle tap
    void fw_set_health_bar(bool on) { g_healthBar = on; }
    bool fw_get_health_bar() { return g_healthBar; }

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

        // Frozen solid (a ghost-boss ice-ball): the enemy can't move for the duration,
        // but it can still be attacked and killed. Timer ticks down here.
        if (self->frozen > 0)
        {
            self->frozen -= 1.0f / 30;
            if (self->frozen < 0) self->frozen = 0;
            self->position_set(self->position);
            return;
        }

        // On fire (hit by a fireball): burn down rapidly, then die in a puff.
        if (self->on_fire > 0)
        {
            self->health -= 6; // ~180 hp/sec at 30fps → the health bar visibly plummets
            if (self->health <= 0)
            {
                self->state = ENTITY_DEAD;
                self->health = 0;
                self->on_fire = 0;
                self->position = Vector(-100, -100);
                self->position_set(self->position);
                return;
            }
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
        int len  = (int)strlen(username);
        int boxW = len * 5 + 4;                       // actual backing-box width
        int sx   = (int)(pos.x - game->pos.x) - (len * 2);
        int sy   = (int)(pos.y - game->pos.y) - yOffset;

        // Cull the whole label if any part of the box would fall off an edge.
        // (Culling on the box's true bounds — not a rough margin — so an overflowing
        // label can never wrap around to the far side of the screen.)
        if (sx < 0 || sx + boxW > game->size.x || sy < 0 || sy + 10 > game->size.y)
        {
            return;
        }

        // Turn OFF text wrap on the actual print target (canvas sprite if present,
        // else the raw panel). TFT_eSPI wraps by default: when a label runs past the
        // right edge the tail jumps to x=0 — the "wrap to the other side" the labels
        // showed. With wrap off the overflow is simply clipped at the edge instead.
        if (game->draw->display->getCanvas())
            game->draw->display->getCanvas()->setTextWrap(false, false);
        else if (game->draw->display->getTFT())
            game->draw->display->getTFT()->setTextWrap(false, false);

        // black backing box, then the text on top
        game->draw->display->fillRect(sx, sy, boxW, 10, TFT_BLACK);
        game->draw->text(Vector(sx, sy), username, color);
    }

    // A little health bar that floats under the player's name. Green → yellow → red as
    // HP drops; centred on the player like the name, and off-screen-culled the same way.
    static void draw_player_healthbar(Game *game, Vector pos, float health, float maxHealth, int yOffset)
    {
        if (maxHealth <= 0) return;
        float frac = health / maxHealth;
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        const int W = 28, H = 4;
        int sx = (int)(pos.x - game->pos.x) - W / 2;
        int sy = (int)(pos.y - game->pos.y) - yOffset;
        if (sx < 0 || sx + W > game->size.x || sy < 0 || sy > game->size.y)
            return;
        uint16_t col = (frac > 0.5f) ? 0x07E0 : (frac > 0.25f) ? 0xFFE0 : 0xF800;
        game->draw->display->fillRect(sx - 1, sy - 1, W + 2, H + 2, TFT_BLACK); // backing
        game->draw->display->drawRect(sx, sy, W, H, 0xFFFF);                    // white frame
        int fillW = (int)((W - 2) * frac);
        if (fillW > 0) game->draw->display->fillRect(sx + 1, sy + 1, fillW, H - 2, col);
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

        // if ablaze, engulf the enemy in flickering flames
        if (self->on_fire > 0)
        {
            int bx = (int)(self->position.x - game->pos.x);
            int by = (int)(self->position.y - game->pos.y);
            int w = (int)self->size.x, h = (int)self->size.y;
            uint32_t t = millis();
            for (int fx = 0; fx <= w; fx += 5)
            {
                int flick = (int)((sinf(t * 0.03f + fx) * 0.5f + 0.5f) * 7);
                game->draw->display->fillCircle(bx + fx, by + h - flick - 2, 3, 0xFC00);     // orange
                game->draw->display->fillCircle(bx + fx, by + h - flick - 6, 2, 0xFFE0);     // hot core
            }
        }

        // Frozen: sheath the sprite in a translucent blue-white frost so it reads as
        // iced-over (a few scattered ice specks over the body).
        if (self->frozen > 0)
        {
            int bx = (int)(self->position.x - game->pos.x);
            int by = (int)(self->position.y - game->pos.y);
            int w = (int)self->size.x, h = (int)self->size.y;
            game->draw->display->drawRect(bx - 1, by - 1, w + 2, h + 2, 0xE73F); // icy frame
            for (int fy = 1; fy < h; fy += 3)
                for (int fx = ((fy >> 1) & 1); fx < w; fx += 3)
                    game->draw->display->drawPixel(bx + fx, by + fy, 0xFFFF);    // frost specks
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

                    // check max health (scales with level, not a flat 100)
                    if (other->health > other->max_health)
                    {
                        other->health = other->max_health;
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
    // Up to 5 fireballs may be in flight at once — the dragon can loose the next before
    // the previous ones land.
    static const int DRAGON_FB_MAX = 5;
    static bool  g_fbAct[DRAGON_FB_MAX] = {false, false, false, false, false};
    static float g_fbX[DRAGON_FB_MAX], g_fbY[DRAGON_FB_MAX], g_fbDX[DRAGON_FB_MAX], g_fbDY[DRAGON_FB_MAX];
    static int   g_dragonTurns = 0;   // defensive turns used (max 3)
    static float g_dragonMoveDir = -1; // patrol direction (+1 right / -1 left)
    static float g_dragonMeleeCd = 0; // seconds until it may bite the player again
    // Fractions of max health remaining at which the dragon turns to face the player —
    // one per third of health lost, so it flips exactly 3 times over the fight.
    static const float DRAGON_TURN_AT[3] = {0.667f, 0.334f, 0.08f};

    // Fire only within the dragon's attack arc (clock positions, screen coords with
    // +x = 3 o'clock/0°, +y = 6 o'clock/90°): right-facing fires 1→7 o'clock
    // ([-60°,120°]); left-facing mirrors it at 5→11 o'clock (>=60° or <=-120°).
    static bool dragon_fire_arc(bool facingRight, float dx, float dy)
    {
        float ang = atan2f(dy, dx) * 57.29578f; // degrees in [-180,180]
        return facingRight ? (ang >= -60.0f && ang <= 120.0f)
                           : (ang >= 60.0f || ang <= -120.0f);
    }

    // A fireball sets any (non-excluded) living, not-yet-burning enemy it passes over
    // ablaze — it then burns down rapidly (see enemy_update / enemy_render).
    static bool fb_hit_enemy(Level *lvl, float x, float y, Entity *exclude)
    {
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (!e || e == exclude || e->type != ENTITY_ENEMY || e->state == ENTITY_DEAD || e->on_fire > 0)
                continue;
            if (e->size.x >= 24)
                continue; // bosses (big sprites) are fire-immune — they can't be torched to death
            float ex = e->position.x + e->size.x / 2 - x, ey = e->position.y + e->size.y / 2 - y;
            if (ex * ex + ey * ey < 16 * 16)
            {
                e->on_fire = 1; // catches fire; enemy_update drains it down
                return true;
            }
        }
        return false;
    }

    static void dragon_update(Entity *self, Game *game)
    {
        if (self->state == ENTITY_DEAD) { for (int i = 0; i < DRAGON_FB_MAX; i++) g_fbAct[i] = false; return; }
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

        // Defensive turns: as you wear it down (each third of health lost, up to 3
        // times) it wheels back TOWARD the player to attack mid-flight. After the 3rd
        // it just keeps patrolling, so you can finish it from behind.
        while (g_dragonTurns < 3 && self->health <= self->max_health * DRAGON_TURN_AT[g_dragonTurns])
        {
            if (player) g_dragonMoveDir = (player->position.x < self->position.x) ? -1.0f : 1.0f;
            g_dragonTurns++;
        }

        // ALWAYS fly back and forth across the map, bouncing off the edges, until dead.
        const float buffer = 40;
        float minX = buffer, maxX = lvl->size.x - buffer - self->size.x;
        if (maxX < minX) maxX = minX;
        float step = self->speed * dt;
        float nx = self->position.x + g_dragonMoveDir * step;
        if (nx <= minX) { nx = minX; g_dragonMoveDir = 1.0f; }
        else if (nx >= maxX) { nx = maxX; g_dragonMoveDir = -1.0f; }
        self->direction = (g_dragonMoveDir < 0) ? ENTITY_LEFT : ENTITY_RIGHT; // face travel

        // Aggression: the path height itself slowly homes toward the player's height
        // so the dragon keeps closing on you instead of flying a fixed lane. It still
        // only deviates gently, so it reads as a swooping patrol rather than a chase.
        if (player)
        {
            float wantY = player->position.y + player->size.y / 2 - self->size.y / 2;
            g_dragonBaseY += (wantY - g_dragonBaseY) * 0.04f; // ease toward the player
            if (g_dragonBaseY < 8) g_dragonBaseY = 8;
            float maxY = lvl->size.y - self->size.y - 8;
            if (g_dragonBaseY > maxY) g_dragonBaseY = maxY;
        }

        // only a slight vertical deviation from the (now player-seeking) path
        g_dragonPhase += dt;
        float ny = g_dragonBaseY + sinf(g_dragonPhase * 1.6f) * 16.0f;
        self->position_set(Vector(nx, ny));

        // Melee bite: when the player is right in front of the dragon's jaws it snaps
        // at them for heavy damage on a short cooldown — the fireball is for range, the
        // bite is for when you crowd it.
        if (g_dragonMeleeCd > 0) g_dragonMeleeCd -= dt;
        if (player && g_dragonMeleeCd <= 0)
        {
            bool facingRight = self->direction.x >= 0;
            float mouthX = facingRight ? (self->position.x + self->size.x - 2) : (self->position.x + 2);
            float mouthY = self->position.y + 37; // open jaw at the front-bottom of the head
            float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
            float ddx = px - mouthX, ddy = py - mouthY;
            // in front of the facing side, and within jaw reach
            bool inFront = facingRight ? (px >= self->position.x + self->size.x * 0.4f)
                                       : (px <= self->position.x + self->size.x * 0.6f);
            if (inFront && ddx * ddx + ddy * ddy < 52.0f * 52.0f)
            {
                player->health -= self->strength;
                g_dragonMeleeCd = 0.6f;
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

        // Fireball: only toward the 180° arc IN FRONT of the dragon (its facing side),
        // and only from a reasonable distance so the player can react.
        if (g_dragonFireCd > 0) g_dragonFireCd -= dt;
        float cx = self->position.x + self->size.x / 2, cy = self->position.y + self->size.y / 2;
        int fbCount = 0, freeSlot = -1;
        for (int i = 0; i < DRAGON_FB_MAX; i++)
        {
            if (g_fbAct[i]) fbCount++;
            else if (freeSlot < 0) freeSlot = i;
        }
        // Loose another fireball whenever the cooldown is up and fewer than 3 are aloft.
        if (freeSlot >= 0 && fbCount < DRAGON_FB_MAX && g_dragonFireCd <= 0 && player)
        {
            bool facingRight = self->direction.x >= 0;
            float mx = facingRight ? (self->position.x + self->size.x - 2) : (self->position.x + 2);
            float my = self->position.y + 37; // fire from the open jaw (front-bottom of the head)
            float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
            float dx = px - mx, dy = py - my, dist = sqrtf(dx * dx + dy * dy);
            bool inArc = dragon_fire_arc(facingRight, px - cx, py - cy); // clock-position arc
            if (inArc && dist > 30 && dist < 500)
            {
                float sp = 3.3f;
                g_fbAct[freeSlot] = true; g_fbX[freeSlot] = mx; g_fbY[freeSlot] = my;
                g_fbDX[freeSlot] = dx / dist * sp; g_fbDY[freeSlot] = dy / dist * sp;
                // Longer gap between shots so the (up to 5) fireballs spread out over the
                // dragon's patrol instead of streaming from one spot in one direction —
                // they come from varied angles and are harder to sidestep.
                g_dragonFireCd = 0.7f;
            }
        }
        for (int i = 0; i < DRAGON_FB_MAX; i++)
        {
            if (!g_fbAct[i]) continue;
            g_fbX[i] += g_fbDX[i]; g_fbY[i] += g_fbDY[i];
            if (g_fbX[i] < 0 || g_fbY[i] < 0 || g_fbX[i] > lvl->size.x || g_fbY[i] > lvl->size.y)
                g_fbAct[i] = false;
            else if (fb_hit_enemy(lvl, g_fbX[i], g_fbY[i], self)) // a fireball also incinerates foes
                g_fbAct[i] = false;
            else if (icon_ignite_at(lvl, g_fbX[i], g_fbY[i])) // …and sets houses/trees ablaze
                g_fbAct[i] = false;
            else if (player)
            {
                float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
                float ddx = g_fbX[i] - px, ddy = g_fbY[i] - py;
                if (ddx * ddx + ddy * ddy < 100) // ~10px hit radius
                {
                    player->health -= 25;
                    g_fbAct[i] = false;
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

    // Wing palette (kept as it was): yellow with a light red mottle.
    static const uint16_t DRAGON_WARM[3] = {0xFFE0 /*yellow*/, 0xFD20 /*orange*/, 0xF800 /*red*/};

    // Smooth head→tail colour for a fraction t (0 = head, 1 = tail): red → orange →
    // yellow across most of the body, then compressed to green only in the last tenth so
    // green stays confined to the tail tip. Interpolated in RGB, packed back to RGB565.
    static uint16_t dragon_grad(float t)
    {
        if (t < 0) t = 0; if (t > 1) t = 1;
        float r, g, b;
        if (t < 0.40f)      { float k = t / 0.40f;          r = 255;         g = 0 + k * 150; b = 0; }        // red → orange
        else if (t < 0.72f) { float k = (t - 0.40f) / 0.32f; r = 255;        g = 150 + k * 105; b = 0; }       // orange → yellow
        else if (t < 0.90f) {                                r = 255;        g = 255;          b = 0; }        // yellow hold
        else                { float k = (t - 0.90f) / 0.10f; r = 255 - k * 210; g = 255 - k * 35; b = 0 + k * 70; } // yellow → green
        uint16_t R = ((uint16_t)r >> 3) & 0x1F, G = ((uint16_t)g >> 2) & 0x3F, B = ((uint16_t)b >> 3) & 0x1F;
        return (R << 11) | (G << 5) | B;
    }

    // Multi-colour repaint: the engine's pass-1 blit drew the dragon mask in one flat
    // ink colour; here we redraw the same ink pixels as a gradual head→tail gradient —
    // red head, through orange and yellow, to green only at the very tail tip and the
    // feet. The wings (upper area) keep their previous yellow look.
    static void dragon_paint_warm(Entity *self, Draw *draw, Game *game)
    {
        Image *spr = self->sprite;
        if (self->direction.x < 0 && self->sprite_left != nullptr) spr = self->sprite_left;
        else if (self->direction.x > 0 && self->sprite_right != nullptr) spr = self->sprite_right;
        if (spr == nullptr || !spr->is_8bit) return;
        const uint8_t *d = spr->getData();
        int W = (int)spr->size.x, H = (int)spr->size.y;
        int ox = (int)(self->position.x - game->pos.x);
        int oy = (int)(self->position.y - game->pos.y);
        bool headRight = self->direction.x >= 0; // head is on the facing side
        int wingCut = H - 12;                     // wings reach ~12px up from the bottom; keep them yellow
        int footCut = H - 3;                       // bottom rows = feet (small green)
        const uint16_t GREEN = dragon_grad(1.0f);
        for (int y = 0; y < H; y++)
        {
            for (int x = 0; x < W; x++)
            {
                if (pgm_read_byte_near(&d[y * W + x]) != 0x00) continue;
                uint16_t col;
                // Head (t=0) → tail (t=1), t along the facing axis.
                float t = headRight ? (float)(W - 1 - x) / (W - 1) : (float)x / (W - 1);
                float frontCols = headRight ? (float)(W - 1 - x) : (float)x; // columns from the front
                if (y < wingCut && frontCols >= 8)
                {
                    // Wings: yellow with the same light mottle as before — but keep the
                    // top row clean so the horn tips aren't nicked by a stray red pixel.
                    col = (y > 0 && ((x ^ y) & 3) == 0) ? DRAGON_WARM[2] : DRAGON_WARM[0];
                }
                else
                {
                    // Body + head front (front 8 columns have no wing above them): gradient.
                    col = (y >= footCut) ? GREEN : dragon_grad(t); // feet stay green
                }
                draw->drawPixel(Vector(ox + x, oy + y), col);
            }
        }
    }

    static void dragon_render(Entity *self, Draw *draw, Game *game)
    {
        if (self->state == ENTITY_DEAD) return;
        dragon_paint_warm(self, draw, game);
        char hs[24];
        snprintf(hs, sizeof(hs), "DRAGON %.0f", (double)self->health);
        draw_username(game, self->position, hs, (int)self->size.y + 4, 0xFD20);
        for (int i = 0; i < DRAGON_FB_MAX; i++)
        {
            if (!g_fbAct[i]) continue;
            int sx = (int)(g_fbX[i] - game->pos.x), sy = (int)(g_fbY[i] - game->pos.y);
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
        d->speed = 80;              // patrols briskly — aggressive pursuit
        d->attack_timer = 0.8f;
        d->strength = 40;           // heavy bite when the player closes in
        d->health = 1000;
        d->max_health = 1000;
        d->start_position = pos;
        d->end_position = pos;
        g_dragonBaseY = pos.y;
        g_dragonPhase = 0;
        g_dragonFireCd = 1.5f;      // brief grace before the first fireball
        for (int i = 0; i < DRAGON_FB_MAX; i++) g_fbAct[i] = false;
        g_dragonTurns = 0;          // 3 defensive turns available over the fight
        g_dragonMoveDir = -1;       // start flying left
        g_dragonMeleeCd = 0;
        level->entity_add(d);
    }

    // ── Ogre / Ghost bosses ────────────────────────────────────────────────────
    // Big, tough versions of the ordinary ogre/ghost that lob projectiles at the
    // player. Only one boss is ever alive at a time (Shadow Keep's rock-throwing ogre,
    // The Hollow's ice-throwing ghost), so a single shared projectile pool in module
    // statics is enough. Movement, melee and being-worn-down reuse enemy_update /
    // enemy_collision — the boss is just a beefy enemy with a ranged attack layered on.
    static const int BOSS_PROJ_MAX = 4;
    static bool  g_bpAct[BOSS_PROJ_MAX] = {false, false, false, false};
    static float g_bpX[BOSS_PROJ_MAX], g_bpY[BOSS_PROJ_MAX], g_bpDX[BOSS_PROJ_MAX], g_bpDY[BOSS_PROJ_MAX];
    static int   g_bossKind = 0;   // 0 = rocks (ogre/cyclops), 1 = freezing ice-balls (ghost)
    static float g_bossFireCd = 0; // seconds until the boss may throw again
    static float g_freezeImmune = 0; // seconds of post-thaw ice-immunity for the player,
                                     // so an ice-ball can't re-freeze the instant they thaw
    // Patrol state: the bosses walk the FULL width of the map like the dragon, bouncing
    // off the edges. Ogre-type bosses tramp along a fixed ground line (no swoop); the
    // ghost floats — homing its lane toward the player with a gentle vertical swoop.
    static float g_bossMoveDir = -1;         // +1 right / -1 left
    static float g_bossBaseY = 0;            // the walk/swoop lane height
    static float g_bossPhase = 0;            // swoop phase (ghost only)
    static bool  g_bossSwoop = false;        // true = float+swoop (ghost); false = walk flat (ogres)
    static float g_bossAim = 0;              // throw jitter in radians (0 = dead-on; the cyclops is sloppy)
    static const char *g_bossLabel = "BOSS"; // name floated above the boss

    // Nearest-neighbour upscale of an 8-bit mask (0xFF transparent, 0x00 ink) by an
    // integer factor — this is how a boss is drawn BIGGER than the rank-and-file. Returns
    // a malloc'd RAM buffer the caller must free; the Entity ctor copies it (is_progmem
    // = false → from_byte_array copies), so freeing right after spawn is safe.
    static uint8_t *scale_mask(const uint8_t *src, int w, int h, int f)
    {
        uint8_t *out = (uint8_t *)malloc((size_t)w * f * h * f);
        if (!out) return nullptr;
        int W = w * f;
        for (int y = 0; y < h * f; y++)
            for (int x = 0; x < W; x++)
                out[y * W + x] = pgm_read_byte_near(&src[(y / f) * w + (x / f)]);
        return out;
    }

    static void boss_update(Entity *self, Game *game)
    {
        if (self->state == ENTITY_DEAD)
        {
            for (int i = 0; i < BOSS_PROJ_MAX; i++) g_bpAct[i] = false;
            return;
        }
        const float dt = 1.0f / 30;
        Level *lvl = game->current_level;
        if (!lvl) return;

        // keep the melee cadence ticking (enemy_collision gates the boss's bite on it)
        self->elapsed_attack_timer += dt;

        // find the player
        Entity *player = nullptr;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->is_player) { player = e; break; }
        }

        // Walk the FULL width of the map, bouncing off the edges (like the dragon).
        // Ogres tramp along a fixed ground line; the ghost floats, homing its lane
        // toward the player with a gentle vertical swoop.
        const float buffer = 16;
        float minX = buffer, maxX = lvl->size.x - buffer - self->size.x;
        if (maxX < minX) maxX = minX;
        float nx = self->position.x + g_bossMoveDir * self->speed * dt;
        if (nx <= minX) { nx = minX; g_bossMoveDir = 1.0f; }
        else if (nx >= maxX) { nx = maxX; g_bossMoveDir = -1.0f; }
        self->direction = (g_bossMoveDir < 0) ? ENTITY_LEFT : ENTITY_RIGHT;
        float ny = g_bossBaseY;
        if (g_bossSwoop && player)
        {
            float wantY = player->position.y + player->size.y / 2 - self->size.y / 2;
            g_bossBaseY += (wantY - g_bossBaseY) * 0.04f;
            if (g_bossBaseY < 8) g_bossBaseY = 8;
            float maxY = lvl->size.y - self->size.y - 8;
            if (g_bossBaseY > maxY) g_bossBaseY = maxY;
            g_bossPhase += dt;
            ny = g_bossBaseY + sinf(g_bossPhase * 1.6f) * 16.0f;
        }
        self->position_set(Vector(nx, ny));

        // Throw a projectile at the player on a cooldown, when a slot is free and the
        // player is in range.
        if (g_bossFireCd > 0) g_bossFireCd -= dt;
        int freeSlot = -1, inAir = 0;
        for (int i = 0; i < BOSS_PROJ_MAX; i++) { if (g_bpAct[i]) inAir++; else if (freeSlot < 0) freeSlot = i; }
        float cx = self->position.x + self->size.x / 2, cy = self->position.y + self->size.y / 2;
        if (player && freeSlot >= 0 && inAir < BOSS_PROJ_MAX && g_bossFireCd <= 0)
        {
            float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
            float dx = px - cx, dy = py - cy, dist = sqrtf(dx * dx + dy * dy);
            if (dist > 20 && dist < 460)
            {
                float sp = (g_bossKind == 0) ? 3.0f : 3.6f; // ice flies a touch faster
                float ang = atan2f(dy, dx);
                if (g_bossAim > 0) // sloppy shooters (the cyclops) spray off-target
                    ang += ((float)random(-1000, 1001) / 1000.0f) * g_bossAim;
                g_bpAct[freeSlot] = true; g_bpX[freeSlot] = cx; g_bpY[freeSlot] = cy;
                g_bpDX[freeSlot] = cosf(ang) * sp; g_bpDY[freeSlot] = sinf(ang) * sp;
                g_bossFireCd = (g_bossKind == 0) ? 1.5f : 1.8f;
            }
        }

        // Advance projectiles and resolve hits.
        for (int i = 0; i < BOSS_PROJ_MAX; i++)
        {
            if (!g_bpAct[i]) continue;
            g_bpX[i] += g_bpDX[i]; g_bpY[i] += g_bpDY[i];
            if (g_bpX[i] < 0 || g_bpY[i] < 0 || g_bpX[i] > lvl->size.x || g_bpY[i] > lvl->size.y)
            {
                g_bpAct[i] = false; continue;
            }
            // Projectiles also strike any OTHER enemy: rocks damage them, ice-balls
            // damage AND freeze them (10s). A killed foe drops like any other. (The
            // boss can't hit itself.)
            {
                bool hitFoe = false;
                for (int e = 0; e < lvl->getEntityCount(); e++)
                {
                    Entity *en = lvl->getEntity(e);
                    if (!en || en == self || en->type != ENTITY_ENEMY || en->state == ENTITY_DEAD) continue;
                    float ex = en->position.x + en->size.x / 2 - g_bpX[i];
                    float ey = en->position.y + en->size.y / 2 - g_bpY[i];
                    if (ex * ex + ey * ey < 14 * 14)
                    {
                        en->health -= (g_bossKind == 0) ? 30 : 35; // ice hits a bit harder than rock
                        if (g_bossKind == 1) en->frozen = 10.0f;
                        if (en->health <= 0)
                        {
                            en->state = ENTITY_DEAD;
                            en->health = 0;
                            en->position = Vector(-100, -100);
                            en->position_set(en->position);
                        }
                        hitFoe = true;
                        break;
                    }
                }
                if (hitFoe) { g_bpAct[i] = false; continue; }
            }
            // Player hit.
            if (player)
            {
                float ddx = g_bpX[i] - (player->position.x + player->size.x / 2);
                float ddy = g_bpY[i] - (player->position.y + player->size.y / 2);
                if (ddx * ddx + ddy * ddy < 12 * 12)
                {
                    g_bpAct[i] = false;
                    if (g_bossKind == 0)
                        player->health -= 30;                          // rock: solid hit
                    else
                    {
                        player->health -= 35;                          // ice: hits a bit harder than rock…
                        // …and a 1s freeze — but ONLY if not already frozen and not in the
                        // brief post-thaw grace, so repeated ice-balls can't chain-lock you.
                        if (player->frozen <= 0 && g_freezeImmune <= 0)
                            player->frozen = 1.0f;
                    }
                    if (player->health <= 0)
                    {
                        player->state = ENTITY_DEAD;
                        player->health = player->max_health;
                        player->frozen = 0;
                        player->position = player->start_position;
                        player->position_set(player->start_position);
                    }
                    else
                        player->state = ENTITY_ATTACKED;
                }
            }
        }
    }

    static void boss_render(Entity *self, Draw *draw, Game *game)
    {
        if (self->state == ENTITY_DEAD) return;
        char hs[28];
        snprintf(hs, sizeof(hs), "%s %.0f", g_bossLabel, (double)self->health);
        draw_username(game, self->position, hs, (int)self->size.y + 4, self->ink_color);
        for (int i = 0; i < BOSS_PROJ_MAX; i++)
        {
            if (!g_bpAct[i]) continue;
            int sx = (int)(g_bpX[i] - game->pos.x), sy = (int)(g_bpY[i] - game->pos.y);
            if (g_bossKind == 0)
            {
                game->draw->display->fillCircle(sx, sy, 4, 0x8410); // grey rock
                game->draw->display->fillCircle(sx, sy, 2, 0xB596); // lighter core
            }
            else
            {
                game->draw->display->fillCircle(sx, sy, 4, 0x2D7F); // icy-blue ice-ball
                game->draw->display->fillCircle(sx, sy, 2, 0xFFFF); // white core
            }
        }
    }

    // Shared spawn: a scaled-up ogre/ghost/cyclops with boss stats + boss_update/render.
    // kind: 0 = rocks, 1 = freezing ice-balls. swoop: ghost floats+swoops, ogres walk
    // flat. aimJitter: radians of random throw spread (0 = dead-on).
    static void boss_spawn(Level *level, const char *baseName, int kind, const char *label,
                           Vector pos, int scale, float health, float strength, float speed,
                           uint16_t ink, bool swoop, float aimJitter)
    {
        PlayerContext bl = player_context_get(baseName, true);
        PlayerContext br = player_context_get(baseName, false);
        if (bl.data == NULL || br.data == NULL) return;
        int w = (int)bl.size.x, h = (int)bl.size.y;
        uint8_t *sl = scale_mask(bl.data, w, h, scale);
        uint8_t *sr = scale_mask(br.data, w, h, scale);
        if (!sl || !sr) { free(sl); free(sr); return; }
        Vector bsize = Vector(w * scale, h * scale);
        Entity *b = new Entity(level->getBoard(), baseName, ENTITY_ENEMY, pos, bsize,
                               sl, sl, sr, NULL, NULL, boss_update, boss_render, enemy_collision, true, false);
        free(sl); free(sr); // the Entity ctor copied the masks (is_progmem = false)
        b->ink_color = ink;
        b->direction = ENTITY_LEFT;
        b->start_position = pos;
        b->end_position = pos;
        b->speed = speed;
        b->attack_timer = 0.8f;
        b->strength = strength;
        b->health = health;
        b->max_health = health;
        g_bossKind = kind;
        g_bossLabel = label;
        g_bossFireCd = 1.6f;
        g_bossMoveDir = -1;
        g_bossBaseY = pos.y;
        g_bossPhase = 0;
        g_bossSwoop = swoop;
        g_bossAim = aimJitter;
        for (int i = 0; i < BOSS_PROJ_MAX; i++) g_bpAct[i] = false;
        level->entity_add(b);
    }

    void ogre_boss_spawn(Level *level)
    {
        // Shadow Keep: 550 HP, 3× a normal ogre, throws rocks, walks flat. Red-brown ink.
        boss_spawn(level, "ogre", 0, "OGRE BOSS", Vector(300, 170), 3, 550, 34, 48, 0x8A08, false, 0.0f);
    }

    void ghost_boss_spawn(Level *level)
    {
        // The Hollow: 750 HP, 2× a normal ghost, throws freezing ice-balls, floats and
        // swoops like the dragon. Icy-blue ink.
        boss_spawn(level, "ghost", 1, "GHOST BOSS", Vector(300, 170), 2, 750, 22, 56, 0xAEDF, true, 0.0f);
    }

    void cyclops_boss_spawn(Level *level)
    {
        // Stronghold: one-eyed ogre. 450 HP, 3× a normal cyclops, throws rocks like the
        // ogre boss but with sloppy aim (~0.35 rad spread). Walks flat, no swoop. Muddy ink.
        boss_spawn(level, "cyclops", 0, "CYCLOPS BOSS", Vector(300, 170), 3, 450, 30, 46, 0x7B4A, false, 0.35f);
    }

    // ── Strafing ghost (cameo) ──────────────────────────────────────────────────
    // An UNDEFEATABLE ghost that streaks across the map making a few passes, hurling
    // freezing ice-balls at the player, then leaves. ENTITY_NPC (no collision), so it
    // can't be hit and never gates the map. Used for the Frozen Lake ghost (ice-balls)
    // and the Wasteland ogre (rocks).
    static bool  g_gsFbAct[2] = {false, false};
    static float g_gsFbX[2], g_gsFbY[2], g_gsFbDX[2], g_gsFbDY[2];
    static int   g_gsPasses = 0;
    static int   g_gsKind = 0; // 0 = rocks (ogre), 1 = freezing ice-balls (ghost)
    static float g_gsDir = 1, g_gsY = 0, g_gsFireCd = 0;

    static void strafe_update(Entity *self, Game *game)
    {
        if (self->state == ENTITY_DEAD || !self->is_active) return;
        const float dt = 1.0f / 30;
        Level *lvl = game->current_level;
        if (!lvl) return;
        Entity *player = nullptr;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->is_player) { player = e; break; }
        }

        // fly straight across the map at its current lane height
        float nx = self->position.x + g_gsDir * self->speed * dt;
        self->direction = (g_gsDir < 0) ? ENTITY_LEFT : ENTITY_RIGHT;
        self->position_set(Vector(nx, g_gsY));

        // exited a side → count a pass; drop the lane to the player's height and turn
        // back, or leave for good after the last pass
        float off = self->size.x + 40;
        if ((g_gsDir > 0 && nx > lvl->size.x + 20) || (g_gsDir < 0 && nx < -off))
        {
            g_gsPasses--;
            if (g_gsPasses <= 0) { self->is_active = false; g_gsFbAct[0] = g_gsFbAct[1] = false; return; }
            g_gsDir = -g_gsDir;
            if (player)
            {
                g_gsY = player->position.y + player->size.y / 2 - self->size.y / 2;
                if (g_gsY < 8) g_gsY = 8;
                float my = lvl->size.y - self->size.y - 8;
                if (g_gsY > my) g_gsY = my;
            }
            self->position_set(Vector(g_gsDir > 0 ? -self->size.x : lvl->size.x, g_gsY));
        }

        // throw a projectile at the player on a cooldown
        if (g_gsFireCd > 0) g_gsFireCd -= dt;
        float cx = self->position.x + self->size.x / 2, cy = self->position.y + self->size.y / 2;
        int slot = -1;
        for (int i = 0; i < 2; i++) if (!g_gsFbAct[i]) { slot = i; break; }
        if (player && slot >= 0 && g_gsFireCd <= 0)
        {
            float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
            float dx = px - cx, dy = py - cy, d = sqrtf(dx * dx + dy * dy);
            if (d > 20 && d < 500)
            {
                float sp = 3.6f;
                g_gsFbAct[slot] = true; g_gsFbX[slot] = cx; g_gsFbY[slot] = cy;
                g_gsFbDX[slot] = dx / d * sp; g_gsFbDY[slot] = dy / d * sp;
                g_gsFireCd = 1.2f;
            }
        }
        for (int i = 0; i < 2; i++)
        {
            if (!g_gsFbAct[i]) continue;
            g_gsFbX[i] += g_gsFbDX[i]; g_gsFbY[i] += g_gsFbDY[i];
            if (g_gsFbX[i] < 0 || g_gsFbY[i] < 0 || g_gsFbX[i] > lvl->size.x || g_gsFbY[i] > lvl->size.y)
            {
                g_gsFbAct[i] = false; continue;
            }
            if (player)
            {
                float ddx = g_gsFbX[i] - (player->position.x + player->size.x / 2);
                float ddy = g_gsFbY[i] - (player->position.y + player->size.y / 2);
                if (ddx * ddx + ddy * ddy < 12 * 12)
                {
                    g_gsFbAct[i] = false;
                    if (g_gsKind == 0)
                        player->health -= 30; // rock: solid hit
                    else
                    {
                        player->health -= 35; // ice: hits a bit harder than rock + freeze (ghost-boss rules)
                        if (player->frozen <= 0 && g_freezeImmune <= 0) player->frozen = 1.0f;
                    }
                    if (player->health <= 0)
                    {
                        player->state = ENTITY_DEAD;
                        player->health = player->max_health;
                        player->frozen = 0;
                        player->position = player->start_position;
                        player->position_set(player->start_position);
                    }
                    else
                        player->state = ENTITY_ATTACKED;
                }
            }
        }
    }

    static void strafe_render(Entity *self, Draw *draw, Game *game)
    {
        if (self->state == ENTITY_DEAD || !self->is_active) return;
        for (int i = 0; i < 2; i++)
        {
            if (!g_gsFbAct[i]) continue;
            int sx = (int)(g_gsFbX[i] - game->pos.x), sy = (int)(g_gsFbY[i] - game->pos.y);
            if (g_gsKind == 0)
            {
                game->draw->display->fillCircle(sx, sy, 4, 0x8410); // grey rock
                game->draw->display->fillCircle(sx, sy, 2, 0xB596); // lighter core
            }
            else
            {
                game->draw->display->fillCircle(sx, sy, 4, 0x2D7F); // icy-blue ice-ball
                game->draw->display->fillCircle(sx, sy, 2, 0xFFFF); // white core
            }
        }
    }

    // Shared strafe cameo spawn. kind: 0 = rocks, 1 = freezing ice-balls.
    static void strafe_spawn(Level *level, const char *baseName, int kind, int scale, uint16_t ink)
    {
        PlayerContext gl = player_context_get(baseName, true);
        PlayerContext gr = player_context_get(baseName, false);
        if (gl.data == NULL || gr.data == NULL) return;
        int w = (int)gl.size.x, h = (int)gl.size.y;
        uint8_t *sl = scale_mask(gl.data, w, h, scale);
        uint8_t *sr = scale_mask(gr.data, w, h, scale);
        if (!sl || !sr) { free(sl); free(sr); return; }
        Vector gsize = Vector(w * scale, h * scale);
        Vector pos = Vector(-gsize.x, 90);
        Entity *g = new Entity(level->getBoard(), "Strafe", ENTITY_NPC, pos, gsize,
                               sl, sl, sr, NULL, NULL, strafe_update, strafe_render, NULL, true, false);
        free(sl); free(sr);
        g->ink_color = ink;
        g->direction = ENTITY_RIGHT;
        g->speed = 150;                 // fast streak
        g->health = 1; g->max_health = 1;
        g_gsKind = kind;
        g_gsDir = 1; g_gsY = pos.y; g_gsPasses = 3; g_gsFireCd = 0.6f;
        g_gsFbAct[0] = g_gsFbAct[1] = false;
        level->entity_add(g);
    }

    void ghost_strafe_spawn(Level *level)
    {
        // Frozen Lake: a 2× ghost streaks across in 3 passes throwing freezing ice-balls.
        strafe_spawn(level, "ghost", 1, 2, 0xAEDF);
    }

    void ogre_strafe_spawn(Level *level)
    {
        // Wasteland: a 3× ogre streaks across in 3 passes hurling rocks. Red-brown ink.
        strafe_spawn(level, "ogre", 0, 3, 0x8A08);
    }

    // ── Flyby (cameo) dragon ──────────────────────────────────────────────────
    // An UNDEFEATABLE dragon that streaks across the map: it makes N passes, throws
    // fire (at the player, or at a house which it sets alight), then leaves. It's an
    // ENTITY_NPC with no collision, so it can't be hit and never gates the map.
    static int   g_flyPasses = 0, g_flyMode = 0;   // mode 0 = fire at player, 1 = burn targets
    static float g_flyDir = 1, g_flyY = 0, g_flyFireCd = 0, g_flyTX = 0, g_flyTY = 0;
    static bool  g_flyFbActive = false, g_flyFired = false, g_flyDone = false;
    static float g_flyFbX = 0, g_flyFbY = 0, g_flyFbDX = 0, g_flyFbDY = 0;
    // Burn mode: torch flammable icons the dragon passes, up to g_flyBurnN of them.
    static int   g_flyBurnN = 0, g_flyBurnI = 0; // target count / burned-so-far
    static float g_flyMeleeCd = 0; // seconds until the cameo dragon may bite again

    // Nearest un-burnt flammable icon (tree/plant/flower/house) within maxDist of (x,y).
    // If dirX != 0, only icons AHEAD of x in that direction count — so a dragon flying
    // across torches objects progressively along its path, spread over the whole map
    // rather than dumping every shot on the first cluster it meets.
    static Entity *nearest_flammable(Level *lvl, float x, float y, float maxDist, float dirX)
    {
        Entity *best = nullptr; float bd = maxDist * maxDist;
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (!e || e->type != ENTITY_ICON || e->burn_kind == 0 || e->on_fire > 0) continue;
            float ecx = e->position.x + e->size.x / 2;
            if (dirX != 0 && (ecx - x) * dirX < 20) continue; // must be ahead of the dragon
            float dx = ecx - x, dy = e->position.y + e->size.y / 2 - y, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = e; }
        }
        return best;
    }

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

    // Burning world objects (houses/trees) now manage their own flames via the icon
    // burn system (see icon.cpp: icon_burn_update / icon_burn_render / icon_ignite_at).

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

        // throw fire — from the mouth (front of the head)
        bool facingRight = (g_flyDir >= 0);
        float mx = facingRight ? (nx + self->size.x - 2) : (nx + 2);
        float my = ny + 37; // open jaw (front-bottom of the head)
        if (g_flyFireCd > 0) g_flyFireCd -= dt;

        // Melee bite: the attacking cameo dragon also snaps at the player if it streaks
        // past close to their face (mode 0 only — the burn-run cameo ignores the player).
        if (g_flyMeleeCd > 0) g_flyMeleeCd -= dt;
        if (g_flyMode == 0 && player && g_flyMeleeCd <= 0)
        {
            float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
            float bx = px - mx, by = py - my;
            bool inFront = facingRight ? (px >= nx + self->size.x * 0.4f)
                                       : (px <= nx + self->size.x * 0.6f);
            if (inFront && bx * bx + by * by < 52.0f * 52.0f)
            {
                player->health -= 30;
                g_flyMeleeCd = 0.6f;
                if (player->health <= 0)
                {
                    player->state = ENTITY_DEAD; player->health = player->max_health;
                    player->position = player->start_position; player->position_set(player->start_position);
                }
                else player->state = ENTITY_ATTACKED;
            }
        }

        if (!g_flyFbActive)
        {
            if (g_flyMode == 0 && player && g_flyFireCd <= 0)
            {
                float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
                float dx = px - mx, dy = py - my, dd = sqrtf(dx * dx + dy * dy);
                if (dd > 1 && dragon_fire_arc(facingRight, px - cx, py - cy))
                { g_flyFbActive = true; g_flyFbX = mx; g_flyFbY = my; float sp = 3.1f; g_flyFbDX = dx / dd * sp; g_flyFbDY = dy / dd * sp; g_flyFireCd = 1.3f; }
            }
            else if (g_flyMode == 1 && g_flyBurnI < g_flyBurnN && g_flyFireCd <= 0)
            {
                // Torch an un-burnt tree/flower/house AHEAD of the dragon on a cooldown,
                // so the fires end up spread across the whole map (one every so often as
                // it flies over) instead of all dumped on the first cluster at once.
                Entity *tgt = nearest_flammable(lvl, cx, cy, 260, g_flyDir);
                if (!tgt) tgt = nearest_flammable(lvl, cx, cy, 260, 0); // fallback: nearest any
                if (tgt)
                {
                    g_flyTX = tgt->position.x + tgt->size.x / 2;
                    g_flyTY = tgt->position.y + tgt->size.y / 2;
                    float dx = g_flyTX - mx, dy = g_flyTY - my, dd = sqrtf(dx * dx + dy * dy);
                    if (dd > 1) { g_flyFbActive = true; g_flyFbX = mx; g_flyFbY = my; float sp = 4.0f; g_flyFbDX = dx / dd * sp; g_flyFbDY = dy / dd * sp; g_flyFireCd = 0.85f; }
                }
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
                else if (fb_hit_enemy(lvl, g_flyFbX, g_flyFbY, self)) g_flyFbActive = false; // also torch foes
                else if (icon_ignite_at(lvl, g_flyFbX, g_flyFbY)) g_flyFbActive = false;    // …or a house/tree
                else if (off) g_flyFbActive = false;
            }
            else if (g_flyMode == 1)
            {
                float ex = g_flyFbX - g_flyTX, ey = g_flyFbY - g_flyTY;
                if (ex * ex + ey * ey < 220) // reached the target → set it ablaze
                {
                    g_flyFbActive = false;
                    // Ignite the actual target icon; if the aim was slightly off, torch
                    // the nearest flammable object to it so a pass never fizzles.
                    if (!icon_ignite_at(lvl, g_flyTX, g_flyTY))
                    {
                        Entity *h = nearest_icon(lvl, g_flyTX, g_flyTY);
                        if (h) icon_ignite_at(lvl, h->position.x + h->size.x / 2,
                                              h->position.y + h->size.y / 2);
                    }
                    g_flyBurnI++;                              // on to the next target
                }
                else if (off) g_flyFbActive = false;
            }
        }

        // Burn run: once it's torched its quota it stops firing (the fire condition above
        // already gates on g_flyBurnI < g_flyBurnN) and finishes the pass it's on, then
        // flies off the edge — capping the remaining passes to 1 so it doesn't turn back
        // for another, and doesn't vanish mid-map.
        if (g_flyMode == 1 && g_flyBurnN > 0 && g_flyBurnI >= g_flyBurnN && g_flyPasses > 1)
            g_flyPasses = 1;

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
        dragon_paint_warm(self, draw, game); // same fiery red/orange/yellow body
        if (g_flyFbActive)
        {
            int sx = (int)(g_flyFbX - game->pos.x), sy = (int)(g_flyFbY - game->pos.y);
            game->draw->display->fillCircle(sx, sy, 4, 0xFD20);
            game->draw->display->fillCircle(sx, sy, 2, 0xFFE0);
        }
    }

    // mode 1 (burn) torches up to `burnCount` nearby flammable icons over its passes;
    // mode 0 (attack) ignores it. The old fixed-target list is gone — it auto-targets.
    void flyby_dragon_spawn(Level *level, int passes, int mode, const float *targets, int burnCount)
    {
        (void)targets; // legacy param, no longer used
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
        g_flyMeleeCd = 0;
        g_flyTX = 0; g_flyTY = 0;
        g_flyBurnN = burnCount; // how many icons to set ablaze over the run
        g_flyBurnI = 0;
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

        // Frozen (a ghost-boss ice-ball): can't move or attack for the duration. The
        // regen/timers above still tick and the camera below still tracks; only input
        // is skipped (see the frozen gate on the touch block). Ticks down here; the
        // moment it thaws, a brief ice-immunity grace opens so you can't be instantly
        // re-frozen (see g_freezeImmune in the boss ice-ball handler).
        if (self->frozen > 0)
        {
            self->frozen -= 1.0f / 30;
            if (self->frozen <= 0) { self->frozen = 0; g_freezeImmune = 0.5f; }
        }
        else if (g_freezeImmune > 0)
        {
            g_freezeImmune -= 1.0f / 30;
            if (g_freezeImmune < 0) g_freezeImmune = 0;
        }

        Vector oldPos = self->position;
        Vector newPos = oldPos;

        // Edge-zone nav (the original layout): the touch's screen position picks the
        // move. Top/bottom fifth = up/down, left/right quarter = left/right, and a
        // CORNER is in two zones at once → diagonal (e.g. bottom+left = down-left).
        // The central rectangle (no edge) is the attack zone (its original size).
        //
        // Multi-touch (capacitive Pancake): both fingers are classified. Any finger on
        // an edge steers; any finger in the centre attacks — so you can hold a direction
        // and tap the middle to move and attack at the same time.
        const float STEP = 6;
        float mx = 0, my = 0;
        bool centreTap = false;
        bool frozen = self->frozen > 0; // no steering or attacking while iced
        TouchInput *touch = game->input_manager ? game->input_manager->getTouch() : nullptr;

        // Tapping strictly the HP row of the stats HUD (top-left, first line of
        // draw_user_stats' box) toggles the floating health bar under the player's name
        // on/off. Edge-detected so a hold flips it once. Movement is NOT suppressed — the
        // tap still steers as normal, it just also flips the health bar.
        bool hpRowTap = false;
        if (touch && touch->isPressed())
        {
            float tx = touch->x(), ty = touch->y();
            if (tx >= 3 && tx < 107 && ty >= 31 && ty < 50) hpRowTap = true; // HP row only
        }
        if (hpRowTap && !g_statsTapHeld) g_healthBar = !g_healthBar; // toggle on the press edge
        g_statsTapHeld = hpRowTap;

        if (!frozen && touch && touch->isPressed())
        {
            float w = game->size.x, h = game->size.y;
            uint8_t n = touch->count();
            if (n < 1) n = 1; // isPressed() true → at least the primary point is valid
            for (uint8_t i = 0; i < n; i++)
            {
                float px = (i == 0) ? touch->x() : touch->x2();
                float py = (i == 0) ? touch->y() : touch->y2();
                float emx = 0, emy = 0;
                if (py < h / 5) emy -= 1;          // top edge    → up
                else if (py > h * 4 / 5) emy += 1; // bottom edge → down
                if (px < w / 4) emx -= 1;          // left edge   → left
                else if (px > w * 3 / 4) emx += 1; // right edge  → right
                if (emx == 0 && emy == 0) centreTap = true; // this finger is attacking
                else { mx += emx; my += emy; }              // this finger is steering
            }
            // Two fingers steering the same axis shouldn't double the speed.
            if (mx > 1) mx = 1; else if (mx < -1) mx = -1;
            if (my > 1) my = 1; else if (my < -1) my = -1;
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
        //
        // The launcher paints a fixed HUD header over the top FW_HEADER_H px of the
        // panel, so the camera is allowed to pan UP that far past the map's top edge
        // (camera_y down to -FW_HEADER_H). That keeps the map's top row sitting just
        // BELOW the header instead of the player climbing behind it and vanishing "off
        // the top", and it widens the usable vertical scroll by the header height.
        const float FW_HEADER_H = 28.0f; // matches HDRH in the launcher
        float max_cam_y = game->current_level->size.y - game->size.y;
        float min_cam_y = -FW_HEADER_H;
        float camera_y;
        if (max_cam_y <= min_cam_y)
            camera_y = max_cam_y; // map fits under the viewport → bottom-aligned (Pancake)
        else
            camera_y = constrain(self->position.y - game->size.y / 2, min_cam_y, max_cam_y); // follow (V8)

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

        char health[24], level[32], xpS[32];
        snprintf(health, sizeof(health), "HP %.0f/%.0f", (double)self->health, (double)self->max_health);
        snprintf(level, sizeof(level), "LVL %d (%.0f)", lvl, (double)self->xp); // total XP in parens
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
        // Frozen (ghost-boss ice): sheath the hero in frost + flag "FROZEN" so it's
        // clear why the controls aren't responding.
        if (self->frozen > 0)
        {
            int bx = (int)(self->position.x - game->pos.x);
            int by = (int)(self->position.y - game->pos.y);
            int w = (int)self->size.x, h = (int)self->size.y;
            game->draw->display->drawRect(bx - 1, by - 1, w + 2, h + 2, 0xE73F);
            for (int fy = 1; fy < h; fy += 3)
                for (int fx = ((fy >> 1) & 1); fx < w; fx += 3)
                    game->draw->display->drawPixel(bx + fx, by + fy, 0xFFFF);
            draw_username(game, self->position, "FROZEN", 52, 0xE73F);
        }
        // Player name floated above the sprite (raised to 40px so the health bar tucked
        // under it never overlays it). White so it's distinct from the red enemy-health
        // labels and the green stat HUD.
        draw_username(game, self->position, fw_player_name, 40, 0xFFFF);
        // Quick-glance health bar directly under the name — tap the HUD's HP line to
        // toggle it (state persisted by the launcher).
        if (g_healthBar)
            draw_player_healthbar(game, self->position, self->health, self->max_health, 24);
        // Detailed HP/LVL/XP HUD — always shown.
        draw_user_stats(self, Vector(5, 34), game); // fixed HUD at top (below the header)
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
