#define MAXCLIENTS 256				  // in a multiplayer game, can be arbitrarily changed
#define DEFAULTCLIENTS 12
#define MAXTRANS 5000				   // max amount of data to swallow in 1 go
#define CUBE_DEFAULT_SERVER_PORT 28770
#define CUBE_SERVINFO_PORT_LAN 28778
#define CUBE_SERVINFO_OFFSET 1
// SET protocol version to 2106 on stable release!
#define PROTOCOL_VERSION 1105		   // bump when protocol changes
#define DEMO_VERSION 2				  // bump when demo format changes
#define DEMO_MAGIC "ACS_DEMO"
#define MAXMAPSENDSIZE 65536
#define MAXCFGFILESIZE 65536

// network messages codes
enum{
	N_SERVINFO = 0, N_WELCOME, N_CONNECT, // before connection
	N_INITCLIENT, N_SETTEAM, N_RESUME, N_MAPIDENT, N_CDIS, // sent after (dis)connection
	N_CLIENT, N_POS, N_SOUND, N_PINGPONG, N_PINGTIME, // automatic from client
	N_TEXT, N_WHOIS, N_NEWNAME, N_SKIN, N_SWITCHTEAM, // user-initiated
	N_CALLVOTE, N_CALLVOTEERR, N_VOTE, N_VOTERESULT, // votes
	N_LISTDEMOS, N_DEMO, N_DEMOPLAYBACK, // demos
	N_AUTHREQ, N_AUTHCHAL, // auth
	N_SETROLE, N_ROLECHANGE, // privileges
	N_SENDMAP, N_RECVMAP, // map transit
	// editmode ONLY
	N_EDITMODE, N_EDITH, N_EDITT, N_EDITS, N_EDITD, N_EDITE, N_EDITENT, N_NEWMAP,
	// game events
	N_SHOOT, N_SHOOTC, N_EXPLODE, N_AKIMBO, N_RELOAD, // clients to server events
	N_SG, N_SCOPE, N_SUICIDE, N_QUICKSWITCH, N_SWITCHWEAP, N_PRIMARYWEAP, N_THROWNADE, // server directly handled
	N_POINTS, N_KILL, N_DAMAGE, N_REGEN, N_PROJPUSH, // server to client
	// gameplay
	N_TRYSPAWN, N_SPAWNSTATE, N_SPAWN, N_FORCEDEATH, N_FORCEGIB, // spawning
	N_ITEMSPAWN, N_ITEMACC, // items
	N_DROPFLAG, N_FLAGINFO, N_FLAGMSG, N_FLAGCNT, // flags
	N_MAPCHANGE, N_NEXTMAP, // map changes
	N_TIMEUP, N_ACCURACY, N_ARENAWIN, N_FF, // round end/remaining
	// extensions
	N_SERVMSG, N_CONFMSG, N_EXT,
	N_NUM
};

enum { SA_KICK = 0, SA_BAN, SA_REMBANS, SA_MASTERMODE, SA_AUTOTEAM, SA_FORCETEAM, SA_GIVEADMIN, SA_MAP, SA_RECORDDEMO, SA_STOPDEMO, SA_CLEARDEMOS, SA_SERVERDESC, SA_SHUFFLETEAMS, SA_SUBDUE, SA_REVOKE, SA_NUM};
enum { VOTE_NEUTRAL = 0, VOTE_YES, VOTE_NO, VOTE_NUM };
enum { VOTEE_DISABLED = 0, VOTEE_CUR, VOTEE_VETOPERM, VOTEE_MAX, VOTEE_AREA, VOTEE_PERMISSION, VOTEE_INVALID, VOTEE_SHORTREASON, VOTEE_NUM };
enum { MM_OPEN, MM_PRIVATE, MM_NUM };
enum { AT_DISABLED = 0, AT_ENABLED, AT_SHUFFLE, AT_NUM };
enum { FA_PICKUP = 0, FA_DROP, FA_LOST, FA_RETURN, FA_SCORE, FA_KTFSCORE, FA_SCOREFAIL, FA_RESET };
enum { FTR_SILENT = 0, FTR_PLAYERWISH, FTR_AUTOTEAM, FTR_NUM }; // forceteam reasons

enum { DISC_NONE = 0, DISC_EOP, DISC_KICK, DISC_BAN, DISC_TAGT, DISC_REFUSE, DISC_PASSWORD, DISC_LOGINFAIL, DISC_FULL, DISC_PRIVATE,
		DISC_NAME, DISC_DUP, DISC_AKICK, DISC_ABAN, DISC_OVERFLOW, DISC_TIMEOUT, DISC_NUM };

static const char *disc_reason(int reason)
{
	static const char *disc_reasons[] = {
		"normal", "end of packet (overread)", "vote-kicked", "vote-banned", "tag type", "connection refused", "wrong password", "failed login", "server is full", "private",
			"bad name", "duplicate connection", "automatically kicked", "automatically banned", "overflow (packet flood)", "timeout" };
	return reason >= 0 && (size_t)reason < sizeof(disc_reasons)/sizeof(disc_reasons[0]) ? disc_reasons[reason] : "unknown";
}

#define EXT_ACK						 -1
#define EXT_VERSION					 201
#define EXT_ERROR_NONE				  0
#define EXT_ERROR					   1
#define EXT_PLAYERSTATS_RESP_IDS		-10
#define EXT_UPTIME					  0
#define EXT_PLAYERSTATS				 1
#define EXT_TEAMSCORE				   2
#define EXT_PLAYERSTATS_RESP_STATS	  -11

enum { PONGFLAG_PASSWORD = 0, PONGFLAG_BANNED, PONGFLAG_BLACKLIST, PONGFLAG_MASTERMODE = 6, PONGFLAG_NUM };
enum { EXTPING_NOP = 0, EXTPING_NAMELIST, EXTPING_SERVERINFO, EXTPING_MAPROT, EXTPING_NUM };

enum
{
	GMODE_DEMO = -1,
	GMODE_TEAMDEATHMATCH = 0,
	GMODE_COOPEDIT,
	GMODE_DEATHMATCH,
	GMODE_SURVIVOR,
	GMODE_TEAMSURVIVOR,
	GMODE_CTF,
	GMODE_PISTOLFRENZY,
	GMODE_LASTSWISSSTANDING,
	GMODE_ONESHOTONEKILL,
	GMODE_TEAMONESHOTONEKILL,
	GMODE_HTF,
	GMODE_TEAMKTF,
	GMODE_KTF,
	GMODE_REALTDM, GMODE_EXPERTTDM,
	GMODE_REALDM, GMODE_EXPERTDM,
	GMODE_KNIFE, GMODE_HANDHELD,
	GMODE_RCTF,
	GMODE_NUM
};

#define m_lms		(gamemode == GMODE_SURVIVOR || gamemode == GMODE_TEAMSURVIVOR)
#define m_ctf		(gamemode == GMODE_CTF || gamemode == GMODE_RCTF)
#define m_pistol	(gamemode == GMODE_PISTOLFRENZY)
#define m_lss		(gamemode == GMODE_LASTSWISSSTANDING || gamemode == GMODE_HANDHELD)
#define m_osok		(gamemode == GMODE_ONESHOTONEKILL || gamemode == GMODE_TEAMONESHOTONEKILL)
#define m_htf		(gamemode == GMODE_HTF)
#define m_ktf		(gamemode == GMODE_TEAMKTF || gamemode == GMODE_KTF)
#define m_edit		(gamemode == GMODE_COOPEDIT)
#define	m_expert	(gamemode == GMODE_EXPERTTDM || gamemode == GMODE_EXPERTDM)
#define m_real		(gamemode == GMODE_REALTDM || gamemode == GMODE_REALDM)

#define m_noitems		(m_lms || m_osok || gamemode == GMODE_KNIFE)
#define m_noitemsnade	(m_lss)
#define m_nopistol		(m_osok || m_lss || gamemode == GMODE_KNIFE)
#define m_noprimary		(m_pistol || m_lss || gamemode == GMODE_KNIFE)
#define m_duel			(m_lms || gamemode == GMODE_LASTSWISSSTANDING || m_osok)
#define m_flags			(m_ctf || m_htf || m_ktf)
#define m_team			(gamemode==GMODE_TEAMDEATHMATCH || gamemode==GMODE_TEAMONESHOTONEKILL || \
							gamemode==GMODE_TEAMSURVIVOR || m_ctf || m_htf || gamemode==GMODE_TEAMKTF || \
							gamemode==GMODE_REALTDM || gamemode == GMODE_EXPERTTDM)
#define m_fight(mode)	((mode)>=0 && (mode)<GMODE_NUM)
#define m_demo			(gamemode == GMODE_DEMO)
#define m_valid(mode)	(m_fight || m_demo)

struct authrequest{ uint id; bool answer; string chal; };
extern vector<authrequest> authrequests;
