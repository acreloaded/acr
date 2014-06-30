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
extern playerent *player1;
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

enum { GUN_KNIFE = 0, GUN_PISTOL, WEAP_SWORD, GUN_CARBINE, GUN_SHOTGUN, GUN_SUBGUN, GUN_SNIPER, GUN_ASSAULT, GUN_CPISTOL, GUN_GRENADE, GUN_AKIMBO, WEAP_MAX, WEAP_RPG,  GUN_BOLT, GUN_ASSAULT2 };//implement everything after MAX
#define isprimary(n) ((n >= GUN_CARBINE) && (n < GUN_CPISTOL))
#define issecondary(n) (!(isprimary(n) || n == 0))

#define reloadable_gun(g) (!(g == GUN_KNIFE || g == GUN_GRENADE || g == WEAP_SWORD))

#define HEADSIZE 0.4f
#define TORSOPART 0.35f
#define LEGPART (1.f - TORSOPART)

#define PLAYERRADIUS 1.1f
#define PLAYERHEIGHT 4.5f
#define PLAYERABOVEEYE .7f
#define WEAPONBELOWEYE .2f

#define ZOOMLIMIT 2000000000 // client only

enum { PR_CLEAR = 0, PR_ASSIST, PR_SPLAT, PR_HS, PR_KC, PR_KD, PR_HEALSELF, PR_HEALTEAM, PR_HEALENEMY, PR_HEALWOUND, PR_HEALEDBYTEAMMATE, PR_ARENA_WIN, PR_ARENA_WIND, PR_ARENA_LOSE, PR_SECURE_SECURED, PR_SECURE_SECURE, PR_SECURE_OVERTHROW, PR_BUZZKILL, PR_BUZZKILLED, PR_KD_SELF, PR_KD_ENEMY, PR_MAX };

#define HEALTHPRECISION 1
#define HEALTHSCALE 10 // pow(10, HEALTHPRECISION)
#define STARTHEALTH ((m_juggernaut(gamemode, mutators) ? 1000 : 100) * HEALTHSCALE)
#define MAXHEALTH ((m_juggernaut(gamemode, mutators) ? 1100 : 120) * HEALTHSCALE)
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

#define BLEEDDMG 10
#define BLEEDDMGZ 5
#define BLEEDDMGPLUS 15

#define SGRAYS 21
#define SGDMGTOTAL 90

#define SGDMGBONUS 65
#define SGDMGDISTB 50

#define SGCCdmg 500
#define SGCCbase 0
#define SGCCrange 40

#define SGCMdmg 375
#define SGCMbase 25
#define SGCMrange 60

#define SGCOdmg 125
#define SGCObase 45
#define SGCOrange 75

#define SGMAXDMGABS 105
#define SGMAXDMGLOC 84
#define SGBONUSDIST 80
#define SGSEGDMG_O 3
#define SGSEGDMG_M 6
#define SGSEGDMG_C 4
#define SGSPREAD 2.25
#define EXPDAMRAD 10

struct itemstat { int add, start, max, sound; };
extern itemstat ammostats[WEAP_MAX];
extern itemstat powerupstats[I_ARMOUR-I_HEALTH+1];

struct guninfo { string modelname; short sound, reload, reloadtime, attackdelay, damage, piercing, projspeed, part, spread, spreadrem, kick, recoil, magsize, mdl_kick_rot, mdl_kick_back, recoilincrease, recoilbase, maxrecoil, recoilbackfade, pushfactor; bool isauto; };
extern guninfo guns[WEAP_MAX];

static inline int reloadtime(int gun) { return guns[gun].reloadtime; }
static inline int attackdelay(int gun) { return guns[gun].attackdelay; }
static inline int magsize(int gun) { return guns[gun].magsize; }

/** roseta stone:
       0000,         0001,      0010,           0011,            0100,       0101,     0110 */
enum { TEAM_CLA = 0, TEAM_RVSF, TEAM_CLA_SPECT, TEAM_RVSF_SPECT, TEAM_SPECT, TEAM_NUM, TEAM_ANYACTIVE };
extern const char *teamnames[TEAM_NUM+1];
extern const char *teamnames_s[TEAM_NUM+1];

extern float gunspeed(int gun, int ads);

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
enum { HIT_NONE = 0, HIT_TORSO, HIT_LEG, HIT_HEAD };
enum { CS_ALIVE = 0, CS_DEAD, CS_SPAWNING, CS_LAGGED, CS_EDITING, CS_SPECTATE };
enum { CR_DEFAULT = 0, CR_ADMIN };
enum { SM_NONE = 0, SM_DEATHCAM, SM_FOLLOW1ST, SM_FOLLOW3RD, SM_FOLLOW3RD_TRANSPARENT, SM_FLY, SM_OVERVIEW, SM_NUM };
enum { FPCN_VOID = -4, FPCN_DEATHCAM = -2, FPCN_FLY = -2, FPCN_OVERVIEW = -1 };

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
    bool onfloor, onladder, jumpnext, crouching, crouchedinair, trycrouch, cancollide, stuck, scoping, shoot;
    int lastjump;
    float lastjumpheight;
    int lastsplash;
    char move, strafe;
    uchar state, type;
    float eyeheightvel;
    int last_pos;
    int zoomed;

    physent() : o(0, 0, 0), deltapos(0, 0, 0), newpos(0, 0, 0), yaw(270), pitch(0), roll(0), pitchvel(0),
            crouching(false), crouchedinair(false), trycrouch(false), cancollide(true), stuck(false), scoping(false), shoot(false), lastjump(0), lastjumpheight(0), lastsplash(0), state(CS_ALIVE), last_pos(0)
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
        onfloor = onladder = inwater = jumpnext = crouching = crouchedinair = trycrouch = stuck = false;
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
    int primary, secondary, nextprimary, nextsecondary;
    int gunselect;
    bool akimbo;
    int ammo[WEAP_MAX], mag[WEAP_MAX], gunwait[WEAP_MAX];
    int pstatshots[WEAP_MAX], pstatdamage[WEAP_MAX];
//IMPLEMENT secondary, nextsecondary
    playerstate() : armour(0), primary(GUN_ASSAULT), secondary(GUN_PISTOL),
        nextprimary(GUN_ASSAULT), nextsecondary(GUN_PISTOL), akimbo(false) {}
    virtual ~playerstate() {}

    void resetstats() { loopi(WEAP_MAX) pstatshots[i] = pstatdamage[i] = 0; }

    itemstat &itemstats(int type)
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

    bool canpickup(int type)
    {
        switch(type)
        {
            case I_CLIPS: return ammo[akimbo ? GUN_AKIMBO : secondary]<ammostats[akimbo ? GUN_AKIMBO : secondary].max;
            case I_AMMO: return ammo[primary]<ammostats[primary].max;
            case I_GRENADE: return mag[GUN_GRENADE]<ammostats[GUN_GRENADE].max;
            case I_HEALTH: return health<powerupstats[type-I_HEALTH].max;
            case I_HELMET:
            case I_ARMOUR: return armour<powerupstats[type-I_HEALTH].max;
            case I_AKIMBO: return !akimbo;
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

    void respawn()
    {
        health = 100;
        armour = 0;
        gunselect = primary;
        akimbo = false;
        loopi(WEAP_MAX) ammo[i] = mag[i] = gunwait[i] = 0;
        ammo[GUN_KNIFE] = mag[GUN_KNIFE] = 1;
        lastspawn = -1;
    }

    virtual void spawnstate(int gamemode, int mutators)
    {
        if(m_pistol(gamemode, mutators)) primary = GUN_PISTOL;
        else if(m_sniper(gamemode, mutators)) primary = GUN_SNIPER;
        else if(m_lss(gamemode, mutators)) primary = GUN_KNIFE;
        else primary = nextprimary;

        if(!m_noprimary(gamemode, mutators))
        {
            ammo[primary] = ammostats[primary].start-magsize(primary);
            mag[primary] = magsize(primary);
        }

        gunselect = primary;

        //if(m_zombie(gamemode) && team == TEAM_CLA) secondary = WEAP_KNIFE;
        if(m_pistol(gamemode, mutators)) secondary = primary; // no secondary
		else if(m_sniper(gamemode, mutators) || m_demolition(gamemode, mutators) || m_gib(gamemode, mutators)) secondary = WEAP_SWORD;
		else secondary = issecondary(nextsecondary) ? nextsecondary : GUN_PISTOL;

		if(primary != secondary){
			ammo[secondary] = (ammostats[secondary].start-1);
			mag[secondary] = magsize(secondary);
		}

        if(m_sniper(gamemode, mutators)) health = 1;
        if(m_survivor(gamemode, mutators)) // Survivor && Team-Survivor : 2010nov19
        {
            health = 100;
            armour = 100;
            ammo[GUN_GRENADE] = 2;
        }

        if(!m_nosecondary(gamemode, mutators))
        {
            ammo[secondary] = ammostats[secondary].start-magsize(secondary);//ammostats[GUN_PISTOL].max-magsize(GUN_PISTOL);
            mag[secondary] = magsize(secondary);
        }
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
#define HEADSIZE 0.4f

class playerent : public dynent, public playerstate
{
private:
    int curskin, nextskin[2];
public:
    int clientnum, lastupdate, plag, ping;
    enet_uint32 address;
    int lifesequence;                   // sequence id for each respawn, used in damage test
    int frags, flagscore, deaths, points, tks;
    int lastaction, lastmove, lastpain, lastvoicecom, lasthit;
    int clientrole;
    bool attacking, delayedscope, wantsreload;
    string name;
    int team;
    int ads, wantsswitch;
    int weaponchanging;
    int nextweapon; // weapon we switch to
    int spectatemode, followplayercn;
    int eardamagemillis;
    int respawnoffset;
    bool allowmove() { return state!=CS_DEAD || spectatemode==SM_FLY; }

    weapon *weapons[WEAP_MAX];
    weapon *prevweaponsel, *weaponsel, *nextweaponsel, *primweap, *secondweap, *nextprimweap, *nextsecondweap, *lastattackweapon;

    poshist history; // Previous stored locations of this player

    const char *skin_noteam, *skin_cla, *skin_rvsf;

    float deltayaw, deltapitch, newyaw, newpitch;
    int smoothmillis;

    vec head;

    bool ignored, muted;

    // AI
    int ownernum, level;
    class CBot *pBot;
    playerent *enemy;                      // monster wants to kill this entity
    float targetpitch, targetyaw;          // monster wants to look in this direction

    playerent() : curskin(0), clientnum(-1), lastupdate(0), plag(0), ping(0), address(0), lifesequence(0), frags(0), flagscore(0), deaths(0), points(0), tks(0), lastpain(0), lastvoicecom(0), lasthit(0), clientrole(CR_DEFAULT),
                  attacking(false), delayedscope(false), wantsreload(false), team(TEAM_SPECT), wantsswitch(-1), ads(0), spectatemode(SM_NONE), followplayercn(FPCN_VOID), eardamagemillis(0), respawnoffset(0),
                  prevweaponsel(NULL), weaponsel(NULL), nextweaponsel(NULL), primweap(NULL), secondweap(NULL), nextprimweap(NULL), nextsecondweap(NULL), lastattackweapon(NULL),
                  smoothmillis(-1),
                  head(-1, -1, -1), ignored(false), muted(false),
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
        respawn();
    }

    void removeai();

    virtual ~playerent()
    {
        removeai();
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
        if(gun<0 || gun>WEAP_MAX) return;
        vec push(dir);
        push.mul(damage/100.0f*guns[gun].pushfactor);
        vel.add(push);
        extern int lastmillis;
        if(gun==GUN_GRENADE && damage > 50) eardamagemillis = lastmillis+damage*100;
    }

    void resetspec()
    {
        spectatemode = SM_NONE;
        followplayercn = FPCN_VOID;
    }

    void respawn()
    {
        dynent::reset();
        playerstate::respawn();
        history.reset();
        if(weaponsel) weaponsel->reset();
        else weaponsel = weapons[primary];
        lastaction = 0;
        lastattackweapon = NULL;
        ads = 0;
        wantsswitch = -1;
        scoping = attacking = false;
        extern int lastmillis;
        weaponchanging = lastmillis - weapons[gunselect]->weaponchangetime/2; // 2011jan16:ft: for a little no-shoot after spawn
        resetspec();
        eardamagemillis = 0;
        eyeheight = maxeyeheight;
        curskin = nextskin[team_base(team)];
    }

    void spawnstate(int gamemode, int mutators)
    {
        playerstate::spawnstate(gamemode, mutators);
        prevweaponsel = weaponsel = weapons[gunselect];
        primweap = weapons[primary];
        secondweap = weapons[secondary];
        nextprimweap = weapons[nextprimary];
        curskin = nextskin[team_base(team)];
    }

    void selectweapon(int w) {
        if (weaponsel) prevweaponsel = weaponsel; weaponsel = weapons[(gunselect = w)];
        if (!prevweaponsel) prevweaponsel = weaponsel;
    }
    void setprimary(int w) { primweap = weapons[(primary = w)]; }
    void setsecondary(int w) { secondweap = weapons[(secondary = w)]; }
    void setnextprimary(int w) { nextprimweap = weapons[(nextprimary = w)]; }
    void setnextsecondary(int w) { nextsecondweap = weapons[(nextsecondary = w)]; }
    bool isspectating() { return state==CS_SPECTATE || (state==CS_DEAD && spectatemode > SM_NONE); }
    void weaponswitch(weapon *w)
    {
        if(!w) return;
        if(ads){
			if(this == player1){
				wantsswitch = w->type;
				delayedscope = scoping;
				scoping = false;
			}
			return;
		}
		wantsswitch = -1;
        extern int lastmillis;
        weaponsel->ondeselecting();
        weaponchanging = lastmillis;
        nextweaponsel = w;
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
    bool ack;
    flaginfo() : flagent(0), actor(0), state(CTFF_INBASE), ack(false) {}
};

// nades, gibs

enum { BT_NONE, BT_NADE, BT_GIB };

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

struct hitmsg
{
    int target, lifesequence, info;
    ivec dir;
};

class grenadeent : public bounceent
{
public:
    bool local;
    int nadestate;
    float distsincebounce;
    grenadeent(playerent *owner, int millis = 0);
    ~grenadeent();
    void activate(const vec &from, const vec &to);
    void _throw(const vec &from, const vec &vel);
    void explode();
    void splash();
    virtual void destroy();
    virtual bool applyphysics();
    void moveoutsidebbox(const vec &direction, playerent *boundingbox);
    void oncollision();
    void onmoved(const vec &dist);
};

enum {MD_FRAGS = 0, MD_DEATHS, END_MDS};
struct medalsst {bool assigned; int cn; int item;};

#define MAXKILLMSGLEN 16
extern char killmessages[2][WEAP_MAX][MAXKILLMSGLEN];
inline const char *killmessage(int gun, bool gib = false)
{
    if(gun<0 || gun>=WEAP_MAX) return "";

    return killmessages[gib?1:0][gun];
}

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
