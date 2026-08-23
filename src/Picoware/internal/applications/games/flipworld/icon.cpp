#include <ArduinoJson.h>
#include "../../../applications/games/flipworld/game.hpp"
#include "../../../applications/games/flipworld/assets.hpp"
namespace FlipWorld
{
    typedef enum
    {
        ICON_ID_NONE = -1,            // None
        ICON_ID_HOUSE = 0,            // House
        ICON_ID_MAN,                  // Man
        ICON_ID_PLANT,                // Plant
        ICON_ID_TREE,                 // Tree
        ICON_ID_WOMAN,                // Woman
        ICON_ID_FENCE,                // Fence
        ICON_ID_FENCE_END,            // Fence end
        ICON_ID_FENCE_VERTICAL_END,   // Vertical fence end
        ICON_ID_FENCE_VERTICAL_START, // Vertical fence start
        ICON_ID_FLOWER,               // Flower
        ICON_ID_LAKE_BOTTOM,          // Lake bottom
        ICON_ID_LAKE_BOTTOM_LEFT,     // Lake bottom left
        ICON_ID_LAKE_BOTTOM_RIGHT,    // Lake bottom right
        ICON_ID_LAKE_LEFT,            // Lake left
        ICON_ID_LAKE_RIGHT,           // Lake right
        ICON_ID_LAKE_TOP,             // Lake top
        ICON_ID_LAKE_TOP_LEFT,        // Lake top left
        ICON_ID_LAKE_TOP_RIGHT,       // Lake top right
        ICON_ID_ROCK_LARGE,           // Large rock
        ICON_ID_ROCK_MEDIUM,          // Medium rock
        ICON_ID_ROCK_SMALL,           // Small rock
        ICON_ID_WATER,                // Solid water fill tile (for lakes)
        ICON_ID_ICE,                  // Solid ice tile (walkable, slippery)
    } IconID;

    typedef struct
    {
        IconID id;
        const uint8_t *data;
        Vector size;
    } IconContext;

    static IconContext icon_context_get(const char *name)
    {
        if (strcmp(name, "tree") == 0)
            return {ICON_ID_TREE, icon_tree_16x16, Vector(16, 16)};
        if (strcmp(name, "fence") == 0)
            return {ICON_ID_FENCE, icon_fence_16x8px, Vector(16, 8)};
        if (strcmp(name, "fence_end") == 0)
            return {ICON_ID_FENCE_END, icon_fence_end_16x8px, Vector(16, 8)};
        if (strcmp(name, "fence_vertical_end") == 0)
            return {ICON_ID_FENCE_VERTICAL_END, icon_fence_vertical_end_6x8px, Vector(6, 8)};
        if (strcmp(name, "fence_vertical_start") == 0)
            return {ICON_ID_FENCE_VERTICAL_START, icon_fence_vertical_start_6x15px, Vector(6, 15)};
        if (strcmp(name, "rock_small") == 0)
            return {ICON_ID_ROCK_SMALL, icon_rock_small_10x8px, Vector(10, 8)};
        if (strcmp(name, "rock_medium") == 0)
            return {ICON_ID_ROCK_MEDIUM, icon_rock_medium_16x14px, Vector(16, 14)};
        if (strcmp(name, "rock_large") == 0)
            return {ICON_ID_ROCK_LARGE, icon_rock_large_18x19px, Vector(18, 19)};
        if (strcmp(name, "flower") == 0)
            return {ICON_ID_FLOWER, icon_flower_16x16, Vector(16, 16)};
        if (strcmp(name, "plant") == 0)
            return {ICON_ID_PLANT, icon_plant_16x16, Vector(16, 16)};
        if (strcmp(name, "man") == 0)
            return {ICON_ID_MAN, icon_man_7x16, Vector(7, 16)};
        if (strcmp(name, "woman") == 0)
            return {ICON_ID_WOMAN, icon_woman_9x16, Vector(9, 16)};
        if (strcmp(name, "lake_bottom") == 0)
            return {ICON_ID_LAKE_BOTTOM, icon_lake_bottom_31x12px, Vector(31, 12)};
        if (strcmp(name, "lake_bottom_left") == 0)
            return {ICON_ID_LAKE_BOTTOM_LEFT, icon_lake_bottom_left_24x22px, Vector(24, 22)};
        if (strcmp(name, "lake_bottom_right") == 0)
            return {ICON_ID_LAKE_BOTTOM_RIGHT, icon_lake_bottom_right_24x22px, Vector(24, 22)};
        if (strcmp(name, "lake_left") == 0)
            return {ICON_ID_LAKE_LEFT, icon_lake_left_11x31px, Vector(11, 31)};
        if (strcmp(name, "lake_right") == 0)
            return {ICON_ID_LAKE_RIGHT, icon_lake_right_11x31, Vector(11, 31)};
        if (strcmp(name, "lake_top") == 0)
            return {ICON_ID_LAKE_TOP, icon_lake_top_31x12px, Vector(31, 12)};
        if (strcmp(name, "lake_top_left") == 0)
            return {ICON_ID_LAKE_TOP_LEFT, icon_lake_top_left_24x22px, Vector(24, 22)};
        if (strcmp(name, "lake_top_right") == 0)
            return {ICON_ID_LAKE_TOP_RIGHT, icon_lake_top_right_24x22px, Vector(24, 22)};
        if (strcmp(name, "house") == 0)
            return {ICON_ID_HOUSE, icon_house_48x32px, Vector(48, 32)};
        if (strcmp(name, "water") == 0)
            return {ICON_ID_WATER, icon_water_40x24px, Vector(40, 24)};
        if (strcmp(name, "ice") == 0)
            return {ICON_ID_ICE, icon_water_40x24px, Vector(40, 24)}; // reuse solid tile

        return {ICON_ID_NONE, NULL, Vector(0, 0)};
    }

    static void icon_collision(Entity *self, Entity *other, Game *game)
    {
        if (strcmp(other->name, "Player") == 0)
        {
            // Only block when the player overlaps the object by more than a small
            // margin, so the player can move right up close to objects (grazing the
            // edges) before being stopped.
            const float margin = 5;
            float ax1 = self->position.x, ax2 = self->position.x + self->size.x;
            float bx1 = other->position.x, bx2 = other->position.x + other->size.x;
            float ay1 = self->position.y, ay2 = self->position.y + self->size.y;
            float by1 = other->position.y, by2 = other->position.y + other->size.y;
            float ox = (ax2 < bx2 ? ax2 : bx2) - (ax1 > bx1 ? ax1 : bx1);
            float oy = (ay2 < by2 ? ay2 : by2) - (ay1 > by1 ? ay1 : by1);
            if (ox > margin && oy > margin)
            {
                other->position_set(other->old_position);
            }
        }
    }

    // FlipWorld colour port: pick an ink colour for a world icon from its name so
    // the level renders in full colour instead of monochrome black-on-white.
    static uint16_t icon_ink_color(const char *name)
    {
        if (strcmp(name, "ice") == 0)
            return 0xE77F; // near-white pale ice (blue player still stands out)
        if (strstr(name, "lake") != NULL || strcmp(name, "water") == 0)
            return 0x041F; // water blue
        if (strstr(name, "rock") != NULL)
            return 0x8410; // stone grey
        if (strstr(name, "fence") != NULL)
            return 0x9340; // wood brown
        if (strcmp(name, "tree") == 0 || strcmp(name, "plant") == 0)
            return 0x0480; // deep foliage green
        if (strcmp(name, "flower") == 0)
            return 0xF81F; // magenta bloom
        if (strcmp(name, "house") == 0)
            return 0xB483; // warm timber
        if (strcmp(name, "man") == 0)
            return 0x24BF; // blue tunic NPC
        if (strcmp(name, "woman") == 0)
            return 0xFD5A; // rose tunic NPC
        return 0xFFFF;     // neutral white
    }

    // ── Burning world objects ─────────────────────────────────────────────────
    // Flammable icons (see Entity::burn_kind) manage their own fire: once ignited by a
    // dragon's fireball, they render flames, singe the player who stands too close,
    // and — for foliage — char to brown after 10s and can't reignite.
    static const uint16_t ICON_CHAR_BROWN = 0x5982; // burnt-out foliage
    static Entity *icon_find_player(Level *lvl)
    {
        for (int i = 0; i < lvl->getEntityCount(); i++)
        {
            Entity *e = lvl->getEntity(i);
            if (e && e->is_player) return e;
        }
        return nullptr;
    }
    static void icon_burn_update(Entity *self, Game *game)
    {
        if (self->on_fire <= 0) return; // not alight
        const float dt = 1.0f / 30;
        self->on_fire += dt;

        // Foliage burns for 10 s, then chars to brown and becomes inert (can't reignite).
        if (self->burn_kind == 1 && self->on_fire >= 10.0f)
        {
            self->on_fire = 0;
            self->burn_kind = 0;
            self->ink_color = ICON_CHAR_BROWN;
            return;
        }

        // Singe the player if they linger in the flames (proximity DoT).
        Level *lvl = game->current_level;
        if (!lvl) return;
        Entity *player = icon_find_player(lvl);
        if (!player) return;
        float fx = self->position.x + self->size.x / 2, fy = self->position.y + self->size.y / 2;
        float px = player->position.x + player->size.x / 2, py = player->position.y + player->size.y / 2;
        float dx = px - fx, dy = py - fy;
        float rad = (self->burn_kind == 2) ? 26.0f : 16.0f;
        self->elapsed_attack_timer += dt;
        if (dx * dx + dy * dy < rad * rad && self->elapsed_attack_timer >= 0.5f)
        {
            self->elapsed_attack_timer = 0;
            player->health -= (self->burn_kind == 2) ? 8 : 5;
            if (player->health <= 0)
            {
                player->state = ENTITY_DEAD; player->health = player->max_health;
                player->position = player->start_position; player->position_set(player->start_position);
            }
            else
                player->state = ENTITY_ATTACKED;
        }
    }
    static void icon_burn_render(Entity *self, Draw *draw, Game *game)
    {
        if (self->on_fire <= 0) return;
        int bx = (int)(self->position.x - game->pos.x);
        int by = (int)(self->position.y - game->pos.y);
        uint32_t t = millis();
        if (self->burn_kind == 2)
        {
            // House: a row of wide flames across the whole width.
            int flames = (int)(self->size.x / 8);
            if (flames < 1) flames = 1;
            for (int i = 0; i < flames; i++)
            {
                int fxp = bx + i * 8 + 3;
                int flick = (int)((sinf(t * 0.02f + i * 1.3f) * 0.5f + 0.5f) * 8);
                draw->display->fillCircle(fxp, by - flick, 4, 0xFC00);     // orange flame
                draw->display->fillCircle(fxp, by - 4 - flick, 2, 0xFFE0); // hot yellow tip
            }
        }
        else
        {
            // Foliage: a single small flame, only 3 px wide, licking up from the plant.
            int fxp = bx + (int)(self->size.x / 2);
            int baseY = by + (int)(self->size.y * 0.35f);
            int fh = 5 + (int)((sinf(t * 0.02f) * 0.5f + 0.5f) * 4);
            draw->display->fillRect(fxp - 1, baseY - fh, 3, fh, 0xFC00); // 3px orange flame
            draw->display->fillCircle(fxp, baseY - fh, 1, 0xFFE0);       // 3px yellow tip
        }
    }

    // Public: ignite the flammable icon under a point (used by dragon fireballs).
    bool icon_ignite_at(Level *level, float x, float y)
    {
        for (int i = 0; i < level->getEntityCount(); i++)
        {
            Entity *e = level->getEntity(i);
            if (!e || e->type != ENTITY_ICON || e->burn_kind == 0 || e->on_fire > 0)
                continue;
            if (x >= e->position.x && x < e->position.x + e->size.x &&
                y >= e->position.y && y < e->position.y + e->size.y)
            {
                e->on_fire = 0.0001f;      // just alight
                e->elapsed_attack_timer = 0;
                return true;
            }
        }
        return false;
    }

    // Which burn_kind (if any) a named icon has.
    static uint8_t icon_burn_kind(const char *name)
    {
        if (strcmp(name, "house") == 0) return 2;                    // wide persistent blaze
        if (strcmp(name, "tree") == 0 || strcmp(name, "plant") == 0 ||
            strcmp(name, "flower") == 0) return 1;                   // small 10 s blaze
        return 0;                                                    // inert
    }

    static void icon_spawn(Level *level, const char *name, Vector pos)
    {
        // Get the icon context
        IconContext icon = icon_context_get(name);

        // Check if the icon is valid
        if (icon.data == NULL)
        {
            return;
        }

        // The Entity ctor builds its own Image from the PROGMEM icon data (referenced,
        // not copied) which it owns and frees on teardown — so we do NOT share Images
        // via ImageManager. Sharing was a use-after-free: ~Entity deletes sprite,
        // freeing the cached image, and the next map (campaign / replay) would then
        // reuse the dangling ImageManager cache pointer and crash.
        // Ice is walkable (the player skates on it) — no collision callback; every
        // other icon blocks the player.
        void (*collisionCb)(Entity *, Entity *, Game *) = (strcmp(name, "ice") == 0) ? nullptr : icon_collision;
        // Flammable icons (tree/plant/flower/house) get burn callbacks so a dragon's
        // fireball can set them alight; everything else has no update/render.
        uint8_t kind = icon_burn_kind(name);
        void (*updateCb)(Entity *, Game *)        = kind ? icon_burn_update : nullptr;
        void (*renderCb)(Entity *, Draw *, Game *) = kind ? icon_burn_render : nullptr;
        Entity *newEntity = new Entity(
            level->getBoard(),
            "icon",
            ENTITY_ICON,
            pos,
            icon.size,
            icon.data,
            NULL,           // sprite_left_data
            NULL,           // sprite_right_data
            NULL,           // start
            NULL,           // stop
            updateCb,       // update (burn tick for flammable icons)
            renderCb,       // render (flames for flammable icons)
            collisionCb,    // collision (null for walkable ice)
            true,           // is 8-bit
            true            // is progmem
        );
        newEntity->ink_color = icon_ink_color(name); // FlipWorld colour port
        newEntity->burn_kind = kind;
        newEntity->on_fire = 0;
        newEntity->elapsed_attack_timer = 0;
        if (strcmp(name, "flower") == 0)
        {
            // Flowers come in a mix of colours.
            static const uint16_t flowerColors[] = {0xF81F, 0xFD20, 0xFFE0, 0xFC9F, 0x07FF, 0xFFFF, 0xF800, 0xA95F};
            newEntity->ink_color = flowerColors[random(0, 8)];
        }

        // Add to level
        level->entity_add(newEntity);
    }

    static void icon_spawn_line(Level *level, const char *name, Vector pos, int amount, bool horizontal, int spacing = 17)
    {
        for (int i = 0; i < amount; i++)
        {
            Vector newPos = pos;
            if (horizontal)
                newPos.x += i * spacing;
            else
                newPos.y += i * spacing;

            icon_spawn(level, name, newPos);
        }
    }

    void icon_spawn_json(Level *level, const char *json)
    {
        // Check heap
        size_t freeHeap = ESP.getFreeHeap(); // ESP32 port (was RP2040 rp2040.getFreeHeap())
        if (freeHeap < 1024)
        {
            return;
        }

        // Parse the json
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, json);

        // Check heap
        freeHeap = ESP.getFreeHeap();
        if (freeHeap < 1024)
        {
            return;
        }

        // Check for errors
        if (error)
        {
            return;
        }

        // Loop through the json data
        int index = 0;
        while (doc["json_data"][index]["icon"])
        {
            const char *icon = doc["json_data"][index]["icon"];
            float x = doc["json_data"][index]["x"];
            float y = doc["json_data"][index]["y"];
            int amount = doc["json_data"][index]["amount"];
            bool horizontal = doc["json_data"][index]["horizontal"];

            // Check the amount
            if (amount > 1)
            {
                icon_spawn_line(level, icon, Vector(x, y), amount, horizontal);
            }
            else
            {
                icon_spawn(level, icon, Vector(x, y));
            }

            index++;
        }
    }
} // namespace FlipWorld