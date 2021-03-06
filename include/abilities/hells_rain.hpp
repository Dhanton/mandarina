#pragma once

#include <random>

#include "ability.hpp"

class HellsRainAbility : public RechargeAbility
{
public:
    HellsRainAbility* clone() const;

    void onCast(Unit* caster, const ManagersContext& context, u16 clientDelay);
    void C_onCast(C_Unit* unit, CasterComponent* caster, Vector2& casterPos, const C_ManagersContext& context, u32 inputId, bool repeating);

    void loadFromJson(const rapidjson::Document& doc);

private:
    Vector2 calculateRndPos(std::uniform_int_distribution<u16>& distr);

private:
    u16 m_bubbleNumber;
    u16 m_spreadAngle;
    u16 m_spreadDistance;
};
