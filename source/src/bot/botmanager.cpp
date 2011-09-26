//
// C++ Implementation: bot
//
// Description: Code for botmanager
//
// Main bot file
//
// Author:  Rick <rickhelmus@gmail.com>
//
//
//

#include "pch.h"
#include "bot.h"

extern void respawnself();

CBotManager BotManager;

// Bot manager class begin

CBotManager::~CBotManager(void){  }

void CBotManager::Init()
{
	m_bBotsShoot = true;
	m_bIdleBots = false;
	m_iFrameTime = 0;
	m_iPrevTime = lastmillis;
	
	//WaypointClass.Init();
	lsrand(time(NULL));
}

void CBotManager::Think()
{    
	if (m_bInit)
	{
		Init();
		m_bInit = false;
	}

	AddDebugText("m_sMaxAStarBots: %d", m_sMaxAStarBots);
	AddDebugText("m_sCurrentTriggerNr: %d", m_sCurrentTriggerNr);
	short x, y;
	WaypointClass.GetNodeIndexes(player1->o, &x, &y);
	AddDebugText("x: %d y: %d", x, y);
	
	m_iFrameTime = lastmillis - m_iPrevTime;
	if (m_iFrameTime > 250) m_iFrameTime = 250;
	m_iPrevTime = lastmillis;

    // Added by Victor: control multiplayer bots
    const int ourcn = getclientnum();
    if(ourcn >= 0 && m_ai){
	   // handle the bots
	   loopv(players){
		  if(!players[i] || players[i]->ownernum != ourcn) continue;
		  playerent *b = players[i];
		  // ensure there is a bot controller
		  if(!b->pBot){
			 b->pBot = new CACBot;
			 b->pBot->m_pMyEnt = b;

			 // create skills
			 b->pBot->MakeSkill();

			 // Sync waypoints
			 b->pBot->SyncWaypoints();
			 // Try spawn
			 b->pBot->Spawn();
		  }
		  b->pBot->Think();
	   }
    }
}

void CBotManager::BeginMap(const char *szMapName)
{ 
	WaypointClass.Init();
	WaypointClass.SetMapName(szMapName);
	if (*szMapName && !WaypointClass.LoadWaypoints())
		WaypointClass.StartFlood();
	//WaypointClass.LoadWPExpFile(); // UNDONE

	CalculateMaxAStarCount();
	m_sUsingAStarBotsCount = 0;
	PickNextTrigger();
}

void CBotManager::LetBotsHear(int n, vec *loc)
{
	if (!loc) return;
		
	loopv(players)
	{
		if (!players[i] || players[i]->ownernum < 0 || !players[i]->pBot || players[i]->state == CS_DEAD) continue;
		players[i]->pBot->HearSound(n, loc);
	}
}

// Notify all bots of a new waypoint
void CBotManager::AddWaypoint(node_s *pNode)
{

	CalculateMaxAStarCount();
}

// Notify all bots of a deleted waypoint
void CBotManager::DelWaypoint(node_s *pNode)
{
	CalculateMaxAStarCount();
}

void CBotManager::MakeBotFileName(const char *szFileName, const char *szDir1, char *szOutput)
{
	const char *DirSeperator;

#ifdef WIN32
	DirSeperator = "\\";
	strcpy(szOutput, "bot\\");
#else
	DirSeperator = "/";
	strcpy(szOutput, "bot/");
#endif
	
	if (szDir1){
		strcat(szOutput, szDir1);
		strcat(szOutput, DirSeperator);
	}
	
	strcat(szOutput, szFileName);
}

void CBotManager::CalculateMaxAStarCount()
{  
	if (WaypointClass.m_iWaypointCount > 0) // Are there any waypoints?
	{
		m_sMaxAStarBots = 8 - short(ceil((float)WaypointClass.m_iWaypointCount /
							   1000.0f));
		if (m_sMaxAStarBots < 1)
			m_sMaxAStarBots = 1;
	}
	else
		m_sMaxAStarBots = 1;
}

void CBotManager::PickNextTrigger()
{
	short lowest = -1;
	bool found0 = false; // True if found a trigger with nr 0
	
	loopv(ents)
	{
		entity &e = ents[i];
		
#if defined AC_CUBE		
/*		if ((e.type != TRIGGER) || !e.spawned)
			continue;*/
#elif defined VANILLA_CUBE
		if ((e.type != CARROT) || !e.spawned)
			continue;
#endif		
		if (OUTBORD(e.x, e.y)) continue;
		
		vec o(e.x, e.y, S(e.x, e.y)->floor+player1->eyeheight);

		node_s *pWptNearEnt = NULL;
		
		pWptNearEnt = WaypointClass.GetNearestTriggerWaypoint(o, 2.0f);
		
		if (pWptNearEnt)
		{
			if ((pWptNearEnt->sTriggerNr > 0) &&
			    ((pWptNearEnt->sTriggerNr < lowest) || (lowest == -1)))
				lowest = pWptNearEnt->sTriggerNr;
			if (pWptNearEnt->sTriggerNr == 0) found0 = true;
		}
		
#ifdef WP_FLOOD
		pWptNearEnt = WaypointClass.GetNearestTriggerFloodWP(o, 2.0f);
		
		if (pWptNearEnt)
		{
			if ((pWptNearEnt->sTriggerNr > 0) && 
			    ((pWptNearEnt->sTriggerNr < lowest) || (lowest == -1)))
				lowest = pWptNearEnt->sTriggerNr;
			if (pWptNearEnt->sTriggerNr == 0) found0 = true;
		}
				
#endif			
	}
	
	if ((lowest == -1) && found0) lowest = 0;
	
	if (lowest != -1)
		m_sCurrentTriggerNr = lowest;
}

// Bot manager class end

void togglegrap()
{
	if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GrabMode(0)) SDL_WM_GrabInput(SDL_GRAB_ON);
	else SDL_WM_GrabInput(SDL_GrabMode(0));
}

COMMAND(togglegrap, ARG_NONE);

#ifndef RELEASE_BUILD

#ifdef VANILLA_CUBE
void drawbeamtocarrots()
{
	loopv(ents)
	{
		entity &e = ents[i];
		vec o = { e.x, e.y, S(e.x, e.y)->floor+player1->eyeheight };
		if ((e.type != CARROT) || !e.spawned) continue;
		particle_trail(1, 500, player1->o, o);
	}
}

COMMAND(drawbeamtocarrots, ARG_NONE);

void drawbeamtoteleporters()
{
	loopv(ents)
	{
		entity &e = ents[i];
		vec o = { e.x, e.y, S(e.x, e.y)->floor+player1->eyeheight };
		if (e.type != TELEPORT) continue;
		particle_trail(1, 500, player1->o, o);
	}
}

COMMAND(drawbeamtoteleporters, ARG_NONE);
#endif

void testvisible(int iDir)
{
    
	vec angles, end, forward, right, up;
	traceresult_s tr;
	int Dir;
	
	switch(iDir)
	{
		case 0: default: Dir = FORWARD; break;
		case 1: Dir = BACKWARD; break;
		case 2: Dir = LEFT; break;
		case 3: Dir = RIGHT; break;
		case 4: Dir = UP; break;
		case 5: Dir = DOWN; break;
	}
	
	vec from = player1->o;
	from.z -= (player1->eyeheight - 1.25f);
	end = from;
	makevec(&angles, player1->pitch, player1->yaw, player1->roll);
	angles.x=0;
	
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
	
	forward.mul(20.0f);
	end.add(forward);
	    
	TraceLine(from, end, player1, false, &tr);
	
	//debugbeam(from, tr.end);
	char sz[250];
	sprintf(sz, "dist: %f; hit: %d", GetDistance(from, tr.end), tr.collided);
	condebug(sz);
}

COMMAND(testvisible, ARG_1INT);

void mapsize(void)
{
	conoutf("ssize: %d", ssize);
}

COMMAND(mapsize, ARG_NONE);
			
#endif
