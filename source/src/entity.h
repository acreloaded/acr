enum							// static entity types
{
	NOTUSED = 0,				// entity slot not in use in map
	LIGHT,					  // lightsource, attr1 = radius, attr2 = intensity
	PLAYERSTART,				// attr1 = angle, attr2 = team
	I_CLIPS, I_AMMO, I_GRENADE,
	I_HEALTH, I_HELMET, I_ARMOR, I_AKIMBO,
	MAPMODEL,				   // attr1 = angle, attr2 = idx
	CARROT,					 // attr1 = tag, attr2 = type
	LADDER,
	CTF_FLAG,				   // attr1 = angle, attr2 = red/blue
	SOUND,
	CLIP,
	PLCLIP,
	MAXENTTYPES
};

#define isitem(i) ((i) >= I_CLIPS && (i) <= I_AKIMBO)

struct persistent_entity		// map entity
{
	short x, y, z;			  // cube aligned position
	short attr1;
	uchar type;				 // type is one of the above
	uchar attr2, attr3, attr4;
	persistent_entity(short x, short y, short z, uchar type, short attr1, uchar attr2, uchar attr3, uchar attr4) : x(x), y(y), z(z), attr1(attr1), type(type), attr2(attr2), attr3(attr3), attr4(attr4) {}
	persistent_entity() {}
};

struct entity : public persistent_entity
{
	// dynamic states
	bool spawned;
	int spawntime;

	entity(short x, short y, short z, uchar type, short attr1, uchar attr2, uchar attr3, uchar attr4) : persistent_entity(x, y, z, type, attr1, attr2, attr3, attr4), spawned(false), spawntime(0) {}
	entity() {}
	bool fitsmode(int gamemode, int mutators) { return !m_noitems(gamemode, mutators) && isitem(type) && !(m_noitemsammo(gamemode, mutators) && type!=I_AMMO) && !(m_noitemsnade(gamemode, mutators) && type!=I_GRENADE) && !(m_pistol(gamemode, mutators) && type==I_AMMO); }
	void transformtype(int gamemode, int mutators)
	{
		if(m_pistol(gamemode, mutators) && type == I_AMMO) type = I_CLIPS;
		else if(m_noitemsnade(gamemode, mutators)) switch(type){
			case I_CLIPS:
			case I_AMMO:
			case I_ARMOR:
			case I_AKIMBO:
				type = I_GRENADE;
				break;
		}
	}
};

#define HEALTHPRECISION 1
#define HEALTHSCALE 10 // 10 ^ 1
#define STARTHEALTH (100 * HEALTHSCALE)
#define MAXHEALTH (120 * HEALTHSCALE)
#define ZOMBIEHEALTHFACTOR 5

#define SPAWNDELAY (m_affinity(gamemode) ? 5000 : 1500)

#define REGENDELAY 4250
#define REGENINT 2500

#define STARTARMOR 0
#define MAXARMOR 100

struct itemstat { short add, start, max, sound; };
extern itemstat ammostats[WEAP_MAX];
extern itemstat powerupstats[];

#define ADSTIME 275
#define CROUCHTIME 500

#define SGRAYS 24
#define SGSPREAD 295
#define SGADSSPREADFACTOR 20
#define SGGIB 180 * HEALTHSCALE // 18-26 rays (only have 24)
#define NADEPOWER 2
#define NADETTL 4350
#define MARTYRDOMTTL 2500
#define KNIFEPOWER 4.5f
#define KNIFETTL 30000
#define GIBBLOODMUL 1.5f
#define SPAWNPROTECT 2000
#define COMBOTIME 1000

#define MAXLEVEL 100
#define MAXEXP 1000

struct mul{
	union{
		struct{ float head, torso, leg; };
		float val[3];
	};
};
enum { MUL_NORMAL = 0, MUL_SNIPER, MUL_SHOTGUN, MUL_NUM };

static mul muls[MUL_NUM] =
{
	//{ head, torso, leg; }
	{ 3.5f, 1.1f,	1 }, // normal
	{ 5,	1.4f, 	1 }, // snipers
	{ 4,	1.2f,	1 } // shotgun
};

#define BLEEDDMG 10
#define BLEEDDMGZ 5
#define BLEEDDMGPLUS 15

struct guninfo { string modelname; short sound, reload, reloadtime, attackdelay, damage, range, endrange, rangeminus, projspeed, part, spread, kick, magsize, mdl_kick_rot, mdl_kick_back, recoil, maxrecoil, recoilangle, pushfactor; bool isauto; };
extern guninfo guns[WEAP_MAX];

static inline ushort reloadtime(int gun) { return guns[gun].reloadtime; }
static inline ushort attackdelay(int gun) { return guns[gun].attackdelay; }
static inline ushort magsize(int gun) { return guns[gun].magsize; }
static inline ushort reloadsize(int gun) { return gun == WEAP_SHOTGUN ? 1 : guns[gun].magsize; }
static inline ushort effectiveDamage(int gun, float dist, bool explosive) {
	float finaldamage = 0;
	if(dist <= guns[gun].range || (!guns[gun].range && !guns[gun].endrange)) finaldamage = guns[gun].damage;
	else if(dist >= guns[gun].endrange) finaldamage = guns[gun].damage - guns[gun].rangeminus;
	else{
		float subtractfactor = (dist - (float)guns[gun].range) / ((float)guns[gun].endrange - (float)guns[gun].range);
		if(explosive) subtractfactor = sqrtf(subtractfactor);
		finaldamage = guns[gun].damage - subtractfactor * guns[gun].rangeminus;
	}
	return finaldamage * HEALTHSCALE;
}

extern ushort reloadtime(int gun);
extern ushort attackdelay(int gun);
extern ushort magsize(int gun);
extern ushort reloadsize(int gun);
extern ushort effectiveDamage(int gun, float dist, bool explosive = false);

extern const int obit_suicide(int weap);
extern const char *suicname(int obit);
extern const bool isheadshot(int weapon, int style);
extern const int toobit(int weap, int style);
extern const char *killname(int obit, bool headshot);

enum { PERK_NONE = 0, PERK_JAM, PERK_POWER, PERK_TIME, PERK_MAX };
enum { PERK1_NONE = 0, PERK1_AGILE = PERK_MAX, PERK1_HAND, PERK1_LIGHT, PERK1_SCORE, PERK1_MAX, };
enum { PERK2_NONE = 0, PERK2_VISION = PERK_MAX, PERK2_STREAK, PERK2_STEADY, PERK2_HEALTHY, PERK2_MAX };

extern float gunspeed(int gun, int ads, bool lightweight = false);

#define isteam(a,b)   (m_team(gamemode, mutators) && (a)->team == (b)->team)

enum { TEAM_RED = 0, TEAM_BLUE, TEAM_SPECT, TEAM_NUM };
#define team_valid(t) ((t) >= 0 && (t) < TEAM_NUM)
#define team_string(t) ((t) == TEAM_BLUE ? "BLUE" : (t) == TEAM_RED ? "RED" : "SPECT")
#define team_int(t) (!strcmp((t), "RED") ? TEAM_RED : !strcmp((t), "BLUE") ? TEAM_BLUE : TEAM_SPECT)
#define team_opposite(o) ((o) < TEAM_SPECT ? (o) ^ 1 : TEAM_SPECT)
#define team_color(t) ((t) == TEAM_SPECT ? 4 : (t) ? 1 : 3)
#define team_rel_color(a, b) (a == b ? 1 : a && b && b->team != TEAM_SPECT ? isteam(a, b) ? 0 : 3 : 4)

struct teamscore
{
	int team, points, flagscore, frags, assists, deaths;
	teamscore(int team) : team(team), points(0), flagscore(0), frags(0), assists(0), deaths(0) { }
};

enum { ENT_PLAYER = 0, ENT_CAMERA, ENT_BOUNCE };
enum { CS_ALIVE = 0, CS_DEAD, CS_WAITING, CS_EDITING };
enum { PRIV_NONE = 0, PRIV_MASTER, PRIV_ADMIN, PRIV_MAX };

static inline const uchar privcolor(int priv, bool dead = false){
	switch(priv){
		case PRIV_NONE: return dead ? 4 : 5;
		case PRIV_MASTER: return dead ? 8 : 0;
		case PRIV_ADMIN: return dead ? 7 : 3;
		case PRIV_MAX: return dead ? 9 : 1;
	}
	return 5;
}

static inline const char *privname(int priv){ 
	switch(priv){
		case PRIV_NONE: return "user";
		case PRIV_MASTER: return "master";
		case PRIV_ADMIN: return "admin";
		case PRIV_MAX: return "highest";
	}
	return "unknown";
}
enum { SM_NONE = 0, SM_DEATHCAM, SM_FOLLOW1ST, SM_FOLLOW3RD, SM_FOLLOW3RD_TRANSPARENT, SM_FLY, SM_NUM };

struct physent
{
	vec o, vel;						 // origin, velocity
	vec deltapos, newpos;					   // movement interpolation
	float yaw, pitch, roll;			 // used as vec in one place
	float pitchvel, yawvel, pitchreturn, yawreturn;
	float maxspeed;					 // cubes per second, 24 for player
	int timeinair;					  // used for fake gravity
	float radius, eyeheight, maxeyeheight, aboveeye;  // bounding box size
	bool inwater;
	bool onfloor, onladder, jumpnext, crouching, trycrouch, cancollide, stuck;
	int lastsplash;
	char move, strafe;
	uchar state, type;
	float eyeheightvel;

	physent() : o(0, 0, 0), deltapos(0, 0, 0), newpos(0, 0, 0), yaw(270), pitch(0), pitchreturn(0), roll(0), pitchvel(0),
				crouching(false), trycrouch(false), cancollide(true), stuck(false), lastsplash(0), state(CS_ALIVE)
	{
		reset();
	}
	virtual ~physent() {}

	void resetinterp()
	{
		newpos = o;
		newpos.z -= eyeheight;
		deltapos = vec(0, 0, 0);
	}

	void reset()
	{
		vel.x = vel.y = vel.z = eyeheightvel = 0.0f;
		move = strafe = 0;
		timeinair = lastsplash = 0;
		onfloor = onladder = inwater = jumpnext = crouching = trycrouch = stuck = false;
	}

	virtual void oncollision() {}
	virtual void onmoved(const vec &dist) {}
};

struct dynent : physent				 // animated ent
{
	bool k_left, k_right, k_up, k_down;		 // see input code

	animstate prev[2], current[2];			  // md2's need only [0], md3's need both for the lower&upper model
	int lastanimswitchtime[2];
	void *lastmodel[2];
	int lastrendered;

	void stopmoving()
	{
		k_left = k_right = k_up = k_down = jumpnext = false;
		move = strafe = 0;
	}

	void resetanim()
	{
		loopi(2)
		{
			prev[i].reset();
			current[i].reset();
			lastanimswitchtime[i] = -1;
			lastmodel[i] = NULL;
		}
		lastrendered = 0;
	}

	void reset()
	{
		physent::reset();
		stopmoving();
	}

	dynent() { reset(); resetanim(); }
	virtual ~dynent() {}
};

#define MAXNAMELEN 16
#define MAXTEAMLEN 4

struct bounceent;

#define POSHIST_SIZE 7

struct poshist
{
	int nextupdate, curpos, numpos;
	vec pos[POSHIST_SIZE];

	poshist() : nextupdate(0) { reset(); }

	const int size() const { return numpos; }

	void reset()
	{
		curpos = 0;
		numpos = 0;
	}

	void addpos(const vec &o)
	{
		pos[curpos] = o;
		curpos++;
		if(curpos>=POSHIST_SIZE) curpos = 0;
		if(numpos<POSHIST_SIZE) numpos++;
	}

	const vec &getpos(int i) const
	{
		i = curpos-1-i;
		if(i < 0) i += POSHIST_SIZE;
		return pos[i];
	}

	void update(const vec &o, int lastmillis)
	{
		if(lastmillis<nextupdate) return;
		if(o.dist(pos[0]) >= 4.0f) addpos(o);
		nextupdate = lastmillis + 100;
	}
};

struct playerstate
{
	int ownernum; // for bots
	int health, armor, spawnmillis, lastkiller;
	int pointstreak, deathstreak, assists, radarearned, airstrikes, nukemillis;
	int primary, secondary, perk1, perk2, nextprimary, nextsecondary, nextperk1, nextperk2;
	int gunselect, level;
	bool akimbo, scoping;
	int ammo[WEAP_MAX], mag[WEAP_MAX], gunwait[WEAP_MAX];
	ivector damagelog;

	playerstate() : primary(WEAP_ASSAULT), secondary(WEAP_PISTOL), perk1(PERK1_NONE), perk2(PERK2_NONE),
		nextprimary(WEAP_ASSAULT), nextsecondary(WEAP_PISTOL), nextperk1(PERK1_NONE), nextperk2(PERK2_NONE),
		ownernum(-1), level(1), pointstreak(0), deathstreak(0), airstrikes(0), radarearned(0), nukemillis(0), spawnmillis(0), lastkiller(-1) {}
	virtual ~playerstate() {}

	itemstat &itemstats(int type)
	{
		switch(type)
		{
			case I_CLIPS: return ammostats[WEAP_PISTOL];
			case I_AMMO: return ammostats[primary];
			case I_GRENADE: return ammostats[WEAP_GRENADE];
			case I_AKIMBO: return ammostats[WEAP_AKIMBO];
			case I_HEALTH: case I_HELMET: case I_ARMOR:
				return powerupstats[type - I_HEALTH];
			default:
				return *(itemstat *)0;
		}
	}

	bool canpickup(int type)
	{
		switch(type)
		{
			case I_CLIPS: return ammo[akimbo ? WEAP_AKIMBO : WEAP_PISTOL]<ammostats[akimbo ? WEAP_AKIMBO : WEAP_PISTOL].max;
			case I_AMMO: return primary == WEAP_SWORD || ammo[primary]<ammostats[primary].max;
			case I_GRENADE: return mag[WEAP_GRENADE]<ammostats[WEAP_GRENADE].max;
			case I_HEALTH: return health<powerupstats[type-I_HEALTH].max;
			case I_HELMET:
			case I_ARMOR:
				return armor<powerupstats[type-I_HEALTH].max;
			case I_AKIMBO: return !akimbo && ownernum < 0;
			default: return false;
		}
	}

	void additem(itemstat &is, int &v)
	{
		v += is.add;
		if(v > is.max) v = is.max;
	}

	void pickup(int type)
	{
		switch(type)
		{
			case I_CLIPS:
				additem(ammostats[WEAP_PISTOL], ammo[WEAP_PISTOL]);
				additem(ammostats[WEAP_AKIMBO], ammo[WEAP_AKIMBO]);
				break;
			case I_AMMO: additem(ammostats[primary], ammo[primary]); break;
			case I_GRENADE: additem(ammostats[WEAP_GRENADE], mag[WEAP_GRENADE]); break;
			case I_HEALTH: additem(powerupstats[type-I_HEALTH], health); break;
			case I_HELMET:
			case I_ARMOR: additem(powerupstats[type-I_HEALTH], armor); break;
			case I_AKIMBO:
				akimbo = true;
				mag[WEAP_AKIMBO] = guns[WEAP_AKIMBO].magsize;
				additem(ammostats[WEAP_AKIMBO], ammo[WEAP_AKIMBO]);
				break;
		}
	}

	void respawn()
	{
		health = STARTHEALTH;
		armor = STARTARMOR;
		spawnmillis = 0;
		assists = armor = 0;
		gunselect = WEAP_PISTOL;
		akimbo = scoping = false;
		loopi(WEAP_MAX) ammo[i] = mag[i] = gunwait[i] = 0;
		mag[WEAP_KNIFE] = 1;
		lastkiller = -1;
	}

	virtual void spawnstate(int gamemode, int mutators)
	{
		if(m_pistol(gamemode, mutators)) primary = WEAP_PISTOL;
		else if(m_sniper(gamemode, mutators)) primary = WEAP_BOLT;
		else if(m_gib(gamemode, mutators) || m_knife(gamemode, mutators)) primary = WEAP_KNIFE;
		else primary = nextprimary;

		if(primary == WEAP_GRENADE || primary == WEAP_AKIMBO || primary < 0 || primary >= WEAP_MAX) primary = WEAP_ASSAULT;

		if(!m_nopistol(gamemode, mutators)){
			ammo[WEAP_PISTOL] = ammostats[WEAP_PISTOL].start-magsize(WEAP_PISTOL);
			mag[WEAP_PISTOL] = magsize(WEAP_PISTOL);
		}

		if(!m_noprimary(gamemode, mutators)){
			ammo[primary] = ammostats[primary].start-magsize(primary);
			mag[primary] = magsize(primary);
		}

		// extras
		ammo[WEAP_KNIFE] = ammostats[WEAP_KNIFE].start;
		if(!m_noitems(gamemode, mutators) && !m_noitemsammo(gamemode, mutators))
			mag[WEAP_GRENADE] = ammostats[WEAP_GRENADE].start;

		gunselect = primary;

		perk1 = nextperk1;
		perk2 = nextperk2;

		// no classic override

		if(perk1 <= PERK1_NONE || perk1 >= PERK1_MAX) perk1 = rnd(PERK1_MAX-1)+1;
		if(perk2 <= PERK1_NONE || perk1 >= PERK2_MAX) perk2 = rnd(PERK2_MAX-1)+1;

		// special perks need both slots
		if(perk1 < PERK_MAX) perk2 = perk1;

		const int healthsets[3] = { STARTHEALTH - 15 * HEALTHSCALE, STARTHEALTH, STARTHEALTH + 20 * HEALTHSCALE };
		health = healthsets[(!m_regen(gamemode, mutators) && m_sniper(gamemode, mutators) ? 0 : 1) + (perk2 == PERK2_HEALTHY ? 1 : 0)];
	}

	// just subtract damage here, can set death, etc. later in code calling this
	int dodamage(int damage, bool penetration){
		int ad = penetration ? 0 : damage*3/10; // let armor absorb when possible
		if(ad>armor) ad = armor;
		damage -= ad;
		// apply it!
		armor -= ad;
		health -= damage;
		return damage;
	}

	int protect(int millis){
		const int delay = SPAWNPROTECT, spawndelay = millis - spawnmillis;
		int amt = 0;
        if(spawnmillis && delay && spawndelay < delay) amt = delay - spawndelay;
        return amt;
	}
};

#define HEADSIZE 0.4f
#define TORSOPART 0.35f
#define LEGPART (1 - TORSOPART)

#define PLAYERRADIUS 1.1f
#define PLAYERHEIGHT 4.5f
#define PLAYERABOVEEYE .7f
#define WEAPONBELOWEYE .2f

struct eventicon{
    enum { VOICECOM = 0, HEADSHOT, DECAPITATED, FIRSTBLOOD, CRITICAL, REVENGE, BLEED, PICKUP, RADAR, AIRSTRIKE, NUKE, DROPNADE, SUICIDEBOMB, TOTAL };
    int type, millis;
	eventicon(int type, int millis) : type(type), millis(millis){}
};

struct damageinfo{
	vec o;
	int millis, damage;
	damageinfo(vec s, int t, int d) : o(s), millis(t), damage(d) {} // lol read the constructor's parameters
};

class CBot;

struct playerent : dynent, playerstate
{
	int clientnum, lastrecieve, plag, ping;
	int lifesequence;				   // sequence id for each respawn, used in damage test
	int radarmillis; float lastloudpos[3];
	int points, frags, flagscore, deaths;
	int lastaction, lastmove, lastpain, lasthitmarker;
	int priv, vote, voternum, lastregen;
	int ads, wantsswitch; bool wantsreload, delayedscope;
	bool attacking;
	string name;
	int weaponchanging;
	int nextweapon; // weapon we switch to
	int team, skin;
	int spectatemode, followplayercn;
	int eardamagemillis, flashmillis;
	int respawnoffset;
	bool allowmove() { return state!=CS_DEAD || spectatemode==SM_FLY; }
	vector<eventicon> icons;

	weapon *weapons[WEAP_MAX];
	weapon *prevweaponsel, *weaponsel, *nextweaponsel, *primweap, *lastattackweapon;

	poshist history; // Previous stored locations of this player

	const char *skin_noteam, *skin_red, *skin_blue;

	float deltayaw, deltapitch, newyaw, newpitch;
	int smoothmillis;

	vector<damageinfo> damagestack;
	vec head;

	// AI
	CBot *pBot;

	playerent *enemy;  // monster wants to kill this entity
	float targetpitch, targetyaw; // monster wants to look in this direction

	playerent() : spectatemode(SM_NONE), vote(VOTE_NEUTRAL), voternum(MAXCLIENTS), priv(PRIV_NONE), head(-1, -1, -1)
	{
		// ai
		enemy = NULL;
		pBot = NULL;
		targetpitch = targetyaw = 0;

		lastrecieve = plag = ping = lifesequence = points = frags = flagscore = deaths = lastpain = lastregen = lasthitmarker = skin = eardamagemillis = respawnoffset = radarmillis = ads = 0;
		radarearned = airstrikes = nukemillis = 0;
		weaponsel = nextweaponsel = primweap = lastattackweapon = prevweaponsel = NULL;
		type = ENT_PLAYER;
		clientnum = smoothmillis = followplayercn = wantsswitch = -1;
		name[0] = 0;
		team = TEAM_BLUE;
		maxeyeheight = PLAYERHEIGHT;
		aboveeye = PLAYERABOVEEYE;
		radius = PLAYERRADIUS;
		maxspeed = 16.0f;
		skin_noteam = skin_red = skin_blue = NULL;
		respawn();
		damagestack.setsize(0);
		wantsreload = delayedscope = false;
	}

	void addicon(int type)
	{
		switch(type){
			case eventicon::CRITICAL:
			case eventicon::PICKUP:
				loopv(icons) if(icons[i].type == type) icons.remove(i--);
				break;
		}
		extern int lastmillis;
		eventicon icon(type, lastmillis);
		icons.add(icon);
	}

	virtual ~playerent()
	{
		extern void removebounceents(playerent *owner);
		extern void detachsounds(playerent *owner);
		extern void removedynlights(physent *owner);
		extern void zapplayerflags(playerent *owner);
		extern void cleanplayervotes(playerent *owner);
		extern physent *camera1;
		extern void togglespect();
		removebounceents(this);
		detachsounds(this);
		removedynlights(this);
		zapplayerflags(this);
		cleanplayervotes(this);
		if(this==camera1) togglespect();
		icons.setsize(0);
	}

	void damageroll(float damage)
	{
		extern void clamproll(physent *pl);
		float damroll = 2.0f*damage;
		roll += roll>0 ? damroll : (roll<0 ? -damroll : (rnd(2) ? damroll : -damroll)); // give player a kick
		clamproll(this);
	}

	void hitpush(int damage, const vec &dir, int gun, bool slows)
	{
		if(gun<0 || gun>WEAP_MAX || dir.iszero()) return;
		const float pushf = damage*guns[gun].pushfactor/100.f/HEALTHSCALE;
		vec push = dir;
		push.normalize().mul(pushf);
		vel.div(clamp<float>(pushf*5, 1, 5)).add(push);
		extern int lastmillis;
		if(gun==WEAP_GRENADE && damage > 50 * HEALTHSCALE) eardamagemillis = lastmillis+damage*100/HEALTHSCALE;
	}

	void resetspec()
	{
		spectatemode = SM_NONE;
		followplayercn = -1;
	}

	void respawn()
	{
		dynent::reset();
		playerstate::respawn();
		history.reset();
		if(weaponsel) weaponsel->reset();
		lastregen = lasthitmarker = lastaction = weaponchanging = eardamagemillis = radarmillis = flashmillis = 0;
		lastattackweapon = NULL;
		ads = 0.f;
		wantsswitch = -1;
		scoping = attacking = false;
		lastaction = 0;
		nukemillis = 0;
		resetspec();
		eyeheight = maxeyeheight;
		damagestack.setsize(0);
	}

	void spawnstate(int gamemode, int mutators)
	{
		playerstate::spawnstate(gamemode, mutators);
		prevweaponsel = weaponsel = weapons[gunselect];
		primweap = weapons[primary];
	}

	void selectweapon(int w) { prevweaponsel = weaponsel = weapons[(gunselect = w)]; }
	void setprimary(int w) { primweap = weapons[(primary = w)]; }
	bool isspectating() { return team==TEAM_SPECT || (state==CS_DEAD && spectatemode > SM_NONE); }
	void weaponswitch(weapon *w)
	{
		if(!w) return;
		extern playerent *player1;
		if(ads){
			if(this == player1){
				extern void setscope(bool activate);
				setscope(false);
				wantsswitch = w->type;
				delayedscope = true;
			}
			return;
		}
		wantsswitch = -1;
		extern int lastmillis;
		// weaponsel->ondeselecting();
		weaponchanging = lastmillis;
		prevweaponsel = weaponsel;
		nextweaponsel = w;
		extern void addmsg(int type, const char *fmt = NULL, ...);
		if(this == player1 || ownernum == player1->clientnum) addmsg(N_SWITCHWEAP, "ri2", clientnum, w->type);
		w->onselecting();
	}
};

enum { CTFF_INBASE = 0, CTFF_STOLEN, CTFF_DROPPED, CTFF_IDLE };

struct flaginfo
{
	int team;
	entity *flagent;
	int actor_cn;
	playerent *actor;
	vec pos;
	int state; // one of CTFF_*
	flaginfo() : flagent(0), actor(0), state(CTFF_INBASE) {}
};

enum { BT_NONE, BT_NADE, BT_GIB, BT_SHELL, BT_KNIFE };

struct bounceent : physent // nades, gibs
{
	int millis, timetolive, bouncetype; // see enum above
	float rotspeed;
	playerent *owner;

	bounceent() : bouncetype(BT_NONE), rotspeed(1.0f), owner(NULL)
	{
		type = ENT_BOUNCE;
		maxspeed = 40;
		radius = 0.2f;
		eyeheight = maxeyeheight = 0.3f;
		aboveeye = 0.0f;
	}

	virtual ~bounceent() {}

	bool isalive(int lastmillis) { return lastmillis - millis < timetolive; }
	virtual void destroy() {}
	virtual bool applyphysics() { return true; }
};

enum { HIT_NONE = 0, HIT_TORSO, HIT_LEG, HIT_HEAD };

struct grenadeent : bounceent
{
	bool local;
	int nadestate, id;
	float distsincebounce;
	grenadeent(playerent *owner, int millis = 0);
	~grenadeent();
	void activate();
	void _throw(const vec &from, const vec &vel);
	void explode();
	virtual void destroy();
	virtual bool applyphysics();
	void moveoutsidebbox(const vec &direction, playerent *boundingbox);
	void oncollision();
	void onmoved(const vec &dist);
};

struct knifeent : bounceent
{
	bool local;
	int knifestate;
	knifeent(playerent *owner, int millis = 0);
	~knifeent();
	void activate();
	void _throw(const vec &from, const vec &vel);
	void explode();
	virtual void destroy();
	virtual bool applyphysics();
	void moveoutsidebbox(const vec &direction, playerent *boundingbox);
	void oncollision();
};
