//
// C++ Implementation: bot
//
// Description: Main bot code for AssaultCube
//
// Main bot file
//
// Author:  Rick <rickhelmus@gmail.com>
//
//
//

#include "cube.h"
#include "bot.h"

#ifdef AC_CUBE

extern int triggertime;
extern itemstat itemstats[];
extern void spawnstate(playerent *d);

//AC Bot class begin

void CACBot::Spawn()
{
    // Init all bot variabeles
    m_pMyEnt->targetyaw = m_pMyEnt->targetpitch = 0.0f;
    m_pMyEnt->pBot = this;

    m_eCurrentBotState = STATE_NORMAL;
     m_iShootDelay = m_iChangeWeaponDelay = 0;
     m_iCheckEnvDelay = 0;
     m_vPrevOrigin = g_vecZero;
     m_iStuckCheckDelay = lastmillis + 250;
     m_bStuck = false;
     m_iStuckTime = 0;
     m_iStrafeTime = m_iStrafeCheckDelay = 0;
     m_iMoveDir = DIR_NONE;

     m_pPrevEnemy = nullptr;
     m_iCombatNavTime = 0;
     m_iSPMoveTime = 0;
     m_iEnemySearchDelay = 0;
     m_bCombatJump = false;
     m_iCombatJumpDelay = 0;
     m_bShootAtFeet = (RandomLong(1, 100) <= m_pBotSkill->sShootAtFeetWithRLPercent);
     m_iHuntDelay = 0;
     m_vHuntLocation = m_vPrevHuntLocation = g_vecZero;
     m_pHuntTarget = nullptr;
     m_fPrevHuntDist = 0.0f;
     m_iHuntLastTurnLessTime = m_iHuntPlayerUpdateTime = m_iHuntPauseTime = 0;

     m_iLastJumpPad = 0;
     m_pTargetEnt = nullptr;
     m_pTargetFlag = nullptr;
     m_iCheckTeleporterDelay = m_iCheckJumppadsDelay = 0;
     m_iCheckEntsDelay = 0;
     m_iCheckFlagsDelay = 0;
     m_iCheckTriggersDelay = 0;
     m_iLookForWaypointTime = 0;

     m_iAimDelay = 0;
     m_fYawToTurn = m_fPitchToTurn = 0.0f;

     m_vGoal = m_vWaterGoal = g_vecZero;

     ResetWaypointVars();
}

void CACBot::CheckItemPickup()
{
    checkitems(m_pMyEnt);
}

// AC Bot class end

#endif
