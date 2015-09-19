//
// C++ Implementation: ac_bot_ai
//
// Description: The AI part of the bot for ac is here(navigation, shooting etc)
//
//
// Author:  <rickhelmus@gmail.com>
//


#include "cube.h"
#include "bot.h"

#ifdef AC_CUBE

weaponinfo_s WeaponInfoTable[NUMGUNS] =
{
    // DD: desired distance
    // iD: ideal distance
    // FD: fire distance
    // mA: min desired Ammo
    // ---- Type :    minDD,   maxDD,     iD,  minFD,   maxFD,  mA
    { TYPE_MELEE,      0.0f,    5.0f,   4.0f,   0.0f,    5.0f,   1 }, // KNIFE
    { TYPE_NORMAL,     5.0f,   90.0f,  24.0f,   0.0f,  100.0f,   3 }, // PISTOL
    { TYPE_SHOTGUN,    4.0f,   16.0f,   6.0f,   0.0f,   32.0f,   2 }, // SHOTGUN
    { TYPE_AUTO,       5.0f,   64.0f,  20.0f,   0.0f,   65.0f,   5 }, // SUBGUN
    { TYPE_SNIPER,    25.0f,  110.0f,  70.0f,   0.0f,  175.0f,   3 }, // SNIPER
    { TYPE_AUTO,      10.0f,   92.0f,  45.0f,   0.0f,  135.0f,   5 }, // ASSAULT
    { TYPE_GRENADE,   50.0f,   85.0f,  60.0f,  15.0f,  999.0f,   1 }, // GRENADE
    { TYPE_NORMAL,     5.0f,   90.0f,  30.0f,   0.0f,  110.0f,   3 }, // AKIMBO
    { TYPE_SNIPER,    40.0f,  130.0f,  80.0f,   0.0f,  200.0f,   2 }, // BOLT
    { TYPE_AUTO,      30.0f,   80.0f,   0.0f,   0.0f,  150.0f,   3 }, // HEAL
    { TYPE_MELEE,      2.0f,    7.0f,   7.0f,   0.0f,    9.0f,   1 }, // SWORD
    { TYPE_ROCKET,    30.0f,  120.0f,  60.0f,  10.0f,  200.0f,   1 }, // RPG
    { TYPE_AUTO,      10.0f,  120.0f,  48.0f,   0.0f,  125.0f,   5 }, // ASSAULT2
    { TYPE_SNIPER,    30.0f,  120.0f,  75.0f,   0.0f,  200.0f,   2 }, // SNIPER2
    { TYPE_SNIPER,    25.0f,  110.0f,  70.0f,   0.0f,  175.0f,   3 }, // SNIPER3
    { TYPE_NORMAL,     5.0f,   90.0f,  24.0f,   0.0f,  100.0f,   3 }, // PISTOL2
};

// Code of CACBot - Start

bool CACBot::ChoosePreferredWeapon()
{
    if(lastmillis < m_iChangeWeaponDelay) return false;
    short bestWeapon = m_pMyEnt->gunselect;
    short bestWeaponScore = SHRT_MIN;

    short sWeaponScore;
    float flDist = GetDistance(m_pMyEnt->enemy->o);
    char bestWeap[NUMGUNS];
    loopi(NUMGUNS) bestWeap[i] = 0;
    // Choose a weapon
    for(int i=0; i<NUMGUNS; i++)
    {
        sWeaponScore = primary_weap(i) ? 5 : 0; // Primary are usually better

        if (!m_pMyEnt->mag[i] && WeaponInfoTable[i].eWeaponType != TYPE_MELEE) continue;

        sWeaponScore += 3*m_pMyEnt->weapstats[i].kills/(m_pMyEnt->weapstats[i].deaths ? m_pMyEnt->weapstats[i].deaths : 0.5f);
        sWeaponScore -= 2*m_pMyEnt->weapstats[i].deaths/(m_pMyEnt->weapstats[i].kills ? m_pMyEnt->weapstats[i].kills : 0.5f);

        if((flDist >= WeaponInfoTable[i].flMinDesiredDistance) &&
            (flDist <= WeaponInfoTable[i].flMaxDesiredDistance))
        { // In desired range for this weapon
            sWeaponScore += 5; // Increase score much

            if(i == GUN_PISTOL || WeaponInfoTable[i].eWeaponType == TYPE_MELEE)
            {
                if(WeaponInfoTable[m_pMyEnt->primary].eWeaponType == TYPE_SNIPER) sWeaponScore += 10; // At a close range, knife & pistol are strong with a sniper like primary weapon
                if(WeaponInfoTable[m_pMyEnt->primary].eWeaponType == TYPE_SHOTGUN) sWeaponScore -= 2; // Penalize a bit knife and pistol on close range with shotgun
            }

            if(flDist < 50 && WeaponInfoTable[i].eWeaponType == TYPE_SNIPER) sWeaponScore -= 5;
        }
        else if (((flDist < WeaponInfoTable[i].flMinFireDistance) ||
                (flDist > WeaponInfoTable[i].flMaxFireDistance)) && i != GUN_GRENADE && i != GUN_RPG)
            continue; // Wrong distance for this weapon

        if(i == GUN_GRENADE) sWeaponScore += 30; // Nades have high priority

        // Score on the distance to the ideal distance
        float flIdealDiff = fabs(flDist - WeaponInfoTable[i].flIdealDistance);

        if(flIdealDiff < 1.0f) sWeaponScore += 10;
        else if(flIdealDiff <= 5.0f) sWeaponScore += 4;
        else if(flIdealDiff <= 7.5f) sWeaponScore += 2;
        else if(flIdealDiff <= 10.0f) ++sWeaponScore;

        // Now rate the weapon on available ammo in magazine...
        if (WeaponInfoTable[i].sMinDesiredAmmo > 0)
        {
            // Calculate how much percent of the min desired ammo in mag the bot has
            float flDesiredPercent = (float(m_pMyEnt->mag[i])/float(WeaponInfoTable[i].sMinDesiredAmmo))*100.0f;

            if (flDesiredPercent >= 400.0f)
                sWeaponScore += 10;
            else if (flDesiredPercent >= 200.0f)
                sWeaponScore += 8;
            else if (flDesiredPercent >= 100.0f)
                sWeaponScore += 3;
            else if (flDesiredPercent >= 50.0f)
                sWeaponScore -= 2;
            else
                sWeaponScore -= 5;
        }
        else sWeaponScore += 15; // Not needing ammo is an advantage...

        if(sWeaponScore > bestWeaponScore)
        {
            bestWeaponScore = sWeaponScore;
            bestWeapon = i;
            loopi(bestWeapon) bestWeap[i] = 0;
            bestWeap[bestWeapon] = 1;
        }
        else if(sWeaponScore == bestWeaponScore) bestWeap[i] = 1;
    }
    int tie = 0;
    loopi(NUMGUNS) tie += bestWeap[i];
    if (tie)
    {
        if (tie > 1)
        {
            int select = rnd(tie), i = 0;
            while (select >= 0)
            {
                bestWeapon = i;
                select -= bestWeap[i++];
            }
        }
    }
    else bestWeapon = GUN_KNIFE;

    return SelectGun(bestWeapon);
};

void CACBot::Reload(int Gun)
{

};

entity *CACBot::SearchForEnts(bool bUseWPs, float flRange, float flMaxHeight)
{
    /* Entities are scored on the following things:
        - Visibility
        - For ammo: Need(ie has this bot much of this type or not)
        - distance
    */

    float flDist;
    entity *pNewTargetEnt = NULL;
    waypoint_s *pWptNearBot = NULL, *pBestWpt = NULL;
    short sScore, sHighestScore = 0;

    if ((WaypointClass.m_iWaypointCount >= 1) && bUseWPs)
        pWptNearBot = GetNearestWaypoint(15.0f);

#ifdef WP_FLOOD
    if (!pWptNearBot && bUseWPs)
        pWptNearBot = GetNearestFloodWP(5.0f);
#endif

    loopv(ents)
    {
        sScore = 0;
        entity &e = ents[i];
        vec o(e.x, e.y, S(e.x, e.y)->floor+player1->eyeheight);

        if (!ents[i].spawned) continue;
        if (OUTBORD(e.x, e.y)) continue;

        bool bInteresting = false;
        short sAmmo = 0, sMaxAmmo = 0;

        switch(e.type)
        {
            case I_CLIPS:
                sMaxAmmo = ammostats[m_pMyEnt->secondary].max;
                bInteresting = (m_pMyEnt->ammo[m_pMyEnt->secondary]<sMaxAmmo);
                sAmmo = m_pMyEnt->ammo[m_pMyEnt->secondary];
                break;
            case I_AMMO:
                sMaxAmmo = ammostats[m_pMyEnt->primary].max;
                bInteresting = (m_pMyEnt->ammo[m_pMyEnt->primary]<sMaxAmmo);
                sAmmo = m_pMyEnt->ammo[m_pMyEnt->primary];
                break;
            case I_GRENADE:
                sMaxAmmo = ammostats[GUN_GRENADE].max;
                bInteresting = (m_pMyEnt->mag[GUN_GRENADE]<sMaxAmmo);
                sAmmo = -1;
                break;
            case I_HEALTH:
                sMaxAmmo = MAXHEALTH;
                bInteresting = (m_pMyEnt->health < sMaxAmmo);
                sAmmo = m_pMyEnt->health;
                break;
            case I_HELMET:
            case I_ARMOUR:
               sMaxAmmo = MAXARMOUR;
               bInteresting = (m_pMyEnt->armour < sMaxAmmo);
               sAmmo = m_pMyEnt->armour;
               break;
            case I_AKIMBO:
               bInteresting = !m_pMyEnt->akimbo;
               sAmmo = -1;
               break;
        };

        if (!bInteresting)
            continue; // Not an interesting item, skip

        // Score on ammo and need
        // Akimbo & nade
        if (sAmmo == -1)
        {
            sScore += 75; // Bonus
        }
        else
        {
            // Calculate current percentage of max ammo
            float percent = ((float)sAmmo / (float)sMaxAmmo) * 100.0f;
            if (percent > 100.0f) percent = 100.0f;
            sScore += ((100 - short(percent))/2);
        }

        flDist = GetDistance(o);

        if (flDist > flRange) continue;

        // Score on distance
        float f = flDist;
        if (f > 100.0f) f = 100.0f;
        sScore += ((100 - short(f)) / 2);

        waypoint_s *pWptNearEnt = NULL;
        // If this entity isn't visible check if there is a nearby waypoint
        if (!IsReachable(o, flMaxHeight))//(!IsVisible(o))
        {
            if (!pWptNearBot) continue;

#ifdef WP_FLOOD
            if (pWptNearBot->pNode->iFlags & W_FL_FLOOD)
                pWptNearEnt = GetNearestFloodWP(o, 8.0f);
            else
#endif
                pWptNearEnt = GetNearestWaypoint(o, 15.0f);

            if (!pWptNearEnt) continue;
        }

        // Score on visibility
        if (pWptNearEnt == NULL) // Ent is visible
            sScore += 30;
        else
            sScore += 15;

        if (sScore > sHighestScore)
        {
            // Found a valid wp near the bot and the ent,so...lets store it :)
            if (pWptNearEnt)
                pBestWpt = pWptNearEnt;
            else
                pBestWpt = NULL; // Best ent so far doesn't need any waypoints

            sHighestScore = sScore;
            pNewTargetEnt = &ents[i];
        }
    }

    if (pNewTargetEnt)
    {
        // Need waypoints to reach it?
        if (pBestWpt)
        {
            ResetWaypointVars();
            SetCurrentWaypoint(pWptNearBot);
            SetCurrentGoalWaypoint(pBestWpt);
        }

        m_vGoal.x = pNewTargetEnt->x;
        m_vGoal.y = pNewTargetEnt->y;
        m_vGoal.z = S(pNewTargetEnt->x, pNewTargetEnt->y)->floor+player1->eyeheight;
    }

    return pNewTargetEnt;
}

entity *CACBot::SearchForFlags(bool bUseWPs, float flRange, float flMaxHeight)
{
    /*
        Flags are scored on the following:
        - Distance
    */
    float flDist;
    entity *pNewTargetFlag = NULL;
    waypoint_s *pWptNearBot = NULL, *pBestWpt = NULL;
    short sScore, sHighestScore = 0;
    vec vNewGoal = g_vecZero;

    if ((WaypointClass.m_iWaypointCount >= 1) && bUseWPs)
        pWptNearBot = GetNearestWaypoint(200.0f);

#ifdef WP_FLOOD
    if (!pWptNearBot && bUseWPs)
        pWptNearBot = GetNearestFloodWP(64.0f);
#endif

    loopv(ents)
    {
        sScore = 0;
        entity &e = ents[i];
        if(!CanTakeFlag(e)) continue;
        //vec o = g_vecZero;
        vec o = vec(e.x, e.y, S(e.x, e.y)->floor + PLAYERHEIGHT + PLAYERABOVEEYE);
        if(!m_secure(gamemode) && /*e.attr2 >= 0 &&*/ e.attr2 < 2)
        {
            flaginfo &f = flaginfos[e.attr2];
            // flaginfo &of = flaginfos[team_opposite(i)];
            if(f.state == CTFF_DROPPED)
            {
                o = f.pos;
                o.z += PLAYERHEIGHT + PLAYERABOVEEYE;
            }
        }
        if(OUTBORD((int)o.x, (int)o.y)) continue;
        flDist = GetDistance(o);
        if (flDist > flRange) continue;

        // Score on distance
        float ff = flDist;
        if (ff > 100.0f) ff = 100.0f;
        sScore += ((100 - short(ff)) / 2);

        waypoint_s *pWptNearEnt = NULL;
        // If this flag entity isn't visible check if there is a nearby waypoint
        if (!IsReachable(o, flMaxHeight))//(!IsVisible(o))
        {
            if (!pWptNearBot) continue;

#ifdef WP_FLOOD
            if (pWptNearBot->pNode->iFlags & W_FL_FLOOD)
                pWptNearEnt = GetNearestFloodWP(o, 100.0f);
            else
#endif
                pWptNearEnt = GetNearestWaypoint(o, 200.f);

            if (!pWptNearEnt) continue;
        }

        // Score on visibility
        if (pWptNearEnt == NULL) // Ent is visible
            sScore += 6;
        else
            sScore += 3;

        if(sScore > sHighestScore)
        {
            if (pWptNearEnt) pBestWpt = pWptNearEnt;
            else pBestWpt = NULL; // best flag doesn't need any waypoints

            vNewGoal = o;
            pNewTargetFlag = &e;
        }
    }

    if (pNewTargetFlag)
    {
        // Need waypoints to reach it?
        if (pBestWpt)
        {
            ResetWaypointVars();
            SetCurrentWaypoint(pWptNearBot);
            SetCurrentGoalWaypoint(pBestWpt);
        }
        m_vGoal = vNewGoal;
    }

    return pNewTargetFlag;
}

bool CACBot::CanTakeFlag(const entity &e)
{
    if(!m_flags(gamemode)) return false;
    if(m_secure(gamemode))
    {
        if(e.type != CTF_FLAG || e.attr2 < 2 || e.attr2 > 2 + TEAM_SPECT) return false;
        return (e.attr2 - 2) != m_pMyEnt->team || e.attr4;
    }
    else
    {
        if(e.type != CTF_FLAG || (e.attr2 != 0 && e.attr2 != 1)) return false;
        const int f_team = e.attr2;
        flaginfo &f = flaginfos[f_team];
        flaginfo &of = flaginfos[team_opposite(f_team)];
        switch(f.state)
        {
            case CTFF_INBASE: // go to this base
                // in CTF to capture the enemy flag and to return our flag
                if (m_capture(gamemode) && f_team == m_pMyEnt->team && (of.state != CTFF_STOLEN || of.actor != m_pMyEnt)) return false;
                // in HTF to take out own flag
                else if (m_hunt(gamemode) && f_team != m_pMyEnt->team) return false;
                // in BTF to take own flag, and to score it on the enemy base
                else if (m_bomber(gamemode) && f_team != m_pMyEnt->team && (of.state != CTFF_STOLEN || of.actor != m_pMyEnt)) return false;
                // if KTF
                break;
            case CTFF_STOLEN: // go to our stolen flag's base
                // if rCTF and we have our flag
                if (!m_return(gamemode, mutators) || f.actor != m_pMyEnt || f_team != m_pMyEnt->team) return false;
                break;
            case CTFF_IDLE: // not active
                return false;
            case CTFF_DROPPED: // take every dropped flag, regardless of anything!
                break;
        }
        return true;
    }
}

bool CACBot::HeadToTargetEnt()
{
    if (m_pTargetEnt)
    {
        vec o(m_pTargetEnt->x, m_pTargetEnt->y,
                S(m_pTargetEnt->x, m_pTargetEnt->y)->floor+m_pMyEnt->eyeheight);

        if (m_pTargetEnt->spawned && (!UnderWater(m_pMyEnt->o) ||
            !UnderWater(o)))
        {
            bool bIsVisible = false;
            if (m_pCurrentGoalWaypoint)
            {
                if ((GetDistance(o) <= 20.0f) && IsReachable(o, 1.0f))
                    bIsVisible = true;
                else if (HeadToGoal())
                {
                    //debugbeam(m_pMyEnt->o, m_pCurrentWaypoint->pNode->v_origin);
                    //debugbeam(m_pMyEnt->o,
                    //          m_pCurrentGoalWaypoint->pNode->v_origin);
                    AddDebugText("Using WPs for ents");
                    return true;
                }
            }
            else
                bIsVisible = IsVisible(o);

            if (bIsVisible)
            {
                if (m_pCurrentWaypoint || m_pCurrentGoalWaypoint)
                {
                    condebug("ent is now visible");
                    ResetWaypointVars();
                }

                float flHeightDiff = o.z - m_pMyEnt->o.z;
                bool bToHigh = false;
                if (Get2DDistance(o) <= 2.0f)
                {
                    if (flHeightDiff >= 1.5f)
                    {
                        if (flHeightDiff <= JUMP_HEIGHT)
                        {
#ifndef RELEASE_BUILD
                            char sz[64];
                            sprintf(sz, "Ent z diff: %f", o.z-m_pMyEnt->o.z);
                            condebug(sz);
#endif
                            // Jump if close to ent and the ent is high
                            m_pMyEnt->jumpnext = true;
                        }
                        else
                            bToHigh = true;
                    }
                }

                if (!bToHigh)
                {
                    AimToVec(o);
                    return true;
                }
            }
        }
    }

    return false;
}

bool CACBot::HeadToTargetFlag()
{
    if(m_pTargetFlag)
    {
        const vec o = m_vGoal;
        if(CanTakeFlag(*m_pTargetFlag) && (!UnderWater(m_pMyEnt->o) || !UnderWater(o)))
        {
            bool bIsVisible = false;
            if (m_pCurrentGoalWaypoint)
            {
                if ((GetDistance(o) <= 20.0f) && IsReachable(o, 1.0f))
                    bIsVisible = true;
                else if (HeadToGoal())
                {
                    //debugbeam(m_pMyEnt->o, m_pCurrentWaypoint->pNode->v_origin);
                    //debugbeam(m_pMyEnt->o,
                    //        m_pCurrentGoalWaypoint->pNode->v_origin);
                    AddDebugText("Using WPs for flag");
                    return true;
                }
            }
            else
                bIsVisible = IsVisible(o);

            if (bIsVisible)
            {
                if (m_pCurrentWaypoint || m_pCurrentGoalWaypoint)
                {
                    condebug("flag is now visible");
                    ResetWaypointVars();
                }

                float flHeightDiff = o.z - m_pMyEnt->o.z;
                bool bToHigh = false;
                if (Get2DDistance(o) <= 2.0f)
                {
                    if (flHeightDiff >= 1.5f)
                    {
                        if (flHeightDiff <= JUMP_HEIGHT)
                        {
#ifndef RELEASE_BUILD
                            char sz[64];
                            sprintf(sz, "Flag z diff: %f", o.z-m_pMyEnt->o.z);
                            condebug(sz);
#endif
                            // Jump if close to ent and the ent is high
                            m_pMyEnt->jumpnext = true;
                        }
                        else
                            bToHigh = true;
                    }
                }

                if (!bToHigh)
                {
                    AimToVec(o);
                    return true;
                }
            }
        }
    }

    return false;
}

bool CACBot::DoSPStuff()
{
    return false;
}

// Code of CACBot - End

#endif
