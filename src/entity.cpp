#include "entity.hpp"

#include <SFML/Graphics/Sprite.hpp>
#include "helper.hpp"
#include "client_entity_manager.hpp"

BaseEntityComponent::BaseEntityComponent(u8 entityType, u32 uniqueId):
    m_uniqueId(uniqueId),
    m_type(entityType)
{

}

u32 BaseEntityComponent::getUniqueId() const
{
    return m_uniqueId;
}

Vector2 BaseEntityComponent::getPosition() const
{
    return m_pos;
}

void BaseEntityComponent::setPosition(const Vector2& pos)
{
    m_pos = pos;
}

Vector2 BaseEntityComponent::getVelocity() const
{
    return m_vel;
}

void BaseEntityComponent::setVelocity(const Vector2& vel)
{
    m_vel = vel;
}


u8 BaseEntityComponent::getTeamId() const
{
    return m_teamId;
}

void BaseEntityComponent::setTeamId(u8 teamId)
{
    m_teamId = teamId;
}

u8 BaseEntityComponent::getCollisionRadius() const
{
    return m_collisionRadius;
}

void BaseEntityComponent::setCollisionRadius(u8 radius)
{
    m_collisionRadius = radius;
}

u8 BaseEntityComponent::getFlyingHeight() const
{
    return m_flyingHeight;
}

void BaseEntityComponent::setFlyingHeight(u8 flyingHeight)
{
    m_flyingHeight = flyingHeight;
}

bool BaseEntityComponent::isInBush() const
{
    return m_inBush;
}

bool BaseEntityComponent::isSolid() const
{
    return m_solid;
}

void BaseEntityComponent::setSolid(bool solid)
{
    m_solid = solid;
}

bool BaseEntityComponent::canCollide(const BaseEntityComponent& otherEntity) const
{
    //@TODO: should we check flyingheight as well?
    return m_uniqueId != otherEntity.m_uniqueId && m_solid && otherEntity.m_solid;
}

Entity::Entity(u8 entityType, u32 uniqueId):
    BaseEntityComponent(uniqueId, entityType)
{

}

bool Entity::shouldSendToTeam(u8 teamId) const
{
    return true;
}

void Entity::applyInput(const PlayerInput& input, const ManagersContext& context, u16 clientDelay)
{

}

void Entity::onQuadtreeInserted(const ManagersContext& context)
{

}

bool Entity::isDead() const
{
    return m_dead;
}

C_Entity::C_Entity(u8 entityType, u32 uniqueId):
    BaseEntityComponent(uniqueId, entityType)
{
    m_textureId = 0;
    m_scale = 1.f;
}

void C_Entity::updateControlledAngle(float newAngle)
{

}

void C_Entity::applyMovementInput(Vector2& pos, PlayerInput& input, const C_ManagersContext& context, sf::Time dt)
{

}

void C_Entity::applyAbilitiesInput(const PlayerInput& input, const C_ManagersContext& context)
{

}

u16 C_Entity::getControlledMovementSpeed() const
{
    return 0;
}

void C_Entity::updateLocallyVisible(const C_ManagersContext& context)
{

}

void C_Entity::localReveal(C_Entity* entity)
{

}

void C_Entity::insertRenderNode(const C_ManagersContext& managersContext, const Context& context) const
{
    std::vector<RenderNode>& renderNodes = managersContext.entityManager->getRenderNodes();
    renderNodes.emplace_back(m_flyingHeight, m_uniqueId, (float) m_collisionRadius);
    sf::Sprite& sprite = renderNodes.back().sprite;

    sprite.setTexture(context.textures->getResource(m_textureId));
    sprite.setScale(m_scale, m_scale);
    sprite.setOrigin(sprite.getLocalBounds().width/2.f, sprite.getLocalBounds().height/2.f);
    sprite.setPosition(m_pos);
}

HealthComponent::HealthComponent()
{
    //default values
    m_health = 1;
    m_maxHealth = 1;
}

void HealthComponent::dealDamage(u16 damage, Entity* source)
{
    m_health = std::max((int) m_health - (int) damage, 0);
}

void HealthComponent::heal(u16 amount, Entity* source)
{
    m_health = std::min((int) m_health + (int) amount, (int) m_maxHealth);
}

void HealthComponent::onDealDamage(u16 damage, Entity* source)
{

}

void HealthComponent::onHeal(u16 amount, Entity* source)
{

}

u16 HealthComponent::getHealth() const
{
    return m_health;
}

u16 HealthComponent::getMaxHealth() const
{
    return m_maxHealth;
}

TrueSightComponent::TrueSightComponent()
{
    //default value
    m_trueSightRadius = 0;
}

void TrueSightComponent::setTrueSightRadius(u8 trueSightRadius)
{
    m_trueSightRadius = trueSightRadius;
}

u8 TrueSightComponent::getTrueSightRadius() const
{
    return m_trueSightRadius;
}

InvisibleComponent::InvisibleComponent()
{
    m_locallyHidden = false;
    m_forceSent = false;
    m_visionFlags = 0;
    m_teamSentFlags = 0;
    m_invisible = false;
}

void InvisibleComponent::resetInvisibleFlags()
{
    m_visionFlags = 0;
    m_teamSentFlags = 0;
}

void InvisibleComponent::markToSend(u8 teamId)
{
    m_teamSentFlags |= (1 << teamId);
}

void InvisibleComponent::reveal(u8 teamId)
{
    m_visionFlags |= (1 << teamId);
}

void InvisibleComponent::setInvisible(bool invisible)
{
    m_invisible = invisible;
}

bool InvisibleComponent::isInvisible() const
{
    return m_invisible;
}

bool InvisibleComponent::isInvisibleOrBush() const
{
    return m_invisible || _invisible_inBush();
}

bool InvisibleComponent::isRevealedForTeam(u8 teamId) const
{
    return (m_visionFlags & (1 << teamId));
}

bool InvisibleComponent::isMarkedToSendForTeam(u8 teamId) const
{
    return (m_teamSentFlags & (1 << teamId));
}

bool InvisibleComponent::isVisibleForTeam(u8 teamId) const
{
    if (_invisible_teamId() == teamId || !isInvisibleOrBush()) {
        return true;
    } else {
        return isRevealedForTeam(teamId);
    }
}

bool InvisibleComponent::shouldBeHiddenFrom(TrueSightComponent& otherEntity) const
{
    if (_invisible_teamId() == otherEntity._trueSight_teamId()) {
        return false;
    } else if (Helper_vec2length(_invisible_pos() - otherEntity._trueSight_pos()) >= otherEntity.getTrueSightRadius()) {
        return true;
    } else {
        return _invisible_inBush() ? !otherEntity._trueSight_inBush() : false;
    }
}

bool InvisibleComponent::isLocallyHidden() const
{
    return m_locallyHidden;
}

void InvisibleComponent::setLocallyHidden(bool locallyHidden)
{
    m_locallyHidden = locallyHidden;
}

bool InvisibleComponent::isForceSent() const
{
    return m_forceSent;
}

void InvisibleComponent::setForceSent(bool forceSent)
{
    m_forceSent = forceSent;
}