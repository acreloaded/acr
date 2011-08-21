enum							// static entity types
{
	NOTUSED = 0,				// entity slot not in use in map
	LIGHT,					  // lightsource, attr1 = radius, attr2 = intensity
	PLAYERSTART,				// attr1 = angle, attr2 = team
	I_CLIPS, I_AMMO, I_GRENADE,
	I_HEALTH, I_ARMOUR, I_AKIMBO,
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
	bool spawned;			   //the only dynamic state of a map entity
	entity(short x, short y, short z, uchar type, short attr1, uchar attr2, uchar attr3, uchar attr4) : persistent_entity(x, y, z, type, attr1, attr2, attr3, attr4), spawned(false) {}
	entity() {}
	bool fitsmode(int gamemode) { return !m_noitems && isitem(type) && !(m_noitemsnade && type!=I_GRENADE) && !(m_pistol && type==I_AMMO); }
	void transformtype(int gamemode)
	{
		if(m_noitemsnade && type==I_CLIPS) type = I_GRENADE;
		else if(m_pistol && type==I_AMMO) type = I_CLIPS;
	}
};

#define STARTHEALTH 100
#define MAXHEALTH 120

#define REGENDELAY 4250
#define REGENINT 2500

#define STARTARMOR 0
#define MAXARMOR 100

struct itemstat { short add, start, max, sound; };
static itemstat ammostats[] =
{
	{1,  1,   1,	S_ITEMAMMO },   // knife dummy
	{24, 60,  72,	S_ITEMAMMO },   // pistol
	{21, 28,  42,	S_ITEMAMMO },   // shotgun
	{96, 128,  192,	S_ITEMAMMO },   // subgun
	{30, 40,  80,	S_ITEMAMMO },   // sniper
	{16, 24,  32,	S_ITEMAMMO },   // bolt sniper
	{90, 120,  180,	S_ITEMAMMO },   // assault
	{1,  1,   3,	S_ITEMAMMO },   // grenade
	{96, 0,   144,	S_ITEMAKIMBO },  // akimbo
	{40, 60,  80,	S_ITEMAMMO },   // heal
	{50, 100, 200,  S_ITEMAMMO }, // wave gun
	{2,  3,   5,    S_ITEMAMMO }, // crossbow
};

static itemstat powerupstats[] =
{
	{35, STARTHEALTH, MAXHEALTH, S_ITEMHEALTH}, //health
	{40, STARTARMOR, MAXARMOR, S_ITEMARMOUR}, //armour
};

#define ADSTIME 275
#define CROUCHTIME 500

#define SGRAYS 24
#define SGSPREAD 318
#define SGADSSPREADFACTOR 20
#define SGGIB 180 // 18-45 rays
#define NADEPOWER 3
#define NADETTL 2500
#define MARTYRDOMTTL 2000
#define KNIFEPOWER 4.5f
#define KNIFETTL 30000
#define GIBBLOODMUL 1.5f
#define TIPSTICKTTL 1500
#define SPAWNPROTECT 3000

// define the "powerful" arsenal
#define POWERGUN(gun) (gun == GUN_SNIPER || gun == GUN_BOLT || gun == GUN_KNIFE)

struct mul{
	union{
		struct{ float head, torso, leg; };
		float val[3];
	};
};
enum { MUL_NORMAL = 0, MUL_POWERFUL, MUL_SHOTGUN, MUL_NUM };

static mul muls[MUL_NUM] =
{
	//{ head, torso, leg; }
	{ 3.5f, 1,		.67f }, // normal
	{ 5,	1.1f, 	1 }, // powerful
	{ 4,	1.2f,	1 } // shotgun
};

struct guninfo { string modelname; short sound, reload, reloadtime, attackdelay, damage, range, endrange, rangeminus, projspeed, part, spread, kick, magsize, mdl_kick_rot, mdl_kick_back, recoil, maxrecoil, recoilangle, pushfactor; bool isauto; };
static guninfo guns[NUMGUNS] =
{
//	{ modelname;     snd,	  rldsnd,  rldtime, atkdelay,  dmg, rngstart, rngend, rngm,psd,ptt,spr,kick,magsz,mkrot,mkback,rcoil,maxrcl,rca,pushf; auto;}
	{ "knife",      S_KNIFE,    S_ITEMAMMO,    0,   500,    80,    3,    4,   80,   0,   0,  1,    1,   1,    0,  0,     0,    0,       0, 5,   true },
	{ "pistol",     S_PISTOL,   S_RPISTOL,  1400,   90,     40,   40,  120,   20,   0,   0, 90,    9,   12,   6,  2,    32,    48,     70, 1,   false},
	{ "shotgun",    S_SHOTGUN,  S_RSHOTGUN,  750,   200,    10,    4,   16,    6,   0,   0,  1,   12,    7,   9,  5,    60,    80,      5, 2,   false},
	{ "subgun",     S_SUBGUN,   S_RSUBGUN,  2400,   67,     40,   32,   80,   22,   0,   0, 70,    4,   32,   1,  3,    23,    45,     65, 1,   true },
	{ "sniper",     S_SNIPER,   S_RSNIPER,  2000,   100,   120,    1,    2,   70,   0,   0,250,   14,   10,   4,  4,    58,    65,     75, 2,   false},
	{ "bolt",       S_BOLT,     S_RBOLT,    2000,   1500,  150,   80,  800,   40,   0,   0,280,   36,    8,   4,  4,    91,    99,     80, 3,   false},
	{ "assault",    S_ASSAULT,  S_RASSAULT, 2100,   73,     32,   40,  100,   12,   0,   0, 60,    3,   30,   0,  3,    24,    38,     60, 1,   true },
	{ "grenade",    S_NULL,     S_NULL,     1000,   650,   350,    0,   32,  330,  20,   6,  1,    1,    1,   3,  1,     0,    0,       0, 4,   false},
	{ "pistol",     S_PISTOL,   S_RAKIMBO,  1400,   80,     40,   45,  160,   20,   0,   0, 56,    8,   24,   6,  2,    28,    48,     70, 2,   true },
	{ "heal",       S_SUBGUN,   S_NULL,     1200,   100,    20,    4,    8,   10,   0,   0,112,    1,   10,   0,  0,    10,    20,      8, 5,   true },
	{ "wave",       S_NULL,     S_RASSAULT, 2500,   40,     10,    8,   12,    6,   0,   0,168,    2,   50,   0,  2,    16,    19,    360, 0,   true },
	{ "bow",        S_NULL,     S_RASSAULT, 2000,   120,   300,    0,   24,  290,   0,   0, 48,    3,    1,   3,  1,    48,    64,      0, 4,   false},
};

static inline ushort reloadtime(int gun) { return guns[gun].reloadtime; }
static inline ushort attackdelay(int gun) { return guns[gun].attackdelay; }
static inline ushort magsize(int gun) { return guns[gun].magsize; }
static inline ushort reloadsize(int gun) { return gun == GUN_SHOTGUN ? 1 : guns[gun].magsize; }
static inline ushort effectiveDamage(int gun, float dist, bool explosive = false) {
	ushort finaldamage = 0;
	if(dist <= guns[gun].range || (!guns[gun].range && !guns[gun].endrange)) finaldamage = guns[gun].damage;
	else if(dist >= guns[gun].endrange) finaldamage = guns[gun].damage - guns[gun].rangeminus;
	else{
		float subtractfactor = (dist - (float)guns[gun].range) / ((float)guns[gun].endrange - (float)guns[gun].range);
		if(explosive) subtractfactor = sqrtf(subtractfactor);
		finaldamage = guns[gun].damage - (short)(subtractfactor * guns[gun].rangeminus);
	}
	return finaldamage;
}

enum {
	OBIT_KNIFE = 0,
	OBIT_PISTOL,
	OBIT_SHOTGUN,
	OBIT_SUBGUN,
	OBIT_SNIPER,
	OBIT_BOLT,
	OBIT_ASSAULT,
	OBIT_GRENADE,
	OBIT_AKIMBO,
	OBIT_HEAL,
	OBIT_WAVE,
	OBIT_BOW, // guns
	OBIT_START,
	OBIT_DEATH = OBIT_START,
	OBIT_BOT,
	OBIT_BOW_IMPACT,
	OBIT_BOW_STUCK,
	OBIT_KNIFE_BLEED,
	OBIT_KNIFE_IMPACT,
	OBIT_HEADSHOT,
	OBIT_FF,
	OBIT_DROWN,
	OBIT_FALL,
	OBIT_NUM
};

static inline const int obit_suicide(int weap){
	if(weap >= 0 && weap <= OBIT_START) return weap;
	switch(weap - OBIT_START){
		case 0: return OBIT_DEATH; // death
		case 1: return OBIT_DROWN; // drown
		case 2: return OBIT_FALL; // fall
		case 3: return OBIT_FF; // ff
		case 4: return OBIT_BOT;
	}
	return OBIT_DEATH;
}

static inline const char *suicname(int obit){
	static string k;
	*k = 0;
	switch(obit){
		case GUN_GRENADE:
			s_strcat(k, "failed with nades");
			break;
		case GUN_HEAL:
			s_strcat(k, "overdosed on drugs");
			break;
		case GUN_BOW:
			s_strcat(k, "failed to use an explosive crossbow");
			break;
		case OBIT_DEATH:
			s_strcat(k, "requested suicide");
			break;
		case OBIT_BOT:
			s_strcat(k, "acted like a stupid bot");
			break;
		case OBIT_DROWN:
			s_strcat(k, "drowned");
			break;
		case OBIT_FALL:
			s_strcat(k, "failed to fly");
			break;
		default:
			s_strcat(k, "somehow suicided");
			break;
	}
	return k;
}

static inline const bool isheadshot(int weapon, int style){
	return weapon != GUN_SHOTGUN && weapon != GUN_GRENADE && weapon != GUN_BOW && (style & FRAG_GIB) && (weapon != GUN_KNIFE || style & FRAG_FLAG);
}

static inline const int toobit(int weap, int style){
	const bool gib = (style & FRAG_GIB) > 0,
				flag = (style & FRAG_FLAG) > 0;
	switch(weap){
		case GUN_KNIFE: return gib ? GUN_KNIFE : flag ? OBIT_KNIFE_IMPACT : OBIT_KNIFE_BLEED;
		case GUN_BOW: return gib ? flag ? OBIT_BOW_STUCK : GUN_BOW : OBIT_BOW_IMPACT;
	}
	return weap < NUMGUNS ? weap : OBIT_DEATH;
}

static inline const char *killname(int obit, bool headshot){
	static string k;
	*k = 0;
	switch(obit){
		case GUN_GRENADE:
			s_strcat(k, "obliterated");
			break;
		case GUN_KNIFE:
			s_strcat(k, headshot ? "decapitated" : "slashed");
			break;
		case GUN_BOLT:
			s_strcat(k, headshot ? "overkilled" : "quickly killed");
			break;
		case GUN_SNIPER:
			s_strcat(k, headshot ? "expertly sniped" : "sniped");
			break;
		case GUN_SUBGUN:
			s_strcat(k, headshot ? "perforated" : "spliced");
			break;
		case GUN_SHOTGUN:
			s_strcat(k, headshot ? "splattered" : "scrambled");
			break;
		case GUN_ASSAULT:
			s_strcat(k, headshot ? "eliminated" : "shredded");
			break;
		case GUN_PISTOL:
			s_strcat(k, headshot ? "capped" : "pierced");
			break;
		case GUN_AKIMBO:
			s_strcat(k, headshot ? "blasted" : "skewered");
			break;
		case GUN_HEAL:
			s_strcat(k, headshot ? "tranquilized" : "injected");
			break;
		case GUN_WAVE:
			s_strcat(k, headshot ? "disrupted" : "cooked");
			break;
		case GUN_BOW:
			s_strcat(k, "detonated");
			break;
		// special obits
		case OBIT_BOW_IMPACT:
			s_strcat(k, "impacted");
			break;
		case OBIT_BOW_STUCK:
			s_strcat(k, "plastered");
			break;
		case OBIT_KNIFE_BLEED:
			s_strcat(k, "fatally wounded");
			break;
		case OBIT_KNIFE_IMPACT:
			s_strcat(k, "thrown down");
			break;
		default:
			s_strcat(k, headshot ? "pwned" : "killed");
			break;
	}
	return k;
}

static float gunspeed(int gun, bool lightweight = false){
	float ret = lightweight ? 1.07f : 1;
	switch(gun){
		case GUN_KNIFE:
		case GUN_PISTOL:
		case GUN_GRENADE:
		case GUN_HEAL:
			//ret *= 1;
			break;
		case GUN_AKIMBO:
			ret *= .99f;
			break;
		case GUN_SHOTGUN:
			ret *= .97f;
			break;
		case GUN_SNIPER:
		case GUN_BOLT:
			ret *= .95f;
			break;
		case GUN_SUBGUN:
			ret *= .93f;
			break;
		case GUN_ASSAULT:
		case GUN_WAVE:
		case GUN_BOW:
			ret *= .9f;
			break;
	}
	return ret;
}

#define isteam(a,b)   (m_team && a->team == b->team)

enum { TEAM_RED = 0, TEAM_BLUE, TEAM_SPECT, TEAM_NUM };
#define team_valid(t) ((t) >= 0 && (t) < TEAM_NUM)
#define team_string(t) ((t) == TEAM_BLUE ? "BLUE" : (t) == TEAM_RED ? "RED" : "SPECT")
#define team_int(t) (!strcmp((t), "RED") ? TEAM_RED : !strcmp((t), "BLUE") ? TEAM_BLUE : TEAM_SPECT)
#define team_opposite(o) ((o) < TEAM_SPECT ? (o) ^ 1 : TEAM_SPECT)
#define team_color(t) ((t) == TEAM_RED ? 3 : (t) == TEAM_BLUE ? 1 : 4)

enum { ENT_PLAYER = 0, ENT_BOT, ENT_CAMERA, ENT_BOUNCE };
enum { CS_ALIVE = 0, CS_DEAD, CS_SPAWNING, CS_LAGGED, CS_EDITING, CS_SPECTATE };
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
	int health, armour;
	int lastcut, cutter, ownernum;
	int killstreak, assists;
	int primary, nextprimary;
	int gunselect;
	bool akimbo, scoping;
	int ammo[NUMGUNS], mag[NUMGUNS], gunwait[NUMGUNS];
	ivector damagelog;

	playerstate() : primary(GUN_ASSAULT), nextprimary(GUN_ASSAULT), ownernum(-1) {}
	virtual ~playerstate() {}

	itemstat &itemstats(int type)
	{
		switch(type)
		{
			case I_CLIPS: return ammostats[GUN_PISTOL];
			case I_AMMO: return ammostats[primary];
			case I_GRENADE: return ammostats[GUN_GRENADE];
			case I_AKIMBO: return ammostats[GUN_AKIMBO];
			case I_HEALTH: return powerupstats[0]; // FIXME: unify
			case I_ARMOUR: return powerupstats[1];
			default:
				return *(itemstat *)0;
		}
	}

	bool canpickup(int type)
	{
		switch(type)
		{
			case I_CLIPS: return ammo[akimbo ? GUN_AKIMBO : GUN_PISTOL]<ammostats[akimbo ? GUN_AKIMBO : GUN_PISTOL].max;
			case I_AMMO: return ammo[primary]<ammostats[primary].max;
			case I_GRENADE: return mag[GUN_GRENADE]<ammostats[GUN_GRENADE].max;
			case I_HEALTH: return health<powerupstats[type-I_HEALTH].max;
			case I_ARMOUR: return armour<powerupstats[type-I_HEALTH].max;
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
				additem(ammostats[GUN_PISTOL], ammo[GUN_PISTOL]);
				additem(ammostats[GUN_AKIMBO], ammo[GUN_AKIMBO]);
				break;
			case I_AMMO: additem(ammostats[primary], ammo[primary]); break;
			case I_GRENADE: additem(ammostats[GUN_GRENADE], mag[GUN_GRENADE]); break;
			case I_HEALTH: cutter = lastcut = 0; additem(powerupstats[type-I_HEALTH], health); break;
			case I_ARMOUR: additem(powerupstats[type-I_HEALTH], armour); break;
			case I_AKIMBO:
				akimbo = true;
				mag[GUN_AKIMBO] = guns[GUN_AKIMBO].magsize;
				additem(ammostats[GUN_AKIMBO], ammo[GUN_AKIMBO]);
				break;
		}
	}

	void respawn()
	{
		health = STARTHEALTH;
		armour = STARTARMOR;
		cutter = -1;
		killstreak = assists = armour = lastcut = 0;
		gunselect = GUN_PISTOL;
		akimbo = scoping = false;
		loopi(NUMGUNS) ammo[i] = mag[i] = gunwait[i] = 0;
		ammo[GUN_KNIFE] = 2;
		mag[GUN_KNIFE] = 1;
	}

	virtual void spawnstate(int gamemode)
	{
		if(m_pistol) primary = GUN_PISTOL;
		else if(m_osok) primary = GUN_BOLT;
		else if(m_lss) primary = GUN_KNIFE;
		else primary = nextprimary;

		if(primary == GUN_GRENADE || primary == GUN_AKIMBO || primary < 0 || primary >= NUMGUNS) primary = GUN_ASSAULT;

		if(!m_nopistol){
			ammo[GUN_PISTOL] = ammostats[GUN_PISTOL].start-magsize(GUN_PISTOL);
			mag[GUN_PISTOL] = magsize(GUN_PISTOL);
		}

		if(!m_noprimary){
			ammo[primary] = ammostats[primary].start-magsize(primary);
			mag[primary] = magsize(primary);
		}

		if(!m_noitems) mag[GUN_GRENADE] = ammostats[GUN_GRENADE].start;

		gunselect = primary;

		if(m_osok) health = /*guns[GUN_KNIFE].damage + 1*/ 81;
	}

	// just subtract damage here, can set death, etc. later in code calling this
	int dodamage(int damage){
		int ad = damage*30/100; // let armour absorb when possible
		if(ad>armour) ad = armour;
		armour -= ad;
		damage -= ad;
		health -= damage;
		return damage;
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
    enum { VOICECOM = 0, HEADSHOT, DECAPITATED, FIRSTBLOOD, CRITICAL, REVENGE, BLEED, PICKUP, TOTAL };
    int type, millis;
	eventicon(int type, int millis) : type(type), millis(millis){}
};

struct damageinfo{
	vec o;
	int millis;
	damageinfo(vec s, int t) : o(s), millis(t) {}
};

class CBot;

#define botent playerent // botent has been moved into playerent

struct playerent : dynent, playerstate
{
	int clientnum, lastupdate, plag, ping;
	int lifesequence;				   // sequence id for each respawn, used in damage test
	int radarmillis; float lastloudpos[3];
	int points, frags, flagscore, deaths;
	int lastaction, lastmove, lastpain, lasthitmarker;
	int priv, vote, voternum, lastregen;
	int ads, wantsswitch; bool wantsreload;
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

	weapon *weapons[NUMGUNS];
	weapon *prevweaponsel, *weaponsel, *nextweaponsel, *primweap, *nextprimweap, *lastattackweapon;

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

		lastupdate = plag = ping = lifesequence = points = frags = flagscore = deaths = lastpain = lasthitmarker = skin = eardamagemillis = respawnoffset = radarmillis = ads = 0;
		weaponsel = nextweaponsel = primweap = nextprimweap = lastattackweapon = prevweaponsel = NULL;
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

	void hitpush(int damage, const vec &dir, int gun)
	{
		if(gun<0 || gun>NUMGUNS || dir.iszero()) return;
		vec push = dir;
		push.normalize().mul(damage/100.f*guns[gun].pushfactor);
		vel.add(push);
		extern int lastmillis;
		if(gun==GUN_GRENADE && damage > 50) eardamagemillis = lastmillis+damage*100;
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
		lastaction = weaponchanging = eardamagemillis = flashmillis = 0;
		lastattackweapon = NULL;
		ads = 0.f;
		wantsswitch = -1;
		scoping = attacking = false;
		lastaction ;
		resetspec();
		eyeheight = maxeyeheight;
		damagestack.setsize(0);
	}

	void spawnstate(int gamemode)
	{
		playerstate::spawnstate(gamemode);
		prevweaponsel = weaponsel = weapons[gunselect];
		primweap = weapons[primary];
		nextprimweap = weapons[nextprimary];
	}

	void selectweapon(int w) { prevweaponsel = weaponsel = weapons[(gunselect = w)]; }
	void setprimary(int w) { primweap = weapons[(primary = w)]; }
	void setnextprimary(int w) { nextprimweap = weapons[(nextprimary = w)]; }
	bool isspectating() { return state==CS_SPECTATE || (state==CS_DEAD && spectatemode > SM_NONE); }
	void weaponswitch(weapon *w)
	{
		if(!w) return;
		extern playerent *player1;
		if(ads){
			if(this == player1){
				extern void setscope(bool activate);
				setscope(false);
				wantsswitch = w->type;
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
