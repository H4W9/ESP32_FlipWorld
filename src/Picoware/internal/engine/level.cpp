#include "../../internal/engine/entity.hpp"
#include "../../internal/engine/game.hpp"
#include "../../internal/engine/level.hpp"

namespace Picoware
{
    // Default Constructor
    Level::Level()
        : name(""),
          size(Vector(0, 0)),
          gameRef(nullptr),
          _start(nullptr),
          _stop(nullptr),
          entity_count(0),
          entities(nullptr),
          board(VGMConfig),
          clearAllowed(true)
    {
    }

    // Parameterized Constructor
    Level::Level(const char *name, const Vector &size, Game *game, void (*start)(Level &), void (*stop)(Level &))
        : name(name),
          size(size),
          gameRef(game),
          _start(start),
          _stop(stop),
          entity_count(0),
          entities(nullptr),
          board(game->draw->getBoard()),
          clearAllowed(true)
    {
    }

    // Destructor
    Level::~Level()
    {
        clear();
    }

    // Clear all entities
    void Level::clear()
    {
        for (int i = 0; i < entity_count; i++)
        {
            if (entities[i] != nullptr)
            {
                entities[i]->stop(this->gameRef);
                // Only delete entities that are not players (players are managed externally)
                if (!entities[i]->is_player)
                {
                    delete entities[i];
                }
                entities[i] = nullptr;
            }
        }
        // Free the dynamic array
        delete[] entities;
        entities = nullptr;
        entity_count = 0;
    }

    // Get list of collisions for a given entity
    Entity **Level::collision_list(Entity *entity, int &count) const
    {
        count = 0;
        if (entity_count == 0)
        {
            return nullptr;
        }

        Entity **result = new Entity *[entity_count];
        for (int i = 0; i < entity_count; i++)
        {
            if (entities[i] != nullptr &&
                entities[i] != entity && // Skip self
                is_collision(entity, entities[i]))
            {
                result[count++] = entities[i];
            }
        }
        return result;
    }

    // Add an entity to the level
    void Level::entity_add(Entity *entity)
    {
        if (!entity || !this->gameRef)
        {
            return;
        }

        // Allocate a new array with size one greater than the current count
        Entity **newEntities = new Entity *[entity_count + 1];
        if (!newEntities)
        {
            return;
        }

        // Copy the existing entity pointers (if any)
        for (int i = 0; i < entity_count; i++)
        {
            newEntities[i] = entities[i];
        }
        newEntities[entity_count] = entity;

        // Delete the old array
        delete[] entities;
        entities = newEntities;
        entity_count++;

        // Start the new entity
        entity->start(this->gameRef);
        entity->is_active = true;
    }

    // Remove an entity from the level
    void Level::entity_remove(Entity *entity)
    {
        if (entity_count == 0)
            return;

        int remove_index = -1;
        for (int i = 0; i < entity_count; i++)
        {
            if (entities[i] == entity)
            {
                remove_index = i;
                break;
            }
        }
        if (remove_index == -1)
            return;

        // Stop and delete the entity (only if it's not a player - players are managed externally)
        entities[remove_index]->stop(this->gameRef);
        if (!entities[remove_index]->is_player)
        {
            delete entities[remove_index];
        }

        // Allocate a new array with one fewer slot (if any remain)
        Entity **newEntities = (entity_count - 1 > 0) ? new Entity *[entity_count - 1] : nullptr;
        // Copy over all pointers except the removed one
        for (int i = 0, j = 0; i < entity_count; i++)
        {
            if (i == remove_index)
                continue;
            newEntities[j++] = entities[i];
        }

        // Free the old array and update state
        delete[] entities;
        entities = newEntities;
        entity_count--;
    }

    // Check if any entity has collided with the given entity
    bool Level::has_collided(Entity *entity) const
    {
        for (int i = 0; i < entity_count; i++)
        {
            if (entities[i] != nullptr &&
                entities[i] != entity &&
                is_collision(entity, entities[i]))
            {
                return true;
            }
        }
        return false;
    }

    // Determine if two entities are colliding
    bool Level::is_collision(const Entity *a, const Entity *b) const
    {
        return a->position.x < b->position.x + b->size.x &&
               a->position.x + a->size.x > b->position.x &&
               a->position.y < b->position.y + b->size.y &&
               a->position.y + a->size.y > b->position.y;
    }

    // Render all active entities
    void Level::render(Game *game, CameraPerspective perspective, const CameraParams *camera_params)
    {
        if (game->draw->is8bit() && clearAllowed)
        {
            // clear screen
            game->draw->clear(Vector(0, 0), game->size, game->bg_color);
        }

        // If using third person perspective but no camera params provided, calculate them from player
        CameraParams calculated_camera_params;
        if (perspective == CAMERA_THIRD_PERSON && camera_params == nullptr)
        {
            // Find the player entity to calculate 3rd person camera
            Entity *player = nullptr;
            for (int i = 0; i < entity_count; i++)
            {
                if (entities[i] != nullptr && entities[i]->is_player)
                {
                    player = entities[i];
                    break;
                }
            }

            if (player != nullptr)
            {
                // Calculate 3rd person camera position behind the player
                // Use same parameters as Player class for consistency
                float camera_distance = 2.0f; // Closer distance for better visibility

                // Normalize direction vector to ensure consistent behavior
                float dir_length = sqrtf(player->direction.x * player->direction.x + player->direction.y * player->direction.y);
                if (dir_length < 0.001f)
                {
                    // Fallback if direction is zero
                    dir_length = 1.0f;
                    player->direction = Vector(1, 0); // Default forward direction
                }
                Vector normalized_dir = Vector(player->direction.x / dir_length, player->direction.y / dir_length);

                calculated_camera_params.position = Vector(
                    player->position.x - normalized_dir.x * camera_distance,
                    player->position.y - normalized_dir.y * camera_distance);
                calculated_camera_params.direction = normalized_dir;
                calculated_camera_params.plane = player->plane;
                calculated_camera_params.height = 1.6f;
                camera_params = &calculated_camera_params;
            }
        }

        // Pass 1 — clear old positions and draw every sprite. Overlays (name/health
        // text) are deferred to pass 2 below so they always sit ON TOP of all sprites
        // (e.g. a nearby player sprite never covers an enemy's health readout).
        for (int i = 0; i < entity_count; i++)
        {
            Entity *ent = entities[i];
            if (ent != nullptr && ent->is_active)
            {
                // NOTE: the per-entity "clear old position" pass the engine normally does
                // here is intentionally omitted — the FlipWorld launcher wipes the whole
                // canvas to the background every frame, so clearing each entity's old
                // rect is redundant AND harmful: a later entity (e.g. the big dragon)
                // would black out its old rect ON TOP of sprites already drawn this pass,
                // hiding them behind a rectangle.

                if (!ent->is_visible)
                {
                    continue; // Skip rendering if entity is not visible
                }

                // Pick which sprite to draw based on horizontal facing (left/right
                // variants), into a LOCAL — we must NOT reassign ent->sprite, because
                // ent->sprite / sprite_left / sprite_right are three separate Images the
                // entity owns and frees in ~Entity. Aliasing ent->sprite to a left/right
                // image would make ~Entity double-free it (and leak the original) — which
                // crashed on the next campaign/replay map teardown.
                Image *drawSprite = ent->sprite;
                if (ent->direction.x < 0 && ent->sprite_left != nullptr)
                    drawSprite = ent->sprite_left;
                else if (ent->direction.x > 0 && ent->sprite_right != nullptr)
                    drawSprite = ent->sprite_right;

                // Only draw the 2D sprite if it exists
                if (drawSprite != nullptr)
                {
                    // FlipWorld colour port: key the blit off the SPRITE's format, not the
                    // display's. FlipWorld sprites are 1-byte "8-bit" Flipper masks (0x00 ink
                    // / 0xFF paper); on a 16-bit panel the old image(Image*) path skipped them
                    // (their RGB565 buffer is null). Draw the mask in the entity's ink colour
                    // with transparent paper so the coloured world shows through; genuine
                    // 16-bit buffer sprites still take the RGB565 path.
                    if (drawSprite->is_8bit)
                    {
                        game->draw->imageMaskPGM(Vector(ent->position.x - game->pos.x, ent->position.y - game->pos.y), drawSprite->getData(), drawSprite->size, ent->ink_color);
                    }
                    else
                    {
                        game->draw->image(Vector(ent->position.x - game->pos.x, ent->position.y - game->pos.y), drawSprite);
                    }
                }

                // Render 3D sprite if it exists
                if (ent->has3DSprite())
                {
                    //  screen size from the game draw object
                    auto screen_size = game->draw->getSize();

                    if (perspective == CAMERA_FIRST_PERSON)
                    {
                        // First person: render from player's own perspective (original behavior)
                        if (ent->is_player)
                        {
                            // Use entity's own direction and plane for rendering
                            ent->render3DSprite(game->draw, ent->position, ent->direction, ent->plane, 1.5f, screen_size);
                        }
                        else
                        {
                            // For non-player entities, render from the player's perspective
                            // We need to find the player entity to get the view parameters
                            Entity *player = nullptr;
                            for (int j = 0; j < entity_count; j++)
                            {
                                if (entities[j] != nullptr && entities[j]->is_player)
                                {
                                    player = entities[j];
                                    break;
                                }
                            }

                            if (player != nullptr)
                            {
                                ent->render3DSprite(game->draw, player->position, player->direction, player->plane, 1.5f, screen_size);
                            }
                        }
                    }
                    else if (perspective == CAMERA_THIRD_PERSON && camera_params != nullptr)
                    {
                        // Third person: render ALL entities (including player) from the external camera perspective
                        ent->render3DSprite(game->draw, camera_params->position, camera_params->direction, camera_params->plane, camera_params->height, screen_size);
                    }
                }
            }
        }

        // Pass 2 — overlays (name / health text) drawn on top of every sprite.
        for (int i = 0; i < entity_count; i++)
        {
            Entity *ent = entities[i];
            if (ent != nullptr && ent->is_active && ent->is_visible)
            {
                ent->render(game->draw, game);
            }
        }

        if (game->draw->is8bit() && clearAllowed)
        {
            // send newly drawn pixels to the display
            game->draw->swap();
        }
    }

    // Start the level
    void Level::start()
    {
        if (_start != nullptr)
        {
            _start(*this);
        }
    }

    // Stop the level
    void Level::stop()
    {
        if (_stop != nullptr)
        {
            _stop(*this);
        }
    }

    // Update all active entities
    void Level::update(Game *game)
    {
        for (int i = 0; i < entity_count; i++)
        {
            Entity *ent = entities[i];
            if (ent != nullptr && ent->is_active)
            {
                ent->update(game);
                int count = 0;
                Entity **collisions = collision_list(ent, count);
                for (int j = 0; j < count; j++)
                {
                    ent->collision(collisions[j], game);
                }
                delete[] collisions;
            }
        }
    }
}
