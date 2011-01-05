#define MAXCLIENTS 256				  // in a multiplayer game, can be arbitrarily changed
#define DEFAULTCLIENTS 12
#define MAXTRANS 5000				   // max amount of data to swallow in 1 go
#define CUBE_DEFAULT_SERVER_PORT 28770
#define CUBE_SERVINFO_PORT_LAN 28778
#define CUBE_SERVINFO_PORT(serverport) (serverport+1)
#define CUBE_SERVINFO_TO_SERV_PORT(servinfoport) (servinfoport-1)
#define PROTOCOL_VERSION 2103		   // bump when protocol changes
#define DEMO_VERSION 2				  // bump when demo format changes
#define DEMO_MAGIC "ACS_DEMO"
#define MAXMAPSENDSIZE 65536
#define MAXCFGFILESIZE 65536

// network messages codes
enum{
	SV_SERVINFO = 0, SV_WELCOME, SV_CONNECT, // before connection
	SV_INITCLIENT, SV_SETTEAM, SV_RESUME, SV_MAPIDENT, SV_CDIS, // sent after (dis)connection
	SV_CLIENT, SV_POS, SV_SOUND, SV_PINGPONG, SV_PINGTIME, // automatic from client
	SV_TEXT, SV_WHOIS, SV_NEWNAME, SV_SKIN, SV_SWITCHTEAM, // user-initiated
	SV_CALLVOTE, SV_CALLVOTEERR, SV_VOTE, SV_VOTERESULT, // votes
	SV_LISTDEMOS, SV_DEMO, SV_DEMOPLAYBACK, // demos
	SV_AUTHREQ, SV_AUTHCHAL, // auth
	SV_SETROLE, SV_ROLECHANGE, // privileges
	SV_SENDMAP, SV_RECVMAP, // map transit
	// editmode ONLY
	SV_EDITMODE, SV_EDITH, SV_EDITT, SV_EDITS, SV_EDITD, SV_EDITE, SV_EDITENT, SV_NEWMAP,
	// game events
	SV_SHOOT, SV_EXPLODE, SV_AKIMBO, SV_RELOAD, // clients to server events
	SV_SG, SV_SCOPE, SV_SUICIDE, SV_WEAPCHANGE, SV_PRIMARYWEAP, SV_THROWNADE, // server directly handled
	SV_POINTS, SV_DIED, SV_DAMAGE, SV_REGEN, SV_HITPUSH, SV_SHOTFX, // server to client
	// gameplay
	SV_TRYSPAWN, SV_SPAWNSTATE, SV_SPAWN, SV_FORCEDEATH, SV_FORCEGIB, // spawning
	SV_ITEMLIST, SV_ITEMSPAWN, SV_ITEMACC, // items
	SV_DROPFLAG, SV_FLAGINFO, SV_FLAGMSG, SV_FLAGCNT, // flags
	SV_MAPCHANGE, SV_NEXTMAP, // map changes
	SV_TIMEUP, SV_ARENAWIN, // round end/remaining
	// extensions
	SV_SERVMSG, SV_EXTENSION,
	SV_NUM
};

enum { SA_KICK = 0, SA_BAN, SA_REMBANS, SA_MASTERMODE, SA_AUTOTEAM, SA_FORCETEAM, SA_GIVEADMIN, SA_MAP, SA_RECORDDEMO, SA_STOPDEMO, SA_CLEARDEMOS, SA_SERVERDESC, SA_SHUFFLETEAMS, SA_SUBDUE, SA_NUM};
enum { VOTE_NEUTRAL = 0, VOTE_YES, VOTE_NO, VOTE_NUM };
enum { VOTEE_DISABLED = 0, VOTEE_CUR, VOTEE_MUL, VOTEE_MAX, VOTEE_AREA, VOTEE_PERMISSION, VOTEE_INVALID, VOTEE_NUM };
enum { MM_OPEN, MM_PRIVATE, MM_NUM };
enum { AT_DISABLED = 0, AT_ENABLED, AT_SHUFFLE, AT_NUM };
enum { FA_PICKUP = 0, FA_DROP, FA_LOST, FA_RETURN, FA_SCORE, FA_KTFSCORE, FA_SCOREFAIL, FA_RESET };
enum { FTR_SILENT = 0, FTR_PLAYERWISH, FTR_AUTOTEAM, FTR_NUM }; // forceteam reasons

enum { DISC_NONE = 0, DISC_EOP, DISC_KICK, DISC_BAN, DISC_TAGT, DISC_REFUSE, DISC_PASSWORD, DISC_LOGINFAIL, DISC_FULL, DISC_PRIVATE,
		DISC_NAME, DISC_FF, DISC_EDITMODE, DISC_DUP, DISC_OVERFLOW, DISC_TIMEOUT, DISC_NUM };

static const char *disc_reason(int reason)
{
	static const char *disc_reasons[] = {
		"normal", "end of packet (overflow)", "vote-kicked", "vote-banned", "tag type", "connection refused", "wrong password", "failed login", "server is full", "private",
			"bad name", "excessive friendly fire", "the age-old editmode hack is fixed - goto coopedit", "duplicate connection", "overflow (packet flood)", "timeout" };
	return reason >= 0 && (size_t)reason < sizeof(disc_reasons)/sizeof(disc_reasons[0]) ? disc_reasons[reason] : "unknown";
}

#define EXT_ACK						 -1
#define EXT_VERSION					 200
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
	GMODE_BOTTEAMDEATHMATCH,
	GMODE_BOTDEATHMATCH,
	GMODE_LASTSWISSSTANDING,
	GMODE_ONESHOTONEKILL,
	GMODE_TEAMONESHOTONEKILL,
	GMODE_BOTONESHOTONEKILL,
	GMODE_HUNTTHEFLAG,		 // 13
	GMODE_TEAMKEEPTHEFLAG,
	GMODE_KEEPTHEFLAG,
	GMODE_NUM
};

#define m_lms		 (gamemode==3 || gamemode==4)
#define m_ctf		 (gamemode==5)
#define m_pistol	  (gamemode==6)
#define m_lss		 (gamemode==9)
#define m_osok		(gamemode>=10 && gamemode<=12)
#define m_htf		 (gamemode==13)
#define m_ktf		 (gamemode==14 || gamemode==15)
#define m_edit		(gamemode==1)

#define m_noitems	 (m_lms || m_osok)
#define m_noitemsnade (m_lss)
#define m_nopistol	(m_osok || m_lss)
#define m_noprimary   (m_pistol || m_lss)
#define m_noguns	  (m_nopistol && m_noprimary)
#define m_arena	   (m_lms || m_lss || m_osok)
#define m_teammode	(gamemode==0 || gamemode==4 || gamemode==5 || gamemode==7 || gamemode==11 || gamemode==13 || gamemode==14)
#define m_tarena	  (m_arena && m_teammode)
#define m_botmode	 (gamemode==7 || gamemode == 8 || gamemode==12)
#define m_valid(mode) (((mode)>=0 && (mode)<GMODE_NUM) || (mode) == -1)
#define m_mp(mode)	(m_valid(mode) && (mode)>=0 && (mode)!=7 && (mode)!=8 && (mode)!=12)
#define m_demo		(gamemode==-1)
#define m_flags	   (m_ctf || m_htf || m_ktf)

struct authrequest{ uint id; bool answer; string chal; };
extern vector<authrequest> authrequests;
