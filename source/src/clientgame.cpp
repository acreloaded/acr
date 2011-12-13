// clientgame.cpp: core game related stuff

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

int nextmode = G_DM, nextmuts = G_M_TEAM; // nextmode becomes gamemode, nextmuts becomes mutators - after next map load
VAR(gamemode, 1, 0, 0);
VAR(mutators, 1, 0, 0);
VARP(modeacronyms, 0, 0, 1);

flaginfo flaginfos[2];

void mode(int n){ nextmode = n; }
COMMAND(mode, ARG_1INT);
void muts(int n){ nextmuts = n; }
COMMAND(muts, ARG_1INT);

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
	const int maxskin[TEAM_NUM] = { 3, 5, 0 };
	pl->skin = skin % (maxskin[pl->team]+1);
}

/*
bool duplicatename(playerent *d, char *name = NULL)
{
	if(!name) name = d->name;
	if(d!=player1 && !strcmp(name, player1->name)) return true;
	if(!strncmp(name, "you", 3)) return true;
	loopv(players) if(players[i] && d!=players[i] && !strcmp(name, players[i]->name)) return true;
	return false;
}
*/

const char *colorname(playerent *d, bool stats)
{
	if(!d) return "unknown";
	static string cname;
	formatstring(cname)("%s \fs\f%d(%d)", d->name, d->ownernum < 0 ? 6 : 7, d->clientnum);
	if(stats){
		defformatstring(stat)("%d%.*f", d->health > 50 * HEALTHSCALE ? 0 : d->health > 25 * HEALTHSCALE ? 2 : d->health > 0 ? 3 : 4, HEALTHPRECISION, d->health / (float)HEALTHSCALE);
		if(d->armor) formatstring(stat)("%s\f5-\f4%d", stat, d->armor);
		formatstring(cname)("%s \f5[\f%s\f5]", cname, stat);
	}
	concatstring(cname, "\fr");
	return cname;
}

const char *colorping(int ping)
{
	static string cping;
	if(multiplayer(false)) formatstring(cping)("\fs\f%d%d\fr", ping <= 500 ? 0 : ping <= 1000 ? 2 : 3, ping);
	else formatstring(cping)("%d", ping);
	return cping;
}

const char *colorpj(int pj)
{
	static string cpj;
	if(multiplayer(false)) formatstring(cpj)("\fs\f%d%d\fr", pj <= 90 ? 0 : pj <= 170 ? 2 : 3, pj);
	else formatstring(cpj)("%d", pj);
	return cpj;
}

void newname(const char *name)
{
	if(name[0])
	{
		static string name2;
		filtername(name2, name);
		if(!name2[0]) copystring(name2, "unnamed");
		addmsg(N_NEWNAME, "rs", name2);
	}
	else conoutf("your name is: %s", player1->name);
	alias("curname", player1->name);
}

void newteam(char *name)
{
	if(name[0]){
		if(strcmp(name, "BLUE") && strcmp(name, "RED") && strcmp(name, "SPECT")){
			conoutf("\f3\"%s\" %s (try RED, BLUE or SPECT)", name, _("team_invalid"));
			return;
		}
		int nt = team_int(name);
		if(nt == player1->team) return; // same team
		addmsg(N_SWITCHTEAM, "ri", nt);
	}
	else conoutf("%s: %s", _("team_you"), team_string(player1->team));
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

VARP(noob, 0, 0, 1); // pretty useless, just doubles level-up rate
VARFP(level, 1, 1, MAXLEVEL, addexp(0));
VARFP(experience, 0, 0, MAXEXP, addexp(0));
void addexp(int xp){
	xp = xp * xp * (noob ? 2 : 1);
	#define xpfactor clamp(level, 1, 20)
	float factor = xpfactor; // factor to reduce experience collection
	experience += fabs(xp / factor);
	if(experience >= MAXEXP){
		level = clamp(level + 1, 1, MAXLEVEL);
		addmsg(N_LEVELUP, "r");
		factor = xpfactor;
		experience = max(0.f, (experience - MAXEXP) / factor);
	}
	#undef xpfactor
}

bool spawnenqueued = false;

void deathstate(playerent *pl, playerent *act)
{
	if(pl == player1 && editmode) toggleedit(true);
	pl->state = CS_DEAD;
	pl->spectatemode = SM_DEATHCAM;
	pl->respawnoffset = pl->lastpain = lastmillis;
	pl->move = pl->strafe = pl->pitchvel = pl->pitchreturn = pl->ads = 0;
	// position camera (used to be roll/pitch)
	pl->roll = 0;
	/*
	// we should look at this
	vec target = act ? act->o : pl->o;
	target.sub(pl->o);
	// look down at dead body
	if(target.magnitude() < 1) target.z -= 1;
	target.normalize();
	pl->pitch = atan2(target.z, target.x) / RAD;
	pl->yaw = atan2(target.y, target.x) / RAD;
	*/

	pl->scoping = pl->attacking = pl->wantsreload = false;
	pl->wantsswitch = -1;
	pl->weaponsel->onownerdies();
	pl->damagelog.setsize(0);
	pl->radarmillis = 0;

	if(pl==player1){
		if(showscoresondeath) showscores(true);
		setscope(false);
		//damageblend(-1);
		spawnenqueued = false;
	}
	else pl->resetinterp();
}

void spawnstate(playerent *d)			  // reset player state not persistent across spawns
{
	d->respawn();
	d->spawnstate(gamemode, mutators);
	if(d==player1)
	{
		if(player1->skin!=nextskin) setskin(player1, nextskin);
		setscope(false);
	}
}

playerent *newplayerent()				 // create a new blank player
{
	playerent *d = new playerent;
	setskin(d, rnd(6));
	weapon::equipplayer(d); // flowtron : avoid overwriting d->spawnstate(gamemode) stuff from the following line (this used to be called afterwards)
	spawnstate(d);
	return d;
}

void zapplayer(playerent *&d){
	if(d){
		if(d->pBot){
			// C++ guarantees that delete NULL; does nothing
			//if(d->pBot->m_pBotSkill) 
				delete d->pBot->m_pBotSkill;
			delete (CACBot *)d->pBot;
		}
		delete d;
		d = NULL;
	}
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
	loopv(players) if(players[i] && players[i]->type==ENT_PLAYER && !isowned(players[i]))
	{
		playerent *d = players[i];
		const int lagtime = totalmillis-d->lastrecieve;
		if(!lagtime || intermission) continue;
		else if(lagtime>1000 && d->state==CS_ALIVE)
		{
			d->state = CS_WAITING;
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

bool showhudtimer(int maxmillis, int startmillis, const char *msg, bool flash)
{
	static int lasttick = 0;
	if(lasttick > startmillis + maxmillis) return false;
	lasttick = lastmillis;
	const bool queued = spawnenqueued && !m_duke(gamemode, mutators);
	defformatstring(str)("\f%d%s: %.1fs", queued ? 2 : 3, queued ? _("spawn_queued") : _("spawn_wait"), (startmillis + maxmillis - lastmillis) / 1000.f);
	if(lastmillis <= startmillis + maxmillis) hudeditf(HUDMSG_TIMER|HUDMSG_OVERWRITE, flash || queued ? str : str+2);
	else hudeditf(HUDMSG_TIMER, msg);
	return true;
}

int lastspawnattempt = 0;

void showrespawntimer()
{
	if(intermission) return;
	if(m_duke(gamemode, mutators))
	{
		if(!arenaintermission) return;
		showhudtimer(5000, arenaintermission, _("spawn_fight"), lastspawnattempt >= arenaintermission && lastmillis < lastspawnattempt+100);
	}
	else if(player1->state==CS_DEAD)// && (!player1->isspectating() || player1->spectatemode==SM_DEATHCAM))
		showhudtimer(SPAWNDELAY, player1->respawnoffset, _("spawn_ready"), lastspawnattempt >= arenaintermission && lastmillis < lastspawnattempt+100);
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
	if(player1){ // only shoot when connected to server
		shoot(player1, worldpos);
		loopv(players) if(players[i]){
			playerent *p = players[i];
			vec targ(sinf(RAD*p->yaw) * cosf(RAD*p->pitch), -cosf(RAD*p->yaw)* cosf(RAD*p->pitch), sinf(RAD*p->pitch));
			shoot(p, targ.add(p->o));
		}
	}
	movebounceents();
	moveotherplayers();
	gets2c();
	showrespawntimer();

	BotManager.Think(); // let bots think

	movelocalplayer();
	if(getclientnum() >= 0) c2sinfo(); // do this last, to reduce the effective frame lag
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
		if(!other || (m_team(gamemode, mutators) && team == other->team)) continue;
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
		int type = m_team(gamemode, mutators) && !m_zombie(gamemode) ? d->team : 100;
		if(m_duke(gamemode, mutators) && arenaspawn >= 0)
		{
			int x = -1;
			loopi(arenaspawn + 1) x = findentity(PLAYERSTART, x+1, type);
			if(x >= 0) e = &ents[x];
		}
		else if((m_team(gamemode, mutators) || m_duke(gamemode, mutators)) && !m_keep(gamemode) && !m_zombie(gamemode)) // ktf and zombies uses ffa spawns
		{
			loopi(r) spawncycle = findentity(PLAYERSTART, spawncycle+1, type);
			if(spawncycle >= 0) e = &ents[spawncycle];
		}
		else
		{
			float bestdist = -1;

			loopi(r)
			{
				spawncycle = (m_keep(gamemode) || m_zombie(gamemode)) && numspawn[2] > 5 ? findentity(PLAYERSTART, spawncycle+1, 100) : findentity(PLAYERSTART, spawncycle+1);
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
	addmsg(N_TRYSPAWN, "ri", d->clientnum);
	/*
	d->respawn();
	d->spawnstate(gamemode);
	d->state = d==player1 && editmode ? CS_EDITING : CS_ALIVE;
	findplayerstart(d);
	*/
}

void respawnself(){
	spawnplayer(player1);
	spawnenqueued = !spawnenqueued;
}

bool tryrespawn(){
	if(player1->state==CS_DEAD){
		respawnself();

		int respawnmillis = player1->respawnoffset+(m_duke(gamemode, mutators) ? 0 : (m_affinity(gamemode) ? 5000 : 1000));
		if(lastmillis>respawnmillis){
			player1->attacking = false;
			if(m_duke(gamemode, mutators)){
				if(!arenaintermission) hudeditf(HUDMSG_TIMER, _("spawn_nextround"));
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

void dodamage(int damage, playerent *pl, playerent *actor, int weapon, int style, vec src)
{
	if(pl->state == CS_DEAD || intermission) return;

	pl->respawnoffset = pl->lastpain = lastmillis;
	// damage direction/hit push
	if(pl != actor || weapon == WEAP_GRENADE || weapon == WEAP_BOW || pl->o.dist(src) > 4){
		// damage indicator
		pl->damagestack.add(damageinfo(src, lastmillis, damage));
		// push
		vec dir = pl->o;
		dir.sub(src).normalize();
		pl->hitpush(damage, dir, weapon, actor->perk == PERK_POWER);
		// hit markers
		actor->lasthitmarker = lastmillis;
		// assists
		if(pl->damagelog.find(actor->clientnum) < 0) pl->damagelog.add(actor->clientnum);
	}

	if(style & FRAG_CRIT){ // critical damage
		actor->addicon(eventicon::CRITICAL);
		pl->addicon(eventicon::CRITICAL);
	}

	// roll if you are hit
	if(pl==player1 || isowned(pl)) pl->damageroll(damage);

	// sound
	if(pl==gamefocus) playsound(S_PAIN6, SP_HIGH);
	else playsound(S_PAIN1+rnd(5), pl);
}

void dokill(playerent *pl, playerent *act, int weapon, int damage, int style, int combo, float killdist){
	if(pl->state==CS_DEAD || intermission) return;
	// kill message
	bool headshot = isheadshot(weapon, style);
	int obit = OBIT_DEATH;
	string subject, predicate, hashave;
	*subject = *predicate = *hashave = 0;
	formatstring(subject)("\f2\fs%s\f2", act == player1 ? "\f1you" : colorname(act));
	copystring(hashave, act == player1 ? "have" : "has");
	if(pl == act){
		copystring(predicate, suicname(obit = obit_suicide(weapon)));
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
			concatstring(predicate, "\f3");
		}
		if(pl == gamefocus) concatstring(predicate, "!\f2");
	}
	else formatstring(predicate)("%s%s %s%s", style&FRAG_REVENGE ? "\fs\f0vengefully \fr" : "", killname(obit = toobit(weapon, style), headshot),
			isteam(pl, act) ? act==player1 ? "\f3your teammate " : "\f3his teammate " : "", pl == player1 ? "\f1you\f2" : colorname(pl));
	if(killdist) concatformatstring(predicate, " (@%.2f m)", killdist);
	// streaks
	if(act->killstreak++) concatformatstring(predicate, " %d ks", act->killstreak);
	if(pl->deathstreak++) concatformatstring(predicate, " %d ds", pl->deathstreak);
	// assist count
	pl->damagelog.removeobj(pl->clientnum);
	pl->damagelog.removeobj(act->clientnum);
	loopv(pl->damagelog) if(!getclient(pl->damagelog[i])) pl->damagelog.remove(i--);
	// HUD for first person
	if(pl == gamefocus || act == gamefocus || pl->damagelog.find(gamefocus->clientnum) >= 0){
		if(pl->damagelog.length()) hudonlyf("%s %s %s, %d assister%s", subject, hashave, predicate, pl->damagelog.length(), pl->damagelog.length()==1?"":"s");
		else hudonlyf("%s %s %s", subject, hashave, predicate);
	}
	// assists
	if(pl->damagelog.length()){
		concatstring(predicate, ", assisted by");
		bool first = true;
		while(pl->damagelog.length()){
			playerent *p = getclient(pl->damagelog.pop());
			++p->assists;
			concatformatstring(predicate, "%s \fs\f%d%s\fr", first ? "" : !pl->damagelog.length() ? " and" : ",", isteam(p, pl) ? 3 : 2, colorname(p));
			first = false;
		}
	}
	switch(combo){
		case 2: concatstring(predicate, ", \fs\f0\fbdouble-killing\fr"); break;
		case 3: concatstring(predicate, ", \fs\f1\fbtriple-killing\fr"); break;
		case 4: concatstring(predicate, ", \fs\f3\fbmulti-killing\fr"); break;
		case 5: concatstring(predicate, ", \fs\f4\fbslaughering\fr"); break;
		default: if(combo > 1) concatstring(predicate, ", \fs\f5\fbPWNING\fr"); break;
	}
	if(style & FRAG_FIRST) concatstring(predicate, " for \fs\f3\fbfirst blood\fr");
	if(style & FRAG_CRIT) concatstring(predicate, " with a \fs\f1\fbcritical hit\fr");
	conoutf("%s %s", subject, predicate);
	pl->killstreak = act->deathstreak = 0;
	
	int icon = -1;
	if(style & FRAG_GIB){
		if(headshot && weapon != WEAP_SHOTGUN){
			playsound(S_HEADSHOT, act, act == gamefocus ? SP_HIGHEST : SP_HIGH);
			playsound(S_HEADSHOT, pl, pl == gamefocus ? SP_HIGHEST : SP_HIGH); // both get headshot sound
			icon = eventicon::HEADSHOT; pl->addicon(eventicon::DECAPITATED); // both get headshot info
		}
		addgib(pl);
	}
	if(style & FRAG_FIRST) icon = eventicon::FIRSTBLOOD;
	if(icon >= 0) act->addicon(icon);

	addobit(act, obit, style, headshot, pl);
	
	deathstate(pl, act);
	++pl->deaths;
	playsound(S_DIE1+rnd(2), pl);
}

VAR(minutesremaining, 1, 0, 0);
VAR(gametimecurrent, 1, 0, 0);
VAR(gametimemaximum, 1, 0, 0);
VAR(lastgametimeupdate, 1, 0, 0);

void timeupdate(int milliscur, int millismax, int musicseed){
	// if( lastmillis - lastgametimeupdate < 1000 ) return; // avoid double-output, but possibly omit new message if joined 1s before server switches to next minute
	lastgametimeupdate = lastmillis;
	gametimecurrent = milliscur;
	gametimemaximum = millismax;
	minutesremaining = (gametimemaximum - gametimecurrent + 60000 - 1) / 60000;
	if(minutesremaining){
		if(minutesremaining==1){
			musicsuggest(M_LASTMINUTE1 + (musicseed%2), 70000, true);
			hudoutf("%s", _("timer_lastminute"));
		}
		else conoutf("%s %d minutes", _("timer_remain"), minutesremaining);
	}
	else{
		intermission = true;
		player1->attacking = false;
		conoutf("%s\n%s", _("timer_intermission"), _("timer_intermission2"));
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
	players[d->clientnum = cn] = d;
	d->ownernum = -1;
	return d;
}

playerent *getclient(int cn)   // ensure valid entity
{
	return cn == getclientnum() && cn >= 0 ? player1 : players.inrange(cn) ? players[cn] : NULL;
}

void initclient()
{
	clientmap[0] = 0;
	//newname("waitingforname");
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
	f.state = m_keep(gamemode) ? CTFF_IDLE : CTFF_INBASE;
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

struct mdesc { int mode; char *desc; };
vector<mdesc> gmdescs, mutdescs;

void gamemodedesc(char *modenr, char *desc)
{
	if(!modenr || !desc) return;
	struct mdesc &gd = gmdescs.add();
	gd.mode = atoi(modenr);
	gd.desc = newstring(desc);
}

COMMAND(gamemodedesc, ARG_2STR);

void modifierdesc(char *mutnr, char *desc)
{
	if(!mutnr || !desc) return;
	struct mdesc &md = mutdescs.add();
	md.mode = atoi(mutnr);
	md.desc = newstring(desc);
}

COMMANDN(mutatorsdesc, modifierdesc, ARG_2STR);

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
	setvar("fogcolor", 0x8099B3);
	setvar("shadowyaw", 45);
}

void startmap(const char *name, bool reset)   // called just after a map load
{
	copystring(clientmap, name);
	sendmapident = true;
	localfirstkill = false;
	BotManager.BeginMap(name); // Added by Rick
	clearbounceents();
	resetspawns();
	preparectf(!m_affinity(gamemode));
	spawncycle = -1;
	findplayerstart(player1);

	if(!reset) return;

	// resets...
	player1->points =
		player1->frags =
		player1->assists =
		player1->flagscore =
		player1->deaths =
		player1->lifesequence =
		player1->radarearned =
		player1->airstrikes =
		player1->nukemillis = 0;
	loopv(players) if(players[i]) // another chain of resets
		players[i]->points =
		players[i]->frags =
		players[i]->assists =
		players[i]->flagscore =
		players[i]->deaths =
		players[i]->lifesequence =
		players[i]->radarearned =
		players[i]->airstrikes =
		players[i]->nukemillis = 0;
	if(editmode) toggleedit(true);
	intermission = false;
	showscores(false);
	minutesremaining = -1;
	arenaintermission = 0;
	bool noflags = (m_capture(gamemode) || m_keep(gamemode)) && (!numflagspawn[0] || !numflagspawn[1]);
	if(*clientmap) conoutf("game mode is \"%s\"%s", modestr(gamemode, mutators, modeacronyms > 0), noflags ? " - \f2but there are no flag bases on this map" : "");
	loopv(gmdescs) if(gmdescs[i].mode == gamemode) conoutf("\f1%s", gmdescs[i].desc);
	loopv(mutdescs) if((1 << mutdescs[i].mode) & mutators) conoutf("\f2%s", mutdescs[i].desc);

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

void suicide(){ addmsg(N_SUICIDE, "ri", getclientnum()); }

COMMAND(suicide, ARG_NONE);

// console and audio feedback

void flagmsg(int flag, int message, int actor, int flagtime)
{
	static uint flagmusic = 0;
	playerent *act = getclient(actor);
	if(actor != getclientnum() && !act && message != FA_RESET) return;
	bool own = flag == player1->team;
	bool firstperson = actor == getclientnum();
	bool firstpersondrop = false;
	string teamstr_absolute;
	formatstring(teamstr_absolute)("the %s", team_string(flag));
	const char *teamstr = m_ktf2(gamemode, mutators) ? teamstr_absolute : m_keep(gamemode) ? "the" : own ? "your" : "the enemy";
	string subject, predicate, hashave;

	copystring(subject, firstperson ? "you" : colorname(act));
	copystring(hashave, firstperson ? "have" : "has");
	copystring(predicate, " altered a flag");

	switch(message){
		case FA_PICKUP:
			playsound(S_FLAGPICKUP, SP_HIGHEST);
			if(firstperson){
				formatstring(predicate)("picked up %s flag", teamstr);
				if(!own || !m_capture(gamemode)){
					musicsuggest(M_FLAGGRAB, m_capture(gamemode) ? 90*1000 : 900*1000, true);
					flagmusic |= 1 << flag;
				}
			}
			else formatstring(predicate)("got %s flag", teamstr);
			break;
		case FA_LOST:
		case FA_DROP:
		{
			playsound(S_FLAGDROP, SP_HIGHEST);
			formatstring(predicate)("%s %s flag", message == FA_LOST ? "lost" : "dropped", teamstr);
			if(firstperson) firstpersondrop = true;
			break;
		}
		case FA_RETURN:
			playsound(S_FLAGRETURN, SP_HIGHEST);
			formatstring(predicate)("returned %s flag", teamstr);
			break;
		case FA_SCORE:
			playsound(S_FLAGSCORE, SP_HIGHEST);
			formatstring(predicate)("scored for \fs\f%d%s\fr team!", team_rel_color(player1, act), firstperson || isteam(act, player1) ? "your" : "the enemy");
			if(m_capture(gamemode) || m_bomber(gamemode)) firstpersondrop = true;
			break;
		case FA_KTFSCORE:
		{
			playsound(S_VOTEPASS, SP_HIGHEST); // need better ktf sound here
			const int m = flagtime / 60, s = flagtime % 60;
			formatstring(predicate)("kept %s flag for ", teamstr_absolute);
			if(m) formatstring(predicate)("%s%d minute%s", predicate, m, m==1 ? " " : "s ");
			if(s) formatstring(predicate)("%s%d second%s", predicate, s, s==1 ? " " : "s ");;
			concatstring(predicate, "now");
			break;
		}
		case FA_SCOREFAIL: // sound?
			copystring(predicate, "failed to score");
			break;
		case FA_RESET:
			playsound(S_FLAGRETURN, SP_HIGHEST);
			copystring(subject, "\f1the server");
			copystring(hashave, "had just");
			formatstring(predicate)("reset %s flag", teamstr_absolute);
			firstpersondrop = true;
			break;
	}
	conoutf("\f2%s %s", subject, predicate);
	hudonlyf("\f2%s %s %s", subject, hashave, predicate);
	if(firstpersondrop && flagmusic){
		if(!(flagmusic &= ~(1 << flag))) musicfadeout(M_FLAGGRAB);
	}
}

COMMANDN(dropflag, tryflagdrop, ARG_NONE);

const char *votestring(int type, const votedata &vote)
{
	if(type < 0 || type >= SA_NUM) return "<invalid vote type>";
	static string out = {0};
	copystring(out, "unknown vote");
	switch(type){
		// maps
		case SA_MAP:
			formatstring(out)("load map %s in mode %s", vote.str1, modestr(vote.int1, vote.int2, modeacronyms > 0));
			break;

		// playeractions
		case SA_KICK: // int1, str1
		{
			playerent *p = getclient(vote.int1);
			if(p) formatstring(out)("kick player %s for %s", colorname(p), vote.str1);
			break;
		}

		case SA_BAN: // int1, int2
		{
			int minutes = vote.int1, cn = vote.int2;
			playerent *p = getclient(cn);
			if(p) formatstring(out)("ban %s for %d minutes", colorname(p), minutes);
			break;
		}

		case SA_REVOKE: // int1
		case SA_GIVEROLE: // int1, int2
		{
			playerent *p = getclient(vote.int1);
			const char priv = (type == SA_GIVEROLE ? vote.int2 : p->priv);
			if(p) formatstring(out)(
				type == SA_GIVEROLE ? "\f0give \f%d%s \f5to player %s" :
				type == SA_REVOKE ? "revoke \fs\f%d%s\fr from %s" :
				"%s's \fs\f%d%s\fr", privcolor(priv), privname(priv), colorname(p));
			break;
		}

		case SA_FORCETEAM: // int1
		case SA_SUBDUE:
		case SA_SPECT:
		{
			playerent *p = getclient(vote.int1);
			if(p) formatstring(out)(
				type == SA_FORCETEAM ? "force player %s to the enemy team" :
				type == SA_SUBDUE ? "subdue player %s" :
				type == SA_SPECT ? "toggle spectator for %s" :
				"unknown on %s", colorname(p));
			break;
		}

		// int action
		case SA_BOTBALANCE: // int1
			formatstring(out)(
				!vote.int1 ? "disable all bots" :
				vote.int1 < 0 ? "automatically balance bots" :
				"balance to %d bots", vote.int1);
			break;

		case SA_MASTERMODE: // int1
			formatstring(out)("set mastermode to %s", mmfullname(vote.int1));
			break;

		case SA_CLEARDEMOS:
			formatstring(out)(vote.int1 > 0 ? "clear demo %d" : "clear all demos", vote.int1);
			break;

		// string action
		case SA_SERVERDESC:
			formatstring(out)("set server description to '%s'", vote.str1);
			break;

		// enableaction
		case SA_AUTOTEAM: // ...
		case SA_RECORDDEMO:
			formatstring(out)(type == SA_AUTOTEAM ? "%s autoteam" : "%s demo recording for the next match", !vote.int1 ? "disable" : "enable");
			break;

		// static
		case SA_REMBANS: copystring(out, "remove all temporary bans"); break;
		case SA_STOPDEMO: copystring(out, "stop demo recording"); break;
		case SA_SHUFFLETEAMS: copystring(out, "shuffle teams"); break;
	}
	return out;
}

votedisplayinfo *newvotedisplayinfo(playerent *owner, int type, const votedata &vote)
{
	if(type < 0 || type >= SA_NUM) return NULL;
	votedisplayinfo *v = new votedisplayinfo();
	v->owner = owner;
	v->type = type;
	v->millis = totalmillis + (30+10)*1000;
	copystring(v->desc, votestring(type, vote));
	return v;
}

votedisplayinfo *curvote = NULL;
bool veto = false;

void callvote(int type, const votedata &vote)
{
	if(type >= 0 && type < SA_NUM){
		ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		ucharbuf p(packet->data, packet->dataLength);
		putint(p, N_CALLVOTE);
		putint(p, type);
		switch(type)
		{
			case SA_MAP:
				sendstring(vote.str1, p);
				putint(p, nextmode);
				putint(p, nextmuts);
				break;
			case SA_SERVERDESC:
				sendstring(vote.str1, p);
				break;
			case SA_GIVEROLE:
				putint(p, vote.int1);
				putint(p, vote.int2 ? vote.int2 : PRIV_MAX);
				break;
			case SA_BAN:
				putint(p, vote.int1);
				putint(p, vote.int2);
				break;
			case SA_STOPDEMO:
			case SA_REMBANS:
			case SA_SHUFFLETEAMS:
				break;
			case SA_KICK:
				sendstring(vote.str1, p);
				// INTENTIONAL FALLTHROUGH
			default:
				putint(p, vote.int1);
				break;
		}
		enet_packet_resize(packet, p.length());
		sendpackettoserv(1, packet);
	}
	else conoutf("\f3invalid vote");
}

void callvote_parser(char *type, char *arg1, char *arg2, char *arg3)
{
	if(type && inmainloop)
	{
		int t = atoi(type);
		if(t < 0 || t >= SA_NUM)
			conoutf("\f3invalid vote: \f2%s %s %s %s", type, arg1, arg2, arg3);
		else
		{
			// storage of vote data
			static string str1;
			static votedata vote(str1);
			vote = votedata(str1); // reset
			switch(t)
			{
				case SA_MAP:
					vote.int1 = atoi(arg2);
					vote.int2 = atoi(arg3);
					// fallthrough
				case SA_SERVERDESC:
					copystring(str1, arg1);
					break;

				case SA_KICK:
					vote.int1 = atoi(arg1);
					copystring(str1, arg2);
					break;
				case SA_BAN:
				case SA_GIVEROLE:
					vote.int2 = atoi(arg2);
					// fallthrough
				case SA_MASTERMODE:
				case SA_AUTOTEAM:
				case SA_FORCETEAM:
				case SA_RECORDDEMO:
				case SA_CLEARDEMOS:
				case SA_SUBDUE:
				case SA_REVOKE:
				case SA_SPECT:
				case SA_BOTBALANCE:
					vote.int1 = atoi(arg1);
					// fallthrough
				default:
				case SA_REMBANS:
				case SA_STOPDEMO:
				case SA_SHUFFLETEAMS:
					break;
			}
			callvote(t, vote);
		}
		
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
	player1->vote = v; // did you think that our bots could vote? ;)
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

COMMANDN(callvote, callvote_parser, ARG_4STR);
COMMAND(vote, ARG_1EXP);

void cleanplayervotes(playerent *p){	
	if(curvote && curvote->owner==p) curvote->owner = NULL;
}

void whois(int cn){
	addmsg(N_WHOIS, "ri", cn);
}
COMMAND(whois, ARG_1INT);

int sessionid = 0;

SVARP(adminpass, "pwd"); // saved admin password

void setadmin(char *claim, char *password){
	if(!claim || !*claim || *claim == '0') return addmsg(N_SETPRIV, "r");
	addmsg(N_CLAIMPRIV, "rs", genpwdhash(player1->name, *password ? password : adminpass, sessionid));
}
COMMAND(setadmin, ARG_2STR);

void changemap(const char *name){ // silently request map change, server may ignore
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_NEXTMAP);
	sendstring(name, p);
	putint(p, nextmode);
	putint(p, nextmuts);
	enet_packet_resize(packet, p.length());
	sendpackettoserv(1, packet);
}

struct mline { string name, cmd; };
static vector<mline> mlines;

void *kickmenu = NULL, *banmenu = NULL, *forceteammenu = NULL, *giveadminmenu = NULL, *revokemenu = NULL, *whoismenu = NULL, *spectmenu = NULL;

void refreshsopmenu(void *menu, bool init)
{
	menureset(menu);
	mlines.shrink(0);
	vector<playerent *> p;
	p.add(player1);
	loopv(players) if(players[i]) p.add(players[i]);
	loopv(p){
		mline &m = mlines.add();
		copystring(m.name, colorname(p[i]));
		formatstring(m.cmd)("%s %d", menu==kickmenu ? "kick" : (menu==banmenu ? "ban" : (menu==forceteammenu ? "forceteam" : (menu==revokemenu ? "revoke" : (menu==giveadminmenu ? "giverole" : (menu==whoismenu ? "whois" : (menu==spectmenu ? "forcespect" : "unknownplayeraction")))))), p[i]->clientnum);
		if(menu==kickmenu && getalias("_kickbanreason")!=NULL) formatstring(m.cmd)("%s [ %s ]", m.cmd, getalias("_kickbanreason"));
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
	loopv(players) if(players[i] && players[i]->team != TEAM_SPECT && (players[i]->state != CS_DEAD || !m_duke(gamemode, mutators)))
		available.add(players[i]);
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

void radarinfo(int &total, playerent *&last, int &lastremain, const playerent *asSeenBy){
	// we return with the parameters!
	total = 0;
	last = NULL;
	lastremain = 0;
	// loop through players
	loopi(players.length() + 1){
		playerent *pl = players.inrange(i) ? players[i] : player1;
		if(!pl) continue; // null
		if(pl->radarearned <= totalmillis) continue; // no radar!
		if(asSeenBy && asSeenBy != pl && asSeenBy->team != TEAM_SPECT && !isteam(asSeenBy, pl)) continue; // not the same team
		// add to total
		++total;
		// we want the HIGHEST number possible
		if(pl->radarearned > totalmillis + lastremain){
			lastremain = pl->radarearned - totalmillis;
			last = pl;
		}
	}
}

bool radarup(const playerent *who){ // maybe revise into a faster version?
	int total, lastremain;
	playerent *last;
	radarinfo(total, last, lastremain, who);
	return total > 0;
}

void nukeinfo(int &total, playerent *&first, int &firstremain){
	// we return with the parameters!
	total = 0;
	first = NULL;
	firstremain = 0;
	// loop through players
	loopi(players.length() + 1){
		playerent *pl = players.inrange(i) ? players[i] : player1;
		if(!pl) continue; // null
		if(pl->nukemillis <= totalmillis) continue; // no upcoming nuke
		// add to total
		++total;
		// we want the LEAST number possible
		if(!firstremain || pl->nukemillis < totalmillis + firstremain){
			firstremain = pl->nukemillis - totalmillis;
			first = pl;
		}
	}
}
