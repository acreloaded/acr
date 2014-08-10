//
// C++ Implementation: bot
//
// Description:
//
// Contains code of the base bot class
//
// Author:  Rick <rickhelmus@gmail.com>
//
//
//

#include "cube.h"
#include "bot.h"

extern int triggertime;
extern itemstat itemstats[];

const vec g_vecZero(0, 0, 0);

bot_skill_s::bot_skill_s(float sk)
{ // sk is 1 to 100
    const float isk100 = (101 - sk) / 100; // inverse sk 1 to 100 divided by 100
    const float sk100 = sk / 100; // skill divided by 100
    flMinReactionDelay = .015f + isk100 * .285f; // 0.300 to 0.015
    flMaxReactionDelay = .035f + isk100 * .465f; // 0.500 to 0.035
    flMinAimXSpeed = 45.f + sk100 * 285; // 330 to 45
    flMaxAimXSpeed = 60.f + sk100 * 265; // 355 to 60
    flMinAimYSpeed = 125.f + sk100 * 275; // 400 to 125
    flMaxAimYSpeed = 180.f + sk100 * 270; // 450 to 180
    flMinAimXOffset = 15.f + isk100 * 20; // 35 to 15
    flMaxAimXOffset = 20.f + isk100 * 20; // 40 to 20
    flMinAimYOffset = 10.f + isk100 * 20; // 30 to 10
    flMaxAimYOffset = 15.f + isk100 * 20; // 35 to 15
    flMinAttackDelay = .1f + isk100 * 1.4f; // 0.1 to 1.5
    flMaxAttackDelay = .4f + isk100 * 1.6f; // 0.4 to 2.0
    flMinEnemySearchDelay = .09f + isk100 * .21f; // 0.09 to 0.30
    flMaxEnemySearchDelay = .12f + isk100 * .24f; // 0.12 to 0.36
    flAlwaysDetectDistance = 15*sk100;
    sShootAtFeetWithRLPercent = sk100 * 85; // 85 to 0
    bCanPredict = sk >= 80;
    iMaxHearVolume = 15 + sk100 * 60; // 15 to 75
    iFov = 120 + .6*sk;
    bCircleStrafe = sk > 66;
    bCanSearchItemsInCombat = sk >= 70;
}

//Bot class begin

CBot::~CBot()
{
     // Delete all waypoints
     loopi(MAX_MAP_GRIDS)
     {
          loopj(MAX_MAP_GRIDS)
          {
               while (m_WaypointList[i][j].Empty() == false)
                    delete m_WaypointList[i][j].Pop();
          }
     }
}

void CBot::MakeSkill(int sk)
{
    m_sSkillNr = clamp(sk / 20, 0, 4);
    m_pBotSkill = new bot_skill_s(sk);
}

void CBot::Spawn()
{
    // Init all bot variables

     m_pMyEnt->targetyaw = m_pMyEnt->targetpitch = 0.0f;
     m_pMyEnt->enemy = nullptr;
     m_pMyEnt->maxspeed = 22.0f;
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
     m_iSawEnemyTime = 0;
     m_bCombatJump = false;
     m_iCombatJumpDelay = 0;
     m_bShootAtFeet = (RandomLong(1, 100) <= m_pBotSkill->sShootAtFeetWithRLPercent);
     m_iHuntDelay = 0;
     m_vHuntLocation = m_vPrevHuntLocation = g_vecZero;
     m_pHuntTarget = nullptr;
     m_fPrevHuntDist = 0.0f;
     m_iHuntLastTurnLessTime = m_iHuntPlayerUpdateTime = m_iHuntPauseTime = 0;
     m_iLookAroundDelay = m_iLookAroundTime = m_iLookAroundUpdateTime = 0;
     m_fLookAroundYaw = 0.0f;
     m_bLookLeft = false;

     m_iLastJumpPad = 0;
     m_pTargetEnt = nullptr;
     m_pTargetFlag = nullptr;
     while(!m_UnreachableEnts.Empty()) delete m_UnreachableEnts.Pop();
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

void CBot::Think()
{
    if (intermission) return;
    // Bot is dead?
    if (m_pMyEnt->state == CS_DEAD)
        return;
    CheckItemPickup();
    TLinkedList<unreachable_ent_s*>::node_s *p = m_UnreachableEnts.GetFirst(), *tmp;
    while(p)
    {
        if ((lastmillis - p->Entry->time) > 3500)
        {
            tmp = p;
            p = p->next;
            delete tmp->Entry;
            m_UnreachableEnts.DeleteNode(tmp);
            continue;
        }
        p = p->next;
    }
    if (!BotManager.IdleBots()) { MainAI(); }
    else { ResetMoveSpeed(); }
    // Aim to ideal yaw and pitch
    AimToIdeal();
    // Store current location, to see if the bot is stuck
    m_vPrevOrigin = m_pMyEnt->o;
    // Don't check for stuck if the bot doesn't want to move
    if (!m_pMyEnt->move && !m_pMyEnt->strafe) m_iStuckCheckDelay = max(m_iStuckCheckDelay, lastmillis+100.0f);
    // Move the bot
    moveplayer(m_pMyEnt, 1, true);
}

void CBot::AimToVec(const vec &o)
{
     m_pMyEnt->targetpitch = atan2(o.z-m_pMyEnt->o.z, GetDistance(o))*180/PI;
     m_pMyEnt->targetyaw = -(float)atan2(o.x - m_pMyEnt->o.x, o.y -
                                         m_pMyEnt->o.y)/PI*180+180;
}

void CBot::AimToIdeal()
{
     if (m_iAimDelay > lastmillis)
          return;

     float MaxXSpeed = RandomFloat(m_pBotSkill->flMinAimXSpeed,
                                   m_pBotSkill->flMaxAimXSpeed);
     float MaxYSpeed = RandomFloat(m_pBotSkill->flMinAimYSpeed,
                                   m_pBotSkill->flMaxAimYSpeed);
     float XOffset = RandomFloat(m_pBotSkill->flMinAimXOffset,
                                 m_pBotSkill->flMaxAimXOffset);
     float YOffset = RandomFloat(m_pBotSkill->flMinAimYOffset,
                                 m_pBotSkill->flMaxAimYOffset);
     float RealXOffset, RealYOffset;
     float AimXSpeed = MaxXSpeed, AimYSpeed = MaxYSpeed;
     float XDiff = fabs(m_pMyEnt->targetpitch - m_pMyEnt->pitch);
     float YDiff = fabs(m_pMyEnt->targetyaw - m_pMyEnt->yaw);

     // How higher the diff, how higher the offsets and aim speed

     if (XOffset)
     {
          if (RandomLong(0, 1))
               RealXOffset = XDiff * (XOffset / 100.0f);
          else
               RealXOffset = -(XDiff * (XOffset / 100.0f));
     }
     else
          RealXOffset = 0.0f;

     if (YOffset)
     {
          if (RandomLong(0, 1))
               RealYOffset = YDiff * (YOffset / 100.0f);
          else
               RealYOffset = -(YDiff * (YOffset / 100.0f));
     }
     else
          RealYOffset = 0.0f;


     if (XDiff >= 1.0f)
          AimXSpeed = (AimXSpeed * (XDiff / 80.0f)) + (AimXSpeed * 0.25f);
     else
          AimXSpeed *= 0.01f;

     if (YDiff >= 1.0f)
          AimYSpeed = (AimYSpeed * (YDiff / 70.0f)) + (AimYSpeed * 0.25f);
     else
          AimYSpeed *= 0.015f;

     m_fPitchToTurn = fabs((m_pMyEnt->targetpitch + RealXOffset) - m_pMyEnt->pitch);
     m_fYawToTurn = fabs((m_pMyEnt->targetyaw + RealYOffset) - m_pMyEnt->yaw);

     float flIdealPitch = ChangeAngle(AimXSpeed, m_pMyEnt->targetpitch + RealXOffset, m_pMyEnt->pitch);
     float flIdealYaw = ChangeAngle(AimYSpeed, m_pMyEnt->targetyaw + RealYOffset, m_pMyEnt->yaw);

//     m_pMyEnt->pitch = WrapXAngle(m_pMyEnt->targetpitch); // Uncomment for instant aiming
//     m_pMyEnt->yaw = WrapYZAngle(m_pMyEnt->targetyaw);

     m_pMyEnt->pitch = WrapXAngle(flIdealPitch);
     m_pMyEnt->yaw = WrapYZAngle(flIdealYaw);
}

// Function code by Botman
float CBot::ChangeAngle(float speed, float ideal, float current)
{
     float current_180;  // current +/- 180 degrees
     float diff;

     // find the difference in the current and ideal angle
     diff = fabs(current - ideal);

     // speed that we can turn during this frame...
     speed = speed * (float(BotManager.m_iFrameTime)/1000.0f);

     // check if difference is less than the max degrees per turn
     if (diff < speed)
          speed = diff;  // just need to turn a little bit (less than max)

     // check if the bot is already facing the idealpitch direction...
     if (diff <= 0.5f)
          return ideal;

     if ((current >= 180) && (ideal >= 180))
     {
          if (current > ideal)
               current -= speed;
          else
               current += speed;
     }
     else if ((current >= 180) && (ideal < 180))
     {
          current_180 = current - 180;

          if (current_180 > ideal)
               current += speed;
          else
               current -= speed;
     }
     else if ((current < 180) && (ideal >= 180))
     {
          current_180 = current + 180;

          if (current_180 > ideal)
               current += speed;
          else
               current -= speed;
     }
     else  // (current < 180) && (ideal < 180)
     {
          if (current > ideal)
               current -= speed;
          else
               current += speed;
     }


     return current;
}

float CBot::GetDistance(const vec &o)
{
     return o.dist(m_pMyEnt->o);
}

float CBot::GetDistance(const vec &v1, const vec &v2)
{
     return v2.dist(v1);
}

float CBot::GetDistance(entity *e)
{
     vec v(e->x, e->y, e->z);
     return v.dist(m_pMyEnt->o);
}

bool CBot::SelectGun(int Gun)
{
     if(!m_pMyEnt->weaponsel->deselectable() || !m_pMyEnt->weapons[Gun]->selectable() || m_pMyEnt->weaponsel->reloading || m_pMyEnt->weaponchanging) return false;
     if(m_pMyEnt->weaponsel->type != Gun)
        m_pMyEnt->weaponswitch(m_pMyEnt->weapons[Gun]);
     extern weaponinfo_s WeaponInfoTable[NUMGUNS];
     m_bShootAtFeet = WeaponInfoTable[Gun].eWeaponType == TYPE_ROCKET;
     return true;
}

bool CBot::IsVisible(entity *e, bool CheckPlayers)
{
     vec v(e->x, e->y, e->z);
     return ::IsVisible(m_pMyEnt->o, v, (CheckPlayers) ? m_pMyEnt : nullptr);
}

bool CBot::IsVisible(vec o, int Dir, float flDist, bool CheckPlayers, float *pEndDist)
{
     static vec angles, end, forward, right, up;
     static traceresult_s tr;

     end = o;
     angles = GetViewAngles();
     angles.x = 0;

     if (Dir & UP)
          angles.x = WrapXAngle(angles.x + 45.0f);
     else if (Dir & DOWN)
          angles.x = WrapXAngle(angles.x - 45.0f);

     if ((Dir & FORWARD) || (Dir & BACKWARD))
     {
          if (Dir & BACKWARD)
               angles.y = WrapYZAngle(angles.y + 180.0f);

          if (Dir & LEFT)
          {
               if (Dir & FORWARD)
                    angles.y = WrapYZAngle(angles.y - 45.0f);
               else
                    angles.y = WrapYZAngle(angles.y + 45.0f);
          }
          else if (Dir & RIGHT)
          {
               if (Dir & FORWARD)
                    angles.y = WrapYZAngle(angles.y + 45.0f);
               else
                    angles.y = WrapYZAngle(angles.y - 45.0f);
          }
     }
     else if (Dir & LEFT)
          angles.y = WrapYZAngle(angles.y - 90.0f);
     else if (Dir & RIGHT)
          angles.y = WrapYZAngle(angles.y + 90.0f);
     else if (Dir & UP)
          angles.x = WrapXAngle(angles.x + 90.0f);
     else if (Dir & DOWN)
          angles.x = WrapXAngle(angles.x - 90.0f);

     AnglesToVectors(angles, forward, right, up);

     forward.mul(flDist);
     end.add(forward);

     TraceLine(o, end, m_pMyEnt, CheckPlayers, &tr);

     if (pEndDist)
          *pEndDist = GetDistance(o, tr.end);

     return !tr.collided;
}

void CBot::SetMoveDir(int iMoveDir, bool add)
{
     if (iMoveDir & FORWARD)
          m_pMyEnt->move = 1;
     else if (m_iMoveDir & BACKWARD)
          m_pMyEnt->move = -1;
     else if (!add)
          m_pMyEnt->move = 0;

     if (iMoveDir & LEFT)
          m_pMyEnt->strafe = 1;
     else if (m_iMoveDir & RIGHT)
          m_pMyEnt->strafe = -1;
     else if (!add)
          m_pMyEnt->strafe = 0;

     if (iMoveDir & UP)
          m_pMyEnt->jumpnext = true;
}

// Used when switching to another task/state
void CBot::ResetCurrentTask()
{
     switch (m_eCurrentBotState)
     {
     case STATE_ENEMY:
          m_pMyEnt->enemy = nullptr;
          m_pTargetEnt = nullptr;
          m_iCombatNavTime = m_iMoveDir = 0;
          m_bCombatJump = false;
          m_vGoal = g_vecZero;
          break;
     case STATE_FLAG:
          m_pTargetFlag = nullptr;
          m_vGoal = g_vecZero;
          break;
     case STATE_ENT:
          m_pTargetEnt = nullptr;
          m_vGoal = g_vecZero;
          break;
     case STATE_SP:
          m_iSPMoveTime = m_iMoveDir = 0;
          m_pTargetEnt = nullptr;
          m_vGoal = g_vecZero;
          break;
     case STATE_NORMAL:
          m_iStrafeTime = m_iMoveDir = 0;
          break;
     default:
          break;
     }
}

// Bot class end
