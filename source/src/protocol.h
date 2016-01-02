#define MAXCLIENTS 256                  // in a multiplayer game, can be arbitrarily changed
#define DEFAULTCLIENTS 12
#define MAXTRANS 5000                   // max amount of data to swallow in 1 go
#define CUBE_DEFAULT_SERVER_PORT 28770
#define CUBE_SERVINFO_PORT_LAN 28778
#define CUBE_SERVINFO_PORT(serverport) (serverport+1)
#define CUBE_SERVINFO_TO_SERV_PORT(servinfoport) (servinfoport-1)
#define PROTOCOL_VERSION 139            // bump when protocol changes (use negative numbers for mods!)
#define DEMO_VERSION 2                  // bump when demo format changes
#define DEMO_MAGIC "ACR_REPLAY_FILE!"
#define DEMO_MINTIME 10000              // don't keep demo recordings with less than 10 seconds
#define MAXMAPSENDSIZE 65536
#define MAXCFGFILESIZE 65536

// network messages codes, c2s, c2c, s2c
enum
{
    // general
    SV_SERVINFO = 0, SV_WELCOME, // before connection is complete
    SV_INITCLIENT, SV_INITAI, SV_CDIS, SV_DELAI, SV_REASSIGNAI, SV_RESUME, SV_MAPIDENT, // connection and disconnection
    SV_CLIENT, SV_POS, SV_POSC, SV_SOUND, SV_PINGPONG, SV_CLIENTPING, // automatic from client
    SV_TEXT, SV_TEXTPRIVATE, SV_WHOIS, SV_SWITCHNAME, SV_SWITCHSKIN, SV_THIRDPERSON, SV_LEVEL, SV_SETTEAM, // user-initiated
    SV_CALLVOTE, SV_CALLVOTEERR, SV_VOTE, SV_VOTESTATUS, SV_VOTERESULT, // votes
    SV_LISTDEMOS, SV_GETDEMO, SV_DEMOPLAYBACK, // demos
    SV_AUTH_ACR_REQ, SV_AUTH_ACR_CHAL, // auth
    SV_CLAIMPRIV, SV_SETPRIV, // privileges
    SV_SENDMAP, SV_RECVMAP, SV_REMOVEMAP, // map transit
    // for editmode ONLY
    SV_EDITMODE, SV_EDITH, SV_EDITT, SV_EDITS, SV_EDITD, SV_EDITE, SV_EDITW, SV_EDITENT, SV_NEWMAP,
    // game events
    SV_SHOOT, SV_SHOOTC, SV_EXPLODE, SV_AKIMBO, SV_RELOAD, // client to server
    SV_SUICIDE, SV_LOADOUT, SV_QUICKSWITCH, SV_WEAPCHANGE, SV_THROWNADE, SV_THROWKNIFE, // directly handled
    SV_SG, SV_RICOCHET, SV_HEADSHOT, SV_REGEN, SV_HEAL, SV_BLEED, SV_STREAKREADY, SV_STREAKUSE, // server to client
    SV_KNIFEADD, SV_KNIFEREMOVE, // knives
    SV_CONFIRMADD, SV_CONFIRMREMOVE, // kill confirmed
    // gameplay
    SV_POINTS, SV_SCORE, SV_TEAMSCORE, SV_DISCSCORES, SV_KILL, SV_DAMAGE, SV_DAMAGEOBJECTIVE, // scoring
    SV_TRYSPAWN, SV_SPAWNSTATE, SV_SPAWN, SV_FORCEDEATH, // spawning
    SV_ITEMSPAWN, SV_ITEMACC, // items
    SV_DROPFLAG, SV_FLAGINFO, SV_FLAGMSG, SV_FLAGSECURE, SV_FLAGOVERLOAD, // flags
    SV_MAPCHANGE, // maps
    SV_ARENAWIN, SV_ZOMBIESWIN, SV_CONVERTWIN, // round end/remaining
    SV_TIMEUP,
    // ???
    SV_TEAMDENY, SV_SERVERMODE,
    SV_SERVMSG, SV_EXTENSION,
    SV_NUM, SV_CONNECT = SV_NUM
};

#ifdef _DEBUG

extern void protocoldebug(bool enable);

// converts message code to char
extern const char *messagenames[SV_NUM];
#endif

enum { SA_KICK = 0, SA_BAN, SA_REMBANS, SA_MASTERMODE, SA_AUTOTEAM, SA_FORCETEAM, SA_GIVEADMIN, SA_MAP, SA_RECORDDEMO, SA_STOPDEMO, SA_CLEARDEMOS, SA_SERVERDESC, SA_SHUFFLETEAMS, SA_BOTBALANCE, SA_SUBDUE, SA_REVOKE, SA_NUM };
enum { VOTE_NEUTRAL = 0, VOTE_YES, VOTE_NO, VOTE_NUM };
enum { VOTEE_DISABLED = 0, VOTEE_CUR, VOTEE_VETOPERM, VOTEE_MAX, VOTEE_AREA, VOTEE_PERMISSION, VOTEE_INVALID, VOTEE_NUM };
enum { MM_OPEN = 0, MM_PRIVATE, MM_MATCH, MM_NUM }; enum { MM_MASK = 0x03 };
enum { FA_PICKUP = 0, FA_STEAL, FA_DROP, FA_LOST, FA_RETURN, FA_SCORE, FA_NUM, FA_KTFSCORE, FA_SCOREFAIL, FA_RESET };
enum { FTR_SILENT = 0, FTR_PLAYERWISH, FTR_AUTO, FTR_NUM };
enum { PR_CLEAR = 0, PR_ASSIST, PR_SPLAT, PR_HS, PR_KC, PR_KD, PR_HEALSELF, PR_HEALTEAM, PR_HEALENEMY, PR_HEALWOUND, PR_HEALEDBYTEAMMATE, PR_ARENA_WIN, PR_ARENA_WIND, PR_ARENA_LOSE, PR_SECURE_SECURED, PR_SECURE_SECURE, PR_SECURE_OVERTHROW, PR_BUZZKILL, PR_BUZZKILLED, PR_KD_SELF, PR_KD_ENEMY, PR_MAX };


#define DMF 16.0f
#define DNF 100.0f
#define DVELF 4.0f

enum { DISC_NONE = 0, DISC_EOP, DISC_MKICK, DISC_MBAN, DISC_TAGT, DISC_BANREFUSE, DISC_WRONGPW, DISC_SOPLOGINFAIL, DISC_MAXCLIENTS, DISC_MASTERMODE,
    DISC_NAME, DISC_NAME_IP, DISC_NAME_PWD, DISC_DUP, DISC_OVERFLOW, DISC_TIMEOUT,
    DISC_EXT, DISC_EXT2, DISC_EXT3, DISC_NUM };
enum { BAN_NONE = 0, BAN_VOTE, BAN_BLACKLIST };

#define EXT_ACK                         -1
#define EXT_VERSION                     104
#define EXT_ERROR_NONE                  0
#define EXT_ERROR                       1
#define EXT_PLAYERSTATS_RESP_IDS        -10
#define EXT_UPTIME                      0
#define EXT_PLAYERSTATS                 1
#define EXT_TEAMSCORE                   2
#define EXT_PLAYERSTATS_RESP_STATS      -11

enum { PONGFLAG_PASSWORD = 0, PONGFLAG_BANNED, PONGFLAG_BLACKLIST, PONGFLAG_BYPASSBANS, PONGFLAG_BYPASSPRIV, PONGFLAG_MASTERMODE = 6, PONGFLAG_NUM };
enum { EXTPING_NOP = 0, EXTPING_NAMELIST, EXTPING_SERVERINFO, EXTPING_MAPROT, EXTPING_UPLINKSTATS, EXTPING_NUM };

struct authrequest { uint id; unsigned char hash[20]; };
struct connectrequest { int cn, guid; const char *hostname; int id, user; };

// new game mode/mutator system
enum // game modes
{
    G_DEMO = 0,
    G_EDIT,
    G_DM,
    G_CTF,
    G_STF,
    G_HTF,
    G_KTF,
    G_BOMBER,
    G_ZOMBIE,
    G_OVERLOAD,
    G_MAX,
};

enum // game mutators
{
    G_M_NONE = 0,
    // gameplay
    G_M_TEAM = 1 << 0,
    G_M_CLASSIC = 1 << 1,
    G_M_CONFIRM = 1 << 2,
    G_M_VAMPIRE = 1 << 3,
    G_M_CONVERT = 1 << 4,
    G_M_PSYCHIC = 1 << 5,
    G_M_VOID = 1 << 6,
    G_M_JUGGERNAUT = 1 << 7,
    // damage
    G_M_REAL = 1 << 8,
    G_M_EXPERT = 1 << 9,
    // weapons
    G_M_INSTA = 1 << 10,
    G_M_SNIPING = 1 << 11,
    G_M_PISTOL = 1 << 12,
    G_M_GIB = 1 << 13,
    G_M_EXPLOSIVE = 1 << 14,
    // game-specific
    G_M_GSP1 = 1 << 15,

    G_M_GAMEPLAY = G_M_TEAM|G_M_CLASSIC|G_M_CONFIRM|G_M_VAMPIRE|G_M_CONVERT|G_M_PSYCHIC|G_M_VOID|G_M_JUGGERNAUT,
    G_M_DAMAGE = G_M_REAL|G_M_EXPERT, // G_M_JUGGERNAUT does not actually interfere with damage!
    G_M_WEAPON = G_M_INSTA|G_M_SNIPING|G_M_PISTOL|G_M_GIB|G_M_EXPLOSIVE,

    G_M_MOST = G_M_GAMEPLAY|G_M_DAMAGE|G_M_WEAPON,
    G_M_ALL = G_M_MOST|G_M_GSP1,

    G_M_GSN = 1, G_M_GSP = 15, G_M_NUM = 16,
};

struct gametypes
{
    int type, implied, mutators[G_M_GSN+1];
    const char *name, *gsp[G_M_GSN];
};
struct mutstypes
{
    int type, implied, mutators;
    const char *name;
};

extern gametypes gametype[G_MAX];
extern mutstypes mutstype[G_M_NUM];

#define m_valid(g) ((g)>=0 && (g)<G_MAX)

#define m_demo(g)           (g == G_DEMO)
#define m_edit(g)           (g == G_EDIT)
#define m_dm(g)             (g == G_DM)
#define m_capture(g)        (g == G_CTF)
#define m_secure(g)         (g == G_STF)
#define m_hunt(g)           (g == G_HTF)
#define m_keep(g)           (g == G_KTF)
#define m_bomber(g)         (g == G_BOMBER)
#define m_zombie(g)         (g == G_ZOMBIE)
#define m_overload(g)       (g == G_OVERLOAD)

#define m_flags(g)          (m_capture(g) || m_secure(g) || m_hunt(g) || m_keep(g) || m_bomber(g) || m_overload(g))
#define m_ai(g)             (m_valid(g) && !m_demo(g) && !m_edit(g)) // extra bots not available in demo/edit

#define m_implied(a,b)      (gametype[a].implied)
#define m_doimply(a,b,c)    (gametype[a].implied|mutstype[c].implied)
// can add gsp implieds below
#define m_mimplied(a,b)     ((muts & (G_M_GSP1)) && (mode == G_DM || mode == G_BOMBER))

#define m_team(a,b)         ((b & G_M_TEAM) || (m_implied(a,b) & G_M_TEAM))
#define m_insta(a,b)        ((b & G_M_INSTA) || (m_implied(a,b) & G_M_INSTA))
#define m_sniping(a,b)      ((b & G_M_SNIPING) || (m_implied(a,b) & G_M_SNIPING))
#define m_classic(a,b)      ((b & G_M_CLASSIC) || (m_implied(a,b) & G_M_CLASSIC))
#define m_confirm(a,b)      ((b & G_M_CONFIRM) || (m_implied(a,b) & G_M_CONFIRM))
#define m_convert(a,b)      ((b & G_M_CONVERT) || (m_implied(a,b) & G_M_CONVERT))
#define m_vampire(a,b)      ((b & G_M_VAMPIRE) || (m_implied(a,b) & G_M_VAMPIRE))
#define m_psychic(a,b)      ((b & G_M_PSYCHIC) || (m_implied(a,b) & G_M_PSYCHIC))
#define m_void(a,b)         ((b & G_M_VOID) || (m_implied(a,b) & G_M_VOID))
#define m_juggernaut(a,b)   ((b & G_M_JUGGERNAUT) || (m_implied(a,b) & G_M_JUGGERNAUT))
#define m_real(a,b)         ((b & G_M_REAL) || (m_implied(a,b) & G_M_REAL))
#define m_expert(a,b)       ((b & G_M_EXPERT) || (m_implied(a,b) & G_M_EXPERT))
#define m_pistol(a,b)       ((b & G_M_PISTOL) || (m_implied(a,b) & G_M_PISTOL))
#define m_gib(a,b)          ((b & G_M_GIB) || (m_implied(a,b) & G_M_GIB))
#define m_explosive(a,b)    ((b & G_M_EXPLOSIVE) || (m_implied(a,b) & G_M_EXPLOSIVE))

#define m_gsp1(a,b)         ((b & G_M_GSP1) || (m_implied(a,b) & G_M_GSP1))
//#define m_gsp(a,b)          (m_gsp1(a,b))

#define m_spawn_team(a,b)   (m_team(a,b) && !m_keep(a) && !m_zombie(a))
#define m_spawn_reversals(a,b) (!m_capture(a) && !m_hunt(a) && !m_bomber(a))
#define m_noitems(a,b)      (false)
#define m_noitemsammo(a,b)  (m_sniper(a,b) || m_explosive(a,b))
#define m_noitemsnade(a,b)  (m_gib(a,b))
#define m_noprimary(a,b)    (m_nosecondary(a,b) || m_gib(a,b))
#define m_nosecondary(a,b)  (m_gib(a,b))

#define m_survivor(a,b)     ((m_dm(a) || m_bomber(a)) && m_gsp1(a,b))
#define m_onslaught(a,b)    (m_zombie(a) && !m_gsp1(a,b))

#define m_duke(a,b)         (m_survivor(a,b) || m_progressive(a,b))
#define m_sniper(a,b)       (m_insta(a,b) || m_sniping(a,b))
#define m_regen(a,b)        (!m_duke(a,b) && !m_classic(a,b) && !m_vampire(a,b) && !m_sniper(a,b) && !m_juggernaut(a,b))

#define m_lss(a,b)          (m_duke(a,b) && m_gib(a,b))
#define m_return(a,b)       (m_capture(a) && !m_gsp1(a,b))
#define m_ktf2(a,b)         (m_keep(a) && m_gsp1(a,b))
#define m_progressive(a,b)  (m_zombie(a) && m_gsp1(a,b))

