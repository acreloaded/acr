//
// C++ Implementation: ac_bot_ai
//
// Description: The AI part of the bot for ac is here(navigation, shooting etc)
//
//
// Author:  <rickhelmus@gmail.com>
//


#include "pch.h"
#include "bot.h"

#ifdef AC_CUBE

// UNDONE
weaponinfo_s WeaponInfoTable[MAX_WEAPONS] =
{
	{ TYPE_MELEE, 0.0f, 2.0f, 0.0f, 3.5f, 1 }, // KNIFE
	{ TYPE_NORMAL, 0.0f, 20.0f, 0.0f, 50.0f, 6 }, // PISTOL
	{ TYPE_SHOTGUN, 0.0f, 15.0f, 0.0f, 40.0f, 3 }, // SHOTGUN
	{ TYPE_AUTO, 0.0f, 25.0f, 0.0f, 60.0f, 10 }, // SUBGUN
	{ TYPE_SNIPER, 30.0f, 50.0f, 20.0f, 200.0f, 3 }, // SNIPER
	{ TYPE_SNIPER, 30.0f, 50.0f, 20.0f, 200.0f, 2 }, // BOLT
	{ TYPE_AUTO, 0.0f, 25.0f, 0.0f, 60.0f, 10 }, // ASSAULT
	{ TYPE_GRENADE, 30.0f, 25.0f, 0.0f, 50.0f, 1 }, // GRENADE
	{ TYPE_NORMAL, 0.0f, 20.0f, 0.0f, 50.0f, 6 }, // AKIMBO?
	{ TYPE_AUTO, 40.0f, 80.0f, 0.0f, 150.0f, 6 }, // HEAL
	{ TYPE_MELEE, 0.0f, 4.0f, 0.0f, 7.0f, 1 }, // SWORD
	{ TYPE_ROCKET, 0.0f, 20.0f, 0.0f, 50.0f, 2 }, // CROSSBOW
};

// Code of CACBot - Start   

bool CACBot::ChoosePreferredWeapon()
{
    short bestWeapon = m_pMyEnt->gunselect;
    short bestWeaponScore = 0;

	short sWeaponScore;
	float flDist = GetDistance(m_pMyEnt->enemy->o);

	if (m_iChangeWeaponDelay < lastmillis && m_pMyEnt->mag[m_pMyEnt->gunselect])
	{
	    if ((WeaponInfoTable[m_pMyEnt->gunselect].eWeaponType != TYPE_MELEE) || (flDist <= WeaponInfoTable[WEAP_KNIFE].flMaxFireDistance))
			return true;
	};
		  
	// Choose a weapon
	for(int i=0;i<MAX_WEAPONS;i++)
	{
		// If no ammo for this weapon, skip it
		if (!m_pMyEnt->mag[i] && !m_pMyEnt->ammo[i]) continue;
		
		sWeaponScore = 5; // Minimal score for a weapon

		// use the advantage of the melee weapon
		if((flDist >= WeaponInfoTable[i].flMinDesiredDistance) &&
		    (flDist <= WeaponInfoTable[i].flMaxDesiredDistance))
		{
		    if(WeaponInfoTable[i].eWeaponType == TYPE_MELEE) sWeaponScore += 5;
		}
		
		if ((flDist > WeaponInfoTable[i].flMinFireDistance) && // inside the range, continue to evaluate
			    (flDist < WeaponInfoTable[i].flMaxFireDistance))
		{    
		  if(m_pMyEnt->mag[i]) sWeaponScore += 5;
		  if(i > 1) sWeaponScore += 4; // prefer primary guns
		}

		if(sWeaponScore > bestWeaponScore)
		{
		  bestWeaponScore = sWeaponScore;
		  bestWeapon = i;
		}
	}   
	
	{
		m_iChangeWeaponDelay = lastmillis + 8000; //RandomLong(2000, 8000);
		m_bShootAtFeet = false;
		SelectGun(bestWeapon);
		if(!m_pMyEnt->mag[bestWeapon]) 
		{
		  tryreload(m_pMyEnt);
		  m_iShootDelay = lastmillis + reloadtime(bestWeapon) + 10;
		}
		return true;
	}
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
			sMaxAmmo = ammostats[WEAP_PISTOL].max;
			bInteresting = (m_pMyEnt->ammo[WEAP_PISTOL]<sMaxAmmo);
			sAmmo = m_pMyEnt->ammo[WEAP_PISTOL];
			break;
		case I_AMMO:
			sMaxAmmo = ammostats[m_pMyEnt->primary].max;
			bInteresting = (m_pMyEnt->ammo[m_pMyEnt->primary]<sMaxAmmo);
			sAmmo = m_pMyEnt->ammo[m_pMyEnt->primary];
			break;
		case I_GRENADE:
			sMaxAmmo = ammostats[WEAP_GRENADE].max;
			bInteresting = (m_pMyEnt->mag[WEAP_GRENADE]<sMaxAmmo);
			sAmmo = m_pMyEnt->mag[WEAP_GRENADE];
			break;
		case I_HEALTH:
			sMaxAmmo = powerupstats[I_HEALTH-I_HEALTH].max; //FIXME
			bInteresting = (m_pMyEnt->health < sMaxAmmo); 
			sAmmo = m_pMyEnt->health;
			break;
		case I_ARMOR:
			sMaxAmmo = powerupstats[I_ARMOR-I_HEALTH].max; // FIXME
			bInteresting = (m_pMyEnt->armor < sMaxAmmo);
			sAmmo = m_pMyEnt->armor;
			break;
		};
		
		if (!bInteresting)
		    continue; // Not an interesting item, skip
		
		// Score on ammo and need
		if (sAmmo == -1)
		{
			// This entity doesn't have/need ammo
			// Score on type instead
			//switch(e.type)
			{ // UNDONE
			}
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

flaginfo *CACBot::SearchForFlags(bool bUseWPs, float flRange, float flMaxHeight)
{
	/*
		Flags are scored on the following:
		- Distance
	*/
	float flDist;
	flaginfo *pNewTargetFlag = NULL;
	waypoint_s *pWptNearBot = NULL, *pBestWpt = NULL;
	short sScore, sHighestScore = 0;
	vec vNewGoal = g_vecZero;
	
	if ((WaypointClass.m_iWaypointCount >= 1) && bUseWPs)
		pWptNearBot = GetNearestWaypoint(200.0f);

#ifdef WP_FLOOD				
	if (!pWptNearBot && bUseWPs)
		pWptNearBot = GetNearestFloodWP(64.0f);
#endif

	loopi(2){
		sScore = 0;
		flaginfo &f = flaginfos[i];
		flaginfo &of = flaginfos[team_opposite(i)];
		if(f.state == CTFF_IDLE) continue;
		//vec o = g_vecZero;
		vec o = vec(f.flagent->x, f.flagent->y, S(f.flagent->x, f.flagent->y)->floor + PLAYERHEIGHT);
		switch(f.state){
			case CTFF_INBASE: // go to this base
				// if CTF capturing our flag
				if(m_capture(gamemode) && (i != m_pMyEnt->team || of.state != CTFF_STOLEN || of.actor != m_pMyEnt)) continue;
				// in HTF to take out own flag
				else if(m_hunt(gamemode) && i != m_pMyEnt->team) continue;
				// in BTF to take own flag, and to score it on the enemy base
				else if(m_bomber(gamemode) && i != m_pMyEnt->team && (of.state != CTFF_STOLEN || of.actor != m_pMyEnt)) continue;
				// if KTF
				break;
			case CTFF_STOLEN: // go to our stolen flag's base
				// if rCTF and we have our flag
				if(!m_return(gamemode, mutators) || f.actor != m_pMyEnt || f.team != m_pMyEnt->team) continue;
				break;
			case CTFF_DROPPED: // take every dropped flag, regardless of anything!
				o = f.pos;
				o.z += PLAYERHEIGHT;
				break;
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

		if(sScore > sHighestScore){
			if (pWptNearEnt) pBestWpt = pWptNearEnt;
			else pBestWpt = NULL; // best flag doesn't need any waypoints

			vNewGoal = o;
			pNewTargetFlag = &f;
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
	}

	m_vGoal = vNewGoal;
	return pNewTargetFlag;
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
					//		  m_pCurrentGoalWaypoint->pNode->v_origin);
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
		if(!UnderWater(m_pMyEnt->o) || !UnderWater(o))
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
					//		  m_pCurrentGoalWaypoint->pNode->v_origin);
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
