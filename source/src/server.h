// server.h

#define SERVER_BUILTIN_MOD 0
// 1 = super knife (gib only)
// 2 = moon jump (gib only)
// 4 = moon jump always on (requires 2)
// 8 = gungame
// 16 = explosive ammo
// 32 = moonjump with no damage (mario)
// 64 = /suicide for nuke
// 128 = no explosive zombies

#define gamemode smode   // allows the gamemode macros to work with the server mode
#define mutators smuts   // allows the mutators macros to work with the server mode

#define SERVER_PROTOCOL_VERSION    (PROTOCOL_VERSION)    // server with compatible protocol
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
    shotevent(int millis, int id, int weap) : timedevent(GE_SHOT, millis, id), weap(weap) { to = vec(0, 0, 0); pos.setsize(0); }
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

template <int N>
struct projectilestate
{
    int projs[N];
    int numprojs;

    projectilestate() : numprojs(0) {}

    void reset() { numprojs = 0; }

    void add(int val)
    {
        if(numprojs>=N) numprojs = 0;
        projs[numprojs++] = val;
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

struct clientstate : playerstate
{
    vec o;
    int state;
    int lastdeath, spawn, lifesequence;
    bool forced;
    int lastshot;
    projectilestate<6> grenades; // 5000ms TLL / (we can throw one every 650ms+200ms) = 6 nades possible
    projectilestate<3> knives;
    int akimbomillis, crouchmillis, scopemillis, drownmillis;
    bool scoped, crouching, onfloor; float fallz;
    int flagscore, frags, teamkills, deaths, shotdamage, damage, points, events, lastdisc, reconnections;

    clientstate() : state(CS_DEAD) {}

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

    void reset()
    {
        state = CS_DEAD;
        lifesequence = -1;
        grenades.reset();
        knives.reset();
        akimbomillis = 0;
        scoped = forced = false;
        flagscore = frags = teamkills = deaths = shotdamage = damage = points = events = lastdisc = reconnections = 0;
        respawn();
    }

    void respawn()
    {
        playerstate::respawn(gamemode, mutators);
        o = vec(-1e10f, -1e10f, -1e10f);
        lastdeath = 0;
        spawn = 0;
        lastshot = 0;
        akimbomillis = crouchmillis = scopemillis = drownmillis = 0;
        scoped = crouching = onfloor = false;
		fallz = -1e10f;
    }
};

struct savedscore
{
    string name;
    uint ip;
    int frags, flagscore, deaths, teamkills, shotdamage, damage, team, points, events, lastdisc, reconnections;
    bool valid, forced;

    void reset()
    {
        // to avoid 2 connections with the same score... this can disrupt some laggers that eventually produces 2 connections (but it is rare)
        frags = flagscore = deaths = teamkills = shotdamage = damage = points = events = lastdisc = reconnections = 0;
    }

    void save(clientstate &cs, int t)
    {
        frags = cs.frags;
        flagscore = cs.flagscore;
        deaths = cs.deaths;
        teamkills = cs.teamkills;
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
        cs.flagscore = flagscore;
        cs.deaths = deaths;
        cs.teamkills = teamkills;
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

struct medals
{
    int dpt, lasthit, lastgun, ncovers, nhs;
    int combohits, combo, combofrags, combotime, combodamage, ncombos;
    int ask, askmillis, linked, linkmillis, linkreason, upmillis, flagmillis;
    int totalhits, totalshots;
    bool updated, combosend;
    vec pos, flagpos;
    void reset()
    {
        dpt = lasthit = lastgun = ncovers = nhs = 0;
        combohits = combo = combofrags = combotime = combodamage = ncombos = 0;
        askmillis = linkmillis = upmillis = flagmillis = 0;
        linkreason = linked = ask = -1;
        totalhits = totalshots = 0;
        updated = combosend = false;
        pos = flagpos = vec(-1e10f, -1e10f, -1e10f);
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
    int role;
    int connectmillis, lmillis, ldt, spj;
    int mute, spam, lastvc; // server side voice comm spam control
    int acversion, acbuildtype;
    bool isauthed; // for passworded servers
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
    int spawnperm, spawnpermsent;
    int salt;
    string pwd;
    uint authreq; // for AUTH
    string authname; // for AUTH
    int mapcollisions, farpickups;
    enet_uint32 bottomRTT;
    medals md;
    bool upspawnp;
    int lag;
    vec spawnp;
    int nvotes;
    int input, inputmillis;
    int ffire, wn, f, g, t, y, p;
    int yb, pb, oy, op, lda, ldda, fam;
    int nt[10], np, lp, ls, lsm, ld, nd, nlt, lem, led;
    vec cp[10], dp[10], d0, lv, lt, le;
    float dda, tr, sda;
    int ps, ph, tcn, bdt, pws;
    float pr;
    int yls, pls, tls;
    int bs, bt, blg, bp;

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

    const char *gethostname();
    bool hasclient(int cn);

    void removeexplosives();
    void suicide(int weap, int flags = FRAG_NONE);

    void mapchange(bool getmap = false)
    {
        state.reset();
        events.deletecontents();
        timers.deletecontents();
        overflow = 0;
        timesync = false;
        isonrightmap = type == ST_AI || m_edit(gamemode);
        spawnperm = SP_WRONGMAP;
        spawnpermsent = servmillis;
        if(!getmap)
        {
            loggedwrongmap = false;
            freshgame = true;         // permission to spawn at once
        }
        lastevent = 0;
        at3_lastforce = eff_score = 0;
        mapcollisions = farpickups = 0;
        md.reset();
        upspawnp = false;
        lag = 0;
        spawnp = vec(-1e10f, -1e10f, -1e10f);
        lmillis = ldt = spj = 0;
        ffire = 0;
        f = g = y = p = t = 0;
        yb = pb = oy = op = lda = ldda = fam = 0;
        np = lp = ls = lsm = ld = nd = nlt = lem = led = 0;
        d0 = lv = lt = le = vec(0,0,0);
        loopi(10) { cp[i] = dp[i] = vec(0,0,0); nt[i] = 0; }
        dda = tr = sda = 0;
        ps = ph = bdt = pws = 0;
        tcn = -1;
        pr = 0.0f;
        yls = pls = tls = 0;
    }

    void reset()
    {
        name[0] = pwd[0] = demoflags = 0;
        bottomRTT = ping = 9999;
        team = TEAM_SPECT;
        state.state = CS_SPECTATE;
        loopi(2) skin[i] = 0;
        position.setsize(0);
        messages.setsize(0);
        isauthed = haswelcome = false;
        role = CR_DEFAULT;
        lastvotecall = 0;
        lastprofileupdate = fastprofileupdates = 0;
        vote = VOTE_NEUTRAL;
        lastsaytext[0] = '\0';
        saychars = 0;
        spawnindex = -1;
        authreq = 0; // for AUTH
        mapchange();
        freshgame = false;         // don't spawn into running games
        mute = spam = lastvc = badspeech = badmillis = nvotes = 0;
        input = inputmillis = 0;
        wn = -1;
        bs = bt = blg = bp = 0;
    }

    void zap()
    {
        type = ST_EMPTY;
        role = CR_DEFAULT;
        isauthed = haswelcome = false;
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
void disconnect_client(int n, int reason = -1);
void sendservmsg(const char *msg, int cn = -1);
void sendiplist(int receiver, int cn = -1);
int clienthasflag(int cn);
bool refillteams(bool now = false, int ftr = FTR_AUTOTEAM);
void changeclientrole(int client, int role, char *pwd = NULL, bool force=false);
mapstats *getservermapstats(const char *mapname, bool getlayout = false, int *maploc = NULL);
int findmappath(const char *mapname, char *filename = NULL);
int calcscores();
void recordpacket(int chan, void *data, int len);
void senddisconnectedscores(int cn);
void process(ENetPacket *packet, int sender, int chan);
void welcomepacket(packetbuf &p, int n);
void sendwelcome(client *cl, int chan = 1);
void sendpacket(int n, int chan, ENetPacket *packet, int exclude = -1, bool demopacket = false);
int numclients();
bool updateclientteam(int cln, int newteam, int ftr);
void forcedeath(client *cl);
void sendf(int cn, int chan, const char *format, ...);

extern bool isdedicated;
extern string smapname;
extern mapstats smapstats;
extern ssqr *maplayout;

const char *messagenames[SV_NUM] =
{
    "SV_SERVINFO", "SV_WELCOME",
    "SV_INITCLIENT", "SV_INITAI", "SV_CDIS", "SV_DELAI", "SV_REASSIGNAI", "SV_RESUME", "SV_MAPIDENT",
    "SV_CLIENT", "SV_POS", "SV_POSC", "SV_SOUND", "SV_PINGPONG", "SV_CLIENTPING",
    "SV_TEXT", "SV_TEXTPRIVATE", "SV_SWITCHNAME", "SV_SWITCHSKIN", "SV_SETTEAM",
    "SV_CALLVOTE", "SV_CALLVOTESUC", "SV_CALLVOTEERR", "SV_VOTE", "SV_VOTEREMAIN", "SV_VOTERESULT",
    "SV_LISTDEMOS", "SV_SENDDEMOLIST", "SV_GETDEMO", "SV_SENDDEMO", "SV_DEMOPLAYBACK",
    "SV_AUTH_ACR_REQ", "SV_AUTH_ACR_CHAL",
    "SV_CLAIMPRIV", "SV_SETPRIV",
    "SV_SENDMAP", "SV_RECVMAP", "SV_REMOVEMAP",
    "SV_EDITMODE", "SV_EDITH", "SV_EDITT", "SV_EDITS", "SV_EDITD", "SV_EDITE", "SV_EDITENT", "SV_NEWMAP",
    "SV_SHOOT", "SV_SHOOTC", "SV_EXPLODE", "SV_AKIMBO", "SV_RELOAD",
    "SV_SUICIDE", "SV_LOADOUT", "SV_QUICKSWITCH", "SV_WEAPCHANGE", "SV_THROWNADE", "SV_THROWKNIFE",
    "SV_RICOCHET", "SV_HEADSHOT", "SV_REGEN", "SV_HEAL", "SV_BLEED", "SV_STREAKREADY", "SV_STREAKUSE",
    "SV_KNIFEADD", "SV_KNIFEREMOVE",
    "SV_CONFIRMADD", "SV_CONFIRMREMOVE",
    "SV_HUDEXTRAS", "SV_POINTS", "SV_DISCSCORES", "SV_KILL", "SV_DAMAGE",
    "SV_TRYSPAWN", "SV_SPAWNSTATE", "SV_SPAWN", "SV_SPAWNDENY", "SV_FORCEDEATH", "SV_FORCEGIB",
    "SV_ITEMLIST", "SV_ITEMSPAWN", "SV_ITEMACC",
    "SV_FLAGACTION", "SV_FLAGINFO", "SV_FLAGMSG", "SV_FLAGCNT",
    "SV_MAPCHANGE", "SV_NEXTMAP",
    "SV_ARENAWIN", "SV_ZOMBIESWIN", "SV_CONVERTWIN",
    "SV_TIMEUP",
    "SV_GAMEMODE",
    "SV_TEAMDENY", "SV_SERVERMODE",
    "SV_IPLIST",
    "SV_SERVMSG", "SV_EXTENSION",
    "SV_SHOTFX", "SV_ITEMPICKUP",
    "SV_AUTHT", "SV_AUTHREQ", "SV_AUTHTRY", "SV_AUTHANS", "SV_AUTHCHAL"
};

const char *entnames[MAXENTTYPES] =
{
    "none?",
    "light", "playerstart", "pistol", "ammobox","grenades",
    "health", "helmet", "armour", "akimbo",
    "mapmodel", "trigger", "ladder", "ctf-flag", "sound", "clip", "plclip"
};

// see entity.h:61: struct itemstat { int add, start, max, sound; };
// Please update ./ac_website/htdocs/docs/introduction.html if these figures change.
itemstat ammostats[NUMGUNS] =
{
    {  1,  1,   1,  S_ITEMAMMO  },   // knife dummy
    { 20, 60, 100,  S_ITEMAMMO  },   // pistol
    { 15, 30,  30,  S_ITEMAMMO  },   // carbine
    { 14, 28,  21,  S_ITEMAMMO  },   // shotgun
    { 60, 90,  90,  S_ITEMAMMO  },   // subgun
    { 10, 20,  15,  S_ITEMAMMO  },   // sniper
    { 40, 60,  60,  S_ITEMAMMO  },   // assault
    { 30, 45,  75,  S_ITEMAMMO  },   // cpistol
    {  1,  0,   3,  S_ITEMAMMO  },   // grenade
    {100,  0, 100,  S_ITEMAKIMBO}    // akimbo
};

itemstat powerupstats[I_ARMOUR-I_HEALTH+1] =
{
    {33, 0, 100, S_ITEMHEALTH}, // 0 health
    {25, 0, 100, S_ITEMHELMET}, // 1 helmet
    {50, 0, 100, S_ITEMARMOUR}, // 2 armour
};

guninfo guns[NUMGUNS] =
{
    // Please update ./ac_website/htdocs/docs/introduction.html if these figures change.
    //mKR: mdl_kick_rot && mKB: mdl_kick_back
    //reI: recoilincrease && reB: recoilbase && reM: maxrecoil && reF: recoilbackfade
    //pFX: pushfactor
    //modelname                   reload       attackdelay      piercing     part     recoil       mKR       reI          reM           pFX
    //              sound                reloadtime        damage    projspeed  spread     magsize     mKB        reB             reF           isauto
    { "knife",      S_KNIFE,      S_NULL,     0,      500,    50, 100,     0,   0,  1,    1,   1,    0,  0,   0,   0,      0,      0,   1,      false },
    { "pistol",     S_PISTOL,     S_RPISTOL,  1400,   160,    18,   0,     0,   0, 53,   10,   10,   6,  5,   6,  35,     58,     125,  1,      false },
    { "carbine",    S_CARBINE,    S_RCARBINE, 1800,   720,    60,  40,     0,   0, 10,   60,   10,   4,  4,  10,  60,     60,     150,  1,      false },
    { "shotgun",    S_SHOTGUN,    S_RSHOTGUN, 2400,   880,    1,    0,     0,   0,  1,   35,    7,   9,  9,  10, 140,    140,    125,   1,      false },   // CAUTION dmg only sane for server!
    { "subgun",     S_SUBGUN,     S_RSUBGUN,  1650,   80,     16,   0,     0,   0, 45,   15,   30,   1,  2,   5,  25,     50,     188,  1,      true  },
    { "sniper",     S_SNIPER,     S_RSNIPER,  1950,   1500,   82,  25,     0,   0, 50,   50,    5,   4,  4,  10,  85,     85,     100,  1,      false },
    { "assault",    S_ASSAULT,    S_RASSAULT, 2000,   120,    22,   0,     0,   0, 18,   30,   20,   0,  2,   3,  25,     50,     115,  1,      true  },
    { "cpistol",    S_PISTOL,     S_RPISTOL,  1400,   120,    19,   0,     0,   0, 35,   10,   15,   6,  5,   6,  35,     50,     125,  1,      false },   // temporary
    { "grenade",    S_NULL,       S_NULL,     1000,   650,    200,  0,    20,  6,  1,    1,   1,    3,   1,    0,   0,      0,      0,   3,      false },
    { "pistol",     S_PISTOL,     S_RAKIMBO,  1400,   80,     19,   0,     0,   0, 50,   10,   20,   6,  5,   4,  15,     25,     115,  1,      true  },
};

const char *teamnames[TEAM_NUM+1] = {"CLA", "RVSF", "CLA-SPECT", "RVSF-SPECT", "SPECTATOR", "void"};
const char *teamnames_s[TEAM_NUM+1] = {"CLA", "RVSF", "CSPC", "RSPC", "SPEC", "void"};

// for both client and server
// default messages are hardcoded !
char killmessages[2][NUMGUNS][MAXKILLMSGLEN] = {{ "", "busted", "picked off", "peppered", "sprayed", "punctured", "shredded", "busted", "", "busted" }, { "slashed", "", "", "splattered", "", "headshot", "", "", "gibbed", "" }};
