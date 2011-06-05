// clientgame.cpp: core game related stuff

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

int nextmode = 0;		 // nextmode becomes gamemode after next map load
VAR(gamemode, 1, 0, 0);
VARP(modeacronyms, 0, 0, 1);

flaginfo flaginfos[2];

void mode(int n){
	nextmode = n;
}
COMMAND(mode, ARG_1INT);

bool intermission = false;
bool autoteambalance = false;
int arenaintermission = 0;

playerent *player1 = newplayerent();		  // our client
vector<playerent *> players;						// other clients

//int lastmillis = 0, totalmillis = 0;
int curtime = 0;
string clientmap;

extern int framesinmap;

char *getclientmap() { return clientmap; }

extern bool sendmapident;

bool localfirstkill = false;

void setskin(playerent *pl, uint skin)
{
	if(!pl) return;
	if(pl == player1) addmsg(N_SKIN, "ri", skin);
	const int maxskin[2] = { 3, 5 };
	pl->skin = skin % (maxskin[pl->team]+1);
}

bool duplicatename(playerent *d, char *name = NULL)
{
	if(!name) name = d->name;
	if(d!=player1 && !strcmp(name, player1->name)) return true;
	if(!strncmp(name, "you", 3)) return true;
	loopv(players) if(players[i] && d!=players[i] && !strcmp(name, players[i]->name)) return true;
	return false;
}

char *colorname(playerent *d, bool stats)
{
	if(!d) return "unknown";
	static string cname;
	s_sprintf(cname)("%s \fs\f6(%d)", d->name, d->clientnum);
	if(stats){
		s_sprintfd(stat)("%d%d", d->health > 50 ? 0 : d->health > 25 ? 2 : d->health > 0 ? 3 : 4, d->health);
		if(d->armour) s_sprintf(stat)("%s\f5-\f4%d", stat, d->armour);
		s_sprintf(cname)("%s \f5[\f%s\f5]", cname, stat);
	}
	s_strcat(cname, "\fr");
	return cname;
}

char *colorping(int ping)
{
	static string cping;
	if(multiplayer(false)) s_sprintf(cping)("\fs\f%d%d\fr", ping <= 500 ? 0 : ping <= 1000 ? 2 : 3, ping);
	else s_sprintf(cping)("%d", ping);
	return cping;
}

char *colorpj(int pj)
{
	static string cpj;
	if(multiplayer(false)) s_sprintf(cpj)("\fs\f%d%d\fr", pj <= 90 ? 0 : pj <= 170 ? 2 : 3, pj);
	else s_sprintf(cpj)("%d", pj);
	return cpj;
}

void newname(const char *name)
{
	if(name[0])
	{
		static string name2;
		filtername(name2, name);
		if(!name2[0]) s_strcpy(name2, "unnamed");
		addmsg(N_NEWNAME, "rs", name2);
	}
	else conoutf("your name is: %s", player1->name);
	alias("curname", player1->name);
}

void newteam(char *name)
{
	if(name[0]){
		if(strcmp(name, "BLUE") && strcmp(name, "RED") && strcmp(name, "SPECT")){
			conoutf("\f3\"%s\" is not a valid team name (try RED, BLUE or SPECT)", name);
			return;
		}
		int nt = team_int(name);
		if(nt == player1->team) return; // same team
		addmsg(N_SWITCHTEAM, "ri", nt);
	}
	else conoutf("your team is: %s", team_string(player1->team));
}

VARNP(skin, nextskin, 0, 0, 1000);

int curteam() { return player1->team; }
int currole() { return player1->priv; }
int curmode() { return gamemode; }
void curmap(int cleaned) { result(cleaned ? behindpath(getclientmap()) : getclientmap()); }
COMMANDN(team, newteam, ARG_1STR);
COMMANDN(name, newname, ARG_CONC);
COMMAND(curteam, ARG_IVAL);
COMMAND(currole, ARG_IVAL);
COMMAND(curmode, ARG_IVAL);
COMMAND(curmap, ARG_1INT);
VARP(showscoresondeath, 0, 1, 1);

void deathstate(playerent *pl)
{
	if(pl == player1 && editmode) toggleedit(true);
	pl->state = CS_DEAD;
	pl->spectatemode = SM_DEATHCAM;
	pl->respawnoffset = pl->lastpain = lastmillis;
	pl->move = pl->strafe = 0;
	pl->pitch = pl->roll = 0;
	pl->attacking = false;
	pl->weaponsel->onownerdies();
	pl->damagelog.setsize(0);
	pl->radarmillis = 0;

	if(pl==player1){
		if(showscoresondeath) showscores(true);
		setscope(false);
		//damageblend(-1);
	}
	else pl->resetinterp();
}

void spawnstate(playerent *d)			  // reset player state not persistent accross spawns
{
	d->respawn();
	d->spawnstate(gamemode);
	if(d==player1)
	{
		if(player1->skin!=nextskin) setskin(player1, nextskin);
		setscope(false);
	}
}

playerent *newplayerent()				 // create a new blank player
{
	playerent *d = new playerent;
	d->lastupdate = totalmillis;
	setskin(d, rnd(6));
	weapon::equipplayer(d); // flowtron : avoid overwriting d->spawnstate(gamemode) stuff from the following line (this used to be called afterwards)
	spawnstate(d);
	return d;
}

botent *newbotent()				 // create a new blank player
{
	botent *d = new botent;
	d->lastupdate = totalmillis;
	setskin(d, rnd(6));
	spawnstate(d);
	weapon::equipplayer(d);
	loopv(players) if(i!=getclientnum() && !players[i])
	{
		players[i] = d;
		d->clientnum = i;
		return d;
	}
	if(players.length()==getclientnum()) players.add(NULL);
	d->clientnum = players.length();
	players.add(d);
	return d;
}

void freebotent(botent *d)
{
	loopv(players) if(players[i]==d)
	{
		DELETEP(players[i]);
		players.remove(i);
	}
}

void zapplayer(playerent *&d)
{
	DELETEP(d);
}

void movelocalplayer()
{
	if(player1->state==CS_DEAD && !player1->allowmove())
	{
		if(lastmillis-player1->lastpain<2000)
		{
			player1->move = player1->strafe = 0;
			moveplayer(player1, 10, false);
		}
	}
	else if(!intermission)
	{
		moveplayer(player1, 10, true);
		checkitems(player1);
	}
}

// use physics to extrapolate player position
VARP(smoothmove, 0, 75, 100);
VARP(smoothdist, 0, 8, 16);

void predictplayer(playerent *d, bool move)
{
	d->o = d->newpos;
	d->o.z += d->eyeheight;
	d->yaw = d->newyaw;
	d->pitch = d->newpitch;
	if(move)
	{
		moveplayer(d, 1, false);
		d->newpos = d->o;
		d->newpos.z -= d->eyeheight;
	}
	float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
	if(k>0)
	{
		d->o.add(vec(d->deltapos).mul(k));
		d->yaw += d->deltayaw*k;
		if(d->yaw<0) d->yaw += 360;
		else if(d->yaw>=360) d->yaw -= 360;
		d->pitch += d->deltapitch*k;
	}
}

void moveotherplayers()
{
	loopv(players) if(players[i] && players[i]->type==ENT_PLAYER)
	{
		playerent *d = players[i];
		const int lagtime = totalmillis-d->lastupdate;
		if(!lagtime || intermission) continue;
		else if(lagtime>1000 && d->state==CS_ALIVE)
		{
			d->state = CS_LAGGED;
			continue;
		}
		if(d->state==CS_ALIVE || d->state==CS_EDITING)
		{
			if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
			else moveplayer(d, 1, false);
		}
		else if(d->state==CS_DEAD && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
	}
}


bool showhudtimer(int maxsecs, int startmillis, const char *msg, bool flash)
{
	static string str = "";
	static int tickstart = 0, curticks = -1, maxticks = -1;
	int nextticks = (lastmillis - startmillis) / 200;
	if(tickstart!=startmillis || maxticks != 5*maxsecs)
	{
		tickstart = startmillis;
		maxticks = 5*maxsecs;
		curticks = -1;
		s_strcpy(str, "\f3");
	}
	if(curticks >= maxticks) return false;
	nextticks = min(nextticks, maxticks);
	while(curticks < nextticks)
	{
		if(++curticks%5) s_strcat(str, ".");
		else
		{
			s_sprintfd(sec)("%d", maxsecs - (curticks/5));
			s_strcat(str, sec);
		}
	}
	if(nextticks < maxticks) hudeditf(HUDMSG_TIMER|HUDMSG_OVERWRITE, flash ? str : str+2);
	else hudeditf(HUDMSG_TIMER, msg);
	return true;
}

int lastspawnattempt = 0;

void showrespawntimer()
{
	if(intermission) return;
	if(m_duel)
	{
		if(!arenaintermission) return;
		showhudtimer(5, arenaintermission, "FIGHT!", lastspawnattempt >= arenaintermission && lastmillis < lastspawnattempt+100);
	}
	else if(player1->state==CS_DEAD && m_flags && (!player1->isspectating() || player1->spectatemode==SM_DEATHCAM))
	{
		int secs = 5;
		showhudtimer(secs, player1->respawnoffset, "READY!", lastspawnattempt >= arenaintermission && lastmillis < lastspawnattempt+100);
	}
}

struct scriptsleep { int wait; char *cmd; };
vector<scriptsleep> sleeps;

void addsleep(int msec, const char *cmd)
{
	scriptsleep &s = sleeps.add();
	s.wait = msec+lastmillis;
	s.cmd = newstring(cmd);
}

void addsleep_(char *msec, char *cmd)
{
	addsleep(atoi(msec), cmd);
}

void resetsleep()
{
	loopv(sleeps) DELETEA(sleeps[i].cmd);
	sleeps.shrink(0);
}

COMMANDN(sleep, addsleep_, ARG_2STR);

void updateworld(int curtime, int lastmillis)		// main game update loop
{
	// process command sleeps
	loopv(sleeps)
	{
		if(sleeps[i].wait && lastmillis > sleeps[i].wait)
		{
			char *cmd = sleeps[i].cmd;
			sleeps[i].cmd = NULL;
			execute(cmd);
			delete[] cmd;
			if(sleeps[i].cmd || !sleeps.inrange(i)) break;
			sleeps.remove(i--);
		}
	}

	physicsframe();
	checkweaponswitch();
	checkakimbo();
	if(getclientnum()>=0){ // only shoot when connected to server
		shoot(player1, worldpos);
		loopv(players) if(players[i]) shoot(players[i], worldpos);
	}
	movebounceents();
	moveotherplayers();
	gets2c();
	showrespawntimer();

	BotManager.Think(); // let bots think

	movelocalplayer();
	if(getclientnum() >= 0){ // do this last, to reduce the effective frame lag
		c2sinfo(player1);
		loopv(players) if(players[i] && players[i]->ownernum == getclientnum()) c2sinfo(players[i]);
	}
}

#define SECURESPAWNDIST 15
int spawncycle = -1;
int fixspawn = 2;

// returns -1 for a free place, else dist to the nearest enemy
float nearestenemy(vec place, int team)
{
	float nearestenemydist = -1;
	loopv(players)
	{
		playerent *other = players[i];
		if(!other || (m_team && team == other->team)) continue;
		float dist = place.dist(other->o);
		if(dist < nearestenemydist || nearestenemydist == -1) nearestenemydist = dist;
	}
	if(nearestenemydist >= SECURESPAWNDIST || nearestenemydist < 0) return -1;
	else return nearestenemydist;
}

void findplayerstart(playerent *d, bool mapcenter, int arenaspawn)
{
	int r = fixspawn-->0 ? 4 : rnd(10)+1;
	entity *e = NULL;
	if(!mapcenter)
	{
		int type = m_team ? d->team : 100;
		if(m_duel && arenaspawn >= 0)
		{
			int x = -1;
			loopi(arenaspawn + 1) x = findentity(PLAYERSTART, x+1, type);
			if(x >= 0) e = &ents[x];
		}
		else if((m_team || m_duel) && !m_ktf) // ktf uses ffa spawns
		{
			loopi(r) spawncycle = findentity(PLAYERSTART, spawncycle+1, type);
			if(spawncycle >= 0) e = &ents[spawncycle];
		}
		else
		{
			float bestdist = -1;

			loopi(r)
			{
				spawncycle = m_ktf && numspawn[2] > 5 ? findentity(PLAYERSTART, spawncycle+1, 100) : findentity(PLAYERSTART, spawncycle+1);
				if(spawncycle < 0) continue;
				float dist = nearestenemy(vec(ents[spawncycle].x, ents[spawncycle].y, ents[spawncycle].z), d->team);
				if(!e || dist < 0 || (bestdist >= 0 && dist > bestdist)) { e = &ents[spawncycle]; bestdist = dist; }
			}
		}
	}

	if(e)
	{
		d->o.x = e->x;
		d->o.y = e->y;
		d->o.z = e->z;
		d->yaw = e->attr1;
		d->pitch = 0;
		d->roll = 0;
	}
	else
	{
		d->o.x = d->o.y = (float)ssize/2;
		d->o.z = 4;
	}

	entinmap(d);
}

void spawnplayer(playerent *d)
{
	d->respawn();
	d->spawnstate(gamemode);
	d->state = d==player1 && editmode ? CS_EDITING : CS_ALIVE;
	findplayerstart(d);
}

void respawnself(){
	if(!m_duel) addmsg(N_TRYSPAWN, "r");
}

bool tryrespawn(){
	if(player1->state==CS_DEAD){
		respawnself();
		int respawnmillis = player1->respawnoffset+(m_duel ? 0 : (m_flags ? 5000 : 1000));
		if(lastmillis>respawnmillis){
			player1->attacking = false;
			if(m_duel){
				if(!arenaintermission) hudeditf(HUDMSG_TIMER, "waiting for new round to start...");
				else lastspawnattempt = lastmillis;
				return false;
			}
			return true;
		}
		else lastspawnattempt = lastmillis;
	}
	return false;
}

// damage arriving from the network, monsters, yourself, all ends up here.

void dodamage(int damage, playerent *pl, playerent *actor, int weapon)
{
	if(pl->state != CS_ALIVE || intermission) return;

	pl->respawnoffset = pl->lastpain = actor->lasthitmarker = lastmillis;
	if(actor == player1 && weapon == GUN_KNIFE && damage > 1000) return;

	if(actor != pl && pl->damagelog.find(actor->clientnum) < 0) pl->damagelog.add(actor->clientnum);

	if(pl==player1){
		if(weapon == GUN_KNIFE && pl->health > 0) pl->addicon(eventicon::BLEED);
		if(weapon != GUN_GRENADE && actor != pl){
			vec dir = pl->o;
			dir.sub(actor->o);
			pl->hitpush(damage, dir, weapon);
		}
		pl->damageroll(damage);
	}
	pl->damagestack.add(damageinfo(actor->o, lastmillis));
	damageeffect(damage * (weapon == GUN_KNIFE && damage < guns[GUN_KNIFE].damage ? 5 : 1), pl);

	if(pl==player1) playsound(S_PAIN6, SP_HIGH);
	else playsound(S_PAIN1+rnd(5), pl);
}

void dokill(playerent *pl, playerent *act, int weapon, int damage, int style, float killdist){
	if(pl->state!=CS_ALIVE || intermission) return;

	// kill message
	string subject, predicate, hashave;
	s_sprintf(subject)("\f2\fs%s\f2", act == player1 ? "\f1you" : colorname(act));
	s_strcpy(hashave, act == player1 ? "have" : "has");
	if(pl == act){
		s_strcpy(predicate, suicname(weapon));
		if(pl == player1){
			// radar scan
			loopv(players){
				playerent *p = players[i];
				if(!p || isteam(p, pl)) continue;
				p->radarmillis = lastmillis + 1000;
				p->lastloudpos[0] = p->o.x;
				p->lastloudpos[1] = p->o.y;
				p->lastloudpos[2] = p->yaw;
			}
			s_strcat(predicate, "\f3");
		}
		if(pl == gamefocus) s_strcat(predicate, "!\f2");
		if(killdist) s_sprintf(predicate)("%s (@%.2f m)", predicate, killdist);
	}
	else s_sprintf(predicate)("%s %s%s (@%.2f m)", killname(weapon, style),
		isteam(pl, act) ? act==player1 ? "your teammate " : "his teammate " : "", pl == player1 ? "\f1you\f2" : colorname(pl), killdist);
	// killstreak
	if(act->killstreak++) s_sprintf(predicate)("%s (%d killstreak)", predicate, act->killstreak);
	// assist count
	pl->damagelog.removeobj(pl->clientnum);
	pl->damagelog.removeobj(act->clientnum);
	// HUD for first person
	if(pl == gamefocus || act == gamefocus) hudonlyf(pl->damagelog.length() ? "%s %s %s, %d assister%s" : "%s %s %s", subject, hashave, predicate,
		pl->damagelog.length(), pl->damagelog.length()==1?"":"s");
	// assists
	if(pl->damagelog.length()){
		playerent *p = NULL;
		s_strcat(predicate, ", assisted by");
		bool first = true;
		loopv(pl->damagelog){
			p = getclient(pl->damagelog.pop());
			if(!p) continue;
			p->assists++;
			s_sprintf(predicate)("%s%s \fs\f%d%s\fr", predicate, first ? "" : !pl->damagelog.length() ? " and" : ",", isteam(p, pl) ? 3 : 2, colorname(p));
			first = false;
		}
	}
	conoutf("%s %s", subject, predicate);
	pl->killstreak = 0;
	
	int icon = -1;
	if(style & FRAG_GIB){
		if(pl != act && weapon != GUN_SHOTGUN && weapon != GUN_GRENADE && (weapon != GUN_KNIFE || style & (FRAG_GIB | FRAG_OVER))){
			playsound(S_HEADSHOT, act, act == gamefocus ? SP_HIGHEST : SP_HIGH);
			playsound(S_HEADSHOT, pl, pl == gamefocus ? SP_HIGHEST : SP_HIGH); // both get headshot sound
			icon = eventicon::HEADSHOT; pl->addicon(eventicon::DECAPITATED); // both get headshot info
		}
		addgib(pl);
	}
	if(style & FRAG_FIRST) icon = eventicon::FIRSTBLOOD;
	if(icon >= 0) act->addicon(icon);
	
	deathstate(pl);
	pl->deaths++;
	playsound(S_DIE1+rnd(2), pl);
}

VAR(minutesremaining, 1, 0, 0);
VAR(gametimecurrent, 1, 0, 0);
VAR(gametimemaximum, 1, 0, 0);
VAR(lastgametimeupdate, 1, 0, 0);

void timeupdate(int milliscur, int millismax){
	// if( lastmillis - lastgametimeupdate < 1000 ) return; // avoid double-output, but possibly omit new message if joined 1s before server switches to next minute
	lastgametimeupdate = lastmillis;
	gametimecurrent = milliscur;
	gametimemaximum = millismax;
	minutesremaining = (gametimemaximum - gametimecurrent + 60000 - 1) / 60000;
	if(minutesremaining){
		if(minutesremaining==1){
			musicsuggest(M_LASTMINUTE1 + rnd(2), 70000, true);
			hudoutf("1 minute left!");
		}
		else conoutf("time remaining: %d minutes", minutesremaining);
	}
	else{
		intermission = true;
		player1->attacking = false;
		conoutf("intermission:");
		conoutf("game has ended!");
		consolescores();
		showscores(true);
		if(identexists("start_intermission")) execute("start_intermission");
	}
}

playerent *newclient(int cn)   // ensure valid entity
{
	if(cn == getclientnum()) return player1;
	if(cn<0 || cn>=MAXCLIENTS)
	{
		neterr("clientnum", cn);
		return NULL;
	}
	while(cn>=players.length()) players.add(NULL);
	playerent *d = players[cn];
	if(d) return d;
	d = newplayerent();
	players[cn] = d;
	d->clientnum = cn;
	return d;
}

playerent *getclient(int cn)   // ensure valid entity
{
	return cn == getclientnum() ? player1 : players.inrange(cn) ? players[cn] : NULL;
}

void initclient()
{
	clientmap[0] = 0;
	newname("unarmed");
	player1->team = TEAM_BLUE;
}

entity flagdummies[2] = // in case the map does not provide flags
{
	entity(-1, -1, -1, CTF_FLAG, 0, 0, 0, 0),
	entity(-1, -1, -1, CTF_FLAG, 0, 1, 0, 0)
};

void initflag(int i)
{
	flaginfo &f = flaginfos[i];
	f.flagent = &flagdummies[i];
	f.pos = vec(f.flagent->x, f.flagent->y, f.flagent->z);
	f.actor = NULL;
	f.actor_cn = -1;
	f.team = i;
	f.state = m_ktf ? CTFF_IDLE : CTFF_INBASE;
}

void zapplayerflags(playerent *p)
{
	loopi(2) if(flaginfos[i].state==CTFF_STOLEN && flaginfos[i].actor==p) initflag(i);
}

void preparectf(bool cleanonly=false)
{
	loopi(2) initflag(i);
	if(!cleanonly)
	{
		loopv(ents)
		{
			entity &e = ents[i];
			if(e.type==CTF_FLAG)
			{
				e.spawned = true;
				if(e.attr2>=2) { conoutf("\f3invalid ctf-flag entity (%i)", i); e.attr2 = 0; }
				flaginfo &f = flaginfos[e.attr2];
				f.flagent = &e;
				f.pos.x = (float) e.x;
				f.pos.y = (float) e.y;
				f.pos.z = (float) e.z;
			}
		}
	}
}

struct gmdesc { int mode; char *desc; };
vector<gmdesc> gmdescs;

void gamemodedesc(char *modenr, char *desc)
{
	if(!modenr || !desc) return;
	struct gmdesc &gd = gmdescs.add();
	gd.mode = atoi(modenr);
	gd.desc = newstring(desc);
}

COMMAND(gamemodedesc, ARG_2STR);

void resetmap()
{
	resetsleep();
	clearminimap();
	cleardynlights();
	pruneundos();
	clearworldsounds();
	particlereset();
	setvar("gamespeed", 100);
	setvar("paused", 0);
	setvar("fog", 180);
	setvar("fogcolour", 0x8099B3);
	setvar("shadowyaw", 45);
}

int suicided = -1;

void startmap(const char *name, bool reset)   // called just after a map load
{
	s_strcpy(clientmap, name);
	sendmapident = true;
	localfirstkill = false;
	BotManager.BeginMap(name); // Added by Rick
	clearbounceents();
	resetspawns();
	preparectf(!m_flags);
	suicided = -1;
	spawncycle = -1;
	findplayerstart(player1);

	if(!reset) return;

	player1->points = player1->frags = player1->assists = player1->flagscore = player1->deaths = player1->lifesequence = 0;
	loopv(players) if(players[i]) players[i]->frags = players[i]->assists = players[i]->points = players[i]->flagscore = players[i]->deaths = players[i]->lifesequence = 0;
	if(editmode) toggleedit(true);
	intermission = false;
	showscores(false);
	minutesremaining = -1;
	arenaintermission = 0;
	bool noflags = (m_ctf || m_ktf) && (!numflagspawn[0] || !numflagspawn[1]);
	if(*clientmap) conoutf("game mode is \"%s\"%s", modestr(gamemode, modeacronyms > 0), noflags ? " - \f2but there are no flag bases on this map" : "");
	loopv(gmdescs) if(gmdescs[i].mode == gamemode) conoutf("\f1%s", gmdescs[i].desc);

	// run once
	if(firstrun)
	{
		persistidents = false;
		execfile("config/firstrun.cfg");
		persistidents = true;
		firstrun = false;
	}
	// execute mapstart event
	const char *mapstartonce = getalias("mapstartonce");
	if(mapstartonce && mapstartonce[0])
	{
		addsleep(0, mapstartonce); // do this as a sleep to make sure map changes don't recurse inside a welcome packet
		alias("mapstartonce", "");
	}
}

void suicide()
{
	if(player1->state == CS_ALIVE && suicided!=player1->lifesequence)
	{
		addmsg(N_SUICIDE, "r");
		suicided = player1->lifesequence;
	}
}

COMMAND(suicide, ARG_NONE);

// console and audio feedback

void flagmsg(int flag, int message, int actor, int flagtime)
{
	static int musicplaying = -1;
	playerent *act = getclient(actor);
	if(actor != getclientnum() && !act && message != FA_RESET) return;
	bool own = flag == player1->team;
	bool firstperson = actor == getclientnum();
	bool firstpersondrop = false;
	const char *teamstr = m_ktf ? "the" : own ? "your" : "the enemy";
	string subject, predicate, hashave;

	s_strcpy(subject, firstperson ? "you" : colorname(act));
	s_strcpy(hashave, firstperson ? "have" : "has");
	s_strcpy(predicate, " altered a flag");

	switch(message){
		case FA_PICKUP:
			playsound(S_FLAGPICKUP, SP_HIGHEST);
			if(firstperson){
				s_sprintf(predicate)("picked up %s flag", teamstr);
				if(!m_ctf || !own){
					musicsuggest(M_FLAGGRAB, m_ctf ? 90*1000 : 900*1000, true);
					musicplaying = flag;
				}
			}
			else s_sprintf(predicate)("got %s flag", teamstr);
			break;
		case FA_LOST:
		case FA_DROP:
		{
			playsound(S_FLAGDROP, SP_HIGHEST);
			s_sprintf(predicate)("%s %s flag", message == FA_LOST ? "lost" : "dropped", firstperson ? "the" : teamstr);
			if(firstperson) firstpersondrop = true;
			break;
		}
		case FA_RETURN:
			playsound(S_FLAGRETURN, SP_HIGHEST);
			s_sprintf(predicate)("returned %s flag", firstperson ? "your" : teamstr);
			break;
		case FA_SCORE:
			playsound(S_FLAGSCORE, SP_HIGHEST);
			if(firstperson){
				s_strcpy(predicate, "scored!");
				if(m_ctf) firstpersondrop = true;
			}
			else s_sprintf(predicate)("scored for %s team", teamstr);
			break;
		case FA_KTFSCORE:
		{
			playsound(S_VOTEPASS, SP_HIGHEST); // need better ktf sound here
			const int m = flagtime / 60, s = flagtime % 60;
			s_strcpy(predicate, "kept the flag for ");
			if(m) s_sprintf(predicate)("%s%d minute%s", predicate, m, m==1 ? " " : "s ");
			if(s) s_sprintf(predicate)("%s%d second%s", predicate, s, s==1 ? " " : "s ");;
			s_strcat(predicate, "now");
			break;
		}
		case FA_SCOREFAIL: // sound?
			s_strcpy(predicate, "failed to score because his flag is in base");
			break;
		case FA_RESET:
			playsound(S_FLAGRETURN, SP_HIGHEST);
			s_strcpy(subject, "\f1the server");
			s_strcpy(hashave, "had");
			s_strcpy(predicate, "reset the flag");
			firstpersondrop = true;
			break;
	}
	conoutf("\f2%s %s", subject, predicate);
	hudonlyf("\f2%s %s %s", subject, hashave, predicate);
	if(firstpersondrop && flag == musicplaying)
	{
		musicfadeout(M_FLAGGRAB);
		musicplaying = -1;
	}
}

COMMANDN(dropflag, tryflagdrop, ARG_NONE);

char *votestring(int type, char *arg1, char *arg2)
{
	if(type < 0 || type >= SA_NUM) return "<invalid vote type>";
	const char *msgs[SA_NUM] = { "kick player %s for %s", "ban player %s for %d minutes", "remove all bans", "set mastermode to %s", "%s autoteam", "force player %s to the enemy team", "\f0give \f%d%s \f5to player %s", "load map %s in mode %s", "%s demo recording for the next match", "stop demo recording", "clear%s demo%s%s", "set server description to '%s'", "shuffle teams", "subdue player %s", "revoke \fs\f%d%s\fr from %s", "toggle spectator for %s"};
	const char *msg = msgs[type];
	char *out = newstring(_MAXDEFSTR);
	out[_MAXDEFSTR] = '\0';
	s_strcpy(out, "unknown vote");
	switch(type){
		case SA_FORCETEAM:
		case SA_SUBDUE:
		case SA_SPECT:
		{
			int cn = atoi(arg1);
			playerent *p = getclient(cn);
			if(!p) break;
			s_sprintf(out)(msg, colorname(p));
			break;
		}
		case SA_KICK:
		{
			int cn = atoi(arg1);
			playerent *p = getclient(cn);
			if(!p) break;
			s_sprintf(out)(msg, colorname(p), arg2);
			break;
		}
		case SA_REVOKE:
		{
			playerent *p = getclient(atoi(arg1));
			if(!p) break;
			s_sprintf(out)(msg, privcolor(p->priv), privname(p->priv), colorname(p));
			break;
		}
		case SA_BAN:
		{
			int cn = atoi(arg2), minutes = atoi(arg1);
			playerent *p = getclient(cn);
			if(!p) break;
			s_sprintf(out)(msg, colorname(p), minutes);
			break;
		}
		case SA_GIVEADMIN:
		{
			int cn = atoi(arg1), priv = atoi(arg2);
			playerent *p = getclient(cn);
			if(!p) break;
			s_sprintf(out)(msg, privcolor(priv), privname(priv), colorname(p));
			break;
		}
		case SA_MASTERMODE:
			s_sprintf(out)(msg, mmfullname(atoi(arg1)));
			break;
		case SA_AUTOTEAM:
		case SA_RECORDDEMO:
			s_sprintf(out)(msg, atoi(arg1) == 0 ? "disable" : "enable");
			break;
		case SA_MAP:
			s_sprintf(out)(msg, arg2, modestr(atoi(arg1), modeacronyms > 0));
			break;
		case SA_SERVERDESC:
			s_sprintf(out)(msg, arg2);
			break;
		case SA_CLEARDEMOS:
		{
			bool demo = atoi(arg1) > 0;
			s_sprintf(out)(msg, !demo ? "all " : "", demo ? "s" : " ", demo ? "" : arg1);
			break;
		}
		default:
			s_sprintf(out)(msg, arg1, arg2);
			break;
	}
	return out;
}

votedisplayinfo *newvotedisplayinfo(playerent *owner, int type, char *arg1, char *arg2)
{
	if(type < 0 || type >= SA_NUM) return NULL;
	votedisplayinfo *v = new votedisplayinfo();
	v->owner = owner;
	v->type = type;
	v->millis = totalmillis + (30+10)*1000;
	char *votedesc = votestring(type, arg1, arg2);
	s_strcpy(v->desc, votedesc);
	DELETEA(votedesc);
	return v;
}

votedisplayinfo *curvote = NULL;
bool veto = false;

void callvote(int type, char *arg1, char *arg2)
{
	if(type >= 0 && type < SA_NUM){
		ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		ucharbuf p(packet->data, packet->dataLength);
		putint(p, N_CALLVOTE);
		putint(p, type);
		switch(type)
		{
			case SA_MAP:
				sendstring(arg1, p);
				putint(p, nextmode);
				break;
			case SA_SERVERDESC:
				sendstring(arg1, p);
				break;
			case SA_GIVEADMIN:
				putint(p, atoi(arg1));
				putint(p, atoi(arg2) ? atoi(arg2) : PRIV_MAX);
				break;
			case SA_BAN:
				putint(p, atoi(arg2));
				putint(p, atoi(arg1));
				break;
			case SA_STOPDEMO:
			case SA_REMBANS:
			case SA_SHUFFLETEAMS:
				break;
			case SA_KICK:
				sendstring(arg2, p);
			default:
				putint(p, atoi(arg1));
				break;
		}
		enet_packet_resize(packet, p.length());
		sendpackettoserv(1, packet);
	}
	else conoutf("\f3invalid vote");
}

void scallvote(char *type, char *arg1, char *arg2)
{
	if(type && inmainloop)
	{
		int t = atoi(type);
		if(t==SA_MAP) // FIXME
		{
			string n;
			itoa(n, nextmode);
			callvote(t, arg1, n);
		}
		else callvote(t, arg1, arg2);
	}
}

bool vote(int v)
{
	if(!curvote || v < 0 || v >= VOTE_NUM) return false;
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_VOTE);
	putint(p, v);
	enet_packet_resize(packet, p.length());
	sendpackettoserv(1, packet);
	// flowtron : 2008 11 06 : I don't think the following comments are still current
	if(!curvote) { /*printf(":: curvote vanished!\n");*/ return false; } // flowtron - happens when I call "/stopdemo"! .. seems the map-load happens in-between
	player1->vote = v;
	return true;
}

void displayvote(votedisplayinfo *v)
{
	if(!v) return;
	DELETEP(curvote);
	curvote = v;
	conoutf("%s called a vote: %s", v->owner ? colorname(v->owner) : "", curvote->desc);
	playsound(S_CALLVOTE, SP_HIGHEST);
	player1->vote = VOTE_NEUTRAL;
	loopv(players) if(players[i]) players[i]->vote = VOTE_NEUTRAL;
	veto = false;
}

void clearvote() { DELETEP(curvote); }

COMMANDN(callvote, scallvote, ARG_3STR); //fixme,ah
COMMAND(vote, ARG_1EXP);

void cleanplayervotes(playerent *p){	
	if(curvote && curvote->owner==p) curvote->owner = NULL;
}

void whois(int cn){
	addmsg(N_WHOIS, "ri", cn);
}
COMMAND(whois, ARG_1INT);

int sessionid = 0;

void setmaster(int claim){
	addmsg(N_SETROLE, "ris", claim ? PRIV_MASTER : PRIV_NONE, "");
}
COMMAND(setmaster, ARG_1INT);

SVARP(adminpass, "pwd"); // saved admin password

void setadmin(char *claim, char *password){
	if(!claim || !password) return;
	else addmsg(N_SETROLE, "ris", atoi(claim)!=0?PRIV_MAX:PRIV_NONE, genpwdhash(player1->name, *password ? password : adminpass, sessionid));
}
COMMAND(setadmin, ARG_2STR);

void changemap(const char *name){ // silently request map change, server may ignore
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_NEXTMAP);
	sendstring(name, p);
	putint(p, nextmode);
	enet_packet_resize(packet, p.length());
	sendpackettoserv(1, packet);
}

struct mline { string name, cmd; };
static vector<mline> mlines;

void *kickmenu = NULL, *banmenu = NULL, *forceteammenu = NULL, *giveadminmenu = NULL, *revokemenu = NULL, *whoismenu = NULL;

void refreshsopmenu(void *menu, bool init)
{
	menureset(menu);
	mlines.shrink(0);
	loopv(players) if(players[i])
	{
		mline &m = mlines.add();
		s_strcpy(m.name, colorname(players[i]));
		s_sprintf(m.cmd)("%s %d", menu==kickmenu ? "kick" : (menu==banmenu ? "ban" : (menu==forceteammenu ? "forceteam" : (menu==revokemenu ? "revoke" : (menu==giveadminmenu ? "giverole" : (menu==whoismenu ? "whois" : "unknownplayeraction"))))), i);
		if(menu==kickmenu && getalias("_kickbanreason")!=NULL) s_sprintf(m.cmd)("%s [ %s ]", m.cmd, getalias("_kickbanreason"));
		menumanual(menu, m.name, m.cmd);
	}
}

extern bool watchingdemo;

// rotate through all spec-able players
playerent *updatefollowplayer(int shiftdirection)
{
	if(!shiftdirection)
	{
		playerent *f = players.inrange(player1->followplayercn) ? players[player1->followplayercn] : NULL;
		if(f && (watchingdemo || !f->isspectating())) return f;
	}

	// collect spec-able players
	vector<playerent *> available;
	loopv(players) if(players[i])
	{
		if(players[i]->state==CS_DEAD && m_duel) continue;
		available.add(players[i]);
	}
	if(!available.length()) return NULL;

	// rotate
	int oldidx = -1;
	if(players.inrange(player1->followplayercn)) oldidx = available.find(players[player1->followplayercn]);
	if(oldidx<0) oldidx = 0;
	int idx = (oldidx+shiftdirection) % available.length();
	if(idx<0) idx += available.length();

	player1->followplayercn = available[idx]->clientnum;
	return players[player1->followplayercn];
}

// set new spect mode
void spectate(int mode)
{
	if(!player1->isspectating()) return;
	if(mode == player1->spectatemode) return;
	showscores(false);
	switch(mode)
	{
		case SM_FOLLOW1ST:
		case SM_FOLLOW3RD:
		case SM_FOLLOW3RD_TRANSPARENT:
		{
			if(players.length() && updatefollowplayer()) break;
			else mode = SM_FLY;
		}
		case SM_FLY:
		{
			if(player1->spectatemode != SM_FLY)
			{
				// set spectator location to last followed player
				playerent *f = updatefollowplayer();
				if(f)
				{
					player1->o = f->o;
					player1->yaw = f->yaw;
					player1->pitch = 0.0f;
					player1->resetinterp();
				}
				else entinmap(player1); // or drop 'em at a random place
			}
			break;
		}
		default: break;
	}
	player1->spectatemode = mode;
}


void togglespect() // cycle through all spectating modes
{
	int mode;
	if(player1->spectatemode==SM_NONE) mode = SM_FOLLOW1ST; // start with 1st person spect
	else mode = SM_FOLLOW1ST + ((player1->spectatemode - SM_FOLLOW1ST + 1) % (SM_NUM-SM_FOLLOW1ST));
	spectate(mode);
}

void changefollowplayer(int shift)
{
	updatefollowplayer(shift);
}

COMMAND(spectate, ARG_1INT);
COMMAND(togglespect, ARG_NONE);
COMMAND(changefollowplayer, ARG_1INT);

int isalive() { return player1->state==CS_ALIVE ? 1 : 0; }
COMMANDN(alive, isalive, ARG_IVAL);

void serverextension(char *ext, char *args)
{
	if(!ext || !ext[0]) return;
	size_t n = args ? strlen(args)+1 : 0;
	if(n>0) addmsg(N_EXT, "rsis", ext, n, args);
	else addmsg(N_EXT, "rsi", ext, n);
}

COMMAND(serverextension, ARG_2STR);

