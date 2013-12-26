// server.h

#define SERVER_BUILTIN_MOD 0
// 1 = super knife (gib only)
// 2 = moon jump (gib only)
// 4 = moon jump always on (no effect unless 2 is set)
// 8 = gungame
// 16 = explosive ammo
// 32 = moonjump with no damage (mario)
// 64 = /suicide for nuke
// 128 = no explosive zombies

#define gamemode smode   // allows the gamemode macros to work with the server mode
#define mutators smuts // and mutators too

#define valid_flag(f) (f >= 0 && f < 2)

enum { GE_NONE = 0, /* sequenced */ GE_SHOT, GE_PROJ, GE_AKIMBO, GE_RELOAD, /* immediate */ GE_SUICIDEBOMB, /* unsequenced */ GE_HEAL, GE_AIRSTRIKE };
enum { ST_EMPTY, ST_LOCAL, ST_TCPIP, ST_AI };

#if (SERVER_BUILTIN_MOD & 8)
const int gungame[] = {
	WEAP_ASSAULT2,
	WEAP_SNIPER,
	WEAP_SNIPER2,
	WEAP_ASSAULT,
	WEAP_SUBGUN,
	WEAP_BOLT,
	WEAP_SHOTGUN,
	WEAP_PISTOL,
	WEAP_RPG,
	WEAP_HEAL, // nuke after killing with this
};
const int GUNGAME_MAX = sizeof(gungame)/sizeof(*gungame);
#endif

extern bool canreachauthserv;

static int interm = 0, minremain = 0, gamemillis = 0, gamelimit = 0, gamemusicseed = 0;
static const int DEATHMILLIS = 300;
int smode = G_DM, smuts = G_M_TEAM, mastermode = MM_OPEN, botbalance = -1;
int progressiveround = 1, zombiebalance = 1, zombiesremain = 1;

struct client;

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
	virtual bool flush(client *ci, int fmillis);
	virtual void process(client *ci) = 0;
};

struct shotevent : timedevent
{
	int weap;
	vec to;
	vector<posinfo> pos;
	shotevent(int millis, int id, int weap) : timedevent(GE_SHOT, millis, id), weap(weap) { to = vec(0, 0, 0); pos.setsize(0); }
	bool compact;
	void process(client *ci);
};

struct destroyevent : timedevent
{
	int weap, flags;
	vec o;
	destroyevent(int millis, int id, int weap, int flags, const vec &o) : timedevent(GE_PROJ, millis, id), weap(weap), flags(flags), o(o) {}
	void process(client *ci);
};

// switchevent?

struct akimboevent : timedevent
{
	akimboevent(int millis, int id) : timedevent(GE_AKIMBO, millis, id) {}
	void process(client *ci);
};

struct reloadevent : timedevent
{
	int weap;
	reloadevent(int millis, int id, int weap) : timedevent(GE_RELOAD, millis, id), weap(weap) {}
	void process(client *ci);
};

// unordered
/*
struct bowevent : timedevent
{
	vec o;
	void process(client *ci);
};
*/

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
	vec o, aim, vel, lasto, lastspeedo, sg[SGRAYS], flagpickupo;
	float pitchvel, aspeed;
	int state, omillis, lastomillis, lmillis, smillis, movemillis, ldt, spj, speedtime, speedallow;
	int lastdeath, lastpain, lastffkill, lastspawn, lifesequence, skin, streakused;
	int lastkill, combo;
	bool crouching, onfloor; float fallz;
	int crouchmillis, scopemillis;
	int drownmillis; char drownval;
	int streakondeath;
	int lastshot, lastregen;
	projectilestate<6> grenades; // 5000ms TLL / (we can throw one every 650ms+200ms) = 6 nades possible
	projectilestate<3> knives;
	int akimbomillis;
	int points, flagscore, frags, deaths, shotdamage, damage;
	ivector revengelog;
	vector<wound> wounds;
	bool valid;
#if (SERVER_BUILTIN_MOD & 8)
	int gungame;
#endif

	clientstate() : state(CS_DEAD), valid(true), playerstate() {}

	bool isalive(int gamemillis)
	{
		if(interm) return false;
		return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
	}

	bool waitexpired(int gamemillis)
	{
		const int wait = gamemillis - lastshot;
		loopi(WEAP_MAX) if(wait < gunwait[i]) return false;
		return true;
	}

	void updateshot(int gamemillis)
	{
		const int wait = gamemillis-lastshot;
		loopi(WEAP_MAX)
			if(gunwait[i])
				gunwait[i] = max(gunwait[i] - wait, 0);
		lastshot = gamemillis;
	}

	void allowspeeding(int gamemillis, int allow){
		speedallow = max(speedallow, gamemillis + allow);
	}

	clientstate &invalidate(){
		valid = false;
		return *this;
	}

	void reset()
	{
		state = CS_DEAD;
		lifesequence = -1;
		skin = 0;
		grenades.reset();
		knives.reset();
		akimbomillis = 0;
		points = flagscore = frags = deaths = shotdamage = damage = lastffkill = 0;
		revengelog.setsize(0);
		pointstreak = deathstreak = streakused = 0;
		radarearned = airstrikes = nukemillis = 0;
		valid = true;
#if (SERVER_BUILTIN_MOD & 8)
		gungame = 0;
#endif
		respawn();
	}

	void respawn()
	{
		playerstate::respawn(gamemode, mutators);
		o = lasto = lastspeedo = vec(-1e10f, -1e10f, -1e10f);
		aim = vel = vec(0, 0, 0);
		pitchvel = aspeed = speedtime = speedallow = 0;
		omillis = lastomillis = lmillis = smillis = movemillis = ldt = spj = 0;
		drownmillis = drownval = 0;
		lastspawn = -1;
		lastdeath = lastpain = lastshot = lastregen = 0;
		lastkill = combo = 0;
		akimbomillis = 0;
		damagelog.setsize(0);
		wounds.shrink(0); // no more wounds!
		crouching = false;
		crouchmillis = scopemillis = 0;
		onfloor = false;
		fallz = -1e10f;
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
	int points, frags, assists, flagscore, deaths, shotdamage, damage;

	void save(clientstate &cs)
	{
		points = cs.points;
		frags = cs.frags;
		assists = cs.assists;
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
	int ping, team, vote, priv;
	bool muted;
	int acversion, acbuildtype, acthirdperson;
	int connectmillis;
	bool connected, connectauth, wantsmap;
	int authtoken, authuser, authpriv, masterverdict, guid; uint authreq; string authname;
	bool haswelcome;
	bool isonrightmap;
	bool timesync;
	int overflow;
	int gameoffset, lastevent, lastvotecall, lastkickcall, name_relay;
	char newname[MAXNAMELEN+1];
	int demoflags;
	clientstate state;
	vector<timedevent *> events, timers;
	vector<uchar> position, messages;
	string lastsaytext;
	int saychars, lastsay, spamcount;
	int lastdemothrottle, democount;
	int at3_score, at3_lastforce, eff_score;
	bool at3_dontmove;
	int spawnindex;
	int salt;
	string pwd;
	int mapcollisions;
	enet_uint32 bottomRTT;
#if (SERVER_BUILTIN_MOD & 64)
	bool nuked;
#endif

	void addevent(timedevent *e)
	{
		if(events.length()<256) events.add(e);
		else delete e;
	}

	void addtimer(timedevent *e)
	{
		if(timers.length()<256) timers.add(e);
		else delete e;
	}

	void invalidateheals()
	{
		loopv(timers) if(timers[i]->type == GE_HEAL) timers[i]->valid = false;
	}

	int getmillis(int millis, int id)
	{
		if(!timesync || (!events.length() && state.waitexpired(millis)))
		{
			timesync = true;
			gameoffset = millis-id;
			return millis;
		}
		return gameoffset+id;
	}

	void removeexplosives() {
		state.grenades.reset(); // remove active/flying nades
		state.knives.reset(); // remove active/flying knives (usually useless, since knives are fast)
		// remove all dealt wounds
		extern vector<client *> clients;
		loopv(clients){
			if(clients[i]->type == ST_EMPTY) continue;
			clientstate &cs = clients[i]->state;
			loopvj(cs.wounds)
				if(cs.wounds[j].inflictor == clientnum)
					//cs.wounds.remove(j--);
					cs.wounds[j].inflictor = -1;
		}
	}

	void suicide(int weap, int flags = FRAG_NONE){
		extern void serverdied(client *target, client *actor, int damage, int gun, int style, const vec &source, float killdist = 0.f);
		if(state.state != CS_DEAD) serverdied(this, this, 0, weap, flags, state.o);
	}

	void mapchange()
	{
		state.reset();
		events.deletecontents();
		timers.deletecontents();
		removeexplosives();
		overflow = 0;
		timesync = wantsmap = false;
		isonrightmap = m_edit(gamemode);
		lastevent = 0;
		at3_lastforce = 0;
		mapcollisions = 0;
#if (SERVER_BUILTIN_MOD & 64)
		nuked = false;
#endif
	}

	void reset()
	{
		name[0] = demoflags = 0;
		ping = bottomRTT = 9999;
		team = 0; // TEAM_RED
		position.setsize(0);
		messages.setsize(0);
		connected = connectauth = wantsmap = haswelcome = false;
		lastvotecall = lastkickcall = 0;
		vote = VOTE_NEUTRAL;
		lastsaytext[0] = '\0';
		newname[0] = '\0';
		name_relay = 0;
		saychars = 0;
		authtoken = authuser = authreq = 0;
		authpriv = -1;
		authname[0] = '\0';
		lastdemothrottle = democount = 0;
		spawnindex = -1;
		mapchange();
		priv = PRIV_NONE;
		muted = false;
		authpriv = -1;
		acversion = acbuildtype = acthirdperson = guid = 0;
		masterverdict = DISC_NONE;
	}

	void zap()
	{
		type = ST_EMPTY;
		priv = PRIV_NONE;
		muted = false;
		authtoken = authuser = authreq = 0;
		authpriv = -1;
		authname[0] = '\0';
		guid = 0;
		masterverdict = DISC_NONE;
		connected = connectauth = wantsmap = haswelcome = false;
		removeexplosives();
	}
};

struct savedlimit
{
	enet_uint32 ip;
	int lastvotecall, lastkickcall, lastdemothrottle;
	int saychars, lastsay, spamcount, democount;
#if (SERVER_BUILTIN_MOD & 64)
	bool nuked;
#endif

	void save(client &cl)
	{
		ip = cl.peer->address.host;
		lastvotecall = cl.lastvotecall;
		lastkickcall = cl.lastkickcall;
		lastdemothrottle = cl.lastdemothrottle;
		saychars = cl.saychars;
		lastsay = cl.lastsay;
		spamcount = cl.spamcount;
		democount = cl.democount;
#if (SERVER_BUILTIN_MOD & 64)
		nuked = cl.nuked;
#endif
	}

	void restore(client &cl)
	{
		// obviously don't set his IP
		cl.lastvotecall = lastvotecall;
		cl.lastkickcall = lastkickcall;
		cl.lastdemothrottle = lastdemothrottle;
		cl.saychars = saychars;
		cl.lastsay = lastsay;
		cl.spamcount = spamcount;
		cl.democount = democount;
#if (SERVER_BUILTIN_MOD & 64)
		cl.nuked = nuked;
#endif
	}
};

struct ban
{
	enet_uint32 host;
	int millis;
};

struct sconfirm{
	int id, team, actor, target;
	int points, frag, death;
	vec o;
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

#define FORBIDDENSIZE 15
struct serverforbiddenlist
{
	int num;
    char entries[100][2][FORBIDDENSIZE+1]; // 100 entries and 2 words (15 chars each) per entry is more than enough

    void initlist()
    {
        num = 0;
        memset(entries,'\0',2*100*(FORBIDDENSIZE+1));
    }

    void addentry(char *s)
    {
        int len = strlen(s);
        if ( len > 128 || len < 3 ) return;
        int n = 0;
        string s1, s2;
        char *c1 = s1, *c2 = s2;
        if (num < 100 && (n = sscanf(s,"%s %s",s1,s2)) > 0 ) // no warnings
        {
            strncpy(entries[num][0],c1,FORBIDDENSIZE);
            if ( n > 1 ) strncpy(entries[num][1],c2,FORBIDDENSIZE);
            else entries[num][1][0]='\0';
            num++;
        }
    }

    void read(const char *name)
    {
		static string forbiddenfilename;
		static int forbiddenfilesize;
        if(!name && getfilesize(forbiddenfilename) == forbiddenfilesize) return;
        initlist();
		int len;
        char *buf = loadcfgfile(forbiddenfilename, name, &len);
		forbiddenfilesize = len;
		if(!buf) return;

        char *l, *p = buf;
        logline(ACLOG_VERBOSE, "reading forbidden list '%s'", forbiddenfilename);
        while(p < buf + forbiddenfilesize)
        {
            l = p; p += strlen(p) + 1;
            addentry(l);
        }
		logline(ACLOG_INFO,"read %d forbidden entries from '%s'", num, forbiddenfilename);
        DELETEA(buf);
    }

    const char *forbidden(const char *s)
    {
        loopi(num){
			bool found = true;
			loopj(2){
				if(*entries[i][j] == '\0')
					break;
				else if(!findpattern(s,entries[i][j])){
					found = false;
					break;
				}
			}
			if(found){
				static char match[2*(FORBIDDENSIZE+1)];
				copystring(match, entries[i][0]);
				for(int j = 1; j<2; ++j){
					if(!*entries[i][j]) break;
					concatformatstring(match, " %s", entries[i][j]);
				}
				return match;
			}
        }
        return NULL;
    }
} forbiddens;

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

void clearai(), checkai();
//void startgame(const char *newname, int newmode, int newtime = -1, bool notify = true);
void resetmap(const char *newname, int newmode, int newmuts, int newtime = -1, bool notify = true);
void disconnect_client(int n, int reason = -1);
int clienthasflag(int cn);
bool updateclientteam(int client, int team, int ftr);
void convertcheck(bool quick = false);
void shuffleteams(int ftr = FTR_AUTOTEAM);
bool refillteams(bool now = false, int ftr = FTR_AUTOTEAM, bool aionly = true);
void setpriv(int client, int priv);
int mapavailable(const char *mapname);
void getservermap(void);
// int findmappath(const char *mapname, char *filename = NULL);
mapstats *getservermapstats(const char *mapname, bool getlayout = false, int *maploc = NULL);
int findmappath(const char *mapname, char *filename = NULL);
void sendf(int cn, int chan, const char *format, ...);
void sendteamscore(int team, int reciever = -1);

int explosion(client &owner, const vec &o2, int weap, bool teamcheck, bool gib = true, client *cflag = NULL);
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
void sendheadshot(const vec &from, const vec &to, int damage);

enum { MAP_NOTFOUND = 0, MAP_TEMP, MAP_CUSTOM, MAP_LOCAL, MAP_OFFICIAL, MAP_MAX };
const char *maplocstr[MAP_MAX] = { "not found", "incoming", "custom", "local", "official", };
#define readonlymap(x) ((x) >= MAP_CUSTOM)
#define distributablemap(x) ((x) == MAP_TEMP || (x) == MAP_CUSTOM)

#ifdef _DEBUG
// converts message code to char
const char *messagenames(int n){
	const char *msgnames[N_NUM] = {
		"N_SERVINFO", "N_WELCOME", // before connection
		"N_INITCLIENT", "N_INITAI", "N_SETTEAM", "N_RESUME", "N_MAPIDENT", "N_DISC", "N_DELAI", "N_REASSIGNAI", // sent after (dis)connection
		"N_CLIENT", "N_POS", "N_SOUND", "N_PINGPONG", "N_PINGTIME", // automatic from client
		"N_TEXT", "N_WHOIS", "N_WHOISINFO", "N_NEWNAME", "N_SKIN", "N_THIRDPERSON", "N_LEVEL", "N_SWITCHTEAM", // user-initiated
		"N_CALLVOTE", "N_CALLVOTEERR", "N_VOTE", "N_VOTEREMAIN", "N_VOTERESULT", // votes
		"N_LISTDEMOS", "N_DEMO", "N_DEMOPLAYBACK", // demos
		"N_AUTHREQ", "N_AUTHCHAL", // auth
		"N_CLAIMPRIV", "N_SETPRIV", // privileges
		"N_MAPC2S", "N_MAPS2C", "N_MAPDELETE", "N_MAPREQ", "N_MAPFAIL", // map transit
		// editmode ONLY
		"N_EDITMODE", "N_EDITH", "N_EDITT", "N_EDITS", "N_EDITD", "N_EDITE", "N_EDITW", "N_EDITENT", "N_NEWMAP",
		// game events
		"N_SHOOT", "N_SHOOTC", "N_PROJ", "N_AKIMBO", "N_RELOAD", // clients to server events
		"N_SG", "N_SUICIDE", "N_QUICKSWITCH", "N_SWITCHWEAP", "N_LOADOUT", "N_THROWNADE", "N_THROWKNIFE", // server directly handled
		"N_RICOCHET", "N_REGEN", "N_HEAL", "N_BLEED", "N_STREAKREADY", "N_STREAKUSE", "N_HEADSHOT", "N_MULTI", // server to client
		"N_KNIFEADD", "N_KNIFEREMOVE", // knives
		"N_CONFIRMADD", "N_CONFIRMREMOVE", // kill confirmed
		// gameplay
		"N_POINTS", "N_SCORE", "N_TEAMSCORE", "N_KILL", "N_DAMAGE", // scoring
		"N_TRYSPAWN", "N_SPAWNSTATE", "N_SPAWN", "N_FORCEDEATH", "N_FORCEGIB", // spawning
		"N_ITEMSPAWN", "N_ITEMACC", // items
		"N_DROPFLAG", "N_FLAGINFO", "N_FLAGMSG", "N_FLAGSECURE", // flags
		"N_MAPCHANGE", "N_NEXTMAP", // map changes
		"N_TIMEUP", "N_ACCURACY", "N_ARENAWIN", "N_ZOMBIESWIN", "N_CONVERTWIN" // round end/remaining
		// extensions
		"N_SERVMSG", "N_EXT",
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

const itemstat ammostats[WEAP_MAX] =
{
	{ 1,  1,  2,  S_ITEMAMMO },    // knife dummy
	{ 2,  5,  6,  S_ITEMAMMO },    // pistol
	{21, 28, 42,  S_ITEMAMMO },    // shotgun
	{ 3,  4,  6,  S_ITEMAMMO },    // subgun
	{ 1,  2,  3,  S_ITEMAMMO },    // sniper
	{ 2,  3,  4,  S_ITEMAMMO },    // bolt sniper
	{ 3,  4,  6,  S_ITEMAMMO },    // m16
	{ 1,  1,  3,  S_ITEMAMMO },    // grenade
	{ 4,  0,  6,  S_ITEMAKIMBO },  // akimbo
	{ 4,  6,  8,  S_ITEMAMMO },    // heal
	{ 1,  1,  1,  S_ITEMAMMO },    // sword dummy
	{ 1,  2,  3,  S_ITEMAMMO },    // RPG
	{ 3,  4,  6,  S_ITEMAMMO },    // ak47
	{ 2,  3,  4,  S_ITEMAMMO },    // sniper2
};

const itemstat powerupstats[] =
{
	{35 * HEALTHSCALE, STARTHEALTH, MAXHEALTH, S_ITEMHEALTH }, //health
	{15, STARTARMOR, MAXARMOR, S_ITEMHELMET }, // helmet
	{40, STARTARMOR, MAXARMOR, S_ITEMARMOUR }, // armor
};

// weaponry
const mul muls[MUL_NUM] =
{
	//{ head, torso, leg; }
	{ 5.5f, 1.2f, 1.f }, // normal
	{ 4.0f, 1.4f, 1.f }, // snipers
	{ 5.0f, 1.3f, 1.f } // shotgun
};

guninfo guns[WEAP_MAX] =
{
//  { modelname;    sound,      reload,    reloadtime, attackdelay, damage, range, endrange, rangeminus, projspeed, part, spread, spreadrem, kick, addsize, magsize, mdl_kick_rot, mdl_kick_back, recoil, maxrecoil, recoilangle, pushfactor; auto;}
    { "knife",      S_KNIFE,    S_ITEMAMMO,         0,         500,     80,     4,        5,         72,         0,    0,      1,         0,    1,       0,       1,            0,             0,      0,         0,           0,          3, true },
    { "pistol",     S_PISTOL,   S_RPISTOL,       1400,          90,     32,    24,       90,          8,         0,    0,     90,        90,    9,      12,      13,            6,             2,     32,        48,          70,          1, false},
    { "shotgun",    S_SHOTGUN,  S_RSHOTGUN,       750,         200,     10,     6,       16,          7,         0,    0,    199,         9,   12,       1,       6,            9,             5,     60,        70,           5,          2, false},
    { "subgun",     S_SUBGUN,   S_RSUBGUN,       2400,          67,     35,    20,       64,         20,         0,    0,     70,        93,    4,      32,      33,            1,             3,     27,        45,          65,          1, true },
    { "sniper",     S_SNIPER,   S_RSNIPER,       2000,         120,     45,    70,      110,          9,         0,    0,    235,        96,   14,      20,      21,            4,             4,     59,        68,          75,          2, false},
    { "bolt",       S_BOLT,     S_RBOLT,         2000,        1500,    120,    80,      130,         48,         0,    0,    250,        97,   36,       8,       9,            4,             4,     86,        90,          80,          3, false},
    { "assault",    S_ASSAULT,  S_RASSAULT,      2100,          73,     28,    45,       92,          9,         0,    0,     65,        95,    3,      30,      31,            0,             3,     25,        42,          60,          1, true },
    { "grenade",    S_NULL,     S_NULL,          1000,         650,    220,     0,       55,         27,        20,    6,      1,         0,    1,       0,       1,            3,             1,      0,         0,           0,          3, false},
    { "pistol",     S_PISTOL,   S_RAKIMBO,       1400,          80,     32,    30,       90,          8,         0,    0,     60,         0,    8,      24,      26,            6,             2,     28,        49,          72,          2, true },
    { "heal",       S_SUBGUN,   S_NULL,          1200,         100,     20,     4,        8,         10,         0,    0,     50,         1,    1,      10,      11,            0,             0,     10,        20,           8,          4, true },
    { "sword",      S_SWORD,    S_NULL,             0,         480,     90,     7,        9,         81,         0,    0,      1,         0,    1,       0,       1,            0,             2,      0,         0,           0,          0, true },
    { "rpg",        S_RPG,      S_NULL,          2000,         120,    190,     0,       32,         18,         0,    0,    200,        75,    3,       1,       1,            3,             1,     48,        50,           0,          2, false},
    { "assault2",   S_ASSAULT2, S_RASSAULT2,     2000,         100,     42,    48,      120,         12,         0,    0,    150,        94,    3,      30,      31,            0,             3,     30,        47,          62,          1, true },
    { "sniper2",    S_SNIPER2,  S_RSNIPER2,      2000,         120,    110,    75,      120,         45,         0,    0,    300,        98,  120,      10,      11,            4,             4,     95,        96,          85,          5, false},
};

const int obit_suicide(int weap){
	if(melee_weap(weap)) return OBIT_FF;
	if(weap >= 0 && weap <= OBIT_START) return weap;
	switch(weap - OBIT_START){
		case 0: return OBIT_ASSIST;
		case 1: return OBIT_NUKE;
		case 2: return OBIT_FALL;
		case 3: return OBIT_FALL_WATER;
		case 10: return OBIT_DEATH;
		case 11: return OBIT_BOT;
		case 12: return OBIT_CHEAT;
		case 13: return OBIT_DROWN;
		case 14: return OBIT_SPAWN;
		case 21: return OBIT_TEAM;
		case 22: return OBIT_SPECT;
	}
	return OBIT_DEATH;
}

const char *suicname(int obit){
	static string k;
	*k = 0;
	switch(obit){
		// ricochet from teamkill specific
		case WEAP_SHOTGUN:
			concatstring(k, "suic_shotgun");
			break;
		case WEAP_SUBGUN:
		case WEAP_ASSAULT:
		case WEAP_ASSAULT2:
			concatstring(k, "suic_rifle");
			break;
		case WEAP_SNIPER:
		case WEAP_BOLT:
		case WEAP_SNIPER2:
			concatstring(k, "suic_snipe");
			break;
		case WEAP_PISTOL:
		case WEAP_AKIMBO:
			concatstring(k, "suic_pistol");
			break;
		// END of ricochet
		// teams
		case OBIT_SPECT:
			concatstring(k, "suic_spect");
			break;
		case OBIT_TEAM:
			concatstring(k, "suic_team");
			break;
		// weapons
		case WEAP_GRENADE:
			concatstring(k, "suic_nade");
			break;
		case WEAP_HEAL:
			concatstring(k, "suic_heal");
			break;
		case WEAP_RPG:
			concatstring(k, "suic_rpg");
			break;
		case OBIT_DEATH:
			concatstring(k, "suic_death");
			break;
		case OBIT_AIRSTRIKE:
			concatstring(k, "suic_airstrike");
			break;
		case OBIT_NUKE:
			concatstring(k, "suic_nuke");
			break;
		case OBIT_FF:
			concatstring(k, "suic_ff");
			break;
		case OBIT_BOT:
			concatstring(k, "suic_bot");
			break;
		case OBIT_DROWN:
			concatstring(k, "suic_drown");
			break;
		case OBIT_FALL:
			concatstring(k, "suic_fall");
			break;
		case OBIT_FALL_WATER:
			concatstring(k, "suic_fall_water");
			break;
		case OBIT_CHEAT:
			concatstring(k, "suic_cheat");
			break;
		case OBIT_SPAWN:
			concatstring(k, "suic_spawn");
			break;
		default:
			concatstring(k, "suicide");
			break;
	}
	return k;
}

const bool isheadshot(int weapon, int style){
	if(!(style & FRAG_GIB)) return false; // only gibs headshot
	switch(weapon){
		case WEAP_KNIFE:
		case WEAP_SWORD:
		case WEAP_GRENADE:
		case WEAP_RPG:
			if(style & FRAG_FLAG) break; // these weapons headshot if FRAG_FLAG is set
		case WEAP_MAX + 1:
		case WEAP_MAX + 10:
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
		case WEAP_RPG: return gib ? OBIT_IMPACT : flag ? OBIT_RPG_STUCK : WEAP_RPG;
		case WEAP_GRENADE: return gib ? WEAP_GRENADE : OBIT_AIRSTRIKE;
		case WEAP_MAX + 0: return OBIT_ASSIST; // assisted suicide
		case WEAP_MAX + 1: return OBIT_NUKE;
		case WEAP_MAX + 2: return OBIT_FALL;
		case WEAP_MAX + 3: return OBIT_FALL_WATER; // splash?
	}
	return weap < WEAP_MAX ? weap : OBIT_DEATH;
}

const char *killname(int obit, bool headshot){
	static string k;
	*k = 0;
	switch(obit){
		case WEAP_GRENADE:
			concatstring(k, headshot ? "kill_nade_hs" : "kill_nade");
			break;
		case WEAP_KNIFE:
			concatstring(k, headshot ? "kill_knife_hs" : "kill_knife");
			break;
		case WEAP_BOLT:
			concatstring(k, headshot ? "kill_bolt_hs" : "kill_bolt");
			break;
		case WEAP_SNIPER:
			concatstring(k, headshot ? "kill_sniper_hs" : "kill_sniper");
			break;
		case WEAP_SNIPER2:
			concatstring(k, headshot ? "kill_sniper2_hs" : "kill_sniper2");
			break;
		case WEAP_SUBGUN:
			concatstring(k, headshot ? "kill_smg_hs" : "kill_smg");
			break;
		case WEAP_SHOTGUN:
			concatstring(k, headshot ? "kill_shotgun_hs" : "kill_shotgun");
			break;
		case WEAP_ASSAULT:
			concatstring(k, headshot ? "kill_ar_hs" : "kill_ar");
			break;
		case WEAP_ASSAULT2:
			concatstring(k, headshot ? "kill_ak_hs" : "kill_ak");
			break;
		case WEAP_PISTOL:
			concatstring(k, headshot ? "kill_pistol_hs" : "kill_pistol");
			break;
		case WEAP_AKIMBO:
			concatstring(k, headshot ? "kill_akimbo_hs" : "kill_akimbo");
			break;
		case WEAP_HEAL:
			concatstring(k, headshot ? "kill_heal_hs" : "kill_heal");
			break;
		case WEAP_SWORD:
			concatstring(k, headshot ? "kill_sword_hs" : "kill_sword");
			break;
		case WEAP_RPG:
			concatstring(k, "kill_rpg");
			break;
		// special obits
		case OBIT_IMPACT:
			concatstring(k, headshot ? "kill_impact_hs" : "kill_impact");
			break;
		case OBIT_RPG_STUCK:
			concatstring(k, "kill_rpg_stuck");
			break;
		case OBIT_KNIFE_BLEED:
			concatstring(k, "kill_knife_bleed");
			break;
		case OBIT_KNIFE_IMPACT:
			concatstring(k, "kill_knife_impact");
			break;
		case OBIT_AIRSTRIKE:
			concatstring(k, "kill_airstrike");
			break;
		case OBIT_ASSIST:
			concatstring(k, headshot ? "kill_assist_hs" : "kill_assist");
			break;
		case OBIT_FALL:
			concatstring(k, "kill_fall");
			break;
		case OBIT_NUKE:
			concatstring(k, "kill_nuke");
			break;
		default:
			concatstring(k, headshot ? "kill_hs" : "kill");
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
		case WEAP_SWORD:
			ret *= .98f;
			break;
		case WEAP_AKIMBO:
			ret *= .96f;
			break;
		case WEAP_SNIPER:
		case WEAP_BOLT:
		case WEAP_SNIPER2:
			ret *= .93f;
			break;
		case WEAP_SHOTGUN:
		case WEAP_SUBGUN:
			ret *= .93f;
			break;
		case WEAP_ASSAULT:
		case WEAP_ASSAULT2:
		case WEAP_RPG:
			ret *= .92f;
			break;
	}
	return ret;
}

// format names for the server
const char *formatname(client *c)
{
	if(!c) return "unknown";
	static string cname[3];
	static int idx = 0;
	if(idx >= 3) idx %= 3;
	if(c->type == ST_AI) formatstring(cname[idx])("%s [%d-%d]", c->name, c->clientnum, c->state.ownernum);
	else formatstring(cname[idx])("%s (%d)", c->name, c->clientnum);
	return cname[idx++];
}

// overloading!
const char *formatname(client &c)
{
	return formatname(&c);
}
