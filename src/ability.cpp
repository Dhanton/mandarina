#include "ability.hpp"

#include "helper.hpp"
#include "unit.hpp"
#include "game_mode.hpp"
#include "buff.hpp"
#include "buffs/recharge_ability_buff.hpp"

u8 Ability::stringToType(const std::string& typeStr)
{
    if (typeStr == "NONE") return ABILITY_NONE;

    #define DoAbility(class_name, type, json_id) \
        if (typeStr == #type) return ABILITY_##type;
    #include "abilities.inc"
    #undef DoAbility

    return ABILITY_NONE;
}

u8 Ability::getAbilityType() const
{
    return m_type;
}

void Ability::setAbilityType(u8 abilityType)
{
    m_type = abilityType;
}

u8 Ability::getIconTextureId() const
{
    return m_iconTextureId;
}

void Ability::setIconTextureId(u8 iconTextureId)
{
    m_iconTextureId = iconTextureId;
}

bool Ability::hasHotkey() const
{
    return true;
}

void Ability::applyServerCorrection(float diff)
{

}

u16 Ability::takeSnapshot() const
{
    return 0;
}

void Ability::packData(CRCPacket& outPacket) const
{
    outPacket << takeSnapshot();
}

float Ability::getPercentage() const
{
    return 1.f;
}

u16 Ability::getMaxTime() const
{
    return 0;
}

float Ability::getTotalPercentage() const
{
    return 1.f;
}

bool Ability::areBuffsAdded() const
{
    return m_buffsAdded;
}

void Ability::addBuffsToCaster(Unit* unit, const ManagersContext& context)
{
    m_buffsAdded = true;
}

void Ability::loadFromJson(const rapidjson::Document& doc)
{
    m_buffsAdded = false;
}

void CooldownAbility::update(sf::Time eTime, GameMode* gameMode)
{
    float globalTimeMultiplier = 1.f;

    if (gameMode) {
        globalTimeMultiplier = gameMode->getAbilityTimeMultiplier();
    }

    if (m_currentCharges < m_maxCharges) {
        float delta = m_currentCooldown - eTime.asSeconds() * globalTimeMultiplier;

        if (delta <= 0.f) {
            m_currentCharges++;

            if (m_currentCharges < m_maxCharges) {
                m_currentCooldown = m_cooldown + delta;
            } else {
                m_currentCooldown = 0.f;
            }
        } else {
            m_currentCooldown = delta;
        }
    }

    if (m_currentNextChargeDelay < m_nextChargeDelay) {
        m_currentNextChargeDelay += eTime.asSeconds() * globalTimeMultiplier;
    }
}

bool CooldownAbility::canBeCasted(const Status& status) const
{
    return m_currentCharges > 0 && m_currentNextChargeDelay >= m_nextChargeDelay;
}

void CooldownAbility::refresh()
{
    m_currentCharges = m_maxCharges;
    m_currentCooldown = 0.f;
}

void CooldownAbility::applyServerCorrection(float diff)
{
    const float finalPercentage = Helper_clamp(getTotalPercentage() + diff, 0.f, 1.f);

    m_currentCharges = std::floor(finalPercentage * static_cast<float>(m_maxCharges));

    if (m_currentCharges < m_maxCharges) {
        m_currentCooldown = (1.f - finalPercentage * static_cast<float>(m_maxCharges) + static_cast<float>(m_currentCharges)) * m_cooldown;
    } else {
        m_currentCooldown = 0;
    }
}

u16 CooldownAbility::takeSnapshot() const
{
    return Helper_percentageTo16bit(getTotalPercentage());
}

void CooldownAbility::toZero()
{
    m_currentCharges = 0;
    m_currentCooldown = m_cooldown;
}

void CooldownAbility::loadFromJson(const rapidjson::Document& doc)
{
    Ability::loadFromJson(doc);

    m_maxCharges = doc["charges"].GetUint();
    m_cooldown = doc["cooldown"].GetFloat();

    if (doc.HasMember("next_charge_delay")) {
        m_nextChargeDelay = doc["next_charge_delay"].GetFloat();
    } else {
        m_nextChargeDelay = 0.f;
    }
    
    m_currentNextChargeDelay = m_nextChargeDelay;

    if (doc.HasMember("starting_charges")) {
        m_currentCharges = doc["starting_charges"].GetUint();
    } else {
        m_currentCharges = m_maxCharges;
    }

    if (doc.HasMember("starting_cooldown")) {
        m_currentCooldown = doc["starting_cooldown"].GetFloat();
    } else {
        m_currentCooldown = 0.f;
    }
}

u8 CooldownAbility::getMaxCharges() const
{
    return m_maxCharges;
}

u8 CooldownAbility::getCurrentCharges() const
{
    return m_currentCharges;
}

float CooldownAbility::getCooldown() const
{
    return m_cooldown;
}

float CooldownAbility::getCurrentCooldown() const
{
    return m_currentCooldown;
}

float CooldownAbility::getPercentage() const
{
    return 1.f - m_currentCooldown/m_cooldown;
}

u16 CooldownAbility::getMaxTime() const
{
    return static_cast<u16>(m_cooldown);
}

float CooldownAbility::getTotalPercentage() const
{
    if (m_currentCharges == m_maxCharges) {
        return 1.f;
    }
    
    return (1.f - m_currentCooldown/m_cooldown)/static_cast<float>(m_maxCharges) + static_cast<float>(m_currentCharges)/static_cast<float>(m_maxCharges);
}

void CooldownAbility::onCastUpdate()
{
    if (m_currentCharges != 0) {
        m_currentCharges--;
        m_currentNextChargeDelay = 0.f;
        
        if (m_currentCooldown == 0.f) {
            m_currentCooldown = m_cooldown;
        }
    }
}

void RechargeAbility::update(sf::Time eTime, GameMode* gameMode)
{
    float globalTimeMultiplier = 1.f;

    if (gameMode) {
        globalTimeMultiplier = gameMode->getAbilityTimeMultiplier();
    }

    //default speed is one charge every 3 seconds
    addToPercentage(0.0033333f * eTime.asSeconds() * m_timeRechargeMultiplier * globalTimeMultiplier);
}

bool RechargeAbility::canBeCasted(const Status& status) const
{
    return m_percentage >= 1.f;
}

void RechargeAbility::refresh()
{
    m_percentage = 1.f;
}

void RechargeAbility::applyServerCorrection(float diff)
{
    m_percentage = Helper_clamp(m_percentage + diff, 0.f, 1.f);
}

u16 RechargeAbility::takeSnapshot() const
{
    return Helper_percentageTo16bit(getTotalPercentage());
}

void RechargeAbility::loadFromJson(const rapidjson::Document& doc)
{
    Ability::loadFromJson(doc);

    if (doc.HasMember("starting_percentage")) {
        m_percentage = Helper_clamp(doc["starting_percentage"].GetFloat(), 0.f, 1.f);
    } else {
        m_percentage = 0.f;
    }

    if (doc.HasMember("recharge_multiplier")) {
        m_rechargeMultiplier = doc["recharge_multiplier"].GetFloat();
    } else {
        m_rechargeMultiplier = 1.f;
    }

    if (doc.HasMember("time_recharge_multiplier")) {
        m_timeRechargeMultiplier = doc["time_recharge_multiplier"].GetFloat();
    } else {
        m_timeRechargeMultiplier = 1.f;
    }
}

float RechargeAbility::getPercentage() const
{
    return m_percentage;
}

void RechargeAbility::addToPercentage(float amount)
{
    m_percentage = std::min(1.f, m_percentage + amount);
}

float RechargeAbility::getTotalPercentage() const
{
    return m_percentage;
}

void RechargeAbility::addBuffsToCaster(Unit* unit, const ManagersContext& context)
{
    Ability::addBuffsToCaster(unit, context);

    Buff* buff = unit->addBuff(BUFF_RECHARGE_ABILITY);
    
    if (buff) {
        static_cast<RechargeAbilityBuff*>(buff)->setParentAbility(this, context);
    }
}

void RechargeAbility::onCastUpdate()
{
    m_percentage = 0.f;
}

float RechargeAbility::getRechargeMultiplier() const
{
    return m_rechargeMultiplier;
}

float RechargeAbility::getTimeRechargeMultiplier() const
{
    return m_timeRechargeMultiplier;
}

PassiveAbility* PassiveAbility::clone() const
{
    return new PassiveAbility(*this);
}

void PassiveAbility::onCast(Unit* caster, const ManagersContext& context, u16 clientDelay)
{

}

void PassiveAbility::C_onCast(C_Unit* unit, CasterComponent* caster, Vector2& pos, const C_ManagersContext& context, u32 inputId, bool repeating)
{

}

void PassiveAbility::update(sf::Time eTime, GameMode* gameMode)
{

}

bool PassiveAbility::canBeCasted(const Status& status) const
{
    return true;
}

void PassiveAbility::refresh()
{

}

bool PassiveAbility::hasHotkey() const
{
    return false;
}

void PassiveAbility::addBuffsToCaster(Unit* unit, const ManagersContext& context)
{
    Ability::addBuffsToCaster(unit, context);

    Buff* buff = unit->addBuff(m_buffType);
}

void PassiveAbility::loadFromJson(const rapidjson::Document& doc)
{
    Ability::loadFromJson(doc);

    if (doc.HasMember("buff")) {
        m_buffType = Buff::stringToType(doc["buff"].GetString());   
    } else {
        m_buffType = BUFF_NONE;
    }
}
