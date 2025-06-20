// server.h

#define SERVER_BUILTIN_MOD 0
// bitwise disjunction/OR (|) or sum (+) of following:
// 1: moon jump
// 2: moon jump always on, not just gib only (requires 1)
// 4: moon jump Mario - no damage allowed (requires 1)
// 8: gungame
// 16: explosive ammo
// 32: super knife (gib only)
// 64: /suicide for nuke
// 128: no explosive zombies
// 256: infinite ammo

#define gamemode smode   // allows the gamemode macros to work with the server mode
#define mutators smuts   // allows the mutators macros to work with the server mode

#define SERVER_PROTOCOL_VERSION    PROTOCOL_VERSION    // server with compatible protocol
//#define SERVER_PROTOCOL_VERSION   (-PROTOCOL_VERSION)  // server with gameplay modification but compatible to vanilla client (NOT USED)
//#define SERVER_PROTOCOL_VERSION  (PROTOCOL_VERSION)    // server with incompatible protocol (change PROTOCOL_VERSION in file protocol.h to a negative number!)

#define valid_flag(f) (f >= 0 && f < 2)

enum { GE_NONE = 0, /* sequenced */ GE_SHOT, GE_PROJ, GE_HIT, GE_AKIMBO, GE_RELOAD, /* immediate */ GE_SUICIDEBOMB, /* unsequenced */ GE_HEAL, GE_AIRSTRIKE };
enum { ST_EMPTY, ST_LOCAL, ST_TCPIP, ST_AI };

extern int smode, smuts, servmillis;

struct posinfo
{
    int cn;
    vec o, head;
};

struct timedevent
{
    bool valid;
    int type, millis, id;
    timedevent(int type, int millis, int id) : valid(true), type(type), millis(millis), id(id) { }
    virtual ~timedevent() {}
    virtual bool flush(struct client *ci, int fmillis);
    virtual void process(struct client *ci) = 0;
};

struct shotevent : timedevent
{
    int weap;
    vec to;
    vector<posinfo> pos;
    shotevent(int millis, int id, int weap) : timedevent(GE_SHOT, millis, id), weap(weap), to(0,0,0), compact(false) { pos.setsize(0); }
    bool compact;
    void process(struct client *ci);
};

struct destroyevent : timedevent
{
    int weap, flags;
    vec o;
    destroyevent(int millis, int id, int weap, int flags, const vec &o) : timedevent(GE_PROJ, millis, id), weap(weap), flags(flags), o(o) {}
    void process(struct client *ci);
};

// switchevent?

struct akimboevent : timedevent
{
    akimboevent(int millis, int id) : timedevent(GE_AKIMBO, millis, id) {}
    void process(struct client *ci);
};

struct reloadevent : timedevent
{
    int weap;
    reloadevent(int millis, int id, int weap) : timedevent(GE_RELOAD, millis, id), weap(weap) {}
    void process(struct client *ci);
};

// unordered

struct healevent : timedevent
{
    int hp;
    healevent(int millis, int actor, int hp) : timedevent(GE_HEAL, millis, actor), hp(hp) {}
    void process(client *ci);
};

struct suicidebomberevent : timedevent
{
    suicidebomberevent(int actor) : timedevent(GE_SUICIDEBOMB, 0, actor) {}
    void process(client *ci);
};

struct airstrikeevent : timedevent
{
    vec o;
    airstrikeevent(int millis, const vec &o) : timedevent(GE_AIRSTRIKE, millis, 0), o(o) {}
    void process(client *ci);
};

template <int N>
struct projectilestate
{
    int projs[N];
    int numprojs;
    int throwable;

    projectilestate() : /*projs(),*/ numprojs(0), throwable(0) {}

    void reset() { numprojs = 0; }

    void add(int val)
    {
        if(numprojs>=N) numprojs = 0;
        projs[numprojs++] = val;
        ++throwable;
    }

    bool removeany()
    {
        if (!numprojs) return false;
        --numprojs;
        return true;
    }

    bool remove(int val)
    {
        loopi(numprojs) if(projs[i]==val)
        {
            projs[i] = projs[--numprojs];
            return true;
        }
        return false;
    }
};

static const int DEATHMILLIS = 300;

struct wound
{
    int inflictor, lastdealt;
    vec offset;
};

#if (SERVER_BUILTIN_MOD & 8) == 8
const int gungame[] =
{
    GUN_ASSAULT2,
    GUN_SNIPER3,
    GUN_SNIPER,
    GUN_SNIPER2,
    GUN_ASSAULT,
    GUN_SUBGUN,
    GUN_BOLT,
    GUN_SHOTGUN,
    GUN_PISTOL,
    GUN_PISTOL2,
    GUN_RPG,
    GUN_HEAL, // nuke after killing with this
};
const int GUNGAME_MAX = sizeof(gungame) / sizeof(*gungame);
#endif

struct clientstate : playerstate
{
    vec o, vel, sg[SGRAYS], flagpickupo;
    int state;
    int lastdeath, lifesequence;
    bool forced;
    int lastshot, lastkill, combo;
    projectilestate<6> grenades; // 5000ms TLL / (we can throw one every 650ms+200ms) = 6 nades possible
    projectilestate<3> knives;
    int akimbomillis, crouchmillis, scopemillis, drownmillis, drownval;
    bool scoped, crouching, onfloor; float fallz;
    int flagscore, frags, assists, deaths, shotdamage, damage, points, events, lastdisc, reconnections;
    int smallflags, pointstreak, deathstreak, airstrikes, radarearned, nukemillis, streakused, streakondeath;
    vector<int> damagelog, revengelog;
    vector<wound> wounds;
    bool valid;
#if (SERVER_BUILTIN_MOD & 8)
    int gungame;
#endif

    clientstate() : state(CS_DEAD), valid(true) {}

    bool isalive(int gamemillis)
    {
        return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
    }

    bool waitexpired(int gamemillis)
    {
        int wait = gamemillis - lastshot;
        loopi(NUMGUNS) if(wait < gunwait[i]) return false;
        return true;
    }

    void updateshot(int gamemillis)
    {
        const int wait = gamemillis - lastshot;
        loopi(NUMGUNS)
            if (gunwait[i])
                gunwait[i] = max(gunwait[i] - wait, 0);
        lastshot = gamemillis;
    }

    inline clientstate &invalidate()
    {
        valid = false;
        return *this;
    }

    void reset()
    {
        state = CS_DEAD;
        lifesequence = -1;
        grenades.reset();
        knives.reset();
        akimbomillis = 0;
        scoped = forced = false;
        flagscore = frags = assists = deaths = shotdamage = damage = points = events = lastdisc = reconnections = 0;
        smallflags = pointstreak = deathstreak = airstrikes = radarearned = nukemillis = streakused = 0;
        streakondeath = -1;
        revengelog.setsize(0);
        valid = true;
        respawn();
#if (SERVER_BUILTIN_MOD & 8)
        gungame = 0;
#endif
    }

    void respawn()
    {
        playerstate::respawn(gamemode, mutators);
        o = vec(-1e10f, -1e10f, -1e10f);
        vel = vec(0, 0, 0);
        lastdeath = 0;
        lastshot = lastkill = combo = 0;
        akimbomillis = crouchmillis = scopemillis = drownmillis = drownval = 0;
        scoped = crouching = onfloor = false;
        fallz = -1e10f;
        damagelog.setsize(0);
        wounds.shrink(0); // no more wounds!
    }

    float crouchfactor(int gamemillis)
    {
        int crouched;
        if (crouching)
            crouched = min(gamemillis - crouchmillis, CROUCHTIME);
        else
            crouched = max(CROUCHTIME - gamemillis + crouchmillis, 0);
        return 1.f - crouched * (1.f - CROUCHHEIGHTMUL) / CROUCHTIME;
    }

    void addwound(int owner, const vec &woundloc);
};

struct savedscore
{
    string name;
    uint ip;
    int frags, assists, flagscore, deaths, shotdamage, damage, team, points, events, lastdisc, reconnections;
    bool valid, forced;

    void reset()
    {
        // to avoid 2 connections with the same score... this can disrupt some laggers that eventually produces 2 connections (but it is rare)
        frags = assists = flagscore = deaths = shotdamage = damage = points = events = lastdisc = reconnections = 0;
    }

    void save(clientstate &cs, int t)
    {
        frags = cs.frags;
        assists = cs.assists;
        flagscore = cs.flagscore;
        deaths = cs.deaths;
        shotdamage = cs.shotdamage;
        damage = cs.damage;
        points = cs.points;
        forced = cs.forced;
        events = cs.events;
        lastdisc = cs.lastdisc;
        reconnections = cs.reconnections;
        team = t;
        valid = true;
    }

    void restore(clientstate &cs)
    {
        cs.frags = frags;
        cs.assists = assists;
        cs.flagscore = flagscore;
        cs.deaths = deaths;
        cs.shotdamage = shotdamage;
        cs.damage = damage;
        cs.points = points;
        cs.forced = forced;
        cs.events = events;
        cs.lastdisc = lastdisc;
        cs.reconnections = reconnections;
        reset();
    }
};

struct client                   // server side version of "dynent" type
{
    int type;
    int clientnum, ownernum, bot_seed;
    ENetPeer *peer;
    string hostname;
    string name;
    int team;
    char lang[3];
    int ping;
    int skin[2], level;
    int vote;
    int role, authpriv;
    int connectmillis, lmillis, ldt, spj;
    int mute, spam, lastvc; // server side voice comm spam control
    int acversion, acbuildtype, acthirdperson, acguid;
    bool isauthed; // for passworded servers
    bool connectauth;
    bool haswelcome;
    bool isonrightmap, loggedwrongmap, freshgame;
    bool timesync;
    int overflow;
    int gameoffset, lastevent, lastvotecall;
    int lastprofileupdate, fastprofileupdates;
    int demoflags;
    clientstate state;
    vector<timedevent *> events, timers;
    vector<uchar> position, messages;
    string lastsaytext;
    int saychars, lastsay, spamcount, badspeech, badmillis;
    int at3_score, at3_lastforce, eff_score;
    bool at3_dontmove;
    int spawnindex;
    int salt;
    string pwd;
    uint authtoken, authuser, authreq;
    int masterdisc;
    string authname;
    int mapcollisions, farpickups;
    enet_uint32 bottomRTT;
    vec spawnp;
    int nvotes;
    int input, inputmillis;
    int f, g, t, y, p;
#if (SERVER_BUILTIN_MOD & 64)
    bool nuked;
#endif

    void addevent(timedevent *e)
    {
        if (events.length() < 256) events.add(e);
        else delete e;
    }

    void addtimer(timedevent *e)
    {
        if (timers.length() < 256) timers.add(e);
        else delete e;
    }

    void invalidateheals()
    {
        loopv(timers)
            if (timers[i]->type == GE_HEAL)
                timers[i]->valid = false;
    }

    int getmillis(int millis, int id)
    {
        if (!timesync || (!events.length() && state.waitexpired(millis)))
        {
            timesync = true;
            gameoffset = millis - id;
            return millis;
        }
        return gameoffset + id;
    }

    const char *formatname();
    const char *gethostname();
    bool hasclient(int cn);

    void removeexplosives();
    void suicide(int weap, int flags = FRAG_NONE);
    void cheat(const char *reason);

    void mapchange(bool getmap = false)
    {
        state.reset();
        events.deletecontents();
        timers.deletecontents();
        overflow = 0;
        timesync = false;
        isonrightmap = type == ST_AI || m_edit(gamemode);
        if(!getmap)
        {
            loggedwrongmap = false;
            freshgame = true;         // permission to spawn at once
        }
        lastevent = 0;
        at3_lastforce = eff_score = 0;
        mapcollisions = farpickups = 0;
        spawnp = vec(-1e10f, -1e10f, -1e10f);
        lmillis = ldt = spj = 0;
        f = g = y = p = t = 0;
#if (SERVER_BUILTIN_MOD & 64)
        nuked = false;
#endif
    }

    void reset()
    {
        name[0] = pwd[0] = demoflags = 0;
        bottomRTT = ping = 9999;
        team = TEAM_SPECT;
        state.state = CS_DEAD;
        loopi(2) skin[i] = 0;
        position.setsize(0);
        messages.setsize(0);
        isauthed = connectauth = haswelcome = false;
        role = CR_DEFAULT;
        authpriv = -1;
        lastvotecall = 0;
        lastprofileupdate = fastprofileupdates = 0;
        vote = VOTE_NEUTRAL;
        lastsaytext[0] = '\0';
        saychars = 0;
        spawnindex = -1;
        authtoken = authuser = authreq = 0;
        authname[0] = '\0';
        masterdisc = DISC_NONE;
        mapchange();
        freshgame = false;         // don't spawn into running games
        mute = spam = lastvc = badspeech = badmillis = nvotes = 0;
        input = inputmillis = 0;
    }

    void zap()
    {
        type = ST_EMPTY;
        role = authpriv = CR_DEFAULT;
        isauthed = connectauth = haswelcome = false;
    }
};

struct savedlimit
{
    enet_uint32 ip;
    int lastvotecall;
    int saychars, lastsay, spamcount;
#if (SERVER_BUILTISV_MOD & 64)
    bool nuked;
#endif

    void save(client &cl)
    {
        lastvotecall = cl.lastvotecall;
        saychars = cl.saychars;
        lastsay = cl.lastsay;
        spamcount = cl.spamcount;
#if (SERVER_BUILTIN_MOD & 64)
        nuked = cl.nuked;
#endif
    }

    void restore(client &cl)
    {
        cl.lastvotecall = lastvotecall;
        cl.saychars = saychars;
        cl.lastsay = lastsay;
        cl.spamcount = spamcount;
#if (SERVER_BUILTIN_MOD & 64)
        cl.nuked = nuked;
#endif
    }
};

struct ban
{
    ENetAddress address;
    int millis, type;
};

struct worldstate
{
    enet_uint32 uses;
    vector<uchar> positions, messages;
};

struct clientidentity
{
    uint ip;
    int clientnum;
};

struct sflaginfo
{
    int state;
    int actor_cn;
    int drop_cn, dropmillis;
    float pos[3];
    int lastupdate;
    int stolentime;
    int damage, damagetime;
    short x, y;          // flag entity location
} sflaginfos[2];

struct ssecure
{
    int id, team, enemy, overthrown;
    vec o;
    int last_service;
};
vector<ssecure> ssecures;

struct sconfirm
{
    int id, team, actor, target;
    int points, frag, death;
    vec o;
};

struct sknife
{
    int id, millis;
    vec o;
};

struct demofile
{
    string info;
    string file;
    uchar *data;
    int len;
    vector<clientidentity> clientssent;
};

void clearai(), checkai();

void startgame(const char *newname, int newmode, int newmuts, int newtime = -1, bool notify = true);
void disconnect_client(client &cl, int reason = -1);
void sendservmsg(const char *msg, client *cl = NULL);
int clienthasflag(int cn);
void convertcheck(bool quick = false);
void shuffleteams(int ftr = FTR_AUTO);
bool refillteams(bool now = false, int ftr = FTR_AUTO);
void setpriv(client &cl, int priv);
mapstats *getservermapstats(const char *mapname, bool getlayout = false, int *maploc = NULL);
int findmappath(const char *mapname, char *filename = NULL);
int calcscores();
void recordpacket(int chan, void *data, int len);
void senddisconnectedscores(client *cl = NULL);
void process(ENetPacket *packet, int sender, int chan);
void welcomepacket(packetbuf &p, client *c);
void sendwelcome(client &cl, int chan = 1);
void sendpacket(client *cl, int chan, ENetPacket *packet, int exclude = -1, bool demopacket = false);
int numclients();
bool canspawn(const client &c, bool connecting = false);
bool updateclientteam(client &cl, int newteam, int ftr);
void forcedeath(client &cl);
void sendf(client *cl, int chan, const char *format, ...);
void addpt(client &c, int points, int reason = -1);
void serverdied(client &target, client &actor, int damage, int gun, int style, const vec &source, float killdist = 0);
void serverdamage(client &target, client &actor, int damage, int gun, int style, const vec &source, float dist = 0);
int explosion(client &owner, const vec &o2, int weap, bool teamcheck, bool gib = true, client *cflag = NULL);
void nuke(client &owner, bool suicide = true, bool forced_all = true, bool friendly_fire = false);

extern bool isdedicated;
extern string smapname;
extern mapstats smapstats;
extern ssqr *maplayout;

const char *messagenames[SV_NUM] =
{
    "SV_SERVINFO", "SV_WELCOME",
    "SV_INITCLIENT", "SV_INITAI", "SV_CDIS", "SV_DELAI", "SV_REASSIGNAI", "SV_RESUME", "SV_MAPIDENT",
    "SV_CLIENT", "SV_POS", "SV_POSC", "SV_SOUND", "SV_PINGPONG", "SV_CLIENTPING",
    "SV_TEXT", "SV_TYPING", "SV_WHOIS", "SV_SWITCHNAME", "SV_SWITCHSKIN", "SV_THIRDPERSON", "SV_LEVEL", "SV_SETTEAM",
    "SV_CALLVOTE", "SV_CALLVOTEERR", "SV_VOTE", "SV_VOTESTATUS", "SV_VOTERESULT",
    "SV_LISTDEMOS", "SV_GETDEMO", "SV_DEMOPLAYBACK",
    "SV_AUTH_ACR_REQ", "SV_AUTH_ACR_CHAL",
    "SV_CLAIMPRIV", "SV_SETPRIV",
    "SV_SENDMAP", "SV_RECVMAP", "SV_REMOVEMAP",
    "SV_EDITMODE", "SV_EDITH", "SV_EDITT", "SV_EDITS", "SV_EDITD", "SV_EDITE", "SV_EDITW", "SV_EDITENT", "SV_NEWMAP",
    "SV_SHOOT", "SV_SHOOTC", "SV_EXPLODE", "SV_AKIMBO", "SV_RELOAD",
    "SV_SUICIDE", "SV_LOADOUT", "SV_QUICKSWITCH", "SV_WEAPCHANGE", "SV_THROWNADE", "SV_THROWKNIFE",
    "SV_SG", "SV_RICOCHET", "SV_HEADSHOT", "SV_REGEN", "SV_HEAL", "SV_BLEED", "SV_STREAKREADY", "SV_STREAKUSE",
    "SV_KNIFEADD", "SV_KNIFEREMOVE",
    "SV_CONFIRMADD", "SV_CONFIRMREMOVE",
    "SV_POINTS", "SV_SCORE", "SV_TEAMSCORE", "SV_DISCSCORES", "SV_KILL", "SV_DAMAGE", "SV_DAMAGEOBJECTIVE",
    "SV_TRYSPAWN", "SV_SPAWNSTATE", "SV_SPAWN", "SV_FORCEDEATH",
    "SV_ITEMSPAWN", "SV_ITEMACC",
    "SV_DROPFLAG", "SV_FLAGINFO", "SV_FLAGMSG", "SV_FLAGSECURE", "SV_FLAGOVERLOAD",
    "SV_MAPCHANGE",
    "SV_ARENAWIN", "SV_ZOMBIESWIN", "SV_CONVERTWIN",
    "SV_TIMEUP",
    "SV_TEAMDENY", "SV_SERVERMODE",
    "SV_SERVMSG", "SV_EXTENSION",
};

const char *entnames[MAXENTTYPES] =
{
    "none?",
    "light", "playerstart", "pistol", "ammobox","grenades",
    "health", "helmet", "armour", "akimbo",
    "mapmodel", "trigger", "ladder", "ctf-flag", "sound", "clip", "plclip"
};

// pickup stats
itemstat ammostats[NUMGUNS] =
{
    { 1, 1, 2, S_ITEMAMMO },    // knife "dummy"
    { 2, 5, 6, S_ITEMAMMO },    // pistol
    { 21, 28, 42, S_ITEMAMMO }, // shotgun
    { 3, 4, 6, S_ITEMAMMO },    // subgun
    { 1, 2, 3, S_ITEMAMMO },    // m21
    { 3, 4, 6, S_ITEMAMMO },    // m16
    { 1, 1, 3, S_ITEMAMMO },    // grenade
    { 4, 0, 6, S_ITEMAKIMBO },  // akimbo
    { 2, 3, 4, S_ITEMAMMO },    // bolt sniper
    { 4, 6, 8, S_ITEMAMMO },    // heal
    { 1, 1, 1, S_ITEMAMMO },    // sword dummy
    { 1, 3, 4, S_ITEMAMMO },    // RPG
    { 3, 4, 6, S_ITEMAMMO },    // ak47
    { 2, 3, 4, S_ITEMAMMO },    // m82
    { 3, 4, 5, S_ITEMAMMO },    // mk12
    { 2, 5, 6, S_ITEMAMMO },    // m1911
    { 3, 4, 6, S_ITEMAMMO },    // m16 pro
    { 21, 28, 42, S_ITEMAMMO }, // shotgun pro
    { 6, 8, 12, S_ITEMAMMO },   // ACR pro
};

itemstat powerupstats[I_ARMOUR-I_HEALTH+1] =
{
    { 33 * HEALTHSCALE, STARTHEALTH, MAXHEALTH, S_ITEMHEALTH }, // 0 health
    { 25,               STARTARMOUR, MAXARMOUR, S_ITEMHELMET }, // 1 helmet
    { 50,               STARTARMOUR, MAXARMOUR, S_ITEMARMOUR }, // 2 armour
};

guninfo guns[NUMGUNS] =
{
    //mKR: mdl_kick_rot && mKB: mdl_kick_back
    //reB: recoil && reM: maxrecoil && reF: recoilbackfade && reA: recoilangle
    //pFX: pushfactor
    // modelname                reload     attackdelay    range rangesub    spread      kick magsize   mKB     reM      reA    isauto
    //             sound             reloadtime     damage  endrange piercing spreadrem  addsize    mKR    reB      reF     pfX             mulset
    { "knife",    S_KNIFE,    S_ITEMAMMO,     0,  500, 80,   4,   5, 72, 100,   1,   0,   1,  0,  1, 0, 0,   0,   0, 100,  0, 3, true,  MUL_NORMAL },
    { "pistol",   S_PISTOL,   S_RPISTOL,   1400,   90, 32,  24,  90,  8,   0,  90,  90,   9, 12, 13, 6, 2,  32,  48, 100, 70, 1, false, MUL_NORMAL },
    { "shotgun",  S_SHOTGUN,  S_RSHOTGUN,   750,  200, 10,   6,  16,  7,   0, 190,   9,  12,  1,  6, 9, 5,  75,  83, 100,  5, 2, false, MUL_SHOT   },
    { "subgun",   S_SUBGUN,   S_RSUBGUN,   2400,   67, 35,  20,  64, 20,   0,  70,  93,   4, 32, 33, 1, 3,  36,  60, 100, 65, 1, true,  MUL_NORMAL },
    { "sniper",   S_SNIPER,   S_RSNIPER,   2000,  120, 45,  70, 110,  9,   0, 235,  96,  14, 20, 21, 4, 4,  65,  74, 100, 75, 2, false, MUL_SNIPER },
    { "assault",  S_ASSAULT,  S_RASSAULT,  2100,   73, 28,  45,  92,  9,   0,  65,  95,   3, 30, 31, 0, 3,  25,  42, 100, 60, 1, true,  MUL_NORMAL },
    { "grenade",  S_NULL,     S_NULL,      1000,  650, 220,  0,  55, 27,   0,   1,   0,   1,  0,  1, 3, 1,   0,   0, 100,  0, 3, false, MUL_NORMAL },
    { "pistol",   S_PISTOL,   S_RAKIMBO,   1400,   80, 32,  30,  90,  8,   0,  60,   0,   8, 24, 26, 6, 2,  28,  49, 100, 72, 2, true,  MUL_NORMAL },
    { "bolt",     S_BOLT,     S_RBOLT,     2000, 1500, 120, 80, 130, 48,  40, 250,  97,  36,  8,  9, 4, 4, 110, 135, 100, 80, 3, false, MUL_SNIPER },
    { "heal",     S_HEAL,     S_NULL,      1200,  100, 20,   4,   8, 10,   0,  50,   1,   1, 10, 11, 0, 0,  10,  20, 100,  8, 4, true,  MUL_NORMAL },
    { "sword",    S_SWORD,    S_NULL,         0,  480, 90,   7,   9, 81, 100,   1,   0,   1,  0,  1, 0, 2,   0,   0, 100,  0, 0, true,  MUL_NORMAL },
    { "rpg",      S_RPG,      S_NULL,      2000,  120, 190,  0,  32, 15,  50, 200,  75,   3,  1,  1, 3, 1,  48,  50, 100,  0, 2, false, MUL_NORMAL },
    { "assault2", S_ASSAULT2, S_RASSAULT2, 2000,  100, 42,  48, 120, 12,   0, 150,  94,   3, 30, 31, 0, 3,  42,  62, 100, 62, 1, true,  MUL_NORMAL },
    { "sniper2",  S_SNIPER2,  S_RSNIPER2,  2000,  120, 110, 75, 120, 45,  35, 300,  98, 120, 10, 11, 4, 4,  95,  96, 100, 85, 5, false, MUL_SNIPER },
    { "sniper3",  S_SNIPER3,  S_RSNIPER3,  2100,  120, 36,  65, 100, 16,   0, 235,  96,   3, 20, 21, 0, 3,  47,  59, 100, 62, 1, true,  MUL_SNIPER },
    { "pistol2",  S_PISTOL2,  S_RPISTOL2,  1400,  110, 42,  26,  96, 14,   0, 130,  92,  10,  7,  8, 6, 2,  48,  54, 100, 70, 1, false, MUL_NORMAL },
    { "assault",  S_ASSAULT,  S_RASSAULT,  2100,   73, 49,  45,  92,  9,   0,  65,  95,   3, 30, 31, 0, 3,  25,  42, 100, 60, 1, true,  MUL_PRO    },
    { "shotgun",  S_SHOTGUN,  S_RSHOTGUN,   750,  200, 10,   6,  16,  7,   0, 190,   9,  12,  1,  6, 9, 5,  75,  83, 100,  5, 2, false, MUL_PRO    },
    { "assault",  S_ASSAULT2, S_RASSAULT2, 2000,   75,  1,   0,   0,  0, 100,  65,  95,   3,600,601, 0, 3,  25,  42, 100, 60, 1, true,  MUL_PRO2   },
};

const mul muls[MUL_NUM] =
{
    // torso, head
    {  1.2f,  5.5f }, // normal
    {  1.4f,  4.0f }, // sniper
    {  1.3f,  5.0f }, // shotgun
    {  0.0f,  1.0f }, // pro (0 for legs)
    { 10.0f, 100.f }, // pro
};

int effectiveDamage(int gun, float dist, bool explosive, bool useReciprocal)
{
    if(gun == GUN_ACR_PRO)
        return 1; // 0.1 damage

    if (dist <= guns[gun].range)
        return guns[gun].damage * HEALTHSCALE;
    else if (dist >= guns[gun].endrange)
        return explosive ? 0 : (guns[gun].damage - guns[gun].rangesub) * HEALTHSCALE;
    else
    {
        float subtractfactor = (dist - guns[gun].range) / (guns[gun].endrange - guns[gun].range);
        if (explosive)
        {
            if (useReciprocal)
                return guns[gun].damage / (1 + (guns[gun].damage - 1)*powf(subtractfactor, 4)) * HEALTHSCALE;
            else
            {
                // rangesub becomes the new rangeend
                if (dist >= guns[gun].rangesub) return 0;
                subtractfactor = (dist - guns[gun].range) / (guns[gun].rangesub - guns[gun].range);
                return guns[gun].damage * (1 - subtractfactor * subtractfactor) * HEALTHSCALE;
            }
        }
        else
            return (guns[gun].damage - subtractfactor * guns[gun].rangesub) * HEALTHSCALE;
    }
}

inline const char *weapname(int weap)
{
    const char *weapnames[NUMGUNS] =
    {
        "Knife",
        "USP",
        "M1014",
        "MP5",
        "M21",
        "M16A3",
        "M67",
        "Akimbo",
        "Intervention",
        "Heal",
        "Sword",
        "RPG-7",
        "AK-47",
        "M82",
        "MK12",
        "M1911",
        "M16 Pro",
        "M1014 Pro",
        "ACR Pro",
    };
    return weapnames[weap];
}
const char *suicname(int obit)
{
    if (obit >= 0 && obit < NUMGUNS)
        return weapname(obit);
    switch (obit)
    {
        case OBIT_DEATH:
            return "K";
        case OBIT_BOT:
            return "Bot";
        case OBIT_SPAWN:
            return "Spawn";
        case OBIT_FF:
            return "FF";
        case OBIT_DROWN:
            return "Drown";
        case OBIT_CHEAT:
            return "hax";
        case OBIT_FALL_WATER:
            return "Splash";
        case OBIT_FALL:
            return "Fall";
        case OBIT_NUKE:
            return "Nuke";

        case OBIT_TEAM:
            return "Team";
        case OBIT_SPECT:
            return "Spect";

        case OBIT_REVIVE:
            return "Revive";
    }
    return "x";
}
const char *killname(int obit, int style)
{
    switch (obit)
    {
        case GUN_KNIFE:
            if (style & FRAG_GIB) break;
            else if (style & FRAG_FLAG)
                return "Throwing Knife";
            else
                return "Bleed";
        case GUN_RPG:
            if (style & FRAG_GIB)
                return "RPG Impact";
            else if (style & FRAG_FLAG)
                return "RPG Direct";
            break;
        case GUN_GRENADE:
            if (!(style & FRAG_GIB))
                return "Airstrike";
            break;
        case OBIT_FALL:
            return "Jump";
        case OBIT_NUKE:
            return "Nuke";
        case OBIT_ASSIST:
            return "Assist";
        case OBIT_REVIVE:
            return "Revive";
        case OBIT_JUG:
            return "Juggernaut";
    }
    if (obit >= 0 && obit < NUMGUNS)
        return weapname(obit);
    return "x";
}
bool isheadshot(int weapon, int style)
{
    if (!(style & FRAG_GIB)) return false; // only gibs headshot
    switch (weapon)
    {
        case GUN_KNIFE:
        case GUN_SWORD:
        case GUN_GRENADE:
        case GUN_RPG:
            if (style & FRAG_FLAG) break; // these weapons headshot if FRAG_FLAG is set
        case OBIT_DEATH:
        case OBIT_NUKE:
            return false; // these weapons cannot headshot
    }
    // headshot = gib if otherwise
    return true;
}

float gunspeed(int gun, float zoomed, bool lightweight)
{
    float ret = lightweight ? 1.07f : 1;
    ret *= 1 - zoomed / (lightweight ? 3.5f : 3.f);
    switch (gun){
        case GUN_KNIFE:
        case GUN_PISTOL:
        case GUN_GRENADE:
        case GUN_HEAL:
        case GUN_PISTOL2:
            //ret *= 1;
            break;
        case GUN_SWORD:
            ret *= .98f;
            break;
        case GUN_AKIMBO:
            ret *= .96f;
            break;
        case GUN_SNIPER:
        case GUN_BOLT:
        case GUN_SNIPER2:
            ret *= .93f;
            break;
        case GUN_SHOTGUN:
        case GUN_SUBGUN:
            ret *= .93f;
            break;
        case GUN_ASSAULT:
        case GUN_ASSAULT2:
        case GUN_SNIPER3:
        case GUN_RPG:
            ret *= .92f;
            break;
    }
    return ret;
}

const char *teamnames[TEAM_NUM+1] = {"CLA", "RVSF", "CLA-SPECT", "RVSF-SPECT", "SPECTATOR", "void"};
const char *teamnames_s[TEAM_NUM+1] = {"CLA", "RVSF", "CSPC", "RSPC", "SPEC", "void"};
