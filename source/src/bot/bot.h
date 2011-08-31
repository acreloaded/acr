//
// C++ Interface: bot
//
// Description:
//
// Main bot header
//
//
// Author:  Rick <rickhelmus@gmail.com>
//
//
//


/*

	TODO list

	-bots that "cover" each other in team matches ?
	-If a bot gets ammo it should ignore that kind of ammo for the next 10 seconds. ?
	-bots should ignore players that are too far away ?
	-bots should dodge/jump when shot at
	-If the bot's only weapon is the fist it should make looking for ammo its priority - Done ?
	-bots should rocket jump to get quads and the 150 armor if normal jumping doesn't
	 work (only if health is x so they don't suicide).
	-Make all the "UNDONE's" done :)
	-Finish waypoint navigation
	-Make bot personalities
	-More advanced enemy scoring
	-Bots should less bump into walls
	-(Better) Coop support
	-Check for reachable instead of visible in GetNearestWaypoint
	-Waypoint experience file
	-Bots should avoid team mates
	-Handle failed goals better(ie remember them)
	-Test multiplayer
	-Fix bots getting stuck in each other
	-Optimize hunting code
	-Check/Fix msg code
*/

#ifndef BOT_H
#define BOT_H

#include "../cube.h"

//#define RELEASE_BUILD // Set when you want to make a release build

#define AC_CUBE

#include "bot_util.h"
#include "bot_waypoint.h"

extern itemstat itemstats[];

#ifdef RELEASE_BUILD
inline void condebug(const char *s, int a = 0, int b = 0, int c = 0) {}
inline void debugnav(const char *s, int a = 0, int b = 0, int c = 0) {}
inline void debugbeam(vec &s, vec &e) { }
#else
inline void condebug(const char *s, int a = 0, int b = 0, int c = 0) { /*conoutf(s, a, b, c);*/ }
inline void debugnav(const char *s, int a = 0, int b = 0, int c = 0) { /*conoutf(s, a, b, c);*/ }
inline void debugbeam(vec &s, vec &e) { /*if (!dedserv) particle_trail(1, 500, s, e);*/ }
#endif

#define JUMP_HEIGHT	 4.0f // NOT accurate

#define FORWARD	  (1<<1)
#define BACKWARD	 (1<<2)
#define LEFT		(1<<3)
#define RIGHT	    (1<<4)
#define UP		  (1<<5)
#define DOWN		(1<<6)
#define DIR_NONE	 0

//fixmebot
#define m_sp 1000
#define m_classicsp 1000

enum EBotCommands // For voting of bot commands
{
	COMMAND_ADDBOT=0,
	COMMAND_KICKBOT,
	COMMAND_BOTSKILL
};

struct bot_skill_calc
{
	float flMinReactionDelay; // Minimal reaction time
	float flMaxReactionDelay; // Maximal reaction time
	float flMinAimXSpeed; // Minimal X aim speed in degrees(base value)
	float flMaxAimXSpeed; // Maximal X aim speed in degrees(base value)
	float flMinAimYSpeed; // Minimal Y aim speed in degrees(base value)
	float flMaxAimYSpeed; // Maximal Y aim speed in degrees(base value)
	float flMinAimXOffset; // Minimal X aim offset in degrees(base value)
	float flMaxAimXOffset; // Maximal X aim offset in degrees(base value)
	float flMinAimYOffset; // Minimal Y aim offset in degrees(base value)
	float flMaxAimYOffset; // Maximal Y aim offset in degrees(base value)
	float flMinAttackDelay; // Minimal delay for when a bot can attack again
	float flMaxAttackDelay; // Maximal delay for when a bot can attack again
	float flMinEnemySearchDelay; // Minimal delay for when a bot can search for an
						    // enemy again
	float flMaxEnemySearchDelay; // Maximal delay for when a bot can search for an
						    // enemy again
	short sShootAtFeetWithRLPercent; // Percent that a bot shoot with a rocket
							   // launcher at the enemy feet.
	int iMaxHearVolume; // Max volume that bot can hear
	bool bCanPredict; // Can this bot predict his enemy position?
	bool bCircleStrafe; // Can this bot circle strafe?
	bool bCanSearchItemsInCombat;
	bot_skill_calc(float sk){ // sk is 1 to 100
		const float isk100 = (101 - sk) / 100; // inverse sk 1 to 100 divided by 100
		const float sk100 = sk / 100; // skill divided by 100
		flMinReactionDelay = .015f + isk100 * .285f; // 0.300 to 0.015
		flMaxReactionDelay = .035f + isk100 * .465f; // 0.500 to 0.035
		flMinAimXOffset = 15.f + isk100 * 20; // 35 to 15
		flMaxAimXOffset = 20.f + isk100 * 20; // 40 to 20
		flMinAimYOffset = 10.f + isk100 * 20; // 30 to 10
		flMaxAimYOffset = 15.f + isk100 * 20; // 35 to 15
		flMinAimXSpeed = 45.f + sk100 * 285; // 330 to 45
		flMaxAimXSpeed = 60.f + sk100 * 265; // 355 to 60
		flMinAimYSpeed = 125.f + sk100 * 275; // 400 to 125
		flMaxAimYSpeed = 180.f + sk100 * 270; // 450 to 180
		flMinAttackDelay = .1f + isk100 * 1.4f; // 0.1 to 1.5
		flMaxAttackDelay = .4f + isk100 * 1.6f; // 0.4 to 2.0
		flMinEnemySearchDelay = .09f + isk100 * .21f; // 0.09 to 0.30
		flMaxEnemySearchDelay = .12f + isk100 * .24f; // 0.12 to 0.36
		sShootAtFeetWithRLPercent = sk100 * 85; // 85 to 0
		bCanPredict = sk >= 80;
		iMaxHearVolume = 15 + sk100 * 60; // 75 to 15
		bCircleStrafe = sk >= 64;
		bCanSearchItemsInCombat = sk >= 70;
	}
};

enum EBotWeaponTypes
{
	TYPE_MELEE, // Fist, knife etc
	TYPE_NORMAL, // Normal weapon, distance doesn't really matter(ie pistol)
	TYPE_SHOTGUN,
	TYPE_ROCKET,
	TYPE_SNIPER, // Rifle, sniper etc
	TYPE_GRENADE,
	TYPE_AUTO // Chain gun, machine gun etc
};

struct weaponinfo_s
{
	EBotWeaponTypes eWeaponType;
	float flMinDesiredDistance;
	float flMaxDesiredDistance;
	float flMinFireDistance;
	float flMaxFireDistance;
	short sMinDesiredAmmo;
};

enum ECurrentBotState
{
	STATE_ENEMY, // Bot has an enemy
	STATE_HUNT, // Bot is hunting an enemy
	STATE_ENT, // Bot is heading to an entity
	STATE_SP, // Bot is doing sp specific stuff
	STATE_NORMAL // Bot is doing normal navigation
};

struct unreachable_ent_s
{
	entity *ent;
	int time;

	unreachable_ent_s(entity *e, int t) : ent(e), time(t) { };
};

class CBot
{
public:
	playerent *m_pMyEnt;
	int m_iLastBotUpdate;

	// Combat variabels
	playerent *m_pPrevEnemy;
	int m_iShootDelay;
	int m_iChangeWeaponDelay;
	int m_iCombatNavTime;
	int m_iEnemySearchDelay;
	int m_iSawEnemyTime;
	bool m_bCombatJump;
	float m_iCombatJumpDelay;
	bool m_bShootAtFeet;

	// Hunting variabeles
	int m_iHuntDelay;
	vec m_vHuntLocation, m_vPrevHuntLocation;
	playerent *m_pHuntTarget;
	float m_fPrevHuntDist;
	int m_iHuntPauseTime, m_iHuntPlayerUpdateTime, m_iHuntLastTurnLessTime;
	int m_iLookAroundDelay, m_iLookAroundTime, m_iLookAroundUpdateTime;
	float m_fLookAroundYaw;
	bool m_bLookLeft;

	// Navigation variabeles
	int m_iCheckEnvDelay;
	int m_iLastJumpPad;
	vec m_vPrevOrigin;
	float m_iStuckCheckDelay;
	bool m_bStuck;
	int m_iStuckTime;
	int m_iStrafeTime;
	int m_iStrafeCheckDelay;
	int m_iMoveDir;
	int m_iSPMoveTime;
	bool m_bCalculatingAStarPath;
	TLinkedList<waypoint_s *> m_AStarNodeList;
	TPriorList<waypoint_s *, float> m_AStarOpenList[2];
	TLinkedList<waypoint_s *> m_AStarClosedList[2];
	vec m_vWaterGoal; // Place to go to when underwater

	// Waypoint variabeles
	TLinkedList<waypoint_s *> m_WaypointList[MAX_MAP_GRIDS][MAX_MAP_GRIDS];
	float m_iWaypointTime;
	waypoint_s *m_pCurrentWaypoint;
	waypoint_s *m_pCurrentGoalWaypoint;
	vec m_vGoal;
	waypoint_s *m_pPrevWaypoints[5];
	int m_iRandomWaypointTime;
	float m_fPrevWaypointDistance;
	int m_iLookForWaypointTime;
	int m_iWaypointHeadLastTurnLessTime; // Last time the didn't had to turn much wile
								  // heading to a WP
	int m_iWaypointHeadPauseTime; // Pause time to give the bot time to aim.
	bool m_bGoToDebugGoal;

	// Misc stuff
	ECurrentBotState m_eCurrentBotState;
	entity *m_pTargetEnt;
	TLinkedList<unreachable_ent_s *> m_UnreachableEnts;
	int m_iCheckTeleporterDelay;
	int m_iCheckJumppadsDelay;
	int m_iCheckEntsDelay;
	int m_iCheckTriggersDelay;
	int m_iAimDelay;
	float m_fYawToTurn, m_fPitchToTurn;
	short m_sSkillNr; // legacy support...
	bot_skill_calc *m_pBotSkill; // bot skill pointer

	void AimToVec(const vec &o);
	void AimToIdeal(void);
	float ChangeAngle(float speed, float ideal, float current);
	bool SelectGun(int Gun);
	virtual void CheckItemPickup(void) = 0;
	void SendBotInfo(void);
	float GetDistance(const vec &o);
	float GetDistance(const vec &v1, const vec &v2);
	float GetDistance(entity *e);
	float Get2DDistance(const vec &v) { return ::Get2DDistance(m_pMyEnt->o, v); };
	vec GetViewAngles(void) { return vec(m_pMyEnt->pitch, m_pMyEnt->yaw, m_pMyEnt->roll); };
	void ResetMoveSpeed(void) { m_pMyEnt->move = m_pMyEnt->strafe = 0; };
	void SetMoveDir(int iMoveDir, bool add);
	void ResetCurrentTask();

	// AI Functions
	bool FindEnemy(void);
	void CheckReload(void);
	void ShootEnemy(void);
	bool CheckHunt(void);
	bool HuntEnemy(void);
	void DoCombatNav(void);
	void MainAI(void);
	bool CheckStuck(void);
	bool CheckJump(void);
	bool CheckStrafe(void);
	void CheckFOV(void);
	bool IsVisible(const vec &o, bool CheckPlayers = false) { return ::IsVisible(m_pMyEnt->o, o,
														(CheckPlayers) ? m_pMyEnt :
														 NULL); };
	bool IsVisible(playerent *d, bool CheckPlayers = false) { return ::IsVisible(m_pMyEnt->o, d->o,
															    (CheckPlayers) ?
																m_pMyEnt : NULL); };
	bool IsVisible(entity *e, bool CheckPlayers = false);
	bool IsVisible(vec o, int Dir, float flDist, bool CheckPlayers, float *pEndDist = NULL);
	bool IsVisible(int Dir, float flDist, bool CheckPlayers)
					 { return IsVisible(m_pMyEnt->o, Dir, flDist, CheckPlayers); };
	bool IsInFOV(const vec &o);
	bool IsInFOV(playerent *d) { return IsInFOV(d->o); };
	int GetShootDelay(void);
	virtual bool ChoosePreferredWeapon(void);
	virtual entity *SearchForEnts(bool bUseWPs, float flRange=9999.0f,
							float flMaxHeight=JUMP_HEIGHT) = 0;
	virtual bool HeadToTargetEnt(void) = 0;
	bool CheckItems(void);
	bool InUnreachableList(entity *e);
	virtual bool DoSPStuff(void) = 0;
	vec GetEnemyPos(playerent *d);
	bool AStar(void);
	float AStarCost(waypoint_s *pWP1, waypoint_s *pWP2);
	void CleanAStarLists(bool bPathFailed);
	bool IsReachable(vec to, float flMaxHeight=JUMP_HEIGHT);
	bool WaterNav(void);
	void HearSound(int n, vec *o);

	// Waypoint functions
	bool HeadToWaypoint(void);
	bool FindWaypoint(void);
	void ResetWaypointVars(void);
	void SetCurrentWaypoint(node_s *pWP);
	void SetCurrentWaypoint(waypoint_s *pWP);
	void SetCurrentGoalWaypoint(node_s *pNode);
	void SetCurrentGoalWaypoint(waypoint_s *pWP);
	bool CurrentWPIsValid(void);
	bool ReachedGoalWP(void);
	bool HeadToGoal(void);
	waypoint_s *GetWPFromNode(node_s *pNode);
	waypoint_s *GetNearestWaypoint(vec v_src, float flRange);
	waypoint_s *GetNearestWaypoint(float flRange) { return GetNearestWaypoint(m_pMyEnt->o, flRange); };
	waypoint_s *GetNearestTriggerWaypoint(vec v_src, float flRange);
#ifdef WP_FLOOD
	waypoint_s *GetNearestFloodWP(vec v_origin, float flRange);
	waypoint_s *GetNearestFloodWP(float flRange) { return GetNearestFloodWP(m_pMyEnt->o, flRange); };
	waypoint_s *GetNearestTriggerFloodWP(vec v_origin, float flRange);
#endif
	void SyncWaypoints(void);

	friend class CBotManager;
	friend class CWaypointClass;

	bool m_bSendC2SInit;

	virtual ~CBot(void);

	virtual void Spawn(void);
	virtual void Think(void);
	void GoToDebugGoal(vec o);
};

class CStoredBot // Used to store bots after mapchange, so that they can be readded
{
public:
	char m_szName[32];
	char m_szTeam[32];
	short m_sSkillNr;

	CStoredBot(char *name, int team, short skill) : m_sSkillNr(skill)
		{ strcpy(m_szName, name); strcpy(m_szTeam, team_string(team)); };
};

class CBotManager
{
	bool m_bInit;
	bool m_bBotsShoot;
	bool m_bIdleBots;
    private:
	int m_iFrameTime;
	int m_iPrevTime;
	short m_sBotSkill; // Bad - Worse - Medium - Good - Best
	short m_sMaxAStarBots; // Max bots that can use a* at the same time
	short m_sUsingAStarBotsCount; // Number of bots that are using a*
	short m_sCurrentTriggerNr; // Current waypoint trigger bots should use

	friend class CBot;
	friend class CCubeBot;
	friend class CACBot;
	friend class CWaypointClass;

public:

	// Construction
	CBotManager(void) { m_bInit = true; };

	// Destruction
	~CBotManager(void);

	void Init(void);
	void Think(void);
	void BeginMap(const char *szMapName);
	void LetBotsUpdateStats(void);
	void LetBotsHear(int n, vec *loc);
	void AddWaypoint(node_s *pNode);
	void DelWaypoint(node_s *pNode);
	bool BotsShoot(void) { return m_bBotsShoot; };
	bool IdleBots(void) { return m_bIdleBots; };
	void SetBotsShoot(bool bShoot) { m_bBotsShoot = bShoot; };
	void SetIdleBots(bool bIdle) { m_bIdleBots = bIdle; };
	void CalculateMaxAStarCount(void);
	void PickNextTrigger(void);

	void MakeBotFileName(const char *szFileName, const char *szDir1, char *szOutput);
};

#if defined VANILLA_CUBE
#include "cube_bot.h"
#elif defined AC_CUBE
#include "ac_bot.h"
#endif

extern CBotManager BotManager;
extern const vec g_vecZero;

#if defined AC_CUBE
extern CACWaypointClass WaypointClass;
#elif defined VANILLA_CUBE
extern CCubeWaypointClass WaypointClass;
#endif

#endif
