// clientgame.cpp: core game related stuff

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

int nextmode = G_DM, nextmuts = G_M_TEAM; // nextmode becomes gamemode, nextmuts becomes mutators - after next map load
VAR(gamemode, 1, 0, 0);
VAR(mutators, 1, 0, 0);
VARP(modeacronyms, 0, 0, 1);

flaginfo flaginfos[2];

void mode(int n){ modecheck(nextmode = n, nextmuts); }
COMMAND(mode, ARG_1INT);
void muts(int n){ modecheck(nextmode, nextmuts = n); }
COMMAND(muts, ARG_1INT);

void classicmode(int n)
{
	static const int cmodes[][2] = {
		{ G_DM, G_M_TEAM },
		{ G_DM, G_M_NONE },

		{ G_DM, G_M_GSP1|G_M_TEAM|G_M_SNIPER },
		{ G_DM, G_M_GSP1|G_M_SNIPER },

		{ G_CTF, G_M_GSP1|G_M_TEAM },
		{ G_BOMBER, G_M_TEAM },
		{ G_HTF, G_M_TEAM },
		{ G_KTF, G_M_GSP1 },
		{ G_KTF, G_M_GSP1|G_M_TEAM },

		{ G_DM, G_M_PISTOL },
		{ G_DM, G_M_GSP1|G_M_GIB },

		{ G_DM, G_M_GSP1|G_M_TEAM },
		{ G_DM, G_M_GSP1 },
		{ G_ZOMBIE, G_M_TEAM },
		{ G_ZOMBIE, G_M_GSP1|G_M_TEAM },
	};
	if(n >= 0 && n < sizeof(cmodes)/sizeof(*cmodes))
		modecheck(nextmode = cmodes[n][0], nextmuts = cmodes[n][1]);
}
COMMAND(classicmode, ARG_1INT);

bool intermission = false;
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
	if(d->ownernum < 0) formatstring(cname)("%s \fs\f6(%d)", d->name, d->clientnum);
	else formatstring(cname)("%s \fs\f7[%d-%d]", d->name, d->clientnum, d->ownernum);
	if(stats){
		defformatstring(stat)("%d%.*f", d->health > 50 * HEALTHSCALE ? 0 : d->health > 25 * HEALTHSCALE ? 2 : d->health > 0 ? 3 : 4, HEALTHPRECISION, d->health / (float)HEALTHSCALE);
		if(d->armor) formatstring(stat)("%s\f5-\f4%d", stat, d->armor);
		concatformatstring(cname, " \f5[\f%s\f5]", stat);
	}
	concatstring(cname, "\fr");
	return cname;
}

const char *colorping(int ping)
{
	static string cping;
	if(multiplayer(false)) formatstring(cping)("\fs\f%d%d\fr", ping <= 190 ? 0 : ping <= 300 ? 2 : 3, ping);
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
		int nt = atoi(name);
		if(*name != '0' && !nt){
			if(strcmp(name, "BLUE") && strcmp(name, "RED") && strcmp(name, "SPECT")){
				conoutf("\f3\"%s\" %s (try 0 to 2, RED, BLUE or SPECT)", name, _("team_invalid"));
				return;
			}
			else nt = team_int(name);
		}
		if(!team_valid(nt) || nt == player1->team) return; // invalid/same team
		addmsg(N_SWITCHTEAM, "ri", nt);
	}
	else conoutf("%s: %s", _("team_you"), team_string(player1->team));
}

void nextskin(int skin)
{
	addmsg(N_SKIN, "ri", skin);
}
COMMANDN(skin, nextskin, ARG_1INT);

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
VARP(showscoresondeath, 0, 0, 1);

VARP(noob, 0, 0, 1); // pretty useless, just doubles level-up rate
VARFP(level, 1, 1, MAXLEVEL, addmsg(N_LEVEL, "ri", level));
VARFP(experience, 0, 0, MAXEXP, addexp(0));
int lastexpadd = INT_MIN, lastexpaddamt = 0;
void addexp(int xp){
	if(xp){
		if(lastmillis <= lastexpadd + COMBOTIME)
			lastexpaddamt += xp;
		else
			lastexpaddamt = xp;
		lastexpadd = lastmillis;
	}
	// no boost from negative points
	if(xp < 0)
		return;
	xp = xp * xp * (noob ? 2 : 1);
	#define xpfactor ((float)clamp(level, 1, 20))
	experience += fabs((float)xp / xpfactor);
	if(experience >= MAXEXP){
		level = clamp(level + 1, 1, MAXLEVEL);
		addmsg(N_LEVEL, "ri", level);
		experience = max(0.f, (experience - MAXEXP) / xpfactor);
	}
	#undef xpfactor
}

int lastexptexttime = INT_MIN;
string lastexptext;

void expreason(const char *reason){
	formatstring(lastexptext)(*reason == '\f' ? "%s" : "\f2%s", reason);
	lastexptexttime = lastmillis;
}
COMMAND(expreason, ARG_1STR);

bool spawnenqueued = false;

void deathstate(playerent *pl)
{
	if(pl == player1 && editmode) toggleedit(true);
	pl->state = CS_DEAD;
	pl->spectatemode = SM_DEATHCAM;
	pl->respawnoffset = pl->lastpain = lastmillis;
	pl->move = pl->strafe = pl->pitchvel = pl->pitchreturn = pl->ads = 0;
	// position camera (used to be roll/pitch)
	pl->roll = 0;

	pl->scoping = pl->attacking = pl->wantsreload = false;
	pl->wantsswitch = -1;
	pl->weaponsel->onownerdies();
	pl->damagelog.setsize(0);
	pl->radarmillis = 0;

	if(pl==player1){
		if(showscoresondeath) showscores(true);
		setscope(false);
		damageblend(-1);
		spawnenqueued = false;
	}
	else pl->resetinterp();
}

void spawnstate(playerent *d)			  // reset player state not persistent across spawns
{
	d->respawn();
	d->spawnstate(d->team, gamemode, mutators);
	if(d==player1) setscope(false);
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
	DELETEP(d);
}

void movelocalplayer()
{
	if(player1->state==CS_DEAD && player1->spectatemode != SM_FLY)
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
	const bool wave = m_progressive(gamemode, mutators), queued = !wave && spawnenqueued && !m_duke(gamemode, mutators);
	defformatstring(str)("\f%d%s %.1fs", wave ? 1 : queued ? 2 : 3, _(wave ? "spawn_wave" : queued ? "spawn_queued" : "spawn_wait"), (startmillis + maxmillis - lastmillis) / 1000.f);
	if(lastmillis <= startmillis + maxmillis) hudeditf(HUDMSG_TIMER|HUDMSG_OVERWRITE, flash || wave || queued ? str : str+2);
	else hudeditf(HUDMSG_TIMER, msg);
	return true;
}

int lastspawnattempt = 0;

void showrespawntimer()
{
	if(intermission) return;
	if(m_duke(gamemode, mutators) || (m_convert(gamemode, mutators) && arenaintermission))
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
	if(player1){ // only shoot when connected to server
		vec targ;
		loopi(players.length()+1) if(!players.inrange(i) || players[i]){
			playerent *p = !players.inrange(i) ? player1 : players[i];
			if(p == player1 && !(m_zombie(gamemode) && thirdperson < 0)) targ = worldhitpos;
			else{
				targ = vec(sinf(RAD*p->yaw) * cosf(RAD*p->pitch), -cosf(RAD*p->yaw)* cosf(RAD*p->pitch), sinf(RAD*p->pitch));
				targ.add(p->o);
			}
			shoot(p, targ);
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

void respawnself(){
	addmsg(N_TRYSPAWN, "r");
	spawnenqueued = !spawnenqueued;
}

bool tryrespawn(){
	if(player1->state==CS_DEAD){
		respawnself();

		int respawnmillis = player1->respawnoffset+(m_duke(gamemode, mutators) ? 0 : SPAWNDELAY);
		if(lastmillis>respawnmillis){
			player1->attacking = false;
			if(m_duke(gamemode, mutators) || m_convert(gamemode, mutators)){
				if(!arenaintermission && !m_convert(gamemode, mutators)) hudeditf(HUDMSG_TIMER, _("spawn_nextround"));
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
	if(!pl || !actor || pl->state == CS_DEAD || intermission) return;

	pl->respawnoffset = pl->lastpain = lastmillis;
	// damage direction/hit push
	if(pl != actor || weapon == WEAP_GRENADE || weapon == WEAP_RPG || pl->o.dist(src) > 4){
		// damage indicator
		pl->damagestack.add(damageinfo(src, lastmillis, damage));
		// push
		vec dir = pl->o;
		dir.sub(src).normalize();
		pl->hitpush(damage, dir, weapon, actor->perk1 == PERK_POWER);
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
	if(pl==player1 || isowned(pl))
	{
		pl->damageroll(damage);
		if(pl==player1) damageblend(damage);
	}

	// sound
	if(pl==focus) playsound(S_PAIN6, SP_HIGH);
	else playsound(S_PAIN1+rnd(5), pl);
}

VARP(showobits, 0, 4, 5); // 0: off, 1: self, duke, 2: +announce, 3: +human died (all except bot death), 4: +human killer (all except bot vs bot), 5: all
VARP(obitdetails, 0, 15, 15); // 0: minimal (killer, type, killed) | flags: 1: distance, 2: revenge, 4: assists, 8: scoping

void dokill(playerent *pl, playerent *act, int weap, int damage, int style, int combo, float killdist){
	if(!pl || !act || intermission) return;
	pl->lastkiller = act->clientnum;

	const bool headshot = isheadshot(weap, style);
	const int obit = (pl == act) ? obit_suicide(weap) : toobit(weap, style);

	// add gib
	if(style & FRAG_GIB) addgib(pl);

	int icon = -1, sound = S_NULL;
	// sounds/icons, by priority
	if(style & FRAG_FIRST)
	{
		sound = S_V_FIRST;
		icon = eventicon::FIRSTBLOOD;
	}
	else if(headshot && weap != WEAP_SHOTGUN) // shotgun doesn't count as a 'real' headshot
	{
		sound = S_V_HEADSHOT;
		icon = eventicon::HEADSHOT;
		pl->addicon(eventicon::DECAPITATED); // both get headshot info icon
	}
	else if(style & FRAG_CRIT) icon = eventicon::CRITICAL;

	// dis/play it!
	if(icon >= 0) act->addicon(icon);
	if(sound < S_NULL)
	{
		playsound(sound, act, act == focus ? SP_HIGHEST : SP_HIGH);
		if(pl->o.dist(act->o) >= 4)
			playsound(sound, pl, pl == focus ? SP_HIGHEST : SP_HIGH); // both get sounds if 1 meter apart...
	}

	// killfeed
	addobit(act, obit, style, headshot, pl);
	// death state
	deathstate(pl);
	// sound
	playsound(S_DIE1+rnd(2), pl);

	if(pl != act)
	{
		++pl->deathstreak;
		act->deathstreak = 0;
	}
	pl->pointstreak = 0;

	// kill message
	if(!showobits) return;
	if(showobits >= 5);
	else if(showobits >= 4 && act->ownernum >= 0);
	else if(showobits >= 3 && pl->ownernum >= 0);
	else if(showobits >= 2 && icon >= 0);
	else if(/*showobits >= 1 && */pl == focus || act == focus || m_duke(gamemode, mutators));
	else return;
	string subject, predicate, hashave;
	*subject = *predicate = *hashave = 0;
	formatstring(subject)("\f2\fs%s\f2", act == focus ? "\f1you" : colorname(act));
	copystring(hashave, act == focus ? "have" : "has");
	if(pl == act)
	{
		copystring(predicate, _(suicname(obit)));
		if(pl == focus)
		{
			// radar scan if the player suicided
			loopv(players)
			{
				playerent *p = players[i];
				if(!p || isteam(p, pl)) continue;
				p->radarmillis = lastmillis + 1000;
				p->lastloudpos[0] = p->o.x;
				p->lastloudpos[1] = p->o.y;
				p->lastloudpos[2] = p->yaw;
			}
			concatstring(predicate, "\f3!\f2");
		}
	}
	else
	{
		// revenge (2)
		if((obitdetails & 2) && style&FRAG_REVENGE) formatstring(predicate)("\fs\f0%s \fr", _("obit_revenge"));
		concatformatstring(predicate, "%s %s%s", _(killname(obit, headshot)),
			isteam(pl, act) ? act==player1 ? "\f3your teammate " : "\f3his teammate " : "", pl == player1 ? "\f1you\f2" : colorname(pl));
	}
	// assist count
	pl->damagelog.removeobj(pl->clientnum);
	pl->damagelog.removeobj(act->clientnum);
	loopv(pl->damagelog) if(!getclient(pl->damagelog[i])) pl->damagelog.remove(i--);
	// HUD for first person
	if(pl == focus || act == focus || pl->damagelog.find(focus->clientnum) >= 0){
		if((obitdetails & 4) && pl->damagelog.length()) hudonlyf("%s %s %s, %d assister%s", subject, hashave, predicate, pl->damagelog.length(), pl->damagelog.length()==1?"":"s");
		else hudonlyf("%s %s %s", subject, hashave, predicate);
	}
	// kill distance (1)
	if((obitdetails & 1) && killdist) concatformatstring(predicate, " (@%.2f m)", killdist / 4.f);
	// assists (4)
	if((obitdetails & 4) && pl->damagelog.length()){
		concatstring(predicate, _("obit_assist"));
		bool first = true;
		while(pl->damagelog.length()){
			playerent *p = getclient(pl->damagelog.pop());
			const bool tk = isteam(p, pl);
			p->pointstreak += tk ? -2 : 2;
			concatformatstring(predicate, "%s \fs\f%d%s\fr", first ? "" : !pl->damagelog.length() ? " and" : ",", tk ? 3 : 2, colorname(p));
			first = false;
		}
	}
	// combo
	switch(combo){
		case 2: concatformatstring(predicate, ", \fs\f0\fb%s\fr", _("obit_combo_2")); break;
		case 3: concatformatstring(predicate, ", \fs\f1\fb%s\fr", _("obit_combo_3")); break;
		case 4: concatformatstring(predicate, ", \fs\f3\fb%s\fr", _("obit_combo_4")); break;
		case 5: concatformatstring(predicate, ", \fs\f4\fb%s\fr", _("obit_combo_5")); break;
		default: if(combo > 1) concatformatstring(predicate, ", \fs\f5\fb%s\fr", _("obit_combo_6")); break;
	}
	// style
	if(style & FRAG_FIRST) concatformatstring(predicate, "%s\fs\f3\fb%s\fr%s", _("obit_first_pre"), _("obit_first_mid"), _("obit_first_post"));
	if(style & FRAG_CRIT) concatformatstring(predicate, "%s\fs\f1\fb%s\fr%s", _("obit_crit_pre"), _("obit_crit_mid"), _("obit_crit_post"));
	// scoping (8)
	if((obitdetails & 8) && pl != act && weap < WEAP_MAX && ads_gun(weap)){
		char scopestyle = 0;
		if(style & FRAG_SCOPE_NONE) scopestyle = (style & FRAG_SCOPE_FULL) ? 3 : 1;
		else scopestyle = (style & FRAG_SCOPE_FULL) ? 4 : 2;
		switch(scopestyle){
			//case 1: concatstring(predicate, " \fs\f2without\fr scoping"); break;
			case 2: concatformatstring(predicate, "%s\fs\f0\fb%s\fr%s", _("obit_scope_quick_pre"), _("obit_scope_quick_mid"), _("obit_scope_quick_post")); break;
			case 3: concatformatstring(predicate, "%s\fs\f1\fb%s\fr%s", _("obit_scope_recent_pre"), _("obit_scope_recent_mid"), _("obit_scope_recent_post")); break;
			case 4: concatformatstring(predicate, "%s\fs\f3\fb%s\fr%s", _("obit_scope_hard_pre"), _("obit_scope_hard_mid"), _("obit_scope_hard_post")); break;
		}
	}
	conoutf("%s %s", subject, predicate);
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
			if(e.type==CTF_FLAG && e.attr2 < 2)
			{
				e.spawned = true;
				if(e.attr2 < 0) { conoutf("\f3invalid ctf-flag entity (%i as %d)", i, e.attr2); continue; }
				flaginfo &f = flaginfos[e.attr2];
				f.flagent = &e;
				f.pos.x = (float) e.x;
				f.pos.y = (float) e.y;
				f.pos.z = (float) e.z;
			}
		}
	}
}

struct mdesc { void *data; char *desc; };
vector<mdesc> gmdescs, mutdescs, gspdescs;

void gamemodedesc(char *modenr, char *desc)
{
	if(!modenr || !desc) return;
	struct mdesc &gd = gmdescs.add();
	gd.data = new int(atoi(modenr));
	gd.desc = newstring(desc);
}

COMMAND(gamemodedesc, ARG_2STR);

void modifierdesc(char *mutnr, char *desc)
{
	if(!mutnr || !desc) return;
	struct mdesc &md = mutdescs.add();
	md.data = new int(atoi(mutnr));
	md.desc = newstring(desc);
}

COMMANDN(mutatorsdesc, modifierdesc, ARG_2STR);

void gspmutdesc(char *modenr, char *gspnr, char *desc)
{
	if(!modenr || !gspnr || !desc) return;
	struct mdesc &gd = gspdescs.add();
	gd.data = new int[2];
	((int *)gd.data)[0] = atoi(modenr);
	((int *)gd.data)[1] = atoi(gspnr);
	gd.desc = newstring(desc);
}
COMMAND(gspmutdesc, ARG_3STR);

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
	if(m_ai(gamemode)) BotManager.BeginMap(name); // Added by Rick
	clearbounceents();
	resetspawns();
	preparectf(!m_affinity(gamemode) || m_secure(gamemode));

	if(!reset) return;

	// resets...
	loopi(TEAM_NUM) teamscores[i] = teamscore(i);
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
	loopv(gmdescs) if(*(int *)gmdescs[i].data == gamemode) conoutf("\f1%s", gmdescs[i].desc);
	loopv(mutdescs)
	{
		if(i >= G_M_GSP) break;
		if((1 << *(int *)mutdescs[i].data) & mutators) conoutf("\f2%s", mutdescs[i].desc);
	}
	loopv(gspdescs) if(*(int *)gspdescs[i].data == gamemode && (1 << (((int *)gspdescs[i].data)[1] + G_M_GSP)) & mutators) conoutf("\f3%s", gspdescs[i].desc);

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
	string subject, predicate, hashave, verb_past, verb_perfect;

	copystring(subject, firstperson ? "you" : colorname(act));
	copystring(hashave, firstperson ? "have" : "has");
	copystring(verb_past, "altered");
	*verb_perfect = '\0';
	copystring(predicate, "a flag");

	switch(message){
		case FA_PICKUP:
			playsound(S_FLAGPICKUP, SP_HIGHEST);
			copystring(verb_past, "got");
			copystring(verb_perfect, "gotten");
			if(firstperson){
				// don't know it it's pickup or steal...
				formatstring(predicate)("%s flag", teamstr);
				if(!own || !m_capture(gamemode)){
					musicsuggest(M_FLAGGRAB, m_capture(gamemode) ? 90*1000 : 900*1000, true);
					flagmusic |= 1 << flag;
				}
			}
			else formatstring(predicate)("%s flag", teamstr);
			break;
		case FA_LOST:
		case FA_DROP:
		{
			playsound(S_FLAGDROP, SP_HIGHEST);
			copystring(verb_past, message == FA_LOST ? "lost" : "dropped");
			formatstring(predicate)("%s flag" , teamstr);
			firstpersondrop = true;
			break;
		}
		case FA_RETURN:
			playsound(S_FLAGRETURN, SP_HIGHEST);
			copystring(verb_past, "returned");
			formatstring(predicate)("%s flag", teamstr);
			firstpersondrop = true;
			break;
		case FA_SCORE:
			playsound(S_FLAGSCORE, SP_HIGHEST);
			copystring(verb_past, "scored");
			formatstring(predicate)("for \fs\f%d%s\fr team!", team_rel_color(player1, act), firstperson || isteam(act, player1) ? "your" : "the enemy");
			firstpersondrop = true;
			break;
		case FA_KTFSCORE:
		{
			playsound(S_VOTEPASS, SP_HIGHEST); // need better ktf sound here
			const int m = flagtime / 60, s = flagtime % 60;
			copystring(verb_past, "kept");
			formatstring(predicate)("%s flag for ", teamstr_absolute);
			if(m) concatformatstring(predicate, "%d minute%s", m, m==1 ? " " : "s ");
			if(s) concatformatstring(predicate, "%d second%s", s, s==1 ? " " : "s ");
			concatstring(predicate, "now");
			break;
		}
		case FA_SCOREFAIL: // sound?
			copystring(verb_past, "failed");
			copystring(predicate, "to score");
			break;
		case FA_RESET:
			playsound(S_FLAGRETURN, SP_HIGHEST);
			copystring(subject, "\f1the server");
			copystring(hashave, "had just");
			copystring(verb_past, "reset");
			formatstring(predicate)("%s flag", teamstr_absolute);
			firstpersondrop = true;
			break;
	}
	conoutf("\f2%s %s %s", subject, verb_past, predicate);
	hudonlyf("\f2%s %s %s %s", subject, hashave, *verb_perfect ? verb_perfect : verb_past, predicate);
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
			if(*vote.str1 == '+') formatstring(out)("set next map to %s in mode %s", vote.str1 + 1, modestr(vote.int1, vote.int2, modeacronyms > 0));
			else formatstring(out)("load map %s in mode %s", vote.str1, modestr(vote.int1, vote.int2, modeacronyms > 0));
			break;

		// playeractions
		case SA_KICK: // int1, str1
		{
			playerent *p = getclient(vote.int1);
			if(p) formatstring(out)("kick player %s for %s", colorname(p), vote.str1);
			else formatstring(out)("kick someone (%d) for %s", vote.int1, vote.str1);
			break;
		}

		case SA_BAN: // int1, int2, str1
		{
			int cn = vote.int1, minutes = vote.int2;
			playerent *p = getclient(cn);
			if(p) formatstring(out)("ban %s for %d minutes for %s", colorname(p), minutes, vote.str1);
			else formatstring(out)("ban someone (%d) for %d minutes for %s", cn, minutes, vote.str1);
			break;
		}

		case SA_GIVEROLE: // int1, int2
		{
			playerent *p = getclient(vote.int1);
			const char priv = vote.int2;
			if(p) formatstring(out)("\f0give \f%d%s \f5to player %s", privcolor(priv), privname(priv), colorname(p));
			else formatstring(out)("give someone (%d) \f%d%s", vote.int1, privcolor(priv), privname(priv));
			break;
		}

		case SA_REVOKE: // int1
		{
			playerent *p = getclient(vote.int1);
			if(p) formatstring(out)("revoke \fs\f%d%s\fr from %s", privcolor(p->priv), privname(p->priv), colorname(p));
			else formatstring(out)("revoke someone (%d)'s privilege", vote.int1);
			break;
		}

		case SA_FORCETEAM: // int1
		case SA_SUBDUE:
		case SA_SPECT:
		{
			playerent *p = getclient(vote.int1);
			formatstring(out)(
				type == SA_FORCETEAM ? "force player %s to the enemy team" :
				type == SA_SUBDUE ? "subdue player %s" :
				type == SA_SPECT ? "toggle spectator for %s" :
				"unknown on %s", p ? colorname(p) : "?");
			break;
		}

		// int action
		case SA_BOTBALANCE: // int1
			formatstring(out)(
				vote.int1 == 1 ? "bots balance teams only" :
				!vote.int1 ? "disable all bots" :
				vote.int1 < 0 ? "automatically balance bots" :
				"balance to %d players", vote.int1);
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
		case SA_NEXTMAP: copystring(out, "load the next map"); break;
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
				sendstring(vote.str1, p);
				putint(p, vote.int1);
				putint(p, vote.int2);
				break;
			case SA_STOPDEMO:
			case SA_REMBANS:
			case SA_SHUFFLETEAMS:
			case SA_NEXTMAP:
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
					copystring(str1, arg3);
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
				case SA_NEXTMAP:
					break;
			}
			callvote(t, vote);
		}
		
	}
}

bool vote(int v)
{
	if(!curvote || curvote->result != VOTE_NEUTRAL || v < 0 || v >= VOTE_NUM) return false;
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
		formatstring(m.cmd)("%s %d", menu==kickmenu ? "kick" : (menu==banmenu ? "ban 10" : (menu==forceteammenu ? "forceteam" : (menu==revokemenu ? "revoke" : (menu==giveadminmenu ? "giverole" : (menu==whoismenu ? "whois" : (menu==spectmenu ? "forcespect" : "unknownplayeraction")))))), p[i]->clientnum);
		if((menu==kickmenu||menu==banmenu) && getalias("_kickbanreason")!=NULL) concatformatstring(m.cmd, " \"%s\"", getalias("_kickbanreason"));
		menumanual(menu, m.name, m.cmd);
	}
}

extern bool watchingdemo;
VARFP(thirdperson, -MAXTHIRDPERSON, 0, MAXTHIRDPERSON, addmsg(N_THIRDPERSON, "ri", thirdperson)); // FIXME use a different protocol message for thirdperson?

VARP(spectatebots, 0, 0, 1);

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
	loopv(players) if(players[i] && players[i]->team != TEAM_SPECT && (players[i]->ownernum < 0 || spectatebots) && (players[i]->state != CS_DEAD || !m_duke(gamemode, mutators)))
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
		case SM_FOLLOWSAME:
		case SM_FOLLOWALT:
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
	if(player1->spectatemode==SM_NONE) mode = SM_FOLLOWSAME; // start with 1st person spect
	else mode = SM_FOLLOWSAME + ((player1->spectatemode - SM_FOLLOWSAME + 1) % (SM_NUM-SM_FOLLOWSAME));
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
		if(pl->radarearned <= lastmillis) continue; // no radar!
		if(asSeenBy && asSeenBy != pl && asSeenBy->team != TEAM_SPECT && !isteam(asSeenBy, pl)) continue; // not the same team
		// add to total
		++total;
		// we want the HIGHEST number possible
		if(pl->radarearned > lastmillis + lastremain){
			lastremain = pl->radarearned - lastmillis;
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
		if(pl->nukemillis <= lastmillis) continue; // no upcoming nuke
		// add to total
		++total;
		// we want the LEAST number possible
		if(!firstremain || pl->nukemillis < lastmillis + firstremain){
			firstremain = pl->nukemillis - lastmillis;
			first = pl;
		}
	}
}
