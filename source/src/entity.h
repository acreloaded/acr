enum                            // static entity types
{
    NOTUSED = 0,                // entity slot not in use in map
    LIGHT,                      // lightsource, attr1 = radius, attr2 = intensity
    PLAYERSTART,                // attr1 = angle, attr2 = team
    I_CLIPS, I_AMMO, I_GRENADE,
    I_HEALTH, I_HELMET, I_ARMOUR, I_AKIMBO,
                                // helmet : 2010may16 -> mapversion:8
    MAPMODEL,                   // attr1 = angle, attr2 = idx
    CARROT,                     // attr1 = tag, attr2 = type
    LADDER,
    CTF_FLAG,                   // attr1 = angle, attr2 = red/blue
    SOUND,
    CLIP,
    PLCLIP,
    MAXENTTYPES
};

enum {MAP_IS_BAD, MAP_IS_EDITABLE, MAP_IS_GOOD};

extern const char *entnames[MAXENTTYPES];
#define isitem(i) ((i) >= I_CLIPS && (i) <= I_AKIMBO)

struct persistent_entity        // map entity
{
    short x, y, z;              // cube aligned position
    short attr1;
    uchar type;                 // type is one of the above
    uchar attr2, attr3, attr4;
    persistent_entity(short x, short y, short z, uchar type, short attr1, uchar attr2, uchar attr3, uchar attr4) : x(x), y(y), z(z), attr1(attr1), type(type), attr2(attr2), attr3(attr3), attr4(attr4) {}
    persistent_entity() {}
};

struct entity : persistent_entity
{
    // dynamic states of a map entity
    bool spawned; int spawntime;
    int lastmillis;
    entity(short x, short y, short z, uchar type, short attr1, uchar attr2, uchar attr3, uchar attr4) : persistent_entity(x, y, z, type, attr1, attr2, attr3, attr4), spawned(false), spawntime(0) {}
    entity() {}
    bool fitsmode(int gamemode, int mutators) { return !m_noitems(gamemode, mutators) && isitem(type) && !(m_noitemsnade(gamemode, mutators) && type!=I_GRENADE) && !(m_pistol(gamemode, mutators) && type==I_AMMO); }
    void transformtype(int gamemode, int mutators)
    {
        if(m_noitemsnade(gamemode, mutators) && type==I_CLIPS) type = I_GRENADE;
        else if(m_pistol(gamemode, mutators) && ( type==I_AMMO || type==I_GRENADE )) type = I_CLIPS;
    }
};

#define HEADSIZE 0.4f
#define TORSOPART 0.35f
#define LEGPART (1.f - TORSOPART)

#define PLAYERRADIUS 1.1f
#define PLAYERHEIGHT 4.5f
#define PLAYERABOVEEYE .7f
#define WEAPONBELOWEYE .2f

enum { PR_CLEAR = 0, PR_ASSIST, PR_SPLAT, PR_HS, PR_KC, PR_KD, PR_HEALSELF, PR_HEALTEAM, PR_HEALENEMY, PR_HEALWOUND, PR_HEALEDBYTEAMMATE, PR_ARENA_WIN, PR_ARENA_WIND, PR_ARENA_LOSE, PR_SECURE_SECURED, PR_SECURE_SECURE, PR_SECURE_OVERTHROW, PR_BUZZKILL, PR_BUZZKILLED, PR_KD_SELF, PR_KD_ENEMY, PR_MAX };

#define HEALTHPRECISION 1
#define HEALTHSCALE 10 // pow(10, HEALTHPRECISION)
#define STARTHEALTH ((m_juggernaut(gamemode, mutators) ? 1000 : 100) * HEALTHSCALE)
#define STARTARMOUR 0
#define MAXHEALTH ((m_juggernaut(gamemode, mutators) ? 1100 : 120) * HEALTHSCALE)
#define MAXARMOUR 100
#define VAMPIREMAX (STARTHEALTH + 200 * HEALTHSCALE)

#define MAXZOMBIEROUND 30
#define ZOMBIEHEALTHFACTOR 5
#define MAXTHIRDPERSON 25

#define SPAWNDELAY (m_secure(gamemode) ? 2000 : m_flags(gamemode) ? 5000 : 1500)
#define SPAWNPROTECT (m_flags(gamemode) ? 1000 : m_team(gamemode, mutators) ? 1500 : 1250)

#define REGENDELAY 4250
#define REGENINT 2500

#define SWITCHTIME(perk) ((perk) ? 200 : 400)
#define ADSTIME(perk) ((perk) ? 200 : 300)
#define CROUCHTIME 500
#define CROUCHHEIGHTMUL .75f
#define COMBOTIME 1000

#define NADEPOWER 2
#define NADETTL 4350
#define MARTYRDOMTTL 2500
#define KNIFEPOWER 4.5f
#define KNIFETTL 30000
#define GIBBLOODMUL 1.5f

#define BLEEDDMG 10
#define BLEEDDMGZ 5
#define BLEEDDMGPLUS 15

#define SGRAYS 24
#define SGGIB 180 // 18-26 rays (only have 24)

struct itemstat { int add, start, max, sound; };
extern itemstat ammostats[NUMGUNS];
extern itemstat powerupstats[I_ARMOUR-I_HEALTH+1];

struct guninfo { string modelname; short sound, reload, reloadtime, attackdelay, damage, range, endrange, rangesub, piercing, spread, spreadrem, kick, addsize, magsize, mdl_kick_rot, mdl_kick_back, recoilincrease, recoilbase, maxrecoil, recoilbackfade, recoilangle, pushfactor; bool isauto; };
extern guninfo guns[NUMGUNS];

struct mul { float torso, head; };
enum { MUL_NORMAL = 0, MUL_SNIPER, MUL_SHOTGUN, MUL_NUM };
extern const mul muls[MUL_NUM];

static inline int reloadtime(int gun) { return guns[gun].reloadtime; }
static inline int attackdelay(int gun) { return guns[gun].attackdelay; }
static inline int magsize(int gun) { return guns[gun].magsize; }
static inline int reloadsize(int gun) { return guns[gun].addsize; }

extern int effectiveDamage(int gun, float dist, bool explosive = false, bool useReciprocal = true);
extern const char *suicname(int obit);
extern const char *killname(int obit, int style);
extern bool isheadshot(int weapon, int style);

/** roseta stone:
       0000,         0001,      0010,           0011,            0100,       0101,     0110 */
enum { TEAM_CLA = 0, TEAM_RVSF, TEAM_CLA_SPECT, TEAM_RVSF_SPECT, TEAM_SPECT, TEAM_NUM, TEAM_ANYACTIVE };
extern const char *teamnames[TEAM_NUM+1];
extern const char *teamnames_s[TEAM_NUM+1];

#define TEAM_VOID TEAM_NUM
#define isteam(a,b)   (m_team(gamemode, mutators) && (a) == (b))
#define team_opposite(o) (team_isvalid(o) && (o) < TEAM_SPECT ? (o) ^ 1 : TEAM_SPECT)
#define team_base(t) ((t) & 1)
#define team_basestring(t) ((t) == 1 ? teamnames[1] : ((t) == 0 ? teamnames[0] : "SPECT"))
#define team_isvalid(t) ((int(t)) >= 0 && (t) < TEAM_NUM)
#define team_isactive(t) ((t) == TEAM_CLA || (t) == TEAM_RVSF)
#define team_isspect(t) ((t) > TEAM_RVSF && (t) < TEAM_VOID)
#define team_group(t) ((t) == TEAM_SPECT ? TEAM_SPECT : team_base(t))
#define team_tospec(t) ((t) == TEAM_SPECT ? TEAM_SPECT : team_base(t) + TEAM_CLA_SPECT - TEAM_CLA)
// note: team_isactive and team_base can/should be used to check the limits for arrays of size '2'
static inline const char *team_string(int t, bool abbr = false) { const char **n = abbr ? teamnames_s : teamnames; return team_isvalid(t) ? n[t] : n[TEAM_NUM]; }
#define team_color(t) (team_isspect(t) ? 4 : team_base(t) ? 1 : 3)
#define team_rel_color(a, b) (a == b ? 1 : a && b && !team_isspect(b->team) ? isteam(a->team, b->team) ? 0 : 3 : 4)

enum { ENT_PLAYER = 0, ENT_BOT, ENT_CAMERA, ENT_BOUNCE };
enum { HIT_NONE = 0, HIT_LEG, HIT_TORSO, HIT_HEAD };
enum { CS_ALIVE = 0, CS_DEAD, CS_SPAWNING, CS_LAGGED, CS_EDITING, CS_SPECTATE };
enum { CR_DEFAULT = 0, CR_ADMIN };
enum { SM_NONE = 0, SM_DEATHCAM, SM_FOLLOW1ST, SM_FOLLOW3RD, SM_FOLLOW3RD_TRANSPARENT, SM_FLY, SM_OVERVIEW, SM_NUM };
enum { FPCN_VOID = -4, FPCN_DEATHCAM = -2, FPCN_FLY = -2, FPCN_OVERVIEW = -1 };

enum { PERK_NONE = 0, PERK_RADAR, PERK_NINJA, PERK_POWER, PERK_TIME, PERK_MAX };
enum { PERK1_NONE = 0, PERK1_AGILE = PERK_MAX, PERK1_HAND, PERK1_LIGHT, PERK1_SCORE, PERK1_MAX, };
enum { PERK2_NONE = 0, PERK2_VISION = PERK_MAX, PERK2_STREAK, PERK2_STEADY, PERK2_HEALTH, PERK2_MAX };

class worldobject
{
public:
    virtual ~worldobject() {};
};

class physent : public worldobject
{
public:
    vec o, vel, vel_t;                         // origin, velocity
    vec deltapos, newpos;                       // movement interpolation
    float yaw, pitch, roll;             // used as vec in one place
    float pitchvel;
    float maxspeed;                     // cubes per second, 24 for player
    int timeinair;                      // used for fake gravity
    float radius, eyeheight, maxeyeheight, aboveeye;  // bounding box size
    bool inwater;
    bool onfloor, onladder, jumpnext, crouching, crouchedinair, trycrouch, sprinting, cancollide, stuck, shoot;
    int lastjump;
    float lastjumpheight;
    int lastsplash;
    char move, strafe;
    uchar state, type;
    float eyeheightvel;
    int last_pos;
    float zoomed;

    physent() : o(0, 0, 0), deltapos(0, 0, 0), newpos(0, 0, 0), yaw(270), pitch(0), roll(0), pitchvel(0),
            crouching(false), crouchedinair(false), trycrouch(false), sprinting(false), cancollide(true), stuck(false), shoot(false), lastjump(0), lastjumpheight(0), lastsplash(0), state(CS_ALIVE), last_pos(0)
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
        vel.x = vel.y = vel.z = eyeheightvel = vel_t.x = vel_t.y = vel_t.z = 0.0f;
        move = strafe = 0;
        timeinair = lastjump = lastsplash = 0;
        onfloor = onladder = inwater = jumpnext = crouching = crouchedinair = trycrouch = sprinting = stuck = false;
        last_pos = 0;
        zoomed = 0;
    }

    virtual void oncollision() {}
    virtual void onmoved(const vec &dist) {}
};

class dynent : public physent                 // animated ent
{
public:
    bool k_left, k_right, k_up, k_down;         // see input code

    animstate prev[2], current[2];              // md2's need only [0], md3's need both for the lower&upper model
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

#define MAXNAMELEN 15

class bounceent;

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

class playerstate
{
public:
    int health, armour;
    int lastspawn;
    int primary, secondary, perk1, perk2, nextprimary, nextsecondary, nextperk1, nextperk2;
    int gunselect;
    bool akimbo, scoping;
    int ammo[NUMGUNS], mag[NUMGUNS], gunwait[NUMGUNS];
    int pstatshots[NUMGUNS], pstatdamage[NUMGUNS];

    playerstate() : armour(0), primary(GUN_ASSAULT), secondary(GUN_PISTOL), perk1(PERK1_NONE), perk2(PERK2_NONE),
        nextprimary(GUN_ASSAULT), nextsecondary(GUN_PISTOL), nextperk1(PERK1_NONE), nextperk2(PERK2_NONE),
        akimbo(false), scoping(false) {}
    virtual ~playerstate() {}

    void resetstats() { loopi(NUMGUNS) pstatshots[i] = pstatdamage[i] = 0; }

    const itemstat &itemstats(int type)
    {
        switch(type)
        {
            case I_CLIPS: return ammostats[secondary];
            case I_AMMO: return ammostats[primary];
            case I_GRENADE: return ammostats[GUN_GRENADE];
            case I_AKIMBO: return ammostats[GUN_AKIMBO];
            case I_HEALTH:
            case I_HELMET:
            case I_ARMOUR:
                return powerupstats[type-I_HEALTH];
            default:
                return *(itemstat *)0;
        }
    }

    bool canpickup(int type, bool bot)
    {
        switch(type)
        {
            case I_CLIPS: return ammo[akimbo ? GUN_AKIMBO : secondary]<ammostats[akimbo ? GUN_AKIMBO : secondary].max;
            case I_AMMO: return primary == GUN_SWORD || ammo[primary]<ammostats[primary].max;
            case I_GRENADE: return mag[GUN_GRENADE]<ammostats[GUN_GRENADE].max;
            case I_HEALTH: return health<powerupstats[type-I_HEALTH].max;
            case I_HELMET:
            case I_ARMOUR: return armour<powerupstats[type-I_HEALTH].max;
            case I_AKIMBO: return !akimbo && !bot;
            default: return false;
        }
    }

    void additem(const itemstat &is, int &v)
    {
        v += is.add;
        if(v > is.max) v = is.max;
    }

    void pickup(int type)
    {
        switch(type)
        {
            case I_CLIPS:
                additem(ammostats[secondary], ammo[secondary]);
                additem(ammostats[GUN_AKIMBO], ammo[GUN_AKIMBO]);
                break;
            case I_AMMO: additem(ammostats[primary], ammo[primary]); break;
            case I_GRENADE: additem(ammostats[GUN_GRENADE], mag[GUN_GRENADE]); break;
            case I_HEALTH: additem(powerupstats[type-I_HEALTH], health); break;
            case I_HELMET:
            case I_ARMOUR:
                additem(powerupstats[type-I_HEALTH], armour); break;
            case I_AKIMBO:
                akimbo = true;
                mag[GUN_AKIMBO] = guns[GUN_AKIMBO].magsize;
                additem(ammostats[GUN_AKIMBO], ammo[GUN_AKIMBO]);
                break;
        }
    }

    void respawn(int gamemode, int mutators)
    {
        health = STARTHEALTH;
        armour = STARTARMOUR;
        gunselect = GUN_PISTOL;
        akimbo = scoping = false;
        loopi(NUMGUNS) ammo[i] = mag[i] = gunwait[i] = 0;
        ammo[GUN_KNIFE] = mag[GUN_KNIFE] = 1;
        lastspawn = -1;
    }

    virtual void spawnstate(int team, int gamemode, int mutators)
    {
        if (m_zombie(gamemode) && team == TEAM_CLA) primary = GUN_SWORD;
        else if(m_pistol(gamemode, mutators)) primary = GUN_PISTOL;
        else if (m_gib(gamemode, mutators)) primary = GUN_KNIFE;
        else if (m_demolition(gamemode, mutators)) primary = GUN_RPG; // inversion
        else switch (nextprimary)
        {
            default: primary = m_sniper(gamemode, mutators) ? GUN_BOLT : GUN_ASSAULT; break;
            case GUN_KNIFE:
            case GUN_SHOTGUN:
            case GUN_SUBGUN:
            case GUN_ASSAULT:
            case GUN_GRENADE:
            case GUN_AKIMBO:
            case GUN_ASSAULT2:
                if (m_sniper(gamemode, mutators))
                {
                    primary = GUN_BOLT;
                    break;
                }
                // fallthrough
            // Only bolt/M82/M21 for insta mutator
            case GUN_SNIPER:
                if (m_insta(gamemode, mutators))
                {
                    primary = GUN_BOLT;
                    break;
                }
                // fallthrough
            // Only bolt/M82 for sniping mutator
            case GUN_BOLT:
            case GUN_SNIPER2:
                primary = nextprimary;
                break;
        }

        if (m_zombie(gamemode) && team == TEAM_CLA) secondary = GUN_KNIFE;
        else if (m_pistol(gamemode, mutators)) secondary = primary; // no secondary
        else if (m_sniper(gamemode, mutators) || m_demolition(gamemode, mutators) || m_gib(gamemode, mutators))
            secondary = GUN_SWORD;
        else switch (nextsecondary)
        {
            default: secondary = GUN_PISTOL; break;
            case GUN_PISTOL:
            case GUN_HEAL:
            case GUN_SWORD:
            case GUN_RPG:
                secondary = nextsecondary;
                break;
        }

        // always have a primary
        ammo[primary] = ammostats[primary].start - 1;
        mag[primary] = magsize(primary);
        // secondary ammo
        if(secondary != primary)
        {
            ammo[secondary] = ammostats[secondary].start - 1;
            mag[secondary] = magsize(secondary);
        }
        ammo[GUN_GRENADE] = ammostats[GUN_GRENADE].start - 1;
        mag[GUN_GRENADE] = magsize(GUN_GRENADE);

        gunselect = primary;

        if (m_zombie(gamemode) && team == TEAM_CLA)
        {
            perk1 = PERK1_AGILE;
            perk2 = PERK2_STREAK;
        }
        else
        {
            perk1 = nextperk1;
            perk2 = nextperk2;
        }

        if (perk1 <= PERK1_NONE || perk1 >= PERK1_MAX) perk1 = rnd(PERK1_MAX - 1) + 1;
        if (perk2 <= PERK2_NONE || perk2 >= PERK2_MAX) perk2 = rnd(PERK2_MAX - PERK_MAX - 1) + PERK_MAX + 1;
        // special perks need both slots
        if (perk1 < PERK_MAX) perk2 = perk1;

        const int healthsets[3] = { STARTHEALTH - 15 * HEALTHSCALE, STARTHEALTH, STARTHEALTH + 20 * HEALTHSCALE };
        health = healthsets[(!m_regen(gamemode, mutators) && m_sniper(gamemode, mutators) ? 0 : 1) + (perk2 == PERK2_HEALTH ? 1 : 0)];
        if (m_zombie(gamemode))
        {
            switch (team)
            {
                case TEAM_CLA:
                    if (m_onslaught(gamemode, mutators))
                    {
                        health = STARTHEALTH * ZOMBIEHEALTHFACTOR;
                        armour += 50;
                    }
                    else health = STARTHEALTH + rnd(STARTHEALTH * ZOMBIEHEALTHFACTOR);
                    break;
                case TEAM_RVSF:
                    if (!m_onslaught(gamemode, mutators)) break;
                    // humans for onslaught only
                    if (perk2 == PERK2_HEALTH) health = STARTHEALTH * ZOMBIEHEALTHFACTOR; // all 500
                    else health = STARTHEALTH * (rnd(ZOMBIEHEALTHFACTOR - 2) + 2) + (STARTHEALTH / 2); // 250 - 450
                    armour += 2000;
                    break;
            }
        }
        // half health for vampire
        if (m_vampire(gamemode, mutators)) health >>= 1;
    }

    // just subtract damage here, can set death, etc. later in code calling this
    int dodamage(int damage, int gun)
    {
        guninfo gi = guns[gun];
        if(damage == INT_MAX)
        {
            damage = health;
            armour = health = 0;
            return damage;
        }

        // 4-level armour - tiered approach: 16%, 33%, 37%, 41%
        // Please update ./ac_website/htdocs/docs/introduction.html if this changes.
        int armoursection = 0;
        int ad = damage;
        if(armour > 25) armoursection = 1;
        if(armour > 50) armoursection = 2;
        if(armour > 75) armoursection = 3;
        switch(armoursection)
        {
            case 0: ad = (int) (16.0f/25.0f * armour); break;             // 16
            case 1: ad = (int) (17.0f/25.0f * armour) - 1; break;         // 33
            case 2: ad = (int) (4.0f/25.0f * armour) + 25; break;         // 37
            case 3: ad = (int) (4.0f/25.0f * armour) + 25; break;         // 41
            default: break;
        }

        //ra - reduced armor
        //rd - reduced damage
        int ra = (int) (ad * damage/100.0f);
        int rd = ra-(ra*(gi.piercing/100.0f)); //Who cares about rounding errors anyways?

        armour -= ra;
        damage -= rd;

        health -= damage;
        return damage;
    }

    int protect(int millis, int gamemode, int mutators)
    {
        const int delay = SPAWNPROTECT, spawndelay = millis - lastspawn;
        int amt = 0;
        if(lastspawn && delay && spawndelay < delay)
            amt = delay - spawndelay;
        return amt;
    }
};

#ifndef STANDALONE
struct eventicon
{
    enum
    {
        CHAT = 0,
        VOICECOM,
        HEADSHOT,
        DECAPITATED,
        FIRSTBLOOD,
        CRITICAL,
        REVENGE,
        BLEED,
        PICKUP,
        RADAR,
        AIRSTRIKE,
        NUKE,
        JUGGERNAUT,
        DROPNADE,
        SUICIDEBOMB,
        TOTAL
    };
    int type, millis;
    eventicon(int type, int millis) : type(type), millis(millis){}
};

struct damageinfo
{
    vec o;
    int millis, damage;
    damageinfo(vec o, int t, int d) : o(o), millis(t), damage(d) {}
};

struct kd{
     int kills;
     int deaths;
};

class playerent : public dynent, public playerstate
{
private:
    int curskin, nextskin[2];
public:
    int clientnum, lastupdate, plag, ping;
    enet_uint32 address;
    int lifesequence;                   // sequence id for each respawn, used in damage test
    int radarmillis; vec lastloudpos;
    int frags, flagscore, deaths, points, tks;
    int lastaction, lastmove, lastpain, lastvoicecom, lasthit;
    int clientrole;
    bool attacking;
    string name;
    int team;
    int weaponchanging;
    int nextweapon; // weapon we switch to
    int spectatemode, followplayercn;
    int eardamagemillis;
    int respawnoffset;
    vector<eventicon> icons;
    kd weapstats[NUMGUNS];
    bool allowmove() { return state!=CS_DEAD || spectatemode==SM_FLY; }

    weapon *weapons[NUMGUNS];
    weapon *prevweaponsel, *weaponsel, *nextweaponsel, *lastattackweapon;

    poshist history; // Previous stored locations of this player

    const char *skin_noteam, *skin_cla, *skin_rvsf;

    float deltayaw, deltapitch, newyaw, newpitch;
    int smoothmillis;

    vec head, eject, muzzle;

    bool ignored, muted;

    // AI
    int ownernum, level;
    class CBot *pBot;
    playerent *enemy;                      // monster wants to kill this entity
    float targetpitch, targetyaw;          // monster wants to look in this direction

    playerent() : curskin(0), clientnum(-1), lastupdate(0), plag(0), ping(0), address(0), lifesequence(0),
                  radarmillis(0), lastloudpos(0, 0, 0),
                  frags(0), flagscore(0), deaths(0), points(0), tks(0), lastpain(0), lastvoicecom(0), lasthit(0), clientrole(CR_DEFAULT),
                  team(TEAM_SPECT), spectatemode(SM_NONE), followplayercn(FPCN_VOID), eardamagemillis(0), respawnoffset(0),
                  prevweaponsel(NULL), weaponsel(NULL), nextweaponsel(NULL), lastattackweapon(NULL),
                  smoothmillis(-1),
                  head(-1, -1, -1), eject(-1, -1, -1), muzzle(-1, -1, -1), ignored(false), muted(false),
                  ownernum(-1), level(0), pBot(NULL), enemy(NULL)
    {
        type = ENT_PLAYER;
        name[0] = 0;
        maxeyeheight = PLAYERHEIGHT;
        aboveeye = PLAYERABOVEEYE;
        radius = PLAYERRADIUS;
        maxspeed = 16.0f;
        skin_noteam = skin_cla = skin_rvsf = NULL;
        loopi(2) nextskin[i] = 0;
        loopi(NUMGUNS) weapstats[i].deaths = weapstats[i].kills = 0;
        respawn(G_DM, G_M_NONE);
    }

    void addicon(int type)
    {
        switch (type){
            case eventicon::CRITICAL:
            case eventicon::PICKUP:
                loopv(icons)
                    if (icons[i].type == type)
                        icons.remove(i--);
                break;
        }
        extern int lastmillis;
        eventicon icon(type, lastmillis);
        icons.add(icon);
    }

    void removeai();

    virtual ~playerent()
    {
        removeai();
        icons.shrink(0);
        extern void removebounceents(playerent *owner);
        extern void removedynlights(physent *owner);
        extern void zapplayerflags(playerent *owner);
        extern void cleanplayervotes(playerent *owner);
        extern physent *camera1;
        extern void togglespect();
        removebounceents(this);
        audiomgr.detachsounds(this);
        removedynlights(this);
        zapplayerflags(this);
        cleanplayervotes(this);
        if(this==camera1) togglespect();
    }

    void damageroll(float damage)
    {
        extern void clamproll(physent *pl);
        float damroll = 2.0f*damage;
        roll += roll>0 ? damroll : (roll<0 ? -damroll : (rnd(2) ? damroll : -damroll)); // give player a kick
        clamproll(this);
    }

    void hitpush(int damage, const vec &dir, playerent *actor, int gun)
    {
        if(gun<0 || gun>NUMGUNS) return;
        vec push(dir);
        push.mul(damage/100.0f*guns[gun].pushfactor);
        vel.add(push);
        extern int lastmillis;
        if(gun==GUN_GRENADE && damage > 50 * HEALTHSCALE) eardamagemillis = lastmillis+damage*100;
    }

    void resetspec()
    {
        spectatemode = SM_NONE;
        followplayercn = FPCN_VOID;
    }

    void respawn(int gamemode, int mutators)
    {
        dynent::reset();
        playerstate::respawn(gamemode, mutators);
        history.reset();
        if(weaponsel) weaponsel->reset();
        lastaction = 0;
        lastattackweapon = NULL;
        attacking = false;
        extern int lastmillis;
        weaponchanging = 0; // spawnkill is bad though // lastmillis - weapons[gunselect]->weaponchangetime / 2; // 2011jan16:ft: for a little no-shoot after spawn
        resetspec();
        eardamagemillis = 0;
        eyeheight = maxeyeheight;
        curskin = nextskin[team_base(team)];
    }

    void spawnstate(int team, int gamemode, int mutators)
    {
        playerstate::spawnstate(team, gamemode, mutators);
        prevweaponsel = weaponsel = weapons[gunselect];
        curskin = nextskin[team_base(team)];
    }

    void selectweapon(int w) { prevweaponsel = weaponsel; weaponsel = weapons[(gunselect = w)]; if (!prevweaponsel) prevweaponsel = weaponsel; }
    void setprimary(int w) { primary = w; }
    bool isspectating() { return state==CS_SPECTATE || (state==CS_DEAD && spectatemode > SM_NONE); }
    void weaponswitch(weapon *w)
    {
        if(!w) return;
        extern int lastmillis;
        weaponsel->ondeselecting();
        weaponchanging = lastmillis;
        nextweaponsel = w;
        extern playerent *player1;
        extern void addmsg(int type, const char *fmt, ...);
        if (this == player1 || ownernum == player1->clientnum)
            addmsg(SV_WEAPCHANGE, "ri2", clientnum, w->type);
        w->onselecting();
    }
    int skin(int t = -1) { return nextskin[team_base(t < 0 ? team : t)]; }
    void setskin(int t, int s)
    {
        const int maxskin[2] = { 4, 6 };
        t = team_base(t < 0 ? team : t);
        nextskin[t] = abs(s) % maxskin[t];
    }
};
#endif //#ifndef STANDALONE

// flag-mode entities

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

// nades, gibs

enum { BT_NONE, BT_NADE, BT_GIB, BT_SHELL, BT_KNIFE };

class bounceent : public physent
{
public:
    int millis, timetolive, bouncetype; // see enum above
    float rotspeed;
    bool plclipped;
    playerent *owner;

    bounceent() : bouncetype(BT_NONE), rotspeed(1.0f), plclipped(false), owner(NULL)
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

class grenadeent : public bounceent
{
public:
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

enum {MD_FRAGS = 0, MD_DEATHS, END_MDS};
struct medalsst {bool assigned; int cn; int item;};

#ifndef STANDALONE
struct pckserver
{
    char *addr;
    bool pending, responsive;
    int ping;

    pckserver() : addr(NULL), pending(false), responsive(true), ping(-1) {}
};

enum { PCK_TEXTURE, PCK_SKYBOX, PCK_MAPMODEL, PCK_AUDIO, PCK_MAP, PCK_NUM };

struct package
{
    char *name;
    int type, number;
    bool pending;
    pckserver *source;
    CURL *curl;

    package() : name(NULL), type(-1), number(0), pending(false), source(NULL), curl(NULL) {}
};
#endif
