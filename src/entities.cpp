#include "entities.hpp"

#include "texture_ids.hpp"
#include "helper.hpp"
#include "bit_stream.hpp"

#include "json_parser.hpp"
#include "texture_ids.hpp"

#include "collision_manager.hpp"
#include "server_entity_manager.hpp"
#include "client_entity_manager.hpp"
#include "quadtree.hpp"

WeaponData g_weaponData[WEAPON_MAX_TYPES];

void _loadWeapon(JsonParser* jsonParser, WeaponData& weaponData, const char* filename)
{
    auto* doc = jsonParser->getDocument(filename);

    if (doc->HasMember("scale")) {
        weaponData.scale = (*doc)["scale"].GetFloat();
    } else {
        weaponData.scale = 1.f;
    }

    if (doc->HasMember("angle_offset")) {
        weaponData.angleOffset = (*doc)["angle_offset"].GetFloat();
    } else {
        weaponData.angleOffset = 0.f;
    }
}

void initializeWeaponData(JsonParser* jsonParser)
{
    #define LoadWeapon(weapon_name, callback_func, json_filename) \
        _loadWeapon(jsonParser, g_weaponData[WEAPON_##weapon_name], json_filename); \
        g_weaponData[WEAPON_##weapon_name].callback = &WeaponCallback_##callback_func;
    #include "weapons.inc"
    #undef LoadWeapon
}

//Placeholder callback
void WeaponCallback_devilsBow()
{
    printf("Devils bow fired!\n");
}

bool UnitStatus_equals(const UnitStatus& lhs, const UnitStatus& rhs)
{
    return lhs.stunTime == rhs.stunTime &&
           lhs.rootTime == rhs.rootTime &&
           lhs.disarmTime == rhs.disarmTime &&
           lhs.invisTime == rhs.invisTime &&
           lhs.solid == rhs.solid &&
           lhs.illusion == rhs.illusion;
}

void UnitStatus_packData(const UnitStatus& status, CRCPacket& packet)
{
    BitStream stream;

    stream.pushBit(status.stunTime > sf::Time::Zero);
    stream.pushBit(status.rootTime > sf::Time::Zero);
    stream.pushBit(status.disarmTime > sf::Time::Zero);
    stream.pushBit(status.invisTime > sf::Time::Zero);
    stream.pushBit(status.solid);
    stream.pushBit(status.illusion);

    packet << stream.popByte();
}

void C_UnitStatus_loadFromData(C_UnitStatus& status, CRCPacket& packet)
{
    BitStream stream;

    u8 byte;
    packet >> byte;
    stream.pushByte(byte);

    status.stunned = stream.popBit();
    status.rooted = stream.popBit();
    status.disarmed = stream.popBit();
    status.invisible = stream.popBit();
    status.solid = stream.popBit();
    status.illusion = stream.popBit();
}

Unit g_initialUnitData[UNIT_MAX_TYPES];
C_Unit g_initialCUnitData[UNIT_MAX_TYPES];

bool _BaseUnit_loadFromJson(JsonParser* jsonParser, UnitType type, const char* filename, _BaseUnitData& unit, u8 weaponId)
{
    auto* doc = jsonParser->getDocument(filename);

    if (doc == nullptr) {
        std::cout << "_Unit_loadFromJson error - Invalid json filename " << filename << std::endl;
        return false;
    }

    unit.type = type;

    if (doc->HasMember("flying_height")) {
        unit.flyingHeight = (*doc)["flying_height"].GetInt();
    } else {
        unit.flyingHeight = 0;
    }
    
    unit.maxHealth = (*doc)["max_health"].GetInt();

    if (doc->HasMember("health")) {
        unit.health = (*doc)["health"].GetInt();
    } else {
        unit.health = unit.maxHealth;
    }

    unit.movementSpeed = (*doc)["movement_speed"].GetInt();
    unit.maxAttacksAvailable = (*doc)["max_attacks_available"].GetInt();

    if (doc->HasMember("attacks_available")) {
        unit.attacksAvailable = (*doc)["attacks_available"].GetInt();
    } else {
        unit.attacksAvailable = unit.maxAttacksAvailable;
    }

    unit.aimAngle = 0.f;
    unit.weaponId = weaponId;
    unit.collisionRadius = (*doc)["collision_radius"].GetInt();

    return true;
}

void _Unit_loadFromJson(JsonParser* jsonParser, UnitType type, const char* filename, Unit& unit, u8 weaponId)
{
    bool result = _BaseUnit_loadFromJson(jsonParser, type, filename, unit, weaponId);

    if (result) {
        unit.dead = false;
    }
}

void _C_Unit_loadFromJson(JsonParser* jsonParser, UnitType type, const char* filename, C_Unit& unit, u16 textureId, u8 weaponId)
{
    bool result = _BaseUnit_loadFromJson(jsonParser, type, filename, unit, weaponId);

    if (result) {
        unit.textureId = textureId;
        
        auto* doc = jsonParser->getDocument(filename);

        if (doc->HasMember("scale")) {
            unit.scale = (*doc)["scale"].GetFloat();
        } else {
            unit.scale = 1.f;
        }
    }
}

void loadUnitsFromJson(JsonParser* jsonParser)
{
    #define LoadUnit(unit_name, texture_id, json_filename, weapon_id) \
        _Unit_loadFromJson(jsonParser, UNIT_##unit_name, json_filename, g_initialUnitData[UNIT_##unit_name], weapon_id);

    #include "units.inc"
    #undef LoadUnit
}

void C_loadUnitsFromJson(JsonParser* jsonParser)
{
    #define LoadUnit(unit_name, texture_id, json_filename, weapon_id) \
        _C_Unit_loadFromJson(jsonParser, UNIT_##unit_name, json_filename, g_initialCUnitData[UNIT_##unit_name], TextureId::texture_id, weapon_id);

    #include "units.inc"
    #undef LoadUnit
}

void Unit_init(Unit& unit, UnitType type)
{
    if (type <= UNIT_NONE || type >= UNIT_MAX_TYPES) {
        return;
    }

    unit = g_initialUnitData[type];
}

void C_Unit_init(C_Unit& unit, UnitType type)
{
    if (type <= UNIT_NONE || type >= UNIT_MAX_TYPES) {
        return;
    }

    unit = g_initialCUnitData[type];
}

void Unit_packData(const Unit& unit, const Unit* prevUnit, CRCPacket& outPacket)
{
    UnitStatus_packData(unit.status, outPacket);

    BitStream mainBits;
    
    bool posXChanged = !prevUnit || unit.pos.x != prevUnit->pos.x;
    mainBits.pushBit(posXChanged);

    bool posYChanged = !prevUnit || unit.pos.y != prevUnit->pos.y;
    mainBits.pushBit(posYChanged);

    bool teamIdChanged = !prevUnit || unit.teamId != prevUnit->teamId;
    mainBits.pushBit(teamIdChanged);

    bool flyingHeightChanged = !prevUnit || unit.flyingHeight != prevUnit->flyingHeight;
    mainBits.pushBit(flyingHeightChanged);
    
    bool maxHealthChanged = !prevUnit || unit.maxHealth != prevUnit->maxHealth;
    mainBits.pushBit(maxHealthChanged);

    bool healthChanged = !prevUnit || unit.health != prevUnit->health;
    mainBits.pushBit(healthChanged);

    bool maxAttacksAvailableChanged = !prevUnit || unit.maxAttacksAvailable != prevUnit->maxAttacksAvailable;
    mainBits.pushBit(maxAttacksAvailableChanged);

    bool attacksAvailableChanged = !prevUnit || unit.attacksAvailable != prevUnit->attacksAvailable;
    mainBits.pushBit(attacksAvailableChanged);

    bool aimAngleChanged = !prevUnit || unit.aimAngle != prevUnit->aimAngle;
    mainBits.pushBit(aimAngleChanged);

    bool collisionRadiusChanged = !prevUnit || unit.collisionRadius != prevUnit->collisionRadius;
    mainBits.pushBit(collisionRadiusChanged);

    //send the flags
    u8 byte1 = mainBits.popByte();
    u8 byte2 = mainBits.popByte();
    outPacket << byte1 << byte2;

    //this didn't work (probably the order was incorrect??)
    // outPacket << mainBits.popByte() << mainBits.popByte();

    //send the data if it has changed
    if (posXChanged) {
        outPacket << unit.pos.x;
    }

    if (posYChanged) {
        outPacket << unit.pos.y;
    }

    if (teamIdChanged) {
        outPacket << unit.teamId;
    }

    if (flyingHeightChanged) {
        outPacket << unit.flyingHeight;
    }

    if (maxHealthChanged) {
        outPacket << unit.maxHealth;
    }

    if (healthChanged) {
        outPacket << unit.health;
    }

    if (maxAttacksAvailableChanged) {
        outPacket << unit.maxAttacksAvailable;
    }

    if (attacksAvailableChanged) {
        outPacket << unit.attacksAvailable;
    }

    if (aimAngleChanged) {
        outPacket << Helper_angleTo16bit(unit.aimAngle);
    }
    
    if (collisionRadiusChanged) {
        outPacket << unit.collisionRadius;
    }
}

void C_Unit_loadFromData(C_Unit& unit, CRCPacket& inPacket)
{
    C_UnitStatus_loadFromData(unit.status, inPacket);

    BitStream mainBits;

    u8 byte;
    inPacket >> byte;
    mainBits.pushByte(byte);
    inPacket >> byte;
    mainBits.pushByte(byte);

    bool posXChanged = mainBits.popBit();
    bool posYChanged = mainBits.popBit();
    bool teamIdChanged = mainBits.popBit();
    bool flyingHeightChanged = mainBits.popBit();
    bool maxHealthChanged = mainBits.popBit();
    bool healthChanged = mainBits.popBit();
    bool maxAttacksAvailableChanged = mainBits.popBit();
    bool attacksAvailableChanged = mainBits.popBit();
    bool aimAngleChanged = mainBits.popBit();
    bool collisionRadiusChanged = mainBits.popBit();

    if (posXChanged) {
        inPacket >> unit.pos.x;
    }

    if (posYChanged) {
        inPacket >> unit.pos.y;
    }

    if (teamIdChanged) {
        inPacket >> unit.teamId;
    }

    if (flyingHeightChanged) {
        inPacket >> unit.flyingHeight;
    }

    if (maxHealthChanged) {
        inPacket >> unit.maxHealth;
    }

    if (healthChanged) {
        inPacket >> unit.health;
    }

    if (maxAttacksAvailableChanged) {
        inPacket >> unit.maxAttacksAvailable;
    }

    if (attacksAvailableChanged) {
        inPacket >> unit.attacksAvailable;
    }

    if (aimAngleChanged) {
        u16 angle16bit;
        inPacket >> angle16bit;
        unit.aimAngle = Helper_angleFrom16bit(angle16bit);
    }

    if (collisionRadiusChanged) {
        inPacket >> unit.collisionRadius;
    }
}

//used by client and server
Vector2 _moveColliding_impl(float collisionRadius, const Vector2& newPos, const _BaseUnitData* collisionUnit)
{
    float distance = Helper_vec2length(collisionUnit->pos - newPos);
    float targetDistance = std::abs(collisionUnit->collisionRadius + collisionRadius - distance);

    //push the unit far away if it's too close
    if (distance == 0) {
        // printf("%f\n", distance);
        return Helper_vec2unitary(Vector2(1.f, 1.f)) * targetDistance;
    }

    return Helper_vec2unitary(newPos - collisionUnit->pos) * targetDistance;
}

//has to work for both Unit and C_Unit (kind of ugly, but we need to access status.solid for both unit types)
template<typename UnitType>
//checks two units can collide (not the same unit, both solid, same flying height, etc)
bool _Unit_canCollide(const UnitType& unit1, const UnitType& unit2)
{
    //@TODO: Check flyingHeight is correct
    return unit1.uniqueId != unit2.uniqueId && unit1.status.solid && unit2.status.solid;
}

//move while checking collision
void Unit_moveColliding(Unit& unit, const Vector2& newPos, const ManagersContext& context, bool force)
{
    if (!force && newPos == unit.pos) return;

    //@TODO: Check how this behaves when multiple collision happen at once
    //maybe we have to update position between iterations?

    const Unit* closestUnit = nullptr;
    float closestDistance = 0.f;

    if (unit.status.solid) {      
        const Circlef circle(newPos, unit.collisionRadius);

        auto query = context.collisionManager->getQuadtree()->QueryIntersectsRegion(BoundingBody<float>(circle));

        while (!query.EndOfQuery()) {
            u32 collisionUniqueId = query.GetCurrent()->uniqueId;
            const Unit* collisionUnit = context.entityManager->units.atUniqueId(collisionUniqueId);

            if (collisionUnit) {
                if (_Unit_canCollide(unit, *collisionUnit)) {
                    // _pushToHits(hits, collisionUnit, newPos);
                    float distance = Helper_vec2length(collisionUnit->pos - newPos);

                    if (!closestUnit || distance < closestDistance) {
                        closestUnit = collisionUnit;
                        closestDistance = distance;
                    }
                }
            }

            query.Next();
        }


        if (closestUnit) {
            unit.pos = newPos + _moveColliding_impl(unit.collisionRadius, newPos, closestUnit);

        } else {
            unit.pos = newPos;
        }

    } else {
        unit.pos = newPos;
    }

    context.collisionManager->onUpdateUnit(unit.uniqueId, unit.pos, unit.collisionRadius);
}

void Unit_update(Unit& unit, sf::Time eTime, const ManagersContext& context)
{
    Vector2 newPos = unit.pos;

    //@DELETE
    //we're adding 20 units before we add the player (TESTING)
    //so we move all units but the player randomly
    if (unit.uniqueId < 21 && unit.vel == Vector2()) {
        unit.vel = Vector2(rand() % 200 - 100.f, rand() % 200 - 100.f);
    }

    newPos += unit.vel * eTime.asSeconds();
    //other required movement (like dragged movement, forces and friction, etc)

    if (newPos != unit.pos) {
        Unit_moveColliding(unit, newPos, context);
    }

    //@DELETE
    unit.aimAngle += 100 * eTime.asSeconds();
}

void C_Unit_interpolate(C_Unit& unit, const C_Unit* prevUnit, const C_Unit* nextUnit, double t, double d, bool controlled)
{
    if (prevUnit && nextUnit) {
        Vector2 newPos = unit.pos;
        float newAimAngle = unit.aimAngle;

        //only interpolate position and aimAngle for units we're not controlling
        if (!controlled) {
            newPos = Helper_lerpVec2(prevUnit->pos, nextUnit->pos, t, d);
            newAimAngle = Helper_lerpAngle(prevUnit->aimAngle, nextUnit->aimAngle, t, d);
        }

        //copy all fields
        //@TODO: Some of them might require interpolation (animation data for example)
        unit = *nextUnit;

        //apply interpolation (or no copying at all) to special fields
        unit.pos = newPos;
        unit.aimAngle = newAimAngle;
    }

    if (!prevUnit && nextUnit) {
        //This is the first time the entity is being rendered
        //creation callbacks

        //@TODO: Keep track of already sent entities
        //to see which entities are being sent for the first time
        //in the entire game
        //store it as ranges: entities between [1, 130] and [136, 400] were already sent
        //this is probably the most efficient method in memory (sorted range pairs)
        //or just a simple hash table
    }

    if (prevUnit && !nextUnit) {
        //This is the last time entity is being rendered
        //destruction callbacks
    }
}

void Unit_applyInput(Unit& unit, const PlayerInput& input, const ManagersContext& context)
{
    //@TODO: Check flags to see if we can actually move (stunned, rooted, etc)

    Vector2 newPos = unit.pos;

    bool moved = PlayerInput_repeatAppliedInput(input, newPos, unit.movementSpeed);
    unit.aimAngle = input.aimAngle;

    if (moved) {
        Unit_moveColliding(unit, newPos, context);
    }
}

//we don't need to use this function outside this file for now
void _C_Unit_predictMovement(const C_Unit& unit, Vector2& newPos, const C_ManagersContext& context)
{
    //we don't update the circle to mimic how it works in the server
    //where we only use one Query
    const Circlef circle(newPos, unit.collisionRadius);

    const C_Unit* closestUnit = nullptr;
    float closestDistance = 0.f;

    for (int i = 0; i < context.entityManager->units.firstInvalidIndex(); ++i) {
        const C_Unit& otherUnit = context.entityManager->units[i];

        if (_Unit_canCollide(unit, otherUnit)) {
            const Circlef otherCircle(otherUnit.pos, otherUnit.collisionRadius);

            if (circle.intersects(otherCircle)) {
                // _pushToHits(hits, &otherUnit, newPos);
                float distance = Helper_vec2length(otherUnit.pos - newPos);

                if (!closestUnit || distance < closestDistance) {
                    closestUnit = &otherUnit;
                    closestDistance = distance;
                }
            }
        }
    }

    if (closestUnit) {
        newPos = newPos + _moveColliding_impl(unit.collisionRadius, newPos, closestUnit);
    }
}

void C_Unit_applyInput(const C_Unit& unit, Vector2& unitPos, PlayerInput& input, const C_ManagersContext& context, sf::Time dt)
{
    //@TODO: Check flags to see if we can actually move (stunned, rooted, etc)

    bool moved = PlayerInput_applyInput(input, unitPos, unit.movementSpeed, dt);

    if (moved) {
        _C_Unit_predictMovement(unit, unitPos, context);
    }
}
