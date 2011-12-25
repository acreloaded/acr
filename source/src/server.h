// server.h

#define gamemode smode   // allows the gamemode macros to work with the server mode
#define mutators smuts // and mutators too

#define valid_flag(f) (f >= 0 && f < 2)

enum { GE_NONE = 0, GE_SHOT, GE_PROJ, GE_AKIMBO, GE_RELOAD };
enum { ST_EMPTY, ST_LOCAL, ST_TCPIP, ST_AI };

extern bool canreachauthserv;

static int interm = 0, minremain = 0, gamemillis = 0, gamelimit = 0, gamemusicseed = 0;
static const int DEATHMILLIS = 300;
int smode = G_DM, smuts = G_M_TEAM, mastermode = MM_OPEN, botbalance = -1;

struct head_t{
	int cn;
	vec delta;
};

#define eventcommon int type, millis, id
struct shotevent{
	eventcommon;
	int gun;
	float to[3];
	vector<head_t> *pheads;
	bool compact;
};

struct projevent{
	eventcommon;
	int gun, flag;
	float o[3];
};

struct akimboevent{
	eventcommon;
};

struct reloadevent{
	eventcommon;
	int gun;
};

union gameevent{
	struct { eventcommon; };
	shotevent shot;
	projevent proj;
	akimboevent akimbo;
	reloadevent reload;
};


template <int N>
struct projectilestate
{
	int projs[N];
	int numprojs;
	int throwable;

	projectilestate() : numprojs(0) {}

	void reset() { numprojs = 0; }

	void add(int val)
	{
		if(numprojs>=N) numprojs = 0;
		projs[numprojs++] = val;
		++throwable;
	}

	bool removeany()
	{
		if(!numprojs) return false;
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

struct wound
{
	int inflictor;
	int lastdealt;
	vec offset;
};

struct clientstate : playerstate
{
	vec o, aim, vel, lasto, sg[SGRAYS], flagpickupo;
	float pitchvel;
	int state, lastomillis, movemillis;
	int lastdeath, lastffkill, lastspawn, lifesequence, streakused;
	int lastkill, combo;
	bool crouching, onfloor; float fallz;
	int crouchmillis, scopemillis;
	int drownmillis; char drownval;
	int streakondeath;
	int lastshot, lastregen;
	projectilestate<6> grenades; // 5000ms TLL / (we can throw one every 650ms+200ms) = 6 nades possible
	projectilestate<3> knives;
	int akimbos, akimbomillis;
	int points, flagscore, frags, deaths, shotdamage, damage;
	ivector revengelog;
	vector<wound> wounds;

	clientstate() : state(CS_DEAD), playerstate() {}

	bool isalive(int gamemillis)
	{
		if(interm) return false;
		return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
	}

	bool waitexpired(int gamemillis)
	{
		int wait = gamemillis - lastshot;
		loopi(WEAP_MAX) if(wait < gunwait[i]) return false;
		return true;
	}

	void reset()
	{
		state = CS_DEAD;
		lifesequence = -1;
		grenades.reset();
		knives.reset();
		akimbos = akimbomillis = 0;
		points = flagscore = frags = deaths = shotdamage = damage = lastffkill = 0;
		revengelog.setsize(0);
		pointstreak = streakused = 0;
		radarearned = airstrikes = nukemillis = 0;
		respawn();
	}

	void respawn()
	{
		playerstate::respawn();
		o = lasto = vec(-1e10f, -1e10f, -1e10f);
		aim = vel = vec(0, 0, 0);
		pitchvel = 0;
		lastomillis = movemillis = 0;
		drownmillis = drownval = 0;
		lastspawn = -1;
		lastdeath = lastshot = lastregen = 0;
		lastkill = combo = 0;
		akimbos = akimbomillis = 0;
		damagelog.setsize(0);
		wounds.shrink(0); // no more wounds!
		crouching = false;
		crouchmillis = scopemillis = 0;
		onfloor = false;
		fallz = 0;
		streakondeath = -1;
	}

	void addwound(int owner, const vec &woundloc)
	{
		wound &w = wounds.length() >= 8 ? wounds[0] : wounds.add();
		w.inflictor = owner;
		w.lastdealt = gamemillis;
		w.offset = woundloc;
		w.offset.sub(o);
	}
};

struct savedscore
{
	string name;
	enet_uint32 ip;
	int points, frags, assists, pointstreak, flagscore, deaths, shotdamage, damage;

	void save(clientstate &cs)
	{
		points = cs.points;
		frags = cs.frags;
		assists = cs.assists;
		pointstreak = cs.pointstreak;
		flagscore = cs.flagscore;
		deaths = cs.deaths;
		shotdamage = cs.shotdamage;
		damage = cs.damage;
	}

	void restore(clientstate &cs)
	{
		cs.points = points;
		cs.frags = frags;
		cs.assists = assists;
		cs.pointstreak = pointstreak;
		cs.flagscore = flagscore;
		cs.deaths = deaths;
		cs.shotdamage = shotdamage;
		cs.damage = damage;
	}
};

struct client				   // server side version of "dynent" type
{
	int type;
	int clientnum;
	ENetPeer *peer;
	string hostname;
	string name;
	int ping, team, skin, vote, priv;
	int connectmillis;
	bool connected, connectauth;
	int authtoken, authmillis, authpriv, masterverdict, guid; uint authreq;
	bool haswelcome;
	bool isonrightmap;
	bool timesync;
	int overflow;
	int gameoffset, lastevent, lastvotecall;
	int demoflags;
	clientstate state;
	vector<gameevent> events, timers;
	vector<uchar> position, messages;
	string lastsaytext;
	int saychars, lastsay, spamcount;
	int at3_score, at3_lastforce, eff_score;
	bool at3_dontmove;
	int spawnindex;
	int salt;
	string pwd;
	int mapcollisions;
	enet_uint32 bottomRTT;

	gameevent &addevent()
	{
		static gameevent dummy;
		if(events.length()>32) return dummy;
		return events.add();
	}

	gameevent &addtimer(){
		static gameevent dummy;
		if(timers.length()>256) return dummy;
		return timers.add();
	}

	void removetimers(int type){ loopv(timers) if(timers[i].type == type) timers.remove(i--); }

	void removeexplosives() {
		state.grenades.reset(); // remove active/flying nades
		state.knives.reset(); // remove active/flying knives (usually useless, since knives are fast)
		removetimers(GE_PROJ); // remove stuck crossbows
		// remove all dealt wounds
		extern vector<client *> clients;
		loopv(clients){
			if(clients[i]->type == ST_EMPTY) continue;
			clientstate &cs = clients[i]->state;
			loopvj(cs.wounds) if(cs.wounds[j].inflictor == clientnum) cs.wounds.remove(j--);
		}
	}

	void suicide(int weap, int flags = FRAG_NONE){
		extern void serverdied(client *target, client *actor, int damage, int gun, int style, const vec &source);
		if(state.state != CS_DEAD) serverdied(this, this, 0, weap, flags, state.o);
	}

	void mapchange()
	{
		state.reset();
		events.setsize(0);
		timers.setsize(0);
		overflow = 0;
		timesync = false;
		isonrightmap = m_edit(gamemode);
		lastevent = 0;
		at3_lastforce = 0;
		mapcollisions = 0;
	}

	void reset()
	{
		name[0] = demoflags = authmillis = 0;
		ping = bottomRTT = 9999;
		skin = team = 0;
		position.setsize(0);
		messages.setsize(0);
		connected = connectauth = haswelcome = false;
		lastvotecall = 0;
		vote = VOTE_NEUTRAL;
		lastsaytext[0] = '\0';
		saychars = authreq = 0;
		spawnindex = -1;
		mapchange();
		priv = PRIV_NONE;
		authpriv = -1;
		guid = 0;
		masterverdict = DISC_NONE;
	}

	void zap()
	{
		type = ST_EMPTY;
		priv = PRIV_NONE;
		authpriv = -1;
		guid = 0;
		masterverdict = DISC_NONE;
		connected = connectauth = haswelcome = false;
		removeexplosives();
	}
};

struct savedlimit
{
	enet_uint32 ip;
	int lastvotecall;
	int saychars, lastsay, spamcount;

	void save(client &cl)
	{
		ip = cl.peer->address.host;
		lastvotecall = cl.lastvotecall;
		saychars = cl.saychars;
		lastsay = cl.lastsay;
		spamcount = cl.spamcount;
	}

	void restore(client &cl)
	{
		// obviously don't set his IP
		cl.lastvotecall = lastvotecall;
		cl.saychars = saychars;
		cl.lastsay = lastsay;
		cl.spamcount = spamcount;
	}
};

struct ban
{
	enet_uint32 host;
	int millis;
};

struct server_entity			// server side version of "entity" type
{
	short type, elevation;
	bool spawned;
	int spawntime;
	short x, y;
};

struct server_clip{
	short x, y, elevation;
	uchar xrad, yrad, height;
};

struct sknife{
	int id, millis;
	vec o;
};

struct demofile
{
    string info;
    uchar *data;
    int len;
};

char *loadcfgfile(char *cfg, const char *name, int *len){
	if(name && name[0])
	{
		copystring(cfg, name);
		path(cfg);
	}
	char *p, *buf = loadfile(cfg, len);
	if(!buf)
	{
		if(name) logline(ACLOG_INFO,"could not read config file '%s'", name);
		return NULL;
	}
	if('\r' != '\n') // this is not a joke!
	{
		char c = strchr(buf, '\n') ? ' ' : '\n'; // in files without /n substitute /r with /n, otherwise remove /r
		for(p = buf; (p = strchr(p, '\r')); p++) *p = c;
	}
	for(p = buf; (p = strstr(p, "//")); ) // remove comments
	{
		while(*p != '\n' && *p != '\0') p++[0] = ' ';
	}
	for(p = buf; (p = strchr(p, '\t')); p++) *p = ' ';
	for(p = buf; (p = strchr(p, '\n')); p++) *p = '\0'; // one string per line
	return buf;
}

#define MAXNICKFRAGMENTS 5
enum { NWL_UNLISTED = 0, NWL_PASS, NWL_PWDFAIL, NWL_IPFAIL };

struct nickblacklist {
	struct iprchain	 { struct iprange ipr; const char *pwd; int next; };
	struct blackline	{ int frag[MAXNICKFRAGMENTS]; bool ignorecase; int line; void clear() { loopi(MAXNICKFRAGMENTS) frag[i] = -1; } };
	hashtable<const char *, int> whitelist;
	vector<iprchain> whitelistranges;
	vector<blackline> blacklines;
	vector<const char *> blfraglist;

	void destroylists()
	{
		whitelistranges.setsize(0);
		enumeratek(whitelist, const char *, key, delete key);
		whitelist.clear(false);
		blfraglist.deletecontents();
		blacklines.setsize(0);
	}

	void readnickblacklist(const char *name)
	{
		static string nbfilename;
		static int nbfilesize;
		const char *sep = " ";
		int len, line = 1, errors = 0;
		iprchain iprc;
		blackline bl;

		if(!name && getfilesize(nbfilename) == nbfilesize) return;
		destroylists();
		char *buf = loadcfgfile(nbfilename, name, &len);
		nbfilesize = len;
		if(!buf) return;
		char *l, *s, *r, *p = buf;
		logline(ACLOG_VERBOSE,"reading nickname blacklist '%s'", nbfilename);
		while(p < buf + len)
		{
			l = p; p += strlen(p) + 1;
			l = strtok(l, sep);
			if(l)
			{
				s = strtok(NULL, sep);
				int ic = 0;
				if(s && (!strcmp(l, "accept") || !strcmp(l, "a")))
				{ // accept nickname IP-range
					int *i = whitelist.access(s);
					if(!i) i = &whitelist.access(newstring(s), -1);
					s += strlen(s) + 1;
					while(s < p)
					{
						r = (char *) atoipr(s, &iprc.ipr);
						s += strspn(s, sep);
						iprc.pwd = r && *s ? NULL : newstring(s, strcspn(s, sep));
						if(r || *s)
						{
							iprc.next = *i;
							*i = whitelistranges.length();
							whitelistranges.add(iprc);
							s = r ? r : s + strlen(iprc.pwd);
						}
						else break;
					}
					s = NULL;
				}
				else if(s && (!strcmp(l, "block") || !strcmp(l, "b") || ic++ || !strcmp(l, "blocki") || !strcmp(l, "bi")))
				{ // block nickname fragments (ic == ignore case)
					bl.clear();
					loopi(MAXNICKFRAGMENTS)
					{
						if(ic) strtoupper(s);
						loopvj(blfraglist)
						{
							if(!strcmp(s, blfraglist[j])) { bl.frag[i] = j; break; }
						}
						if(bl.frag[i] < 0)
						{
							bl.frag[i] = blfraglist.length();
							blfraglist.add(newstring(s));
						}
						s = strtok(NULL, sep);
						if(!s) break;
					}
					bl.ignorecase = ic > 0;
					bl.line = line;
					blacklines.add(bl);
				}
				else { logline(ACLOG_INFO," error in line %d, file %s: unknown keyword '%s'", line, nbfilename, l); errors++; }
				if(s && s[strspn(s, " ")]) { logline(ACLOG_INFO," error in line %d, file %s: ignored '%s'", line, nbfilename, s); errors++; }
			}
			line++;
		}
		delete[] buf;
		logline(ACLOG_VERBOSE," nickname whitelist (%d entries):", whitelist.numelems);
		string text;
		enumeratekt(whitelist, const char *, key, int, idx,
		{
			text[0] = '\0';
			for(int i = idx; i >= 0; i = whitelistranges[i].next)
			{
				iprchain &ic = whitelistranges[i];
				if(ic.pwd) concatformatstring(text, "  pwd:\"%s\"", hiddenpwd(ic.pwd));
				else concatformatstring(text, "  %s", iprtoa(ic.ipr));
			}
			logline(ACLOG_VERBOSE, "  accept %s%s", key, text);
		});
		logline(ACLOG_VERBOSE," nickname blacklist (%d entries):", blacklines.length());
		loopv(blacklines)
		{
			text[0] = '\0';
			loopj(MAXNICKFRAGMENTS)
			{
				int k = blacklines[i].frag[j];
				if(k >= 0) { concatstring(text, " "); concatstring(text, blfraglist[k]); }
			}
			logline(ACLOG_VERBOSE, "  %2d block%s%s", blacklines[i].line, blacklines[i].ignorecase ? "i" : "", text);
		}
		logline(ACLOG_INFO,"read %d + %d entries from nickname blacklist file '%s', %d errors", whitelist.numelems, blacklines.length(), nbfilename, errors);
	}

	int checknickwhitelist(const client &c)
	{
		if(c.type != ST_TCPIP) return NWL_PASS;
		iprange ipr;
		ipr.lr = ENET_NET_TO_HOST_32(c.peer->address.host); // blacklist uses host byte order
		int *idx = whitelist.access(c.name);
		if(!idx) return NWL_UNLISTED; // no matching entry
		int i = *idx;
		bool needipr = false, iprok = false, needpwd = false, pwdok = false;
		while(i >= 0)
		{
			iprchain &ic = whitelistranges[i];
			if(ic.pwd)
			{ // check pwd
				needpwd = true;
				if(pwdok || !strcmp(genpwdhash(c.name, ic.pwd, c.salt), c.pwd)) pwdok = true;
			}
			else
			{ // check IP
				needipr = true;
				if(!cmpipmatch(&ipr, &ic.ipr)) iprok = true; // range match found
			}
			i = whitelistranges[i].next;
		}
		if(needpwd && !pwdok) return NWL_PWDFAIL; // wrong PWD
		if(needipr && !iprok) return NWL_IPFAIL; // wrong IP
		return NWL_PASS;
	}

	int checknickblacklist(const char *name)
	{
		if(blacklines.empty()) return -2;  // no nickname blacklist loaded
		string nameuc;
		copystring(nameuc, name);
		strtoupper(nameuc);
		loopv(blacklines)
		{
			loopj(MAXNICKFRAGMENTS)
			{
				int k = blacklines[i].frag[j];
				if(k < 0) return blacklines[i].line; // no more fragments to check
				if(strstr(blacklines[i].ignorecase ? nameuc : name, blfraglist[k]))
				{
					if(j == MAXNICKFRAGMENTS - 1) return blacklines[i].line; // all fragments match
				}
				else break; // this line no match
			}
		}
		return -1; // no match
	}
} nbl;

#define CONFIG_MAXPAR 7

struct configset
{
	string mapname;
	union
	{
		struct { int mode, muts, time, vote, minplayer, maxplayer, skiplines; };
		int par[CONFIG_MAXPAR];
	};
};

// managed character allocation...
struct botname
{
	char *storage;

	botname(){
		storage = new char[MAXNAMELEN+1];
	}

	void putname(char *target, int sk){
		char *name = storage;
		const char *rank = ""; // prepend rank (only if based on skill)

		if(*name == '*') // rank based on skill
		{
			if(*++name == ' ') ++name; // skip space
			// skill is from 45 to 95
			if(sk >= 90) rank = "Lt. "; // 10%
			else if(sk >= 80) rank = "Sgt. "; // 20%
			else if(sk >= 65) rank = "Cpl. "; // 30%
			// 40% Private
			else if(sk >= 55) rank = "Pfc. "; // 20%
			else rank = "Pvt. "; // 20%
		}

		defformatstring(fname)("%s%s", rank, name);
		copystring(target, fname, MAXNAMELEN+1);
	}

	~botname(){
		delete[] storage;
	}
};
vector<botname> botnames;

void mkbotname(client &c){
	if(!m_zombie(gamemode) && botnames.length()) botnames[rnd(botnames.length())].putname(c.name, c.state.level);
	else filtername(c.name, "a bot");
}

void clearai(), checkai();
//void startgame(const char *newname, int newmode, int newtime = -1, bool notify = true);
void resetmap(const char *newname, int newmode, int newmuts, int newtime = -1, bool notify = true);
void disconnect_client(int n, int reason = -1);
int clienthasflag(int cn);
bool updateclientteam(int client, int team, int ftr);
bool refillteams(bool now = false, int ftr = FTR_AUTOTEAM, bool aionly = true);
void setpriv(int client, int priv);
int mapavailable(const char *mapname);
void getservermap(void);
// int findmappath(const char *mapname, char *filename = NULL);
mapstats *getservermapstats(const char *mapname, bool getlayout = false);
void sendf(int cn, int chan, const char *format, ...);

int explosion(client &owner, const vec &o2, int weap, bool gib = true);
/*
int calcscores();
void recordpacket(int chan, void *data, int len);
void senddisconnectedscores(int cn);
void process(ENetPacket *packet, int sender, int chan);
void welcomepacket(packetbuf &p, int n);
void sendwelcome(client *cl, int chan = 1);
void sendpacket(int n, int chan, ENetPacket *packet, int exclude = -1);
int numclients();
void forcedeath(client *cl);

extern bool isdedicated;
extern string smapname;
extern mapstats smapstats;
extern char *maplayout;
*/

#ifdef _DEBUG
// converts message code to char
static const char *messagenames(int n){
	const char *msgnames[N_NUM] = {
		"N_SERVINFO", "N_WELCOME", // before connection
		"N_INITCLIENT", "N_INITAI", "N_SETTEAM", "N_RESUME", "N_MAPIDENT", "N_DISC", "N_DELAI", "N_REASSIGNAI", // sent after (dis)connection
		"N_CLIENT", "N_POS", "N_PHYS", "N_PINGPONG", "N_PINGTIME", // automatic from client
		"N_TEXT", "N_WHOIS", "N_WHOISINFO", "N_NEWNAME", "N_SKIN", "N_LEVELUP", "N_SWITCHTEAM", // user-initiated
		"N_CALLVOTE", "N_CALLVOTEERR", "N_VOTE", "N_VOTERESULT", // votes
		"N_LISTDEMOS", "N_DEMO", "N_DEMOPLAYBACK", // demos
		"N_AUTHREQ", "N_AUTHCHAL", // auth
		"N_REQPRIV", "N_SETPRIV", // privileges
		"N_MAPC2S", "N_MAPS2C", // map transit
		// editmode ONLY
		"N_EDITMODE", "N_EDITH", "N_EDITT", "N_EDITS", "N_EDITD", "N_EDITE", "N_EDITW", "N_EDITENT", "N_NEWMAP",
		// game events
		"N_SHOOT", "N_SHOOTC", "N_PROJ", "N_AKIMBO", "N_RELOAD", // clients to server events
		"N_SG", "N_SCOPE", "N_SUICIDE", "N_QUICKSWITCH", "N_SWITCHWEAP", "N_LOADOUT", "N_THROWNADE", "N_THROWKNIFE", // server directly handled
		"N_RICOCHET", "N_POINTS", "N_KILL", "N_DAMAGE", "N_REGEN", "N_HEAL", "N_KNIFEADD", "N_KNIFEREMOVE", "N_BLEED", "N_STICK", "N_STREAKREADY", "N_STREAKUSE", // server to client
		// gameplay
		"N_TRYSPAWN", "N_SPAWNSTATE", "N_SPAWN", "N_FORCEDEATH", "N_FORCEGIB", // spawning
		"N_ITEMSPAWN", "N_ITEMACC", // items
		"N_DROPFLAG", "N_FLAGINFO", "N_FLAGMSG", "N_FLAGCNT", // flags
		"N_MAPCHANGE", "N_NEXTMAP", // map changes
		"N_TIMEUP", "N_ACCURACY", "N_ARENAWIN", // round end/remaining
		// extensions
		"N_SERVMSG", "N_CONFMSG", "N_EXT",
	};
	if(n < 0 || n >= N_NUM) return "unknown";
	return msgnames[n];
}
#endif

const char *entnames[MAXENTTYPES + 1] =
{
	"none?",
	"light", "playerstart", "pistol", "ammobox","grenades",
	"health", "helmet", "armor", "akimbo",
	"mapmodel", "trigger", "ladder", "ctf-flag", "sound", "clip", "plclip", "max",
};

// pickup stats

itemstat ammostats[WEAP_MAX] =
{
	{1,  1,   2,	S_ITEMAMMO },   // knife dummy
	{24, 60,  72,	S_ITEMAMMO },   // pistol
	{21, 28,  42,	S_ITEMAMMO },   // shotgun
	{96, 128,  192,	S_ITEMAMMO },   // subgun
	{30, 40,  80,	S_ITEMAMMO },   // sniper
	{16, 24,  32,	S_ITEMAMMO },   // bolt sniper
	{90, 120,  180,	S_ITEMAMMO },   // assault
	{1,  1,   3,	S_ITEMAMMO },   // grenade
	{96, 0,   144,	S_ITEMAKIMBO },  // akimbo
	{40, 60,  80,	S_ITEMAMMO },   // heal
	{1,  1,   1,    S_ITEMAMMO }, // sword dummy
	{2,  3,   5,    S_ITEMAMMO }, // crossbow
};

itemstat powerupstats[] =
{
	{35 * HEALTHSCALE, STARTHEALTH, MAXHEALTH, S_ITEMHEALTH }, //health
	{15, STARTARMOR, MAXARMOR, S_ITEMARMOR }, // helmet
	{40, STARTARMOR, MAXARMOR, S_ITEMARMOR }, // armor
};

// weaponry
guninfo guns[WEAP_MAX] =
{
//	{ modelname;     snd,	  rldsnd,  rldtime, atkdelay,  dmg, rngstart, rngend, rngm,psd,ptt,spr,kick,magsz,mkrot,mkback,rcoil,maxrcl,rca,pushf; auto;}
	{ "knife",      S_KNIFE,    S_ITEMAMMO,    0,   500,    80,    3,    4,   80,   0,   0,  1,    1,    1,   0,  0,     0,    0,       0, 5,   true },
	{ "pistol",     S_PISTOL,   S_RPISTOL,  1400,   90,     40,   40,  120,   17,   0,   0, 90,    9,   12,   6,  2,    32,    48,     70, 1,   false},
	{ "shotgun",    S_SHOTGUN,  S_RSHOTGUN,  750,   200,    10,    8,   16,    7,   0,   0,  1,   12,    7,   9,  5,    60,    70,      5, 2,   false},
	{ "subgun",     S_SUBGUN,   S_RSUBGUN,  2400,   67,     40,   32,   80,   22,   0,   0, 70,    4,   32,   1,  3,    23,    45,     65, 1,   true },
	{ "sniper",     S_SNIPER,   S_RSNIPER,  2000,   100,   120,    1,    2,   50,   0,   0,240,   14,   10,   4,  4,    58,    64,     75, 2,   false},
	{ "bolt",       S_BOLT,     S_RBOLT,    2000,   1500,  134,   80,  180,   34,   0,   0,260,   36,    8,   4,  4,    86,    90,     80, 3,   false},
	{ "assault",    S_ASSAULT,  S_RASSAULT, 2100,   73,     32,   40,  100,   12,   0,   0, 60,    3,   30,   0,  3,    24,    38,     60, 1,   true },
	{ "grenade",    S_NULL,     S_NULL,     1000,   650,   250,    0,   32,  225,  20,   6,  1,    1,    1,   3,  1,     0,    0,       0, 4,   false},
	{ "pistol",     S_PISTOL,   S_RAKIMBO,  1400,   80,     40,   45,  160,   17,   0,   0, 56,    8,   24,   6,  2,    28,    48,     70, 2,   true },
	{ "heal",       S_SUBGUN,   S_NULL,     1200,   100,    20,    4,    8,   10,   0,   0, 62,    1,   10,   0,  0,    10,    20,      8, 5,   true },
	{ "sword",      S_NULL,     S_RASSAULT,    0,   480,    90,    4,    7,   90,   0,   0,  1,    1,    1,   0,  2,     0,     0,      0, 0,   true },
	{ "bow",        S_NULL,     S_RASSAULT, 2000,   120,   250,    0,   24,  240,   0,   0, 88,    3,    1,   3,  1,    48,    50,      0, 4,   false},
};

const int obit_suicide(int weap){
	if(melee_weap(weap)) return OBIT_FF;
	if(weap >= 0 && weap <= OBIT_START) return weap;
	switch(weap - OBIT_START){
		case 0: return OBIT_DEATH;
		case 1: return OBIT_DROWN;
		case 2: return OBIT_FALL;
		case 3: return OBIT_FF;
		case 4: return OBIT_BOT;
		case 5: return OBIT_CHEAT;
		case 6: return OBIT_NUKE;
		case 7: return OBIT_TEAM;
		case 8: return OBIT_SPECT;
	}
	return OBIT_DEATH;
}

const char *suicname(int obit){
	static string k;
	*k = 0;
	switch(obit){
		// ricochet specific
		case WEAP_SHOTGUN:
			concatstring(k, "discovered that buckshot bounces");
			break;
		case WEAP_SUBGUN:
		case WEAP_ASSAULT:
			concatstring(k, "sprayed a wall");
			break;
		case WEAP_SNIPER:
		case WEAP_BOLT:
			concatstring(k, "sniped wrong");
			break;
		case WEAP_PISTOL:
		case WEAP_AKIMBO:
			concatstring(k, "ate a bullet");
			break;
		// END of ricochet
		// teams
		case OBIT_SPECT:
			concatstring(k, "abnegated from gameplay");
			break;
		case OBIT_TEAM:
			concatstring(k, "renounced and defected from an old team");
			break;
		// weapons
		case WEAP_GRENADE:
			concatstring(k, "experienced fragmentation");
			break;
		case WEAP_HEAL:
			concatstring(k, "overdosed on drugs");
			break;
		case WEAP_BOW:
			concatstring(k, "failed to use an explosive crossbow");
			break;
		case OBIT_DEATH:
			concatstring(k, "requested suicide");
			break;
		case OBIT_AIRSTRIKE:
			concatstring(k, "called in a danger close mission");
			break;
		case OBIT_NUKE:
			concatstring(k, "deployed a nuke");
			break;
		case OBIT_FF:
			concatstring(k, "tried to knife a teammate");
			break;
		case OBIT_BOT:
			concatstring(k, "acted like a stupid bot");
			break;
		case OBIT_DROWN:
			concatstring(k, "drowned");
			break;
		case OBIT_FALL:
			concatstring(k, "hit the ground");
			break;
		case OBIT_CHEAT:
			concatstring(k, "just got punished for cheating");
			break;
		default:
			concatstring(k, "somehow suicided");
			break;
	}
	return k;
}

const bool isheadshot(int weapon, int style){
	if(!(style & FRAG_GIB)) return false; // only headshots gib
	switch(weapon){
		case WEAP_KNIFE:
		case WEAP_GRENADE:
			if(style & FRAG_FLAG) break; // these weapons headshot if FRAG_FLAG is set
		case WEAP_BOW:
		case WEAP_MAX:
		case WEAP_MAX+5:
			return false; // these weapons cannot headshot
	}
	// headshot = gib if otherwise
	return true;
}

const int toobit(int weap, int style){
	const bool gib = (style & FRAG_GIB) > 0,
				flag = (style & FRAG_FLAG) > 0;
	switch(weap){
		case WEAP_KNIFE: return gib ? WEAP_KNIFE : flag ? OBIT_KNIFE_IMPACT : OBIT_KNIFE_BLEED;
		case WEAP_BOW: return gib ? OBIT_BOW_IMPACT : flag ? OBIT_BOW_STUCK : WEAP_BOW;
		case WEAP_GRENADE: return gib ? WEAP_GRENADE : OBIT_AIRSTRIKE;
		case WEAP_MAX: return OBIT_NUKE;
	}
	return weap < WEAP_MAX ? weap : OBIT_DEATH;
}

const char *killname(int obit, bool headshot){
	static string k;
	*k = 0;
	switch(obit){
		case WEAP_GRENADE:
			concatstring(k, "obliterated");
			break;
		case WEAP_KNIFE:
			concatstring(k, headshot ? "decapitated" : "slashed");
			break;
		case WEAP_BOLT:
			concatstring(k, headshot ? "overkilled" : "quickly killed");
			break;
		case WEAP_SNIPER:
			concatstring(k, headshot ? "expertly sniped" : "sniped");
			break;
		case WEAP_SUBGUN:
			concatstring(k, headshot ? "perforated" : "spliced");
			break;
		case WEAP_SHOTGUN:
			concatstring(k, headshot ? "splattered" : "scrambled");
			break;
		case WEAP_ASSAULT:
			concatstring(k, headshot ? "eliminated" : "shredded");
			break;
		case WEAP_PISTOL:
			concatstring(k, headshot ? "capped" : "pierced");
			break;
		case WEAP_AKIMBO:
			concatstring(k, headshot ? "blasted" : "skewered");
			break;
		case WEAP_HEAL:
			concatstring(k, headshot ? "tranquilized" : "injected");
			break;
		case WEAP_SWORD:
			concatstring(k, headshot ? "sliced" : "impaled");
			break;
		case WEAP_BOW:
			concatstring(k, "detonated");
			break;
		// special obits
		case OBIT_BOW_IMPACT:
			concatstring(k, "impacted");
			break;
		case OBIT_BOW_STUCK:
			concatstring(k, "plastered");
			break;
		case OBIT_KNIFE_BLEED:
			concatstring(k, "fatally wounded");
			break;
		case OBIT_KNIFE_IMPACT:
			concatstring(k, "thrown down");
			break;
		case OBIT_AIRSTRIKE:
			concatstring(k, "bombarded");
			break;
		case OBIT_NUKE:
			concatstring(k, "nuked");
			break;
		default:
			concatstring(k, headshot ? "pwned" : "killed");
			break;
	}
	return k;
}

// perk-related
float gunspeed(int gun, int ads, bool lightweight){
	float ret = lightweight ? 1.07f : 1;
	if(ads) ret *= 1 - ads / (lightweight ? 3500 : 3000.f);
	switch(gun){
		case WEAP_KNIFE:
		case WEAP_PISTOL:
		case WEAP_GRENADE:
		case WEAP_HEAL:
			//ret *= 1;
			break;
		case WEAP_AKIMBO:
			ret *= .99f;
			break;
		case WEAP_SWORD:
			ret *= .98f;
			break;
		case WEAP_SHOTGUN:
			ret *= .97f;
			break;
		case WEAP_SNIPER:
		case WEAP_BOLT:
			ret *= .95f;
			break;
		case WEAP_SUBGUN:
			ret *= .93f;
			break;
		case WEAP_ASSAULT:
		case WEAP_BOW:
			ret *= .9f;
			break;
	}
	return ret;
}

int classic_forceperk(int primary){
	switch(primary){ // no need for break;
		case WEAP_KNIFE:
		case WEAP_PISTOL:
		case WEAP_GRENADE:
		case WEAP_SWORD:
			// one handed: move faster
			return PERK_SPEED;
		case WEAP_SHOTGUN:
		case WEAP_SUBGUN: return PERK_VISION;
		case WEAP_SNIPER:
		case WEAP_BOLT: return PERK_STEADY; // AC had lower spread for snipers
		case WEAP_ASSAULT: return PERK_POWER;
		case WEAP_HEAL: return PERK_PERSIST; // hard to kill, easy to heal
		/*
		// undefined
		case WEAP_BOW:
		case WEAP_AKIMBO:
			break;
		*/
	}
	return PERK_NONE;
}

// format names for the server
const char *formatname(client *c)
{
	if(!c) return "unknown";
	static string cname[3];
	static int idx = 0;
	if(idx >= 3) idx %= 3;
	formatstring(cname[idx])(c->type == ST_AI ? "%s (%d-%d)" : "%s (%d)", c->name, c->clientnum, c->state.ownernum);
	return cname[idx++];
}

// overloading!
const char *formatname(client &c)
{
	return formatname(&c);
}
