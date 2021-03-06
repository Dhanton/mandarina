#pragma once

#include "buff.hpp"

class StormBuff : public Buff
{
public:
    StormBuff* clone() const;

    virtual void onUpdate(sf::Time eTime);

    //these parameters are set by game mode when creating the buff
    void setDamagePerSecond(u16 dmgPerSecond);
    void setMultiplierTimeTreshold(sf::Time multiplierTimeTreshold);
    void setDamageMultiplier(float dmgMultiplier);

private:
    u16 m_damagePerSecond;
    sf::Time m_multiplierTimeTreshold;
    float m_damageMultiplier;

    sf::Time m_timer;
    sf::Time m_tresholdTimer;
};
