#define MAXCLIENTS 64				  // in a multiplayer game, can be arbitrarily changed
#define MAXBOTS 16
#define MAXBOTBALANCE 20
#define DEFAULTCLIENTS 12
#define MAXTRANS 8192				   // max amount of data to swallow in 1 go
#define CUBE_DEFAULT_SERVER_PORT 28770
#define CUBE_SERVINFO_PORT_LAN 28778
#define CUBE_SERVINFO_OFFSET 1
#define PROTOCOL_VERSION 129			   // bump when protocol changes
#define DEMO_VERSION 2				  // bump when demo format changes
#define DEMO_MAGIC "ACR_REPLAY_FILE!"
#define MAXMAPSENDSIZE 65536
#define MAXCFGFILESIZE 65536

// network messages codes
enum{
	N_SERVINFO = 0, N_WELCOME, // before connection is complete
	N_INITCLIENT, N_INITAI, N_SETTEAM, N_RESUME, N_MAPIDENT, N_DISC, N_DELAI, N_REASSIGNAI, // sent after (dis)connection
	N_CLIENT, N_POS, N_SOUND, N_PINGPONG, N_PINGTIME, // automatic from client
	N_TEXT, N_WHOIS, N_WHOISINFO, N_NEWNAME, N_SKIN, N_THIRDPERSON, N_LEVEL, N_SWITCHTEAM, // user-initiated
	N_CALLVOTE, N_CALLVOTEERR, N_VOTE, N_VOTEREMAIN, N_VOTERESULT, // votes
	N_LISTDEMOS, N_DEMO, N_DEMOPLAYBACK, // demos
	N_AUTHREQ, N_AUTHCHAL, // auth
	N_CLAIMPRIV, N_SETPRIV, // privileges
	N_MAPC2S, N_MAPS2C, N_MAPDELETE, N_MAPREQ, N_MAPFAIL, // map transit
	// editmode ONLY
	N_EDITMODE, N_EDITH, N_EDITT, N_EDITS, N_EDITD, N_EDITE, N_EDITW, N_EDITENT, N_NEWMAP,
	// game events
	N_SHOOT, N_SHOOTC, N_PROJ, N_AKIMBO, N_RELOAD, // clients to server events
	N_SG, N_SUICIDE, N_QUICKSWITCH, N_SWITCHWEAP, N_LOADOUT, N_THROWNADE, N_THROWKNIFE, // server directly handled
	N_RICOCHET, N_REGEN, N_HEAL, N_BLEED, N_STREAKREADY, N_STREAKUSE, N_HEADSHOT, N_MULTI, // server to client
	N_KNIFEADD, N_KNIFEREMOVE, // knives
	N_CONFIRMADD, N_CONFIRMREMOVE, // kill confirmed
	// gameplay
	N_POINTS, N_SCORE, N_TEAMSCORE, N_KILL, N_DAMAGE, // scoring
	N_TRYSPAWN, N_SPAWNSTATE, N_SPAWN, N_FORCEDEATH, N_FORCEGIB, // spawning
	N_ITEMSPAWN, N_ITEMACC, // items
	N_DROPFLAG, N_FLAGINFO, N_FLAGMSG, N_FLAGSECURE, // flags
	N_MAPCHANGE, N_NEXTMAP, // map changes
	N_TIMEUP, N_ACCURACY, N_ARENAWIN, N_ZOMBIESWIN, N_CONVERTWIN, // round end/remaining
	// extensions
	N_SERVMSG, N_EXT,
	N_NUM
};

#ifdef _DEBUG
extern void protocoldebug(bool enable);

// converts message code to char
extern const char *messagenames(int n);
#endif

enum { SA_KICK = 0, SA_BAN, SA_REMBANS, SA_MASTERMODE, SA_AUTOTEAM, SA_FORCETEAM, SA_GIVEROLE, SA_MAP, SA_NEXTMAP, SA_RECORDDEMO, SA_STOPDEMO, SA_CLEARDEMOS, SA_SERVERDESC, SA_SHUFFLETEAMS, SA_SUBDUE, SA_REVOKE, SA_SPECT, SA_BOTBALANCE, SA_NUM};
enum { VOTE_NEUTRAL = 0, VOTE_YES, VOTE_NO, VOTE_NUM };
enum { VOTEE_DISABLED = 0, VOTEE_CUR, VOTEE_VETOPERM, VOTEE_MAX, VOTEE_AREA, VOTEE_PERMISSION, VOTEE_INVALID, VOTEE_NUM };
enum { MM_OPEN, MM_LOCKED, MM_PRIVATE, MM_NUM };
#define MM_MASK 3
enum { AT_DISABLED = 0, AT_ENABLED, AT_SHUFFLE, AT_NUM };
enum { FA_PICKUP = 0, FA_DROP, FA_LOST, FA_RETURN, FA_SCORE, FA_KTFSCORE, FA_SCOREFAIL, FA_RESET };
enum { FTR_SILENT = 0, FTR_PLAYERWISH, FTR_AUTOTEAM, FTR_NUM }; // forceteam reasons

// network quantization scale
#define DMF 32.0f           // for world locations
//#define DNF 1000.0f         // for normalized vectors
//#define DVELF 8.0f          // for playerspeed based velocity vectors

enum { DISC_NONE = 0, DISC_EOP, DISC_KICK, DISC_BAN, DISC_TAGT, DISC_REFUSE, DISC_PASSWORD, DISC_LOGINFAIL, DISC_FULL, DISC_PRIVATE,
		DISC_NAME, DISC_NAME_IP, DISC_NAME_PWD, DISC_DUP, DISC_OVERFLOW, DISC_TIMEOUT,
		DISC_MBAN,
		DISC_EXT, DISC_EXT2, DISC_EXT3, DISC_NUM };

static const char *disc_reason(int reason)
{
	static const char *disc_reasons[DISC_NUM] = {
		"normal", "end of packet/overread", "kicked", "banned", "tag type", "connection refused", "wrong password", "failed login", "server is full", "private",
			"bad nickname", "nickname IP protected", "unauthorized nickname", "duplicate connection", "overflow/packet flood", "timeout",
			"globally banned IP address",
			"extension", "ext2", "ext3",
	};
	return reason >= 0 && (size_t)reason < DISC_NUM ? disc_reasons[reason] : "unknown";
}

#define EXT_ACK						 -1
#define EXT_VERSION					 100
#define EXT_ERROR_NONE				  0
#define EXT_ERROR					   1
#define EXT_PLAYERSTATS_RESP_IDS		-10
#define EXT_UPTIME					  0
#define EXT_PLAYERSTATS				 1
#define EXT_TEAMSCORE				   2
#define EXT_PLAYERSTATS_RESP_STATS	  -11

enum { PONGFLAG_PASSWORD = 0, PONGFLAG_BANNED, PONGFLAG_BLACKLIST, PONGFLAG_MUTE, PONGFLAG_MASTERMODE = 6, PONGFLAG_NUM };
enum { EXTPING_NOP = 0, EXTPING_NAMELIST, EXTPING_SERVERINFO, EXTPING_MAPROT, EXTPING_UPLINKSTATS, EXTPING_NUM };

#include "gamemode.h"

struct authrequest{ uint id; bool answer; union { int *hash; char *usr; }; };
struct connectrequest{ int cn, guid; enet_uint32 ip; };
