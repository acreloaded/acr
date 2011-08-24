// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "pch.h"

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#define _dup	dup
#define _fileno fileno
#endif

#include "cube.h"
#include "servercontroller.h"

#define DEBUGCOND (true)

void checkai();
void resetmap(const char *newname, int newmode, int newtime = -1, bool notify = true);
void disconnect_client(int n, int reason = -1);
int clienthasflag(int cn);
bool updateclientteam(int client, int team, int ftr);
bool refillteams(bool now = false, int ftr = FTR_AUTOTEAM);
void setpriv(int client, int priv);
int mapavailable(const char *mapname);
void getservermap(void);
mapstats *getservermapstats(const char *mapname, bool getlayout = false);

servercontroller *svcctrl = NULL;
struct servercommandline scl;

#define valid_flag(f) (f >= 0 && f < 2)

#define SERVERMAP_PATH		  "packages/maps/servermaps/"
#define SERVERMAP_PATH_BUILTIN  "packages/maps/official/"
#define SERVERMAP_PATH_INCOMING "packages/maps/servermaps/incoming/"

static const int DEATHMILLIS = 300;

enum { GE_NONE = 0, GE_SHOT, GE_PROJ, GE_AKIMBO, GE_RELOAD };
enum { ST_EMPTY, ST_LOCAL, ST_TCPIP, ST_AI };

int mastermode = MM_OPEN, botbalance = -1;

// allows the gamemode macros to work with the server mode
#define gamemode smode
string smapname, nextmapname;
int smode = 0, nextgamemode;
mapstats smapstats;

#define eventcommon int type, millis, id
struct shotevent{
	eventcommon;
	int gun;
	float to[3];
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

struct clientstate : playerstate
{
	vec o, aim, vel, lasto, sg[SGRAYS], flagpickupo;
	float pitchvel;
	int state, lastomillis, movemillis;
	int lastdeath, lastffkill, lastspawn, lifesequence, spawnmillis;
	bool crouching;
	int crouchmillis, scopemillis;
	int drownmillis; char drownval;
	int streakondeath;
	int lastshot, lastregen;
	projectilestate<2> grenades, knives;
	int akimbos, akimbomillis;
	int points, flagscore, frags, deaths, shotdamage, damage;

	clientstate() : state(CS_DEAD), playerstate() {}

	bool isalive(int gamemillis)
	{
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
		radarearned = 0;
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
		akimbos = akimbomillis = 0;
		damagelog.setsize(0);
		crouching = false;
		crouchmillis = scopemillis = 0;
		streakondeath = -1;
	}
};

struct savedscore
{
	string name;
	uint ip;
	int points, frags, assists, killstreak, flagscore, deaths, shotdamage, damage;

	void save(clientstate &cs)
	{
		points = cs.points;
		frags = cs.frags;
		assists = cs.assists;
		killstreak = cs.killstreak;
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
		cs.killstreak = killstreak;
		cs.flagscore = flagscore;
		cs.deaths = deaths;
		cs.shotdamage = shotdamage;
		cs.damage = damage;
	}
};

static vector<savedscore> scores;

uint nextauthreq = 1;

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
	int authtoken, authmillis, authpriv; uint authreq;
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

	gameevent &addevent()
	{
		static gameevent dummy;
		if(events.length()>100) return dummy;
		return events.add();
	}

	gameevent &addtimer(){
		static gameevent dummy;
		if(timers.length()>200) return dummy;
		return timers.add();
	}

	void removetimers(int type){
		loopv(timers) if(timers[i].type == type) timers.remove(i--);
	}

	void mapchange()
	{
		state.reset();
		events.setsize(0);
		timers.setsize(0);
		overflow = 0;
		timesync = false;
		isonrightmap = m_edit;
		lastevent = 0;
		at3_lastforce = 0;
		mapcollisions;
	}

	void reset()
	{
		name[0] = demoflags = authmillis = 0;
		ping = 9999;
		skin = team = 0;
		position.setsize(0);
		messages.setsize(0);
		connected = connectauth = haswelcome = false;
		priv = PRIV_NONE;
		lastvotecall = 0;
		vote = VOTE_NEUTRAL;
		lastsaytext[0] = '\0';
		saychars = authreq = 0;
		spawnindex = -1;
		mapchange();
	}

	void zap()
	{
		type = ST_EMPTY;
		priv = authpriv = PRIV_NONE;
		connected = connectauth = haswelcome = false;
	}
};

vector<client *> clients;

bool valid_client(int cn, bool player){
	return clients.inrange(cn) && clients[cn]->type != ST_EMPTY && (!player || clients[cn]->type != ST_AI);
}

struct ban
{
	enet_uint32 host;
	int millis;
};

vector<ban> bans;

ssqr *maplayout = NULL;
int maplayout_factor;

bool hasclient(client *ci, int cn){
	if(!valid_client(cn)) return false;
	client *cp = clients[cn];
	return ci->clientnum == cn || cp->state.ownernum == ci->clientnum;
}

int bsend = 0, brec = 0, laststatus = 0, servmillis = 0, lastfillup = 0;

void recordpacket(int chan, void *data, int len);

void sendpacket(int n, int chan, ENetPacket *packet, int exclude = -1){
	const int realexclude = valid_client(exclude) ? valid_client(clients[exclude]->state.ownernum) ? clients[exclude]->state.ownernum : exclude : -1;
	if(n<0){
		recordpacket(chan, packet->data, (int)packet->dataLength);
		loopv(clients) if(i!=realexclude && valid_client(i, true)) sendpacket(i, chan, packet);
		return;
	}
	switch(clients[n]->type)
	{
		case ST_AI:
		{
			// reroute packets
			const int owner = clients[n]->state.ownernum;
			if(valid_client(owner) && owner != n && owner != realexclude) sendpacket(owner, chan, packet, exclude);
			break;
		}

		case ST_TCPIP:
			enet_peer_send(clients[n]->peer, chan, packet);
			bsend += (int)packet->dataLength;
			break;

		case ST_LOCAL:
			localservertoclient(chan, packet->data, (int)packet->dataLength);
			break;
	}
}

static bool reliablemessages = false;

bool buildworldstate(){ // WAY easier worldstates
	bool flush = false;
	vector<int> recorded;
	vector<uchar> recordmsg, recordpos;
	loopv(clients){
		if(clients[i]->type!=ST_TCPIP || !clients[i]->connected) continue;
		clients[i]->overflow = 0;
		ENetPacket *messagepacket = enet_packet_create(NULL, MAXTRANS, reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0);
		ENetPacket *positionpacket = enet_packet_create(NULL, MAXTRANS, 0);
		ucharbuf p(messagepacket->data, messagepacket->dataLength), pos(positionpacket->data, positionpacket->dataLength);
		loopvj(clients){
			client &c = *clients[j];
			if(c.type == ST_EMPTY || j == i || c.state.ownernum == i) continue;
			// positions
			const int pstart = pos.length();
			pos.put(c.position.getbuf(), c.position.length());
			// messages
			const int mstart = p.length();
			putint(p, N_CLIENT);
			putint(p, j);
			//putuint(p, c.messages.length());
			p.put(c.messages.getbuf(), c.messages.length());
			const int mlen = p.length() - mstart;
			if(c.position.length() || c.position.length()) flush = true;
			if(recorded.find(j) < 0){
				loopk(c.position.length()) recordpos.add(pos.buf[pstart + k]);
				loopk(mlen) recordmsg.add(p.buf[mstart + k]);
				recorded.add(j);
			}
		}
		enet_packet_resize(positionpacket, pos.length());
		sendpacket(i, 0, positionpacket);
		if(!positionpacket->referenceCount) enet_packet_destroy(positionpacket);
		enet_packet_resize(messagepacket, p.length());
		sendpacket(i, 1, messagepacket);
		if(!messagepacket->referenceCount) enet_packet_destroy(messagepacket);
	}
	loopv(clients) if(clients[i]->type != ST_EMPTY){
		clients[i]->position.setsize(0);
		clients[i]->messages.setsize(0);
	}
	reliablemessages = false;
	if(flush){
		recordpacket(0, recordpos.getbuf(), recordpos.length());
		recordpacket(1, recordmsg.getbuf(), recordmsg.length());
	}
	return flush;
}

int countclients(int type, bool exclude = false){
	int num = 0;
	loopv(clients) if((clients[i]->type!=type)==exclude && clients[i]->state.ownernum < 0) num++;
	return num;
}

int numclients() { return countclients(ST_EMPTY, true); }
int numlocalclients() { return countclients(ST_LOCAL); }
int numnonlocalclients() { return countclients(ST_TCPIP); }

int numauthedclients(){
	int num = 0;
	loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->connected) num++;
	return num;
}

int calcscores();

int freeteam(int pl = -1){
	int teamsize[2] = {0};
	int teamscore[2] = {0};
	int t;
	int sum = calcscores();
	loopv(clients) if(clients[i]->type!=ST_EMPTY && i != pl && clients[i]->connected && clients[i]->team < 2)
	{
		t = clients[i]->team;
		teamsize[t]++;
		teamscore[t] += clients[i]->at3_score;
	}
	if(teamsize[0] == teamsize[1])
	{
		return sum > 200 ? (teamscore[0] < teamscore[1] ? 0 : 1) : rnd(2);
	}
	return teamsize[1] < teamsize[0] ? 1 : 0;
}

int findcnbyaddress(ENetAddress *address){
	loopv(clients)
	{
		if(clients[i]->type == ST_TCPIP && clients[i]->peer->address.host == address->host && clients[i]->peer->address.port == address->port)
			return i;
	}
	return -1;
}

savedscore *findscore(client &c, bool insert){
	if(c.type!=ST_TCPIP) return NULL;
	if(!insert)
	{
		loopv(clients)
		{
			client &o = *clients[i];
			if(o.type!=ST_TCPIP) continue;
			if(o.clientnum!=c.clientnum && o.peer->address.host==c.peer->address.host && !strcmp(o.name, c.name))
			{
				static savedscore curscore;
				curscore.save(o.state);
				return &curscore;
			}
		}
	}
	loopv(scores)
	{
		savedscore &sc = scores[i];
		if(!strcmp(sc.name, c.name) && sc.ip==c.peer->address.host) return &sc;
	}
	if(!insert) return NULL;
	savedscore &sc = scores.add();
	copystring(sc.name, c.name);
	sc.ip = c.peer->address.host;
	return &sc;
}

struct server_entity			// server side version of "entity" type
{
	int type;
	bool spawned, hascoord;
	int spawntime;
	short x, y;
};

vector<server_entity> sents;

struct sknife{
	int id, millis;
	vec o;
};

void restoreserverstate(vector<entity> &ents)   // hack: called from savegame code, only works in SP
{
	loopv(sents)
	{
		sents[i].spawned = ents[i].spawned;
		sents[i].spawntime = 0;
	}
}

static int interm = 0, minremain = 0, gamemillis = 0, gamelimit = 0;
static bool mapreload = false, autoteam = true, forceintermission = false, nokills = true;

string servdesc_current;
ENetAddress servdesc_caller;
bool custom_servdesc = false;

bool isdedicated = false;
ENetHost *serverhost = NULL;

void process(ENetPacket *packet, int sender, int chan);
void welcomepacket(ucharbuf &p, int n, ENetPacket *packet, bool forcedeath = false);
void sendwelcome(client *cl, int chan = 1, bool forcedeath = false);

void sendf(int cn, int chan, const char *format, ...){
	int exclude = -1;
	bool reliable = false;
	if(*format=='r') { reliable = true; ++format; }
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	ucharbuf p(packet->data, packet->dataLength);
	va_list args;
	va_start(args, format);
	while(*format) switch(*format++)
	{
		case 'x':
			exclude = va_arg(args, int);
			break;
		case 'v':
		{
			int n = va_arg(args, int);
			int *v = va_arg(args, int *);
			loopi(n) putint(p, v[i]);
			break;
		}
		case 'i':
		{
			int n = isdigit(*format) ? *format++-'0' : 1;
			loopi(n) putint(p, va_arg(args, int));
			break;
		}
		case 'f':
		{
			int n = isdigit(*format) ? *format++-'0' : 1;
			loopi(n) putfloat(p, (float)va_arg(args, double));
			break;
		}
		case 's': sendstring(va_arg(args, const char *), p); break;
		case 'm':
		{
			int n = va_arg(args, int);
			enet_packet_resize(packet, packet->dataLength+n);
			p.buf = packet->data;
			p.maxlen += n;
			p.put(va_arg(args, uchar *), n);
			break;
		}
	}
	va_end(args);
	enet_packet_resize(packet, p.length());
	sendpacket(cn, chan, packet, exclude);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

inline void sendservmsg(const char *msg, int client = -1){ // compact to below every new protocol
	sendf(client, 1, "ris", N_SERVMSG, msg);
}

inline void sendmsg(int msg, int client = -1){
	sendf(client, 1, "ri2", N_CONFMSG, msg);
}
inline void sendmsgs(int msg, char *str, int client = -1){
	sendf(client, 1, "ri2s", N_CONFMSG, msg, str);
}
inline void sendmsgi(int msg, int num, int client = -1){
	sendf(client, 1, "ri3", N_CONFMSG, msg, num);
}

void streakready(client &c, int streak){
	if(streak < 0 || streak >= STREAK_NUM) return;
	sendf(-1, 1, "ri3", N_STREAKREADY, c.clientnum, streak);
}

void usestreak(client &c, int streak, const vec &o = vec(0, 0, 0)){
	if(streak < 0 || streak >= STREAK_NUM) return;
	int info = 0;
	switch(streak){
		case STREAK_AIRSTRIKE: // SPECIAL
			sendf(-1, 1, "ri3f3", N_STREAKUSE, c.clientnum, STREAK_AIRSTRIKE, info, o.x, o.y, o.z);
			return;
		case STREAK_RADAR:
			c.state.radarearned = gamemillis + (info = 15000);
			break;
		case STREAK_NUKE:
			info = 1;
			break;
		case STREAK_DROPNADE:
		case STREAK_REVENGE:
		{
			info = rand();
			c.state.grenades.add(info);
			extern int explosion(client &owner, const vec &o2, int weap);
			if(streak == STREAK_REVENGE) explosion(c, c.state.o, WEAP_GRENADE);
			break;
		}
	}
	sendf(-1, 1, "ri4", N_STREAKUSE, c.clientnum, streak, info);
}

void spawnstate(client *c){
	clientstate &gs = c->state;
	gs.spawnstate(smode);
	++gs.lifesequence;
}

void sendspawn(client *c){
	clientstate &gs = c->state;
	if(gs.lastdeath) gs.respawn();
	spawnstate(c);
	sendf(c->clientnum, 1, "ri9vv", N_SPAWNSTATE, c->clientnum, gs.lifesequence,
		gs.health, gs.armor, gs.perk,
		gs.primary, gs.gunselect, m_duel ? c->spawnindex : -1,
		WEAP_MAX, gs.ammo, WEAP_MAX, gs.mag);
	gs.lastspawn = gamemillis;

	int dstreak = gs.deathstreak + (gs.perk == PERK_KILLSTREAK ? 1 : 0);
	if(dstreak >= 8) gs.streakondeath = STREAK_REVENGE;
	else if(dstreak >= 5) gs.streakondeath = STREAK_DROPNADE;
	else gs.streakondeath = -1;
	streakready(*c, gs.streakondeath);
}

// throwing knives

vector<sknife> sknives;
int sknifeid = 0;

void purgesknives(){
	loopv(sknives) sendf(-1, 1, "ri2", N_KNIFEREMOVE, sknives[i].id);
	sknives.setsize(0);
}

// demo

struct demofile
{
	string info;
	uchar *data;
	int len;
};

vector<demofile> demos;

bool demonextmatch = false;
FILE *demotmp = NULL;
gzFile demorecord = NULL, demoplayback = NULL;
bool recordpackets = false;
int nextplayback = 0;

void writedemo(int chan, void *data, int len){
	if(!demorecord) return;
	int stamp[3] = { gamemillis, chan, len };
	endianswap(stamp, sizeof(int), 3);
	gzwrite(demorecord, stamp, sizeof(stamp));
	gzwrite(demorecord, data, len);
}

void recordpacket(int chan, void *data, int len){
	if(recordpackets) writedemo(chan, data, len);
}

void enddemorecord(){
	if(!demorecord) return;

	gzclose(demorecord);
	recordpackets = false;
	demorecord = NULL;

#ifdef WIN32
	demotmp = fopen(path("demos/demorecord", true), "rb");
#endif
	if(!demotmp) return;

	fseek(demotmp, 0, SEEK_END);
	int len = ftell(demotmp);
	rewind(demotmp);
	if(demos.length() >= scl.maxdemos)
	{
		delete[] demos[0].data;
		demos.remove(0);
	}
	demofile &d = demos.add();
	formatstring(d.info)("%s: %s, %s, %.2f%s", asctime(), modestr(gamemode), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
	sendmsgs(26, d.info);
	logline(ACLOG_INFO, "Demo \"%s\" recorded.", d.info);
	d.data = new uchar[len];
	d.len = len;
	fread(d.data, 1, len, demotmp);
	fclose(demotmp);
	demotmp = NULL;
	if(scl.demopath[0])
	{
		defformatstring(msg)("%s%s_%s_%s.dmo", scl.demopath, timestring(), behindpath(smapname), modestr(gamemode, true));
		path(msg);
		FILE *demo = openfile(msg, "wb");
		if(demo)
		{
			int wlen = (int) fwrite(d.data, 1, d.len, demo);
			fclose(demo);
			logline(ACLOG_INFO, "demo written to file \"%s\" (%d bytes)", msg, wlen);
		}
		else
		{
			logline(ACLOG_INFO, "failed to write demo to file \"%s\"", msg);
		}
	}
}

void putinitai(client &c, ucharbuf &p){
	putint(p, N_INITAI);
	putint(p, c.clientnum);
	putint(p, c.team);
	putint(p, c.skin);
	putint(p, c.state.ownernum);
}

void putinitclient(client &c, ucharbuf &p);

void setupdemorecord(){
	if(numlocalclients() || !m_fight(gamemode)) return;

#ifdef WIN32
	gzFile f = gzopen(path("demos/demorecord", true), "wb9");
	if(!f) return;
#else
	demotmp = tmpfile();
	if(!demotmp) return;
	setvbuf(demotmp, NULL, _IONBF, 0);

	gzFile f = gzdopen(_dup(_fileno(demotmp)), "wb9");
	if(!f)
	{
		fclose(demotmp);
		demotmp = NULL;
		return;
	}
#endif

	sendmsg(20);
	logline(ACLOG_INFO, "Demo recording started.");

	demorecord = f;
	recordpackets = false;

	demoheader hdr;
	memcpy(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic));
	hdr.version = DEMO_VERSION;
	hdr.protocol = PROTOCOL_VERSION;
	endianswap(&hdr.version, sizeof(int), 1);
	endianswap(&hdr.protocol, sizeof(int), 1);
	memset(hdr.desc, 0, DHDR_DESCCHARS);
	defformatstring(desc)("%s, %s, %s %s", modestr(gamemode, false), behindpath(smapname), asctime(), servdesc_current);
	if(strlen(desc) > DHDR_DESCCHARS)
		formatstring(desc)("%s, %s, %s %s", modestr(gamemode, true), behindpath(smapname), asctime(), servdesc_current);
	desc[DHDR_DESCCHARS - 1] = '\0';
	strcpy(hdr.desc, desc);
	gzwrite(demorecord, &hdr, sizeof(demoheader));

	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	welcomepacket(p, -1, packet);
	writedemo(1, p.buf, p.len);
	enet_packet_destroy(packet);

	uchar buf[MAXTRANS];
	loopv(clients)
	{
		client *ci = clients[i];
		if(ci->type==ST_EMPTY) continue;

		ucharbuf q(buf, sizeof(buf));
		putinitclient(*ci, q);
		writedemo(1, buf, q.len);
	}
}

void listdemos(int cn){
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	if(!packet) return;
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_LISTDEMOS);
	putint(p, demos.length());
	loopv(demos) sendstring(demos[i].info, p);
	enet_packet_resize(packet, p.length());
	sendpacket(cn, 1, packet);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

static void cleardemos(int n){
	if(!n){
		loopv(demos) delete[] demos[i].data;
		demos.shrink(0);
	}
	else if(demos.inrange(n-1)){
		delete[] demos[n-1].data;
		demos.remove(n-1);
	}
}

void senddemo(int cn, int num){
	if(!valid_client(cn)) return;
	if(clients[cn]->priv < scl.demodownloadpriv){
		sendmsgi(29, scl.demodownloadpriv, cn);
		return;
	}
	if(!num) num = demos.length();
	if(!demos.inrange(num-1)){
		if(demos.empty()) sendmsg(27, cn);
		else sendmsgi(28, num, cn);
		return;
	}
	demofile &d = demos[num-1];
	sendf(cn, 2, "rim", N_DEMO, d.len, d.data);
}

void enddemoplayback(){
	if(!demoplayback) return;
	gzclose(demoplayback);
	demoplayback = NULL;

	loopv(clients) sendf(i, 1, "ri3", N_DEMOPLAYBACK, 0, i);

	sendmsg(21);

	loopv(clients) sendwelcome(clients[i]);
}

void setupdemoplayback(){
	demoheader hdr;
	int msg = 0;
	defformatstring(file)("demos/%s.dmo", smapname);
	path(file);
	demoplayback = opengzfile(file, "rb9");
	if(!demoplayback) msg = 22;
	else if(gzread(demoplayback, &hdr, sizeof(demoheader))!=sizeof(demoheader) || memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)))
		msg = 23;
	else
	{
		endianswap(&hdr.version, sizeof(int), 1);
		endianswap(&hdr.protocol, sizeof(int), 1);
		if(hdr.version!=DEMO_VERSION) msg = hdr.version<DEMO_VERSION ? 24 : 25;
		else if(hdr.protocol != PROTOCOL_VERSION && !(hdr.protocol < 0 && hdr.protocol == -PROTOCOL_VERSION)) msg = hdr.protocol<PROTOCOL_VERSION ? 24 : 25;
	}
	if(msg){
		if(demoplayback) { gzclose(demoplayback); demoplayback = NULL; }
		sendmsgs(msg, file);
		return;
	}

	sendf(-1, 1, "ri2si", N_DEMOPLAYBACK, 1, file, -1);

	if(gzread(demoplayback, &nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
	{
		enddemoplayback();
		return;
	}
	endianswap(&nextplayback, sizeof(nextplayback), 1);
}

void readdemo(){
	if(!demoplayback) return;
	while(gamemillis>=nextplayback)
	{
		int chan, len;
		if(gzread(demoplayback, &chan, sizeof(chan))!=sizeof(chan) ||
		   gzread(demoplayback, &len, sizeof(len))!=sizeof(len))
		{
			enddemoplayback();
			return;
		}
		endianswap(&chan, sizeof(chan), 1);
		endianswap(&len, sizeof(len), 1);
		ENetPacket *packet = enet_packet_create(NULL, len, 0);
		if(!packet || gzread(demoplayback, packet->data, len)!=len)
		{
			if(packet) enet_packet_destroy(packet);
			enddemoplayback();
			return;
		}
		sendpacket(-1, chan, packet);
		if(!packet->referenceCount) enet_packet_destroy(packet);
		if(gzread(demoplayback, &nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
		{
			enddemoplayback();
			return;
		}
		endianswap(&nextplayback, sizeof(nextplayback), 1);
	}
}

#include "serverpoints.h"

struct sflaginfo
{
	int state;
	int actor_cn;
	int drop_cn, dropmillis; // track drop flag glitch
	float pos[3];
	int lastupdate;
	int stolentime;
	short x, y;		  // flag entity location
} sflaginfos[2];

void putflaginfo(ucharbuf &p, int flag){
	sflaginfo &f = sflaginfos[flag];
	putint(p, N_FLAGINFO);
	putint(p, flag);
	putint(p, f.state);
	switch(f.state)
	{
		case CTFF_STOLEN:
			putint(p, f.actor_cn);
			break;
		case CTFF_DROPPED:
			loopi(3) putfloat(p, f.pos[i]);
			break;
	}
}

int next_afk_check = 200;
void check_afk(){
	next_afk_check = servmillis + 7 * 1000;
	// if we have less than five players or a non-teammode is not full: do nothing!
	if (numclients() < 5 || (numnonlocalclients() < scl.maxclients && !m_team)) return;
	loopv(clients){
		client &c = *clients[i];
		if (c.type != ST_TCPIP || c.connectmillis + 60 * 1000 > servmillis || c.team == TEAM_SPECT || c.priv >= PRIV_ADMIN ||
			c.state.movemillis + scl.afktimelimit > servmillis || clienthasflag(c.clientnum) > -1 ) continue;
		if ( ( c.state.state == CS_DEAD && !m_duel && c.state.lastdeath + 45 * 1000 < gamemillis) ) {
			logline(ACLOG_INFO, "[%s] %s %s", c.hostname, c.name, "is afk");
			updateclientteam(i, TEAM_SPECT, FTR_AUTOTEAM);
		}
	}
}

void sendflaginfo(int flag = -1, int cn = -1){
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	if(flag >= 0) putflaginfo(p, flag);
	else loopi(2) putflaginfo(p, i);
	enet_packet_resize(packet, p.length());
	sendpacket(cn, 1, packet);
	if(packet->referenceCount==0) enet_packet_destroy(packet);
}

void flagmessage(int flag, int message, int actor, int cn = -1){
	if(message == FA_KTFSCORE)
		sendf(cn, 1, "ri5", N_FLAGMSG, flag, message, actor, (gamemillis - sflaginfos[flag].stolentime) / 1000);
	else
		sendf(cn, 1, "ri4", N_FLAGMSG, flag, message, actor);
}

void flagaction(int flag, int action, int actor){
	if(!valid_flag(flag)) return;
	sflaginfo &f = sflaginfos[flag];
	sflaginfo &of = sflaginfos[team_opposite(flag)];
	int score = 0;
	int message = -1;

	if(m_ctf || m_htf)
	{
		switch(action)
		{
			case FA_PICKUP:
				f.state = CTFF_STOLEN;
				f.actor_cn = actor;
				break;
			case FA_LOST:
			case FA_DROP:
				if(actor == -1) actor = f.actor_cn;
				f.state = CTFF_DROPPED;
				loopi(3) f.pos[i] = clients[actor]->state.o[i];
				break;
			case FA_RETURN:
				f.state = CTFF_INBASE;
				break;
			case FA_SCORE:  // ctf: f = carried by actor flag,  htf: f = hunted flag (run over by actor)
				if(m_ctf) score = 1;
				else{ // htf
					score = (of.state == CTFF_STOLEN) ? 1 : 0;
					message = score ? FA_SCORE : FA_SCOREFAIL;
					if(of.actor_cn == actor) score = 2;
				}
				f.state = CTFF_INBASE;
				break;
			case FA_RESET:
				f.state = CTFF_INBASE;
				break;
		}
	}
	else if(m_ktf)  // f: active flag, of: idle flag
	{
		switch(action)
		{
			case FA_PICKUP:
				f.state = CTFF_STOLEN;
				f.actor_cn = actor;
				f.stolentime = gamemillis;
				break;
			case FA_SCORE:  // f = carried by actor flag
				if(valid_client(f.actor_cn) && clients[f.actor_cn]->state.state == CS_ALIVE)
				{
					actor = f.actor_cn;
					score = 1;
					message = FA_KTFSCORE;
					break;
				}
			case FA_LOST:
			case FA_DROP:
				if(actor == -1) actor = f.actor_cn;
			case FA_RESET:
				if(f.state == CTFF_STOLEN){
					actor = f.actor_cn;
					message = FA_LOST;
				}
				f.state = CTFF_IDLE;
				of.state = CTFF_INBASE;
				sendflaginfo(team_opposite(flag));
				break;
		}
	}
	if(score){
		clients[actor]->state.flagscore += score;
		sendf(-1, 1, "ri3", N_FLAGCNT, actor, clients[actor]->state.flagscore);
	}
	if(message < 0) message = action;
	if(valid_client(actor)){
		client &c = *clients[actor];
		switch(message)
		{
			case FA_PICKUP:
				logline(ACLOG_INFO,"[%s] %s stole the flag", c.hostname, c.name);
				break;
			case FA_DROP:
				f.drop_cn = actor;
				f.dropmillis = servmillis;
				logline(ACLOG_INFO,"[%s] %s dropped the flag", c.hostname, c.name);
				break;
			case FA_LOST:
				logline(ACLOG_INFO,"[%s] %s lost the flag", c.hostname, c.name);
				break;
			case FA_RETURN:
				logline(ACLOG_INFO,"[%s] %s returned the flag", c.hostname, c.name);
				break;
			case FA_SCORE:
				logline(ACLOG_INFO, "[%s] %s scored with the flag for %s, new score %d", c.hostname, c.name, team_string(c.team), c.state.flagscore);
				break;
			case FA_KTFSCORE:
				logline(ACLOG_INFO, "[%s] %s scored, carrying for %d seconds, new score %d", c.hostname, c.name, (gamemillis - f.stolentime) / 1000, c.state.flagscore);
				break;
			case FA_SCOREFAIL:
				logline(ACLOG_INFO, "[%s] %s failed to score", c.hostname, c.name);
				break;
		}
	}
	else if(message == FA_RESET) logline(ACLOG_INFO,"the server reset the flag for team %s", team_string(flag));
	f.lastupdate = gamemillis;
	sendflaginfo(flag);
	flagmessage(flag, message, valid_client(actor) ? actor : -1);
	flagpoints(clients[actor], message);
}

int clienthasflag(int cn){
	if(m_flags && valid_client(cn))
	{
		loopi(2) { if(sflaginfos[i].state==CTFF_STOLEN && sflaginfos[i].actor_cn==cn) return i; }
	}
	return -1;
}

void ctfreset(){
	int idleflag = m_ktf ? rnd(2) : -1;
	loopi(2)
	{
		sflaginfos[i].actor_cn = -1;
		sflaginfos[i].state = i == idleflag ? CTFF_IDLE : CTFF_INBASE;
		sflaginfos[i].lastupdate = -1;
	}
}

void sdropflag(int cn){
	int fl = clienthasflag(cn);
	if(fl >= 0) flagaction(fl, FA_LOST, cn);
}

void resetflag(int cn){
	int fl = clienthasflag(cn);
	if(fl >= 0) flagaction(fl, FA_RESET, -1);
}

void htf_forceflag(int flag){
	sflaginfo &f = sflaginfos[flag];
	int besthealth = 0, numbesthealth = 0;
	loopv(clients) if(clients[i]->type!=ST_EMPTY)
	{
		if(clients[i]->state.state == CS_ALIVE && clients[i]->team == flag)
		{
			if(clients[i]->state.health == besthealth)
				numbesthealth++;
			else
			{
				if(clients[i]->state.health > besthealth)
				{
					besthealth = clients[i]->state.health;
					numbesthealth = 1;
				}
			}
		}
	}
	if(numbesthealth)
	{
		int pick = rnd(numbesthealth);
		loopv(clients) if(clients[i]->type!=ST_EMPTY)
		{
			if(clients[i]->state.state == CS_ALIVE && clients[i]->team == flag && --pick < 0)
			{
				f.state = CTFF_STOLEN;
				f.actor_cn = i;
				sendflaginfo(flag);
				flagmessage(flag, FA_PICKUP, i);
				logline(ACLOG_INFO,"[%s] %s got forced to pickup the flag", clients[i]->hostname, clients[i]->name);
				break;
			}
		}
	}
	f.lastupdate = gamemillis;
}

int arenaround = 0;

inline bool canspawn(client *c, bool = false){
	return !m_duel && c->team != TEAM_SPECT;
}

/*
inline bool canspawn(client *c, bool connecting = false){
	return !m_duel || (connecting && numauthedclients() <= 2);
}
*/

struct twoint { int index, value; };
int cmpscore(const int *a, const int *b) { return clients[*a]->at3_score - clients[*b]->at3_score; }
int cmptwoint(const struct twoint *a, const struct twoint *b) { return a->value - b->value; }
ivector tdistrib;
vector<twoint> sdistrib;

void distributeteam(int team){
	int numsp = team == 100 ? smapstats.spawns[2] : smapstats.spawns[team];
	if(!numsp) numsp = 30; // no spawns: try to distribute anyway
	twoint ti;
	tdistrib.shrink(0);
	loopv(clients) if(clients[i]->type!=ST_EMPTY)
	{
		if(team == 100 || team == clients[i]->team)
		{
			tdistrib.add(i);
			clients[i]->at3_score = rand();
		}
	}
	tdistrib.sort(cmpscore); // random player order
	sdistrib.shrink(0);
	loopi(numsp)
	{
		ti.index = i;
		ti.value = rand();
		sdistrib.add(ti);
	}
	sdistrib.sort(cmptwoint); // random spawn order
	int x = 0;
	loopv(tdistrib)
	{
		clients[tdistrib[i]]->spawnindex = sdistrib[x++].index;
		x %= sdistrib.length();
	}
}

void distributespawns(){
	loopv(clients) if(clients[i]->type!=ST_EMPTY)
	{
		clients[i]->spawnindex = -1;
	}
	if(m_team)
	{
		distributeteam(0);
		distributeteam(1);
	}
	else
	{
		distributeteam(100);
	}
}

void arenacheck(){
	if(!m_duel || interm || gamemillis<arenaround || clients.empty()) return;

	if(arenaround){ // start new arena round
		arenaround = 0;
		distributespawns();
		purgesknives();
		loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->connected && clients[i]->team != TEAM_SPECT &&
				clients[valid_client(clients[i]->state.ownernum) ? clients[i]->state.ownernum : i]->isonrightmap){
			clients[i]->state.lastdeath = 1;
			sendspawn(clients[i]);
		}
		return;
	}

	client *alive = NULL;
	bool dead = false;
	int lastdeath = 0;
	loopv(clients){
		client &c = *clients[i];
		if(c.type==ST_EMPTY || !c.connected || c.team == TEAM_SPECT) continue;
		if(c.state.state==CS_ALIVE || (c.state.state==CS_DEAD && c.state.lastspawn>=0)){
			if(!alive){
				if(c.type != ST_AI) alive = &c;
			}
			else if(!m_team || alive->team != c.team) return;
		}
		else if(c.state.state==CS_DEAD){
			dead = true;
			lastdeath = max(lastdeath, c.state.lastdeath);
		}
	}

	if(!dead || gamemillis < lastdeath + 500) return;
	sendf(-1, 1, "ri2", N_ARENAWIN, alive ? alive->clientnum : -1);
	arenaround = gamemillis+5000;
	if(autoteam && m_team) refillteams(true);
}

#define SPAMREPEATINTERVAL  20   // detect doubled lines only if interval < 20 seconds
#define SPAMMAXREPEAT	   3	// 4th time is SPAM
#define SPAMCHARPERMINUTE   220  // good typist
#define SPAMCHARINTERVAL	30   // allow 20 seconds typing at maxspeed

bool spamdetect(client *cl, char *text) // checks doubled lines and average typing speed
{
	if(cl->type != ST_TCPIP || cl->priv >= PRIV_ADMIN) return false;
	bool spam = false;
	int pause = servmillis - cl->lastsay;
	if(pause < 0 || pause > 90*1000) pause = 90*1000;
	cl->saychars -= (SPAMCHARPERMINUTE * pause) / (60*1000);
	cl->saychars += (int)strlen(text);
	if(cl->saychars < 0) cl->saychars = 0;
	if(text[0] && !strcmp(text, cl->lastsaytext) && servmillis - cl->lastsay < SPAMREPEATINTERVAL*1000)
	{
		spam = ++cl->spamcount > SPAMMAXREPEAT;
	}
	else
	{
		 copystring(cl->lastsaytext, text);
		 cl->spamcount = 0;
	}
	cl->lastsay = servmillis;
	if(cl->saychars > (SPAMCHARPERMINUTE * SPAMCHARINTERVAL) / 60)
		spam = true;
	return spam;
}

void sendtext(char *text, client &cl, int flags, int voice){
	if(voice < 0 || voice > S_VOICEEND - S_MAINEND) voice = 0;
	defformatstring(logmsg)("<%s> ", cl.name);
	if(flags & SAY_ACTION) formatstring(logmsg)("* %s ", cl.name);
	string logappend;
	if(flags & SAY_TEAM){
		formatstring(logappend)("(%s) ", team_string(cl.team));
		concatstring(logmsg, logappend);
	}
	if(voice){
		formatstring(logappend)("[%d] ", voice + S_MAINEND);
		concatstring(logmsg, logappend);
	}
	if(spamdetect(&cl, text)){
		//logline(ACLOG_INFO, "%s, SPAM detected", logmsg);
		sendf(cl.clientnum, 1, "ri3s", N_TEXT, cl.clientnum, SAY_DENY << 5, text);
		return;
	}
	if(!m_team) flags &= ~SAY_TEAM;
	logline(ACLOG_INFO, "[%s] %s%s", cl.hostname, logmsg, text);
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_TEXT);
	putint(p, cl.clientnum);
	putint(p, (voice & 0x1F) | flags << 5);
	sendstring(text, p);
	enet_packet_resize(packet, p.length());
	loopv(clients) if(clients[i]->type != ST_AI && clients[i]->type != ST_EMPTY && (!(flags&SAY_TEAM) || clients[i]->team == cl.team || clients[i]->priv >= PRIV_ADMIN)) sendpacket(i, 1, packet);
	recordpacket(1, packet->data, (int)packet->dataLength);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

int spawntime(int type){
	int np = numclients();
	np = np<3 ? 4 : (np>4 ? 2 : 3);		 // spawn times are dependent on number of players
	int sec = 0;
	switch(type)
	{
		case I_CLIPS:
		case I_AMMO:
		case I_GRENADE: sec = np*2; break;
		case I_HEALTH: sec = np*5; break;
		case I_ARMOR: sec = 20; break;
		case I_AKIMBO: sec = 60; break;
	}
	return sec*1000;
}

bool serverpickup(int i, int sender) // server side item pickup, acknowledge first client that moves to the entity
{
	server_entity &e = sents[i];
	if(!e.spawned) return false;
	e.spawned = false;
	e.spawntime = spawntime(e.type);
	if(!valid_client(sender)) return true;
	sendf(-1, 1, "ri3", N_ITEMACC, i, sender);
	clients[sender]->state.pickup(sents[i].type);
	return true;
}

void checkitemspawns(int diff){
	if(!diff) return;
	loopv(sents) if(sents[i].spawntime)
	{
		sents[i].spawntime -= diff;
		if(sents[i].spawntime<=0)
		{
			sents[i].spawntime = 0;
			sents[i].spawned = true;
			sendf(-1, 1, "ri2", N_ITEMSPAWN, i);
		}
	}
}

inline int getmaplayoutid(int x, int y){
	return x + (y << maplayout_factor);
}

inline char maxvdelta(int id){
	ssqr &s = maplayout[id];
	char vdelta = s.vdelta;
	if((&s + 1)->vdelta > vdelta) vdelta = (&s + 1)->vdelta;
	if((&s + (1 << maplayout_factor))->vdelta > vdelta) vdelta = (&s + (1 << maplayout_factor))->vdelta;
	if((&s + (1 << maplayout_factor) + 1)->vdelta > vdelta) vdelta = (&s + (1 << maplayout_factor) + 1)->vdelta;
	return vdelta;
}

float getblockfloor(int id){
	if(!maplayout || maplayout[id].type == SOLID) return -128;
	ssqr &s = maplayout[id];
	return s.floor - (s.type == FHF ? maxvdelta(id) / 4.f : 0);
}

float getblockceil(int id){
	if(!maplayout || maplayout[id].type == SOLID) return 127;
	ssqr &s = maplayout[id];
	return s.ceil + (s.type == CHF ? maxvdelta(id) / 4.f : 0);
}

bool outofborder(const vec &p){
	loopi(2) if(p[i] < 2 || p[i] > (1 << maplayout_factor) - 2) return true;
	return false;
}

bool checkpos(vec &p, bool alter = true){
	bool ret = false;
	vec fix = p;
	const float epsilon = .1f; // the real value is much smaller than that
	// xy
	loopi(2){
		if(fix[i] <= 2){
			fix[i] = 2 + epsilon;
			ret = true;
			
		}
		else if((1 << maplayout_factor) - 2 <= fix[i]){
			fix[i] = (1 << maplayout_factor) - 2 - epsilon;
			ret = true;
		}
	}
	if(!ret){
		// z
		const int mapi = getmaplayoutid(fix.x, fix.y);
		const char ceil = getblockceil(mapi), floor = getblockfloor(mapi);
		if(fix.z >= ceil){
			fix.z = ceil - epsilon;
			ret = true;
		}
		else if(floor >= fix.z){
			fix.z = floor + epsilon;
			ret = true;
		}
	}
	if(alter) p = fix;
	return ret;
}

float sraycube(const vec &o, const vec &ray, vec *surface = NULL){ // server counterpart
	if(surface) *surface = vec(0, 0, 0);
	if(ray.iszero()) return 0;

	vec v = o;
	float dist = 0, dx = 0, dy = 0, dz = 0;

	const int maxtraces = (1 << maplayout_factor << 2);
	for(int numtraces = 0; numtraces < maxtraces; numtraces++){
		int x = int(v.x), y = int(v.y);
		if(x < 0 || y < 0 || x >= (1 << maplayout_factor) || y >= (1 << maplayout_factor)) return dist;
		const int mapid = getmaplayoutid(x, y);
		ssqr s = maplayout[getmaplayoutid(x, y)];
		float floor = getblockfloor(mapid), ceil = getblockceil(mapid);
		if(s.type == SOLID || v.z < floor || v.z > ceil){
			if((!dx && !dy) || s.wtex==DEFAULT_SKY || (s.type != SOLID && v.z > ceil && s.ctex==DEFAULT_SKY)) return dist;
			if(surface && s.type != CORNER){// && s->type!=FHF && s->type!=CHF)
				if(dx<dy) surface->x = ray.x>0 ? -1 : 1;
				else surface->y = ray.y>0 ? -1 : 1;
				ssqr n = maplayout[getmaplayoutid(x+surface->x, y+surface->y)];
				if(n.type == SOLID || (v.z < floor && v.z < n.floor) || (v.z > ceil && v.z > n.ceil)){
					*surface = dx<dy ? vec(0, ray.y>0 ? -1 : 1, 0) : vec(ray.x>0 ? -1 : 1, 0, 0);
					n = maplayout[getmaplayoutid(x+surface->x, y+surface->y)];
					if(n.type == SOLID || (v.z < floor && v.z < n.floor) || (v.z > ceil && v.z > n.ceil))
						*surface = vec(0, 0, ray.z>0 ? -1 : 1);
				}
			}
			dist = max(dist-0.1f, 0.0f);
			break;
		}
		dx = ray.x ? (x + (ray.x > 0 ? 1 : 0) - v.x)/ray.x : 1e16f;
		dy = ray.y ? (y + (ray.y > 0 ? 1 : 0) - v.y)/ray.y : 1e16f;
		dz = ray.z ? ((ray.z > 0 ? ceil : floor) - v.z)/ray.z : 1e16f;
		if(dz < dx && dz < dy)
		{
			if(surface && s.type!=FHF && s.type!=CHF) surface->z = ray.z>0 && s.ctex!=DEFAULT_SKY ? -1 : 1;
			dist += dz;
			break;
		}
		float disttonext = 0.1f + min(dx, dy);
		v.add(vec(ray).mul(disttonext));
		dist += disttonext;
	}
	return dist;
}

void straceShot(const vec &from, vec &to, vec *surface = NULL){
	vec tracer(to);
	tracer.sub(from).normalize();
	const float dist = sraycube(from, tracer, surface);
	to = tracer.mul(dist - .1f).add(from);
}

void forcedeath(client *cl, bool gib = false, bool cheat = false){
	sdropflag(cl->clientnum);
	clientstate &cs = cl->state;
	cs.state = CS_DEAD;
	//cs.respawn();
	cs.lastdeath = gamemillis;
	if(cheat){
		cs.points -= 120;
		cs.frags -= 40;
		cs.assists -= 40;
		cs.deaths += 80;
		sendf(-1, 1, "ri2", N_HACKFAIL, cl->clientnum);
	}
	else sendf(-1, 1, "ri2", gib ? N_FORCEGIB : N_FORCEDEATH, cl->clientnum);
}

void serverdamage(client *target, client *actor, int damage, int gun, int style, const vec &source){
	if(!target || !actor || !damage) return;
	if(m_expert && !(style & FRAG_GIB)) damage = 0;
	clientstate &ts = target->state;
	if(target != actor && isteam(actor, target)){ // friendly fire
		if((damage *= 0.25) > target->state.health - 80) damage = target->state.health - 80; // no more TKs!
		if(damage < 1) return;
		const int returndamage = damage * (m_expert ? 1.5f : .4f);
		if(returndamage) serverdamage(actor, actor, returndamage, WEAP_MAX+3, style, source); // redirect damage to owner
		actor->state.shotdamage += damage; // reduce his accuracy
	}
	if(target->state.damagelog.find(actor->clientnum) < 0) target->state.damagelog.add(actor->clientnum);
	ts.dodamage(damage, actor->state.perk == PERK_POWER);
	ts.lastregen = gamemillis + REGENDELAY - REGENINT;
	const bool gib = style & FRAG_GIB;
	if(ts.health<=0){
		int targethasflag = clienthasflag(target->clientnum);
		bool suic = false;
		target->state.deaths++;
		if(target!=actor) actor->state.frags += /*isteam(target, actor) ? -1 :*/ gib ? 2 : 1;
		else{ // suicide
			actor->state.frags--;
			suic = true;
		}
		++actor->state.killstreak;
		++ts.deathstreak;
		actor->state.deathstreak = ts.killstreak = ts.lastcut = 0;
		ts.cutter = -1;
		ts.damagelog.removeobj(target->clientnum);
		ts.damagelog.removeobj(actor->clientnum);
		target->removetimers(GE_RELOAD);
		loopv(ts.damagelog){
			if(valid_client(ts.damagelog[i])) clients[ts.damagelog[i]]->state.assists++;
			else ts.damagelog.remove(i--);
		}
		if(!suic && nokills){
			style |= FRAG_FIRST;
			nokills = false;
		}
		killpoints(target, actor, gun, style);
		const float killdist = ts.o == source ? 0 : ts.o.dist(source) / 4;
		sendf(-1, 1, "ri8vf", N_KILL, target->clientnum, actor->clientnum, actor->state.frags, gun, style & FRAG_VALID, int(damage * (gib ? GIBBLOODMUL : 1)),
			ts.damagelog.length(), ts.damagelog.length(), ts.damagelog.getbuf(), killdist);
		if(suic && (m_htf || m_ktf) && targethasflag >= 0){
			actor->state.flagscore--;
			sendf(-1, 1, "ri3", N_FLAGCNT, actor->clientnum, actor->state.flagscore);
		}
		target->position.setsize(0);
		ts.state = CS_DEAD;
		ts.lastdeath = gamemillis;
		const char *h = valid_client(actor->state.ownernum) ? clients[actor->state.ownernum]->hostname : actor->hostname;
		if(!suic) logline(ACLOG_INFO, "[%s] %s %s %s (%.2f m)", h, actor->name, killname(toobit(gun, style), isheadshot(gun, style)), target->name, killdist);
		else logline(ACLOG_INFO, "[%s] %s %s (%.2f m)", h, actor->name, suicname(obit_suicide(gun)), killdist);

		if(m_flags && targethasflag >= 0)
		{
			if(m_ctf || m_htf)
				flagaction(targethasflag, FA_LOST, -1);
			else // ktf || tktf
				flagaction(targethasflag, FA_RESET, -1);
		}
		switch(actor->state.killstreak + (actor->state.perk == PERK_KILLSTREAK ? 1 : 0)){
			case 7:
				streakready(*actor, STREAK_AIRSTRIKE);
				break;
			case 9:
				usestreak(*actor, STREAK_RADAR);
				break;
			case 11:
				usestreak(*actor, STREAK_NUKE);
				break;
		}
		usestreak(*target, ts.streakondeath);
	}
	else{
		if(actor->state.perk == PERK_POWER){ damage *= 2; }
		sendf(-1, 1, "ri8", N_DAMAGE, target->clientnum, actor->clientnum, int(damage * (gib ? GIBBLOODMUL : 1)), ts.armor, ts.health, gun, style & FRAG_VALID);
		if(source != target->state.o) sendf(-1, 1, "ri5f3", N_HITPUSH, target->clientnum, gun, damage, actor->state.perk == PERK_POWER ? 1 : 0, source.x, source.y, source.z);
	}
}

#include "serverevents.h"

#define CONFIG_MAXPAR 6

struct configset
{
	string mapname;
	union
	{
		struct { int mode, time, vote, minplayer, maxplayer, skiplines; };
		int par[CONFIG_MAXPAR];
	};
};

vector<configset> configsets;
int curcfgset = -1;

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

void readscfg(const char *name){
	static string cfgfilename;
	static int cfgfilesize;
	const char *sep = ": ";
	configset c;
	char *p, *l;
	int i, len, line = 0;

	if(!name && getfilesize(cfgfilename) == cfgfilesize) return;
	configsets.shrink(0);
	char *buf = loadcfgfile(cfgfilename, name, &len);
	cfgfilesize = len;
	if(!buf) return;
	p = buf;
	logline(ACLOG_VERBOSE,"reading map rotation '%s'", cfgfilename);
	while(p < buf + len)
	{
		l = p; p += strlen(p) + 1; line++;
		l = strtok(l, sep);
		if(l)
		{
			copystring(c.mapname, behindpath(l));
			for(i = 3; i < CONFIG_MAXPAR; i++) c.par[i] = 0;  // default values
			for(i = 0; i < CONFIG_MAXPAR; i++)
			{
				if((l = strtok(NULL, sep)) != NULL)
					c.par[i] = atoi(l);
				else
					break;
			}
			if(i > 2)
			{
				configsets.add(c);
				logline(ACLOG_VERBOSE," %s, %s, %d minutes, vote:%d, minplayer:%d, maxplayer:%d, skiplines:%d", c.mapname, modestr(c.mode, false), c.time, c.vote, c.minplayer, c.maxplayer, c.skiplines);
			}
			else
			{
				logline(ACLOG_INFO," error in line %d, file %s", line, cfgfilename);
			}
		}
	}
	delete[] buf;
	logline(ACLOG_INFO,"read %d map rotation entries from '%s'", configsets.length(), cfgfilename);
}

int cmpiprange(const struct iprange *a, const struct iprange *b){
	if(a->lr < b->lr) return -1;
	if(a->lr > b->lr) return 1;
	return 0;
}

int cmpipmatch(const struct iprange *a, const struct iprange *b) { return - (a->lr < b->lr) + (a->lr > b->ur); }

vector<iprange> ipblacklist, masterbans, masterallows;

void readipblacklist(const char *name){
	static string blfilename;
	static int blfilesize;
	char *p, *l, *r;
	iprange ir;
	int len, line = 0, errors = 0;

	if(!name && getfilesize(blfilename) == blfilesize) return;
	ipblacklist.shrink(0);
	char *buf = loadcfgfile(blfilename, name, &len);
	blfilesize = len;
	if(!buf) return;
	p = buf;
	logline(ACLOG_VERBOSE,"reading ip blacklist '%s'", blfilename);
	while(p < buf + len)
	{
		l = p; p += strlen(p) + 1; line++;
		if((r = (char *) atoipr(l, &ir)))
		{
			ipblacklist.add(ir);
			l = r;
		}
		if(l[strspn(l, " ")])
		{
			for(int i = strlen(l) - 1; i > 0 && l[i] == ' '; i--) l[i] = '\0';
			logline(ACLOG_INFO," error in line %d, file %s: ignored '%s'", line, blfilename, l);
			errors++;
		}
	}
	delete[] buf;
	ipblacklist.sort(cmpiprange);
	int orglength = ipblacklist.length();
	loopv(ipblacklist)
	{
		if(!i) continue;
		if(ipblacklist[i].ur <= ipblacklist[i - 1].ur)
		{
			if(ipblacklist[i].lr == ipblacklist[i - 1].lr && ipblacklist[i].ur == ipblacklist[i - 1].ur)
				logline(ACLOG_VERBOSE," blacklist entry %s got dropped (double entry)", iprtoa(ipblacklist[i]));
			else
				logline(ACLOG_VERBOSE," blacklist entry %s got dropped (already covered by %s)", iprtoa(ipblacklist[i]), iprtoa(ipblacklist[i - 1]));
			ipblacklist.remove(i--); continue;
		}
		if(ipblacklist[i].lr <= ipblacklist[i - 1].ur)
		{
			logline(ACLOG_VERBOSE," blacklist entries %s and %s are joined due to overlap", iprtoa(ipblacklist[i - 1]), iprtoa(ipblacklist[i]));
			ipblacklist[i - 1].ur = ipblacklist[i].ur;
			ipblacklist.remove(i--); continue;
		}
	}
	loopv(ipblacklist) logline(ACLOG_VERBOSE," %s", iprtoa(ipblacklist[i]));
	logline(ACLOG_INFO,"read %d (%d) blacklist entries from '%s', %d errors", ipblacklist.length(), orglength, blfilename, errors);
}

int countmbans() { return masterbans.length(); }

void clearmbans(){ masterbans.setsize(0); }

void addmrange(enet_uint32 start, enet_uint32 end, bool allow){
	iprange &ir = allow ? masterallows.add() : masterbans.add();
	ir.lr = start;
	ir.ur = end;
}

inline bool checkblacklist(enet_uint32 ip, vector<iprange> &ranges){ // ip: network byte order
	iprange t;
	t.lr = ntohl(ip); // blacklist uses host byte order
	t.ur = 0;
	return ranges.search(&t, cmpipmatch) != NULL;
}

inline bool checkmasterbans(enet_uint32 ip) { return checkblacklist(ip, masterbans); }
inline bool checkmasterallows(enet_uint32 ip) { return checkblacklist(ip, masterallows); }
bool checkipblacklist(enet_uint32 ip) { return (checkblacklist(ip, ipblacklist) || (checkmasterbans(ip) && !checkmasterallows(ip))); }

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
				if(ic.pwd) s_strcatf(text, "  pwd:\"%s\"", hiddenpwd(ic.pwd));
				else s_strcatf(text, "  %s", iprtoa(ic.ipr));
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
		ipr.lr = ntohl(c.peer->address.host); // blacklist uses host byte order
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

struct pwddetail
{
	string pwd;
	int line;
	int priv;
};

vector<pwddetail> adminpwds;
#define ADMINPWD_MAXPAR 1

void readpwdfile(const char *name){
	static string pwdfilename;
	static int pwdfilesize;
	const char *sep = " ";
	pwddetail c;
	char *p, *l;
	int i, len, line, par[ADMINPWD_MAXPAR];

	if(!name && getfilesize(pwdfilename) == pwdfilesize) return;
	adminpwds.shrink(0);
	char *buf = loadcfgfile(pwdfilename, name, &len);
	pwdfilesize = len;
	if(!buf) return;
	p = buf; line = 1;
	logline(ACLOG_VERBOSE,"reading admin passwords '%s'", pwdfilename);
	while(p < buf + len)
	{
		l = p; p += strlen(p) + 1;
		l = strtok(l, sep);
		if(l)
		{
			copystring(c.pwd, l);
			par[0] = 0;  // default values
			for(i = 0; i < ADMINPWD_MAXPAR; i++)
			{
				if((l = strtok(NULL, sep)) != NULL)
					par[i] = atoi(l);
				else
					break;
			}
			c.line = line;
			c.priv = par[0];
			adminpwds.add(c);
			logline(ACLOG_VERBOSE,"line%4d: %s (%s)", c.line, hiddenpwd(c.pwd), privname(c.priv));
		}
		line++;
	}
	delete[] buf;
	logline(ACLOG_INFO,"read %d admin passwords from '%s'", adminpwds.length(), pwdfilename);
}

bool checkadmin(const char *name, const char *pwd, int salt, pwddetail *detail = NULL){
	bool found = false;
	loopv(adminpwds)
	{
		if(!strcmp(genpwdhash(name, adminpwds[i].pwd, salt), pwd))
		{
			if(detail) *detail = adminpwds[i];
			found = true;
			break;
		}
	}
	return found;
}

bool updatedescallowed(void) { return scl.servdesc_pre[0] || scl.servdesc_suf[0]; }

void updatesdesc(const char *newdesc, ENetAddress *caller = NULL){
	if(!newdesc || !newdesc[0] || !updatedescallowed())
	{
		copystring(servdesc_current, scl.servdesc_full);
		custom_servdesc = false;
	}
	else
	{
		formatstring(servdesc_current)("%s%s%s", scl.servdesc_pre, newdesc, scl.servdesc_suf);
		custom_servdesc = true;
		if(caller) servdesc_caller = *caller;
	}
}

bool updateclientteam(int client, int team, int ftr){
	if(!valid_client(client) || !team_valid(team)) return false;
	if(clients[client]->team == team){
		if (ftr != FTR_AUTOTEAM) return false;
	}
	else{
		clients[client]->state.grenades.reset();
		clients[client]->state.knives.reset();
		clients[client]->removetimers(GE_PROJ);
	}
	sendf(-1, 1, "ri3", N_SETTEAM, client, (clients[client]->team = team) | (ftr << 4));
	checkai();
	if(m_team || team == TEAM_SPECT) forcedeath(clients[client]);
	return true;
}

int calcscores() // skill eval
{
	int fp12 = (m_ctf || m_htf) ? 55 : 33;
	int fp3 = (m_ctf || m_htf) ? 25 : 15;
	int sum = 0;
	loopv(clients) if(clients[i]->type!=ST_EMPTY)
	{
		clientstate &cs = clients[i]->state;
		sum += clients[i]->at3_score = (cs.frags * 85 + cs.assists * 15) / (cs.deaths ? cs.deaths : 1)
									 + (cs.flagscore < 3 ? fp12 * cs.flagscore : 2 * fp12 + fp3 * (cs.flagscore - 2));
	}
	return sum;
}

ivector shuffle;

void shuffleteams(int ftr = FTR_AUTOTEAM){
	int numplayers = numclients();
	int team, sums = calcscores();
	if(gamemillis < 2 * 60 *1000){ // random
		int teamsize[2] = {0, 0};
		loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->team != TEAM_SPECT){
			sums += rnd(1000);
			team = sums & 1;
			if(teamsize[team] >= numplayers/2) team = team_opposite(team);
			updateclientteam(i, team, ftr);
			teamsize[team]++;
			sums >>= 1;
		}
	}
	else{ // skill sorted
		shuffle.shrink(0);
		sums /= 4 * numplayers + 2;
		team = rnd(2);
		loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->team != TEAM_SPECT) { clients[i]->at3_score += rnd(sums | 1); shuffle.add(i); }
		shuffle.sort(cmpscore);
		loopi(shuffle.length()){
			updateclientteam(shuffle[i], team, ftr);
			team = !team;
		}
	}
}


bool balanceteams(int ftr)  // pro vs noobs never more
{
    if(mastermode != MM_OPEN || numauthedclients() < 3 ) return true;
    int tsize[2] = {0, 0}, tscore[2] = {0, 0};
    int totalscore = 0, nplayers = 0;
    int flagmult = (m_ctf ? 50 : (m_htf ? 25 : 12));

    loopv(clients) if(clients[i]->type!=ST_EMPTY){
        client *c = clients[i];
        if(c->connected){
            int time = servmillis - c->connectmillis + 5000;
            if ( time > gamemillis ) time = gamemillis + 5000;
            tsize[c->team]++;
            // effective score per minute: in a normal game, normal players will do 500 points in 10 minutes
            c->eff_score = c->state.points * 60 * 1000 / time + c->state.points / 6 + c->state.flagscore * flagmult;
            tscore[c->team] += c->eff_score;
            nplayers++;
            totalscore += c->state.points;
        }
    }

    int h = 0, l = 1;
    if ( tscore[1] > tscore[0] ) { h = 1; l = 0; }
    if ( 2 * tscore[h] < 3 * tscore[l] || totalscore < nplayers * 100 ) return true;
    if ( tscore[h] > 3 * tscore[l] && tscore[h] > 150 * nplayers ){
        shuffleteams();
        return true;
    }

    float diffscore = tscore[h] - tscore[l];

    int besth = 0, hid = -1;
    int bestdiff = 0, bestpair[2] = {-1, -1};
    if ( tsize[h] - tsize[l] > 0 ) // the h team has more players, so we will force only one player
    {
        loopv(clients) if( clients[i]->type!=ST_EMPTY )
        {
            client *c = clients[i]; // loop for h
            // client from the h team and without the flag
            if( c->connected && c->team == h && clienthasflag(i) < 0 )
            {
                // do not exchange in the way that weaker team becomes the stronger or the change is less than 20% effective
                if ( 2 * c->eff_score <= diffscore && 10 * c->eff_score >= diffscore && c->eff_score > besth )
                {
                    besth = c->eff_score;
                    hid = i;
                }
            }
        }
        if ( hid >= 0 )
        {
            updateclientteam(hid, l, ftr);
            clients[hid]->at3_lastforce = gamemillis;
            return true;
        }
    } else { // the h score team has less or the same player number, so, lets exchange
        loopv(clients) if(clients[i]->type!=ST_EMPTY)
        {
            client *c = clients[i]; // loop for h
            if( c->connected && c->team == h && clienthasflag(i) < 0 )
            {
                loopvj(clients) if(clients[j]->type!=ST_EMPTY && j != i )
                {
                    client *cj = clients[j]; // loop for l
                    if( cj->connected && cj->team == l && clienthasflag(j) < 0 )
                    {
                        int pairdiff = 2 * (c->eff_score - cj->eff_score);
                        if ( pairdiff <= diffscore && 5 * pairdiff >= diffscore && pairdiff > bestdiff )
                        {
                            bestdiff = pairdiff;
                            bestpair[h] = i;
                            bestpair[l] = j;
                        }
                    }
                }
            }
        }
        if ( bestpair[h] >= 0 && bestpair[l] >= 0 )
        {
            updateclientteam(bestpair[h], l, ftr);
            updateclientteam(bestpair[l], h, ftr);
            clients[bestpair[h]]->at3_lastforce = clients[bestpair[l]]->at3_lastforce = gamemillis;
            return true;
        }
    }
    return false;
}

int lastbalance = 0, waitbalance = 2 * 60 * 1000;

bool refillteams(bool now, int ftr){ // force only minimal amounts of players
	static int lasttime_eventeams = 0;
    int teamsize[2] = {0, 0}, teamscore[2] = {0, 0}, moveable[2] = {0, 0};
    bool switched = false;

    calcscores();
    loopv(clients) if(clients[i]->type!=ST_EMPTY){ // playerlist stocktaking
        client *c = clients[i];
        c->at3_dontmove = true;
        if(c->connected && c->team != TEAM_SPECT){
			teamsize[c->team]++;
			teamscore[c->team] += c->at3_score;
			if(clienthasflag(i) < 0) {
				c->at3_dontmove = false;
				moveable[c->team]++;
			}
        }
    }
    int bigteam = teamsize[1] > teamsize[0];
    int allplayers = teamsize[0] + teamsize[1];
    int diffnum = teamsize[bigteam] - teamsize[!bigteam];
    int diffscore = teamscore[bigteam] - teamscore[!bigteam];
    if(lasttime_eventeams > gamemillis) lasttime_eventeams = 0;
    if(diffnum > 1)
    {
        if(now || gamemillis - lasttime_eventeams > 8000 + allplayers * 1000 || diffnum > 2 + allplayers / 10)
        {
            // time to even out teams
            loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->team != bigteam) clients[i]->at3_dontmove = true;  // dont move small team players
            while(diffnum > 1 && moveable[bigteam] > 0)
            {
                // pick best fitting cn
                int pick = -1;
                int bestfit = 1000000000;
                int targetscore = diffscore / (diffnum & ~1);
                loopv(clients) if(clients[i]->type!=ST_EMPTY && !clients[i]->at3_dontmove) // try all still movable players
                {
                    int fit = targetscore - clients[i]->at3_score;
                    if(fit < 0 ) fit = -(fit * 15) / 10;       // avoid too good players
                    int forcedelay = clients[i]->at3_lastforce ? (1000 - (gamemillis - clients[i]->at3_lastforce) / (5 * 60)) : 0;
                    if(forcedelay > 0) fit += (fit * forcedelay) / 600;   // avoid lately forced players
                    if(fit < bestfit + fit * rnd(100) / 400)   // search 'almost' best fit
                    {
                        bestfit = fit;
                        pick = i;
                    }
                }
                if(pick < 0) break; // should really never happen
                // move picked player
                clients[pick]->at3_dontmove = true;
                moveable[bigteam]--;
                if(updateclientteam(pick, team_opposite(bigteam), ftr)){
                    diffnum -= 2;
                    diffscore -= 2 * clients[pick]->at3_score;
                    clients[pick]->at3_lastforce = gamemillis;  // try not to force this player again for the next 5 minutes
                    switched = true;
                }
            }
        }
    }
    if(diffnum < 2)
    {
        if ( ( gamemillis - lastbalance ) > waitbalance && ( gamelimit - gamemillis ) > 4*60*1000 )
        {
            if ( balanceteams (ftr) )
            {
                waitbalance = 2 * 60 * 1000 + gamemillis / 3;
                switched = true;
            }
            else waitbalance = 20 * 1000;
            lastbalance = gamemillis;
        }
        else if ( lastbalance > gamemillis )
        {
            lastbalance = 0;
            waitbalance = 2 * 60 * 1000;
        }
        lasttime_eventeams = gamemillis;
    }
    return switched;
}

void resetserver(const char *newname, int newmode, int newtime){
	if(m_demo) enddemoplayback();
	else enddemorecord();

	smode = newmode;
	copystring(smapname, newname);

	minremain = newtime >= 0 ? newtime : (m_team ? 15 : 10);
	gamemillis = 0;
	gamelimit = minremain*60000;

	mapreload = false;
	interm = 0;
	if(!laststatus) laststatus = servmillis-61*1000;
	lastfillup = servmillis;
	sents.shrink(0);
	scores.shrink(0);
	ctfreset();
}

inline void putmap(ucharbuf &p){
	putint(p, N_MAPCHANGE);
	putint(p, smode);
	putint(p, mapavailable(smapname));
	sendstring(smapname, p);
	loopv(sents) if(sents[i].spawned){
		putint(p, i);
	}
	putint(p, -1);
	putint(p, sknives.length());
	loopv(sknives){
		putint(p, sknives[i].id);
		putint(p, KNIFETTL+sknives[i].millis-gamemillis);
		putfloat(p, sknives[i].o.x);
		putfloat(p, sknives[i].o.y);
		putfloat(p, sknives[i].o.z);
	}
}

void resetmap(const char *newname, int newmode, int newtime, bool notify){
	bool lastteammode = m_team;
	resetserver(newname, newmode, newtime);

	if(isdedicated) getservermap();

	mapstats *ms = getservermapstats(smapname, true);
	if(ms){
		smapstats = *ms;
		loopi(2)
		{
			sflaginfo &f = sflaginfos[i];
			if(smapstats.flags[i] == 1)	// don't check flag positions, if there is more than one flag per team
			{
				short *fe = smapstats.entposs + smapstats.flagents[i] * 3;
				f.x = *fe;
				fe++;
				f.y = *fe;
			}
			else f.x = f.y = -1;
		}

		entity e;
		loopi(smapstats.hdr.numents)
		{
			e.type = smapstats.enttypes[i];
			e.transformtype(smode);
			server_entity se = { e.type, false, true, 0, smapstats.entposs[i * 3], smapstats.entposs[i * 3 + 1]};
			sents.add(se);
			if(e.fitsmode(smode)) sents[i].spawned = true;
		}
		// copyrevision = copymapsize == smapstats.cgzsize ? smapstats.hdr.maprevision : 0;
	}
	else sendmsg(11);
	if(notify){
		// change map
		sknives.setsize(sknifeid = 0);
		ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		ucharbuf p(packet->data, packet->dataLength);
		putmap(p);
		enet_packet_resize(packet, p.length());
		sendpacket(-1, 1, packet);
		if(!packet->referenceCount) enet_packet_destroy(packet);
		// time remaining
		if(m_fight(smode) || numnonlocalclients()) sendf(-1, 1, "ri3", N_TIMEUP, gamemillis, gamelimit);
	}
	logline(ACLOG_INFO, "");
	logline(ACLOG_INFO, "Game start: %s on %s, %d players, %d minutes remaining, mastermode %d, (itemlist %spreloaded, 'getmap' %sprepared)",
		modestr(smode), smapname, numclients(), minremain, mastermode, ms ? "" : "not ", mapavailable(smapname) ? "" : "not ");
	arenaround = 0;
	nokills = true;
	if(m_duel) distributespawns();
	if(notify){
		// shuffle if previous mode wasn't a team-mode
		if(m_team){
			if(!lastteammode)
				shuffleteams(FTR_SILENT);
			else if(autoteam)
				refillteams(true, FTR_SILENT);
		}
		// prepare spawns; players will spawn, once they've loaded the correct map
		loopv(clients) if(clients[i]->type!=ST_EMPTY){
			client *c = clients[i];
			c->mapchange();
			forcedeath(c);
		}
	}
	checkai();
	purgesknives();
	if(m_demo) setupdemoplayback();
	else if((demonextmatch || scl.demoeverymatch) && *newname && numnonlocalclients() > 0){
		demonextmatch = false;
		setupdemorecord();
	}
	if(notify && m_ktf) sendflaginfo();

	*nextmapname = 0;
	forceintermission = false;
}

int nextcfgset(bool notify = true, bool nochange = false){ // load next maprotation set
	int n = numclients();
	int csl = configsets.length();
	int ccs = curcfgset;
	if(ccs >= 0 && ccs < csl) ccs += configsets[ccs].skiplines;
	configset *c = NULL;
	loopi(csl)
	{
		ccs++;
		if(ccs >= csl || ccs < 0) ccs = 0;
		c = &configsets[ccs];
		if(n >= c->minplayer && (!c->maxplayer || n <= c->maxplayer))
		{
			if(getservermapstats(c->mapname)) break;
			else logline(ACLOG_INFO, "maprot error: map '%s' not found", c->mapname);
		}
	}
	if(!nochange)
	{
		curcfgset = ccs;
		resetmap(c->mapname, c->mode, c->time, notify);
	}
	return ccs;
}

bool isbanned(int cn){
	if(!valid_client(cn)) return false;
	client &c = *clients[cn];
	if(c.type==ST_LOCAL) return false;
	loopv(bans){
		ban &b = bans[i];
		if(b.host == c.peer->address.host) { return true; }
		if(b.millis >= 0 && b.millis < servmillis) { bans.remove(i--); }
	}
	return checkipblacklist(c.peer->address.host) || checkmasterbans(c.peer->address.host);
}

void banclient(client *c, int minutes){
	ban b; 
	b.host = c->peer->address.host;
	b.millis = servmillis + minutes * 60000;
	bans.add(b);
}

void sendserveropinfo(int receiver = -1){
	loopv(clients) if(valid_client(i)) sendf(receiver, 1, "ri3", N_SETPRIV, i, clients[i]->priv);
}

#include "serveractions.h"
static voteinfo *curvote = NULL;

void scallvotesuc(voteinfo *v){
	if(!v->isvalid()) return;
	DELETEP(curvote);
	curvote = v;
	clients[v->owner]->lastvotecall = servmillis;
	logline(ACLOG_INFO, "[%s] client %s called a vote: %s", clients[v->owner]->hostname, clients[v->owner]->name, v->action->desc ? v->action->desc : "[unknown]");
}

void scallvoteerr(voteinfo *v, int error){
	if(!valid_client(v->owner)) return;
	sendf(v->owner, 1, "ri2", N_CALLVOTEERR, error);
	logline(ACLOG_INFO, "[%s] client %s failed to call a vote: %s (%s)", clients[v->owner]->hostname, clients[v->owner]->name, v->action->desc ? v->action->desc : "[unknown]", voteerrorstr(error));
}


void sendcallvote(int cl = -1){
	if(curvote && curvote->result == VOTE_NEUTRAL){
		ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		ucharbuf p(packet->data, packet->dataLength);
		putint(p, N_CALLVOTE);
		putint(p, curvote->owner);
		putint(p, curvote->type);
		putint(p, servmillis - curvote->callmillis);
		switch(curvote->type)
		{
			case SA_MAP:
				sendstring(((mapaction *)curvote->action)->map, p);
				putint(p, ((mapaction *)curvote->action)->mode);
				break;
			case SA_SERVERDESC:
				sendstring(((serverdescaction *)curvote->action)->sdesc, p);
				break;
			case SA_GIVEADMIN:
				putint(p, ((giveadminaction *)curvote->action)->cn);
				putint(p, ((giveadminaction *)curvote->action)->give);
				break;
			case SA_STOPDEMO:
			case SA_REMBANS:
			case SA_SHUFFLETEAMS:
			default:
				break;
			case SA_KICK:
				sendstring(((kickaction *)curvote->action)->reason, p);
			case SA_SUBDUE:
			case SA_REVOKE:
			case SA_FORCETEAM:
			case SA_SPECT:
				putint(p, ((playeraction *)curvote->action)->cn);
				break;
			case SA_BAN:
				putint(p, ((banaction *)curvote->action)->bantime);
				putint(p, ((playeraction *)curvote->action)->cn);
				break;
			case SA_AUTOTEAM:
			case SA_MASTERMODE:
			case SA_RECORDDEMO:
				putint(p, ((enableaction *)curvote->action)->enable ? 1 : 0);
				break;
			case SA_CLEARDEMOS:
				putint(p, ((cleardemosaction *)curvote->action)->demo);
				break;
			case SA_BOTBALANCE:
				putint(p, ((botbalanceaction *)curvote->action)->bb);
				break;
		}
		enet_packet_resize(packet, p.length());
		sendpacket(cl, 1, packet);
		if(!packet->referenceCount) enet_packet_destroy(packet);
	}
}

bool scallvote(voteinfo *v) // true if a regular vote was called
{
	int area = isdedicated ? EE_DED_SERV : EE_LOCAL_SERV;
	int error = -1;

	if(!v || !v->isvalid()) error = VOTEE_INVALID;
	else if(v->action->role > clients[v->owner]->priv) error = VOTEE_PERMISSION;
	else if(!(area & v->action->area)) error = VOTEE_AREA;
	else if(curvote && curvote->result==VOTE_NEUTRAL) error = VOTEE_CUR;
	else if(clients[v->owner]->priv < PRIV_ADMIN && v->action->isdisabled()) error = VOTEE_DISABLED;
	else if(clients[v->owner]->lastvotecall && servmillis - clients[v->owner]->lastvotecall < 60*1000 && clients[v->owner]->priv < PRIV_ADMIN && numclients()>1)
		error = VOTEE_MAX;
	//else if(v->type == SA_KICK && strlen(((kickaction *)v)->reason) < 4) error = VOTEE_SHORTREASON;

	if(error >= 0){
		scallvoteerr(v, error);
		return false;
	}else{
		scallvotesuc(v);
		sendcallvote();
		// owner auto votes yes
		sendf(-1, 1, "ri3", N_VOTE, v->owner, (clients[v->owner]->vote = VOTE_YES));
		curvote->evaluate();
		return true;
	}
}

void setpriv(int cl, int priv){
	if(!valid_client(cl)) return;
	client &c = *clients[cl];
	if(!priv){ // relinquish
		if(!c.priv) return; // no privilege to relinquish
		sendf(-1, 1, "ri3", N_REQPRIV, cl, c.priv | 0x80);
		logline(ACLOG_INFO,"[%s] %s relinquished %s status", c.hostname, c.name, privname(c.priv));
		c.priv = PRIV_NONE;
		sendserveropinfo();
		return;
	}
	else if(c.priv >= priv){
		sendf(cl, 1, "ri3", N_REQPRIV, cl, priv | 0x40);
		return;
	}
	c.priv = priv;
	sendf(-1, 1, "ri3", N_REQPRIV, cl, c.priv);
	logline(ACLOG_INFO,"[%s] %s claimed %s status", c.hostname, c.name, privname(c.priv));
	sendserveropinfo();
	//if(curvote) curvote->evaluate();
}

#include "serverai.h"

void disconnect_client(int n, int reason){
	if(!clients.inrange(n) || clients[n]->type!=ST_TCPIP) return;
	sdropflag(n);
	/*
	loopv(sknives) if(sknives[i].owner == n){
		sendf(-1, 1, "ri2", N_REMOVEKNIFE, sknives[n].id);
		sknives.remove(i--);
	}
	*/
	client &c = *clients[n];
	loopv(clients) if(clients[i]->state.ownernum == n) deleteai(*clients[i]);
	if(c.priv) setpriv(n, PRIV_NONE);
	const char *scoresaved = "";
	if(c.haswelcome)
	{
		savedscore *sc = findscore(c, true);
		if(sc)
		{
			sc->save(c.state);
			scoresaved = ", score saved";
		}
	}
	int sp = (servmillis - c.connectmillis) / 1000;
	if(reason>=0) logline(ACLOG_INFO, "[%s] disconnecting client %s (%s) cn %d, %d seconds played%s", c.hostname, c.name, disc_reason(reason), n, sp, scoresaved);
	else logline(ACLOG_INFO, "[%s] disconnected client %s cn %d, %d seconds played%s", c.hostname, c.name, n, sp, scoresaved);
	c.peer->data = (void *)-1;
	if(reason>=0) enet_peer_disconnect(c.peer, reason);
	clients[n]->zap();
	sendf(-1, 1, "ri3", N_DISC, n, reason);
	if(curvote) curvote->evaluate();
	checkai();
}

void sendwhois(int sender, int cn){
	if(!valid_client(sender) || !valid_client(cn)) return;

	if(clients[cn]->type == ST_TCPIP){
		sendf(-1, 1, "ri3", N_WHOIS, cn, sender);
		uint ip = clients[cn]->peer->address.host;
		uchar mask = 0;
		switch(clients[cn]->priv){
			// admins and server owner: f.f.f.f/32 full ip
			case PRIV_MAX: case PRIV_ADMIN: mask = 32; break;
			// masters and users: f.f.h/12 full, full, half, empty
			case PRIV_MASTER: case PRIV_NONE: default: mask = 20; break;
		}
		if(mask < 32) ip &= (1 << mask) - 1;

		sendf(sender, 1, "ri4", N_WHOISINFO, cn, ip, mask);
	}
}

// sending of maps between clients

string copyname;
int copysize, copymapsize, copycfgsize, copycfgsizegz;
uchar *copydata = NULL;
bool copyrw = false;

int mapavailable(const char *mapname) { return copydata && !strcmp(copyname, behindpath(mapname)) ? copymapsize : 0; }

bool sendmapserv(int n, string mapname, int mapsize, int cfgsize, int cfgsizegz, uchar *data){
	string name;
	FILE *fp;
	bool written = false;

	if(!mapname[0] || mapsize <= 0 || mapsize + cfgsizegz > MAXMAPSENDSIZE || cfgsize > MAXCFGFILESIZE) return false;
	if(smode != 1 && (strcmp(behindpath(mapname), behindpath(smapname)) || (mapavailable(smapname) && !copyrw))) return false; // map is R/O
	copystring(copyname, mapname);
	copymapsize = mapsize;
	copycfgsize = cfgsize;
	copycfgsizegz = cfgsizegz;
	copysize = mapsize + cfgsizegz;
	copyrw = true;
	DELETEA(copydata);
	copydata = new uchar[copysize];
	memcpy(copydata, data, copysize);

	formatstring(name)(SERVERMAP_PATH_INCOMING "%s.cgz", behindpath(copyname));
	path(name);
	fp = fopen(name, "wb");
	if(fp)
	{
		fwrite(copydata, 1, copymapsize, fp);
		fclose(fp);
		formatstring(name)(SERVERMAP_PATH_INCOMING "%s.cfg", behindpath(copyname));
		path(name);
		fp = fopen(name, "wb");
		if(fp)
		{
			uchar *rawcfg = new uchar[copycfgsize];
			uLongf rawsize = copycfgsize;
			if(uncompress(rawcfg, &rawsize, copydata + copymapsize, copycfgsizegz) == Z_OK && rawsize - copycfgsize == 0)
				fwrite(rawcfg, 1, copycfgsize, fp);
			fclose(fp);
			DELETEA(rawcfg);
			written = true;
		}
	}
	return written;
}

ENetPacket *getmapserv(int n){
	if(!mapavailable(smapname)) return NULL;
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS + copysize, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_RECVMAP);
	sendstring(copyname, p);
	putint(p, copymapsize);
	putint(p, copycfgsize);
	putint(p, copycfgsizegz);
	p.put(copydata, copysize);
	enet_packet_resize(packet, p.length());
	return packet;
}

// provide maps by the server

mapstats *getservermapstats(const char *mapname, bool getlayout){
	const char *name = behindpath(mapname);
	defformatstring(filename)(SERVERMAP_PATH "%s.cgz", name);
	path(filename);
	bool found = fileexists(filename, "r");
	if(!found)
	{
		formatstring(filename)(SERVERMAP_PATH_INCOMING "%s.cgz", name);
		path(filename);
		found = fileexists(filename, "r");
		if(!found)
		{
			formatstring(filename)(SERVERMAP_PATH_BUILTIN "%s.cgz", name);
			path(filename);
			found = fileexists(filename, "r");
		}
	}
	if(getlayout) DELETEA(maplayout);
	return found ? loadmapstats(filename, getlayout) : NULL;
}

#define GZBUFSIZE ((MAXCFGFILESIZE * 11) / 10)

void getservermap(void){
	static uchar *gzbuf = NULL;
	string cgzname, cfgname;
	int cgzsize, cfgsize, cfgsizegz;
	const char *name = behindpath(smapname);   // no paths allowed here
	bool mapisrw = false;

	if(!gzbuf) gzbuf = new uchar[GZBUFSIZE];
	if(!gzbuf) return;
	if(!strcmp(name, behindpath(copyname))) return;
	formatstring(cgzname)(SERVERMAP_PATH "%s.cgz", name);
	path(cgzname);
	if(fileexists(cgzname, "r"))
	{
		formatstring(cfgname)(SERVERMAP_PATH "%s.cfg", name);
	}
	else
	{
		formatstring(cgzname)(SERVERMAP_PATH_INCOMING "%s.cgz", name);
		path(cgzname);
		formatstring(cfgname)(SERVERMAP_PATH_INCOMING "%s.cfg", name);
		mapisrw = true;
	}
	path(cfgname);
	uchar *cgzdata = (uchar *)loadfile(cgzname, &cgzsize);
	uchar *cfgdata = (uchar *)loadfile(cfgname, &cfgsize);
	if(cgzdata && (!cfgdata || cfgsize < MAXCFGFILESIZE))
	{
		uLongf gzbufsize = GZBUFSIZE;
		if(!cfgdata || compress2(gzbuf, &gzbufsize, cfgdata, cfgsize, 9) != Z_OK)
		{
			cfgsize = 0;
			gzbufsize = 0;
		}
		cfgsizegz = (int) gzbufsize;
		if(cgzsize + cfgsizegz < MAXMAPSENDSIZE)
		{
			copystring(copyname, name);
			copymapsize = cgzsize;
			copycfgsize = cfgsize;
			copycfgsizegz = cfgsizegz;
			copysize = cgzsize + cfgsizegz;
			copyrw = mapisrw;
			DELETEA(copydata);
			copydata = new uchar[copysize];
			memcpy(copydata, cgzdata, cgzsize);
			memcpy(copydata + cgzsize, gzbuf, cfgsizegz);
			logline(ACLOG_INFO,"loaded map %s, %d + %d(%d) bytes.", cgzname, cgzsize, cfgsize, cfgsizegz);
		}
	}
	DELETEA(cgzdata);
	DELETEA(cfgdata);
}

void sendresume(client &c){
	sendf(-1, 1, "ri9i6vvi",
			N_RESUME, // i9
			c.clientnum, // i9
			c.state.state, // i9
			c.state.lifesequence, // i9
			c.state.gunselect, // i9
			c.state.points, // i9
			c.state.flagscore, // i9
			c.state.frags, // i9
			c.state.assists, // i9
			c.state.killstreak, // i6
			c.state.deathstreak,// i6
			c.state.deaths, // i6
			c.state.health, // i6
			c.state.armor, // i6
			c.state.radarearned - gamemillis, // i6
			WEAP_MAX, c.state.ammo, // v
			WEAP_MAX, c.state.mag, // v
			-1); // i
}

void sendservinfo(client &c){
	sendf(c.clientnum, 1, "ri4", N_SERVINFO, c.clientnum, PROTOCOL_VERSION, c.salt);
}

void putinitclient(client &c, ucharbuf &p){
	if(c.type == ST_AI) return putinitai(c, p);
    putint(p, N_INITCLIENT);
    putint(p, c.clientnum);
	putint(p, c.team);
    putint(p, c.skin);
	putint(p, c.state.level);
    sendstring(c.name, p);
	if(curvote && c.vote != VOTE_NEUTRAL){
		putint(p, N_VOTE);
		putint(p, c.clientnum);
		putint(p, c.vote);
	}
}

void sendinitclient(client &c){
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putinitclient(c, p);
	enet_packet_resize(packet, p.length());
	sendpacket(-1, 1, packet, c.clientnum);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

void welcomeinitclient(ucharbuf &p, int exclude = -1){
	const int realexclude = valid_client(exclude) ? valid_client(clients[exclude]->state.ownernum) ? clients[exclude]->state.ownernum : exclude : -1;
    loopv(clients){
        client &c = *clients[i];
        if(!c.connected || c.clientnum == realexclude) continue;
        putinitclient(c, p);
    }
}

void welcomepacket(ucharbuf &p, int n, ENetPacket *packet, bool forcedeath){
	#define CHECKSPACE(n) \
	{ \
		int space = (n); \
		if(p.remaining() < space) \
		{ \
		   enet_packet_resize(packet, packet->dataLength + max(MAXTRANS, space - p.remaining())); \
		   p.buf = packet->data; \
		   p.maxlen = (int)packet->dataLength; \
		} \
	}

	if(!smapname[0] && configsets.length()) nextcfgset(false);

	client *c = valid_client(n) ? clients[n] : NULL;
	int numcl = numclients();

	putint(p, N_WELCOME);
	putint(p, smapname[0] && !m_demo ? numcl : -1);
	if(scl.motd[0])
	{
		CHECKSPACE(5+2*(int)strlen(scl.motd)+1);
		sendstring(scl.motd, p);
	} else putint(p, 0);
	if(smapname[0] && !m_demo){
		putmap(p);
		if(m_fight(smode) || numnonlocalclients()){
			putint(p, N_TIMEUP);
			putint(p, gamemillis);
			putint(p, gamelimit);
		}
		if(m_flags){
			CHECKSPACE(256);
			loopi(2) putflaginfo(p, i);
		}
	}
	bool restored = false;
	if(c){
		CHECKSPACE(256);
		sendserveropinfo(n);
		putint(p, N_SETTEAM);
        putint(p, n);
        putint(p, (c->team = freeteam(n)) | (FTR_SILENT << 4));

		putint(p, N_FORCEDEATH);
		putint(p, n);
		sendf(-1, 1, "ri2x", N_FORCEDEATH, n, n);
		if(c->type==ST_TCPIP)
		{
			savedscore *sc = findscore(*c, false);
			if(sc)
			{
				sc->restore(c->state);
				restored = true;
			}
		}
	}
	if(clients.length()>1 || restored || !c)
	{
		putint(p, N_RESUME);
		loopv(clients)
		{
			client &c = *clients[i];
			if((c.type!=ST_TCPIP && c.type != ST_AI) || (c.clientnum==n && !restored)) continue;
			CHECKSPACE(512);
			putint(p, c.clientnum);
			putint(p, c.state.state);
			putint(p, c.state.lifesequence);
			putint(p, c.state.gunselect);
			putint(p, c.state.points);
			putint(p, c.state.flagscore);
			putint(p, c.state.frags);
			putint(p, c.state.assists);
			putint(p, c.state.killstreak);
			putint(p, c.state.deathstreak);
			putint(p, c.state.deaths);
			putint(p, c.state.health);
			putint(p, c.state.armor);
			putint(p, c.state.radarearned - gamemillis);
			loopi(WEAP_MAX) putint(p, c.state.ammo[i]);
			loopi(WEAP_MAX) putint(p, c.state.mag[i]);
		}
		putint(p, -1);
		welcomeinitclient(p, n);
	}

	#undef CHECKSPACE
}

void sendwelcome(client *cl, int chan, bool forcedeath){
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	welcomepacket(p, cl->clientnum, packet, forcedeath);
	enet_packet_resize(packet, p.length());
	sendpacket(cl->clientnum, chan, packet);
	if(!packet->referenceCount) enet_packet_destroy(packet);
	cl->haswelcome = true;
}

void checkmove(client &cp){
	const int sender = cp.clientnum;
	clientstate &cs = cp.state;
	float cps = cs.lasto.dist(cs.o);
	if(cs.lasto != cs.o) cs.movemillis = servmillis;
	else if(cps && cs.lastomillis && gamemillis > cs.lastomillis){
		cps *= 1000 / (gamemillis - cs.lastomillis);
		if(cps > 64.f){ // 16 meters per second
			defformatstring(fastmsg)("%s (%d) moved at %.3f meters/second", cp.name, sender, cps / 4);
			sendservmsg(fastmsg);
		}
	}
	// drown underwater
	if(cs.o.z < smapstats.hdr.waterlevel){
		if(cs.drownmillis <= 0){
			if(cs.drownmillis)
				cs.drownval = max(cs.drownval - ((servmillis + cs.drownmillis) / 1000), 0);
			cs.drownmillis = gamemillis;
		}
		char drownstate = (gamemillis - cs.drownmillis) / 1000;
		while(cs.drownval < drownstate){
			cs.drownval++;
			serverdamage(&cp, &cp, powf(cs.drownval, 7.f)/1000000, WEAP_MAX+1, FRAG_NONE, cs.o);
			if(cs.state != CS_ALIVE) return; // dead!
		}
	}
	else if(cs.drownmillis > 0) cs.drownmillis = -cs.drownmillis;
	// out of map check
	if(cp.type!=ST_LOCAL && !m_edit && checkpos(cs.o, false)){
		if(cp.type == ST_AI) serverdamage(&cp, &cp, 1000, WEAP_MAX + 4, FRAG_NONE, cs.o);
		else{
			logline(ACLOG_INFO, "[%s] %s collides with the map (%d)", cp.hostname, cp.name, ++cp.mapcollisions);
			sendmsgi(40, sender);
			sendf(sender, 1, "ri", N_MAPIDENT);
			forcedeath(&cp);
			cp.isonrightmap = false; // cannot spawn until you get the right map
		}
		return; // no pickups for you!
	}
	// item pickups
	loopv(sents){
		server_entity &e = sents[i];
		if(!e.spawned || !cs.canpickup(e.type)) continue;
		const int ls = (1 << maplayout_factor) - 2, maplayoutid = e.x + (e.y << maplayout_factor);
		const bool getmapz = maplayout && e.x > 2 && e.y > 2 && e.x < ls && e.y < ls;
		const char &mapz = getmapz ? getblockfloor(maplayoutid) : 0;
		vec v(e.x, e.y, getmapz ? mapz + PLAYERHEIGHT : cs.o.z);
		float dist = cs.o.dist(v);
		if(dist > 3) continue;
		if(arenaround && arenaround - gamemillis <= 2000){ // no nade pickup during last two seconds of lss intermission
			sendf(sender, 1, "ri2", N_ITEMSPAWN, i);
			continue;
		}
		serverpickup(i, sender);
	}
	// flags
	if(m_flags) loopi(2){ // check flag pickup
		sflaginfo &f = sflaginfos[i];
		sflaginfo &of = sflaginfos[team_opposite(i)];
		vec v(-1, -1, cs.o.z);
		switch(f.state){
			case CTFF_STOLEN:
				if(!m_return || i != cp.team) break;
			case CTFF_INBASE:
				v.x = f.x; v.y = f.y;
				break;
			case CTFF_DROPPED:
				v.x = f.pos[0]; v.y = f.pos[1];
				break;
		}
		if(v.x < 0) continue;
		float dist = cs.o.dist(v);
		if(dist > 2) continue;
		if(m_ctf){
			if(i == cp.team){ // it's our flag
				if(f.state == CTFF_DROPPED){
					if(m_return && (of.state != CTFF_STOLEN || of.actor_cn != sender)) flagaction(i, FA_PICKUP, sender);
					else flagaction(i, FA_RETURN, sender);
				}
				else if(f.state == CTFF_STOLEN && sender == f.actor_cn) flagaction(i, FA_RETURN, sender);
				else if(f.state == CTFF_INBASE && of.state == CTFF_STOLEN && of.actor_cn == sender) flagaction(team_opposite(i), FA_SCORE, sender);
			}
			else if(f.drop_cn != sender || f.dropmillis + 2000 < servmillis){
				if(m_return && of.state == CTFF_STOLEN && of.actor_cn == sender) flagaction(team_opposite(i), FA_RETURN, sender);
				flagaction(i, FA_PICKUP, sender);
			}
		}
		else if(m_htf){
			if(i == cp.team) flagaction(i, FA_PICKUP, sender);
			else if(f.state == CTFF_DROPPED) flagaction(i, FA_SCORE, sender);
		}
		else if(m_ktf && f.state == CTFF_INBASE) flagaction(i, FA_PICKUP, sender);
	}
	// throwing knife pickup
	if(cp.type != ST_AI) loopv(sknives){
		const bool pickup = cs.o.dist(sknives[i].o) < 5 && cs.ammo[WEAP_KNIFE] < 3, expired = gamemillis - sknives[i].millis > KNIFETTL;
		if(pickup || expired){
			if(pickup) sendf(-1, 1, "ri5", N_RELOAD, sender, WEAP_KNIFE, cs.mag[WEAP_KNIFE], ++cs.ammo[WEAP_KNIFE]);
			sendf(-1, 1, "ri2", N_KNIFEREMOVE, sknives[i].id);
			sknives.remove(i--);
		}
	}
}

#include "auth.h"

int checktype(int type, client *cl){ // invalid defined types handled in the processing function
	if(cl && cl->type==ST_LOCAL) return type; // local
	// tag type
	if (type < 0 || type >= N_NUM) return -1; // out of range
	if(!m_edit && cl && cl->type == ST_TCPIP && (type >= N_EDITH && type <= N_NEWMAP)) return -1; // edit
	// overflow
	static const int exempt[] = { N_POS, N_SPAWN, N_SHOOT, N_SHOOTC, N_PROJ };
	loopi(sizeof(exempt)/sizeof(int)) if(type == exempt[i]) return type; // does not contribute to overflow, just because the bots will have to send this too
	if(cl && cl->type == ST_TCPIP && cl->overflow++ > MAXTRANS) return -2; // overflow
	return type; // normal
}

// server side processing of updates: does very quite a bit more than before!
// IS extended to move more gameplay to server (at expense of lag)

void process(ENetPacket *packet, int sender, int chan)   // sender may be -1
{
	ucharbuf p(packet->data, packet->dataLength);
	char text[MAXTRANS];
	client *cl = sender>=0 ? clients[sender] : NULL;
	int type;

	if(cl && !cl->connected){
		if(chan==0) return;
		else if(chan!=1 || getint(p) != N_CONNECT) disconnect_client(sender, DISC_TAGT);
		else
		{
			getstring(text, p);
			if(!*text) copystring(text, "unarmed");
			filtername(text, text);
			copystring(cl->name, text, MAXNAMELEN+1);
			cl->skin = getint(p);
			cl->state.level = getint(p);
			getstring(text, p);
			copystring(cl->pwd, text);
			int connectauth = getint(p);
			cl->state.nextprimary = getint(p);
			cl->state.nextperk = getint(p);

			int clientversion = getint(p), clientdefs = getint(p);
			logversion(*cl, clientversion, clientdefs);

			int disc = p.remaining() ? DISC_TAGT : allowconnect(*cl, cl->pwd, connectauth);

			if(disc) disconnect_client(sender, disc);
			else cl->connected = true;
		}
		if(!cl->connected) return;

		if(cl->type==ST_TCPIP){
			loopv(clients) if(i != sender){
				client *dup = clients[i];
				if(dup->type==ST_TCPIP && dup->peer->address.host==cl->peer->address.host && dup->peer->address.port==cl->peer->address.port)
					disconnect_client(i, DISC_DUP);
			}
		}

		sendwelcome(cl);
		sendinitclient(*cl);
		if(findscore(*cl, false)) sendresume(*cl);

		if(curvote){
			sendcallvote(sender);
			curvote->evaluate();
		}
		checkai();
	}

	if(packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;

	#define QUEUE_MSG { if(cl->type==ST_TCPIP) while(curmsg<p.length()) cl->messages.add(p.buf[curmsg++]); }
	#define QUEUE_BUF(size, body) { \
		if(cl->type==ST_TCPIP) \
		{ \
			curmsg = p.length(); \
			ucharbuf buf = cl->messages.reserve(size); \
			{ body; } \
			cl->messages.addbuf(buf); \
		} \
	}
	#define QUEUE_INT(n) QUEUE_BUF(5, putint(buf, n))
	#define QUEUE_UINT(n) QUEUE_BUF(4, putuint(buf, n))
	#define QUEUE_STR(text) QUEUE_BUF(2*(int)strlen(text)+1, sendstring(text, buf))
	#define QUEUE_FLOAT(n) QUEUE_BUF(sizeof(float), putfloat(buf, n))
	/*
	#define MSG_PACKET(packet) \
		ENetPacket *packet = enet_packet_create(NULL, 16 + p.length() - curmsg, ENET_PACKET_FLAG_RELIABLE); \
		ucharbuf buf(packet->data, packet->dataLength); \
		putint(buf, N_CLIENT); \
		putint(buf, sender); \
		putuint(buf, p.length() - curmsg); \
		buf.put(&p.buf[curmsg], p.length() - curmsg); \
		enet_packet_resize(packet, buf.length());
		*/

	static gameevent dummy; // in case a client sends on behalf of somebody else's bot

	int curmsg;
	while((curmsg = p.length()) < p.maxlen)
	{
		type = checktype(getint(p), cl);

		#ifdef _DEBUG
		if(type!=N_POS && type!=N_PINGTIME && type!=N_PINGPONG)
		{
			DEBUGVAR(cl->name);
			ASSERT(type>=0 && type<N_NUM);
			DEBUGVAR(messagenames[type]);
			protocoldebug(true);
		}
		else protocoldebug(false);
		#endif

		switch(type)
		{

			case N_TEXT:
			{
				int flags = getint(p), voice = flags & 0x1F; flags = (flags >> 5) & 3; // SAY_DENY is server only
				getstring(text, p);
				if(!cl) break;
				filtertext(text, text);
				sendtext(text, *cl, flags, voice);
				break;
			}

			case N_NEWNAME:
				getstring(text, p);
				filtername(text, text);
				if(!text[0]) copystring(text, "unnamed");
				if(!strcmp(cl->name, text)) break; // same name!
				switch(nbl.checknickwhitelist(*cl)){
					case NWL_PWDFAIL:
					case NWL_IPFAIL:
						logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong IP/PWD", cl->hostname, cl->name);
						disconnect_client(sender, DISC_PASSWORD);
						break;

					case NWL_UNLISTED:
					{
						int l = nbl.checknickblacklist(cl->name);
						if(l >= 0)
						{
							logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", cl->hostname, cl->name, l);
							disconnect_client(sender, DISC_NAME);
						}
						break;
					}
				}
				logline(ACLOG_INFO,"[%s] %s changed his name to %s", cl->hostname, cl->name, text);
				copystring(cl->name, text, MAXNAMELEN+1);
				sendf(-1, 1, "ri2s", N_NEWNAME, sender, cl->name);
				break;

			case N_SWITCHTEAM:
			{
				int t = getint(p);
				if(cl->team == t) break;
				
				if(m_team && cl->priv < PRIV_ADMIN && t != TEAM_SPECT){
					int teamsizes[TEAM_NUM];
					loopv(clients) if(i != sender && clients[i]->type!=ST_EMPTY && clients[i]->connected && clients[i]->isonrightmap && clients[i]->team != TEAM_SPECT)
						teamsizes[clients[i]->team]++;
					if(teamsizes[t] > teamsizes[team_opposite(t)]){
						sendf(sender, 1, "ri2", N_SWITCHTEAM, t);
						break;
					}
				}
				updateclientteam(sender, t, FTR_PLAYERWISH);
				break;
			}

			case N_SKIN:
				sendf(-1, 1, "ri3x", N_SKIN, sender, cl->skin = getint(p), sender);
				break;

			case N_LEVELUP:
				sendf(-1, 1, "ri3x", N_LEVELUP, sender, cl->state.level = clamp(cl->state.level + 1, 1, MAXLEVEL), sender);
				break;

			case N_MAPIDENT:
			{
				int gzs = getint(p);
				if(!isdedicated || m_edit || smapstats.cgzsize == gzs){
					if(cl->state.state == CS_DEAD && canspawn(cl, true)) sendspawn(cl);
					cl->isonrightmap = true;
				}
				else{
					sendf(sender, 1, "ri", N_MAPIDENT);
					if(cl->state.state != CS_DEAD) forcedeath(cl);
				}
				break;
			}

			case N_LOADOUT:
			{
				int cn = getint(p), nextprimary = getint(p), perk = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				cp.state.nextperk = perk;
				if(nextprimary<0 && nextprimary>=WEAP_MAX) break;
				cp.state.nextprimary = nextprimary;
				break;
			}

			case N_TRYSPAWN:
			{
				const int cn = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				if(!cl->isonrightmap){
					if(cn == sender) sendf(sender, 1, "ri", N_MAPIDENT);
					break;
				}
				if(cp.state.state!=CS_DEAD || cp.state.lastspawn>=0 || m_duel) break;
				const int waitremain = (m_flags ? 5000 : 1000) - gamemillis + cp.state.lastdeath;
				if(waitremain > 0) /*sendmsgi(41, waitremain, sender)*/;
				else if(cp.team == TEAM_SPECT) updateclientteam(sender, freeteam(sender), FTR_PLAYERWISH);
				else sendspawn(&cp);
				break;
			}

			case N_SPAWN:
			{
				int cn = getint(p), ls = getint(p), gunselect = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				if((cp.state.state!=CS_ALIVE && cp.state.state!=CS_DEAD) || ls!=cp.state.lifesequence || cp.state.lastspawn<0 || gunselect<0 || gunselect>=WEAP_MAX) break;
				cp.state.lastspawn = -1;
				cp.state.spawnmillis = gamemillis;
				cp.state.state = CS_ALIVE;
				cp.state.gunselect = gunselect;
				QUEUE_BUF(5*(7 + 2*WEAP_MAX),
				{
					putint(buf, N_SPAWN);
					putint(buf, cn);
					putint(buf, ls);
					putint(buf, cp.state.health);
					putint(buf, cp.state.armor);
					putint(buf, cp.state.perk);
					putint(buf, gunselect);
					loopi(WEAP_MAX) putint(buf, cp.state.ammo[i]);
					loopi(WEAP_MAX) putint(buf, cp.state.mag[i]);
				});
				break;
			}

			/*
			case N_FALL:
				if(cl->state.state == CS_ALIVE) serverdamage(cl, cl, getint(p) * DAMAGESCALE, WEAP_MAX+2, FRAG_NONE, cl->state.o);
				break;
			*/

			case N_SUICIDE:
			{
				const int cn = getint(p);
				if(!hasclient(cl, cn)) break;
				client *cp = clients[cn];
				if(cp->state.state != CS_ALIVE) break;
				serverdamage(cp, cp, 1000, cn == sender ? WEAP_MAX : WEAP_MAX + 4, cn == sender ? FRAG_GIB : FRAG_NONE, cp->state.o);
				break;
			}

			case N_SCOPE:
			{
				bool scope = getint(p) != 0;
				if(!cl->state.isalive(gamemillis) || !ads_gun(cl->state.gunselect) || cl->state.scoping == scope) break;
				cl->state.scoping = scope;
				cl->state.scopemillis = gamemillis - ADSTIME + min(gamemillis - cl->state.scopemillis, ADSTIME);
				sendf(-1, 1, "ri3x", N_SCOPE, sender, scope ? 1 : 0, sender);
				break;
			}

			case N_SHOOT:
			case N_SHOOTC:
			{
				const int cn = getint(p);
				const bool hascn = hasclient(cl, cn);
				client *cp = hascn ? clients[cn] : NULL;
				static gameevent dummy;
				gameevent &shot = hascn ? cp->addevent() : dummy;
				shot.type = GE_SHOT;
				#define seteventmillis(event, idm) \
				{ \
					event.id = idm; \
					if(!cp->timesync || (cp->events.length()==1 && cp->state.waitexpired(gamemillis))) \
					{ \
						cp->timesync = true; \
						cp->gameoffset = gamemillis - event.id; \
						event.millis = gamemillis; \
					} \
					else event.millis = cp->gameoffset + event.id; \
				}
				int id = getint(p);
				if(hascn) seteventmillis(shot.shot, id);
				shot.shot.gun = getint(p);
				shot.shot.compact = type == N_SHOOTC;
				if(type == N_SHOOT){
					loopk(3) shot.shot.to[k] = getfloat(p);
				}
				break;
			}

			case N_PROJ:
			{
				const int cn = getint(p);
				const bool hascn = hasclient(cl, cn);
				client *cp = hascn ? clients[cn] : NULL;
				gameevent &exp = hascn ? cp->addevent() : dummy;
				exp.type = GE_PROJ;
				int id = getint(p);
				if(hascn) seteventmillis(exp.proj, id);
				exp.proj.gun = getint(p);
				exp.proj.flag = getint(p);
				loopi(3) exp.proj.o[i] = getfloat(p);
				break;
			}

			case N_AKIMBO:
			{
				const int cn = getint(p), id = getint(p);
				if(!hasclient(cl, cn)) break;
				client *cp = clients[cn];
				gameevent &akimbo = cp->addevent();
				akimbo.type = GE_AKIMBO;
				seteventmillis(akimbo.akimbo, id);
				break;
			}

			case N_RELOAD:
			{
				int cn = getint(p), id = getint(p), gun = getint(p);
				if(!hasclient(cl, cn)) break;
				client *cp = clients[cn];
				gameevent &reload = cp->addevent();
				reload.type = GE_RELOAD;
				seteventmillis(reload.reload, id);
				reload.reload.gun = gun;
				break;
			}

			case N_QUICKSWITCH:
			{
				const int cn = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				cp.state.gunselect = cp.state.primary;
				sendf(-1, 1, "ri2x", N_QUICKSWITCH, cn, sender);
				cp.state.scoping = false;
				cp.state.scopemillis = gamemillis - ADSTIME;
				break;
			}

			case N_SWITCHWEAP:
			{
				int cn = getint(p), weaponsel = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				if(weaponsel < 0 || weaponsel >= WEAP_MAX) break;
				cp.state.gunselect = weaponsel;
				sendf(-1, 1, "ri3x", N_SWITCHWEAP, cn, weaponsel, sender);
				cp.state.scoping = false;
				cp.state.scopemillis = gamemillis - ADSTIME;
				break;
			}

			case N_THROWKNIFE:
			{
				const int cn = getint(p);
				vec from, vel;
				loopi(3) from[i] = getfloat(p);
				loopi(3) vel[i] = getfloat(p);
				if(!hasclient(cl, cn)) break;
				clientstate &cps = clients[cn]->state;
				if(cps.knives.throwable <= 0) break;
				cps.knives.throwable--;
				checkpos(from);
				if(vel.magnitude() > KNIFEPOWER) vel.normalize().mul(KNIFEPOWER);
				sendf(-1, 1, "ri2f6x", N_THROWKNIFE, cn, from.x, from.y, from.z, vel.x, vel.y, vel.z, sender);
				break;
			}

			case N_THROWNADE:
			{
				const int cn = getint(p);
				vec from, vel;
				loopi(3) from[i] = getfloat(p);
				loopi(3) vel[i] = getfloat(p);
				int cooked = clamp(getint(p), 1, NADETTL);
				if(!hasclient(cl, cn)) break;
				clientstate &cps = clients[cn]->state;
				if(cps.grenades.throwable <= 0) break;
				cps.grenades.throwable--;
				checkpos(from);
				if(vel.magnitude() > NADEPOWER) vel.normalize().mul(NADEPOWER);
				sendf(-1, 1, "ri2f6ix", N_THROWNADE, cn, from.x, from.y, from.z, vel.x, vel.y, vel.z, cooked, sender);
				break;
			}

			case N_STREAKUSE:
			{
				vec o;
				loopi(3) o[i] = getfloat(p);
				//usestreak(*cl, STREAK_AIRSTRIKE, o);
				break;
			}

			case N_PINGPONG:
				sendf(sender, 1, "ii", N_PINGPONG, getint(p));
				break;

			case N_PINGTIME:
			{
				const int pingtime = clamp(getint(p), 0, 99999);
				if(!cl) break;
				const int newping = cl->ping == 9999 ? pingtime : (cl->ping * 4 + pingtime) / 5;
				loopv(clients) if(clients[i]->type != ST_EMPTY && (i == sender || clients[i]->state.ownernum == sender)) clients[i]->ping = newping;
				sendf(-1, 1, "i3", N_PINGTIME, sender, cl->ping);
				break;
			}

			case N_EDITMODE:
			{
				bool editing = getint(p) != 0;
				if(!m_edit && cl->type == ST_TCPIP){ // unacceptable
					logline(ACLOG_INFO, "[%s] %s tried editmode", cl->hostname, cl->name);
					forcedeath(cl, true, true);
					break;
				}
				if(cl->state.state != (editing ? CS_ALIVE : CS_EDITING)) break;
				cl->state.state = editing ? CS_EDITING : CS_ALIVE;
				sendf(-1, 1, "ri3x", N_EDITMODE, sender, editing ? 1 : 0, sender);
				break;
			}

			// client to client
			// coop editing messages (checktype() checks for editmode)
			case N_EDITH: // height
			case N_EDITT: // (ignore and relay) texture
			case N_EDITS: // solid or not
			case N_EDITD: // delta
			case N_EDITE: // equalize
			{
				int x  = getint(p);
				int y  = getint(p);
				int xs = getint(p);
				int ys = getint(p);
				int v  = getint(p);
				switch(type)
				{
					#define seditloop(body) \
						loop(xx, xs) loop(yy, ys) \
						if(x + xx > 0 && x + xx < (1 << maplayout_factor) && y + yy > 0 && y + yy < (1 << maplayout_factor)){ \
						const int id = (x + xx) + ((y + yy) << maplayout_factor); body }
					case N_EDITH:
					{
						int offset = getint(p);
						seditloop({
							if(!v){ // ceil
								maplayout[id].ceil += offset;
								if(maplayout[id].ceil <= maplayout[id].floor) maplayout[id].ceil = maplayout[id].floor+1;
							}
							else{ // floor
								maplayout[id].floor += offset;
								if(maplayout[id].floor >= maplayout[id].ceil) maplayout[id].floor = maplayout[id].ceil-1;
							}
						});
						break;
					}
					case N_EDITS:
						seditloop({
							maplayout[id].type = v;
						});
						break;
					case N_EDITD:
						seditloop({
							maplayout[id].vdelta += v;
							if(maplayout[id].vdelta < 0) maplayout[id].vdelta = 0;
						});
						break;
					case N_EDITE:
					{
						int low = 127, hi = -128;
						seditloop({
							if(maplayout[id].floor<low) low = maplayout[id].floor;
							if(maplayout[id].ceil>hi) hi = maplayout[id].ceil;
						});
						seditloop({
							if(!v) maplayout[id].ceil = hi; else maplayout[id].floor = low;
							if(maplayout[id].floor >= maplayout[id].ceil) maplayout[id].floor = maplayout[id].ceil-1;
						});
						break;
					}
					// ignore texture
					case N_EDITT: getint(p); break;
				}
				QUEUE_MSG;
				break;
			}

			case N_EDITW:
				// set water level
				smapstats.hdr.waterlevel = getint(p);
				// water color alpha
				loopi(4) getint(p);
				QUEUE_MSG;
				break;

			case N_EDITENT:
			{
				const int id = getint(p), type = getint(p);
				vec o;
				loopi(3) o[i] = getint(p);
				// placeholders
				server_entity see = { NOTUSED, false, false, 0, 0, 0};
				while(sents.length()<=id) sents.add(see);
				// entity
				entity e;
				e.type = type;
				e.transformtype(smode);
				// server entity
				server_entity se = { e.type, false, true, 0, o.x, o.y};
				if(e.fitsmode(smode)) se.spawned = true;
				sents[id] = se;
				// skip attributes
				loopi(4) getint(p);
				QUEUE_MSG;
				break;
			}

			case N_NEWMAP: // the server needs to create a new layout
			{
				const int size = getint(p);
				if(size < 0) maplayout_factor++;
				else maplayout_factor = size;
				DELETEA(maplayout)
				if(maplayout_factor >= 0){
					sents.shrink(0);
					maplayout_factor = clamp(maplayout_factor, SMALLEST_FACTOR, LARGEST_FACTOR);
					smapstats.hdr.waterlevel = -100000;
					const int layoutsize = 1 << (maplayout_factor * 2);
					ssqr defaultblock;
					defaultblock.type = SPACE;
					defaultblock.floor = 0;
					defaultblock.ceil = 16;
					defaultblock.vdelta = 0;
					maplayout = new ssqr[layoutsize + 256];
					loopi(layoutsize) memcpy(maplayout + i, &defaultblock, sizeof(ssqr));
				}
				QUEUE_MSG;
				break;
			}

			case N_POS:
			{
				const int cn = getint(p);
				bool broadcast = true;
				if(!hasclient(cl, cn)) broadcast = false;
				vec newo, newaim, newvel;
				loopi(3) newo[i] = getfloat(p);
				loopi(3) newaim[i] = getfloat(p);
				loopi(3) newvel[i] = getfloat(p);
				const float newpitchvel = getfloat(p);
				const int f = getuint(p), seqcolor = (f>>6)&1;
				const bool newcrouching = (f>>7)&1;
				if(!valid_client(cn)) break;
				client &cp = *clients[cn];
				clientstate &cs = cp.state;
				if((cs.state!=CS_ALIVE && cs.state!=CS_EDITING) || seqcolor!=(cs.lifesequence&1) || !broadcast) break;
				// store location
				cs.lasto = cs.o;
				cs.lastomillis = gamemillis;
				cs.o = newo;
				cs.aim = newaim;
				cs.vel = newvel;
				cs.pitchvel = newpitchvel;
				// crouch
				if(cs.crouching != newcrouching){
					cs.crouching = newcrouching;
					cs.crouchmillis = gamemillis - CROUCHTIME + min(gamemillis - cl->state.crouchmillis, CROUCHTIME);
				}
				// broadcast
				if(cs.spawnmillis + SPAWNPROTECT <= gamemillis){
					cp.position.setsize(0);
					while(curmsg < p.length()) cp.position.add(p.buf[curmsg++]);
				}
				// check movement
				if(cs.state==CS_ALIVE) checkmove(cp);
				break;
			}

			case N_NEXTMAP:
			{
				getstring(text, p);
				filtertext(text, text);
				int mode = getint(p);
				if(mapreload || numclients() == 1) resetmap(text, mode);
				break;
			}

			case N_SENDMAP:
			{
				getstring(text, p);
				filtertext(text, text);
				int mapsize = getint(p);
				int cfgsize = getint(p);
				int cfgsizegz = getint(p);
				if(p.remaining() < mapsize + cfgsizegz)
				{
					p.forceoverread();
					break;
				}
				if(sendmapserv(sender, text, mapsize, cfgsize, cfgsizegz, &p.buf[p.len]))
				{
					sendf(-1, 1, "ri2s", N_SENDMAP, sender, text);
					logline(ACLOG_INFO,"[%s] %s sent map %s, %d + %d(%d) bytes written",
								clients[sender]->hostname, clients[sender]->name, text, mapsize, cfgsize, cfgsizegz);
				}
				else
				{
					logline(ACLOG_INFO,"[%s] %s sent map %s, not written to file",
								clients[sender]->hostname, clients[sender]->name, text);
				}
				p.len += mapsize + cfgsizegz;
				break;
			}

			case N_RECVMAP:
			{
				ENetPacket *mappacket = getmapserv(cl->clientnum);
				if(mappacket)
				{
					resetflag(sender); // drop ctf flag
					// save score
					savedscore *sc = findscore(*cl, true);
					if(sc) sc->save(cl->state);
					// resend state properly
					sendpacket(sender, 2, mappacket);
					cl->mapchange();
					sendwelcome(cl, 2, true);
				}
				else{
					cl->isonrightmap = true;
					sendmsg(13, sender);
				}
				break;
			}

			case N_DROPFLAG:
			{
				loopi(2){
					int fl = clienthasflag(sender);
					if(fl >= 0){
						flagaction(fl, FA_DROP, sender);
					} else break;
				}
				break;
			}

			case N_REQPRIV:
			{
				int wants = getint(p);
				getstring(text, p);
				if(wants){
					int priv = PRIV_NONE;
					if(cl->type == ST_LOCAL) priv = wants;
					else if(wants > PRIV_MASTER){
						if(!*text) break;
						pwddetail pd;
						pd.line = -1;
						if(!checkadmin(cl->name, text, cl->salt, &pd) || !pd.priv){
							if(cl->priv < PRIV_ADMIN && !priv){
								disconnect_client(sender, DISC_LOGINFAIL); // avoid brute-force
								return;
							}
						} else {
							priv = min(wants, pd.priv);
							if(pd.line >= 0) logline(ACLOG_INFO,"[%s] %s used %s password in line %d", cl->hostname, cl->name, privname(wants), pd.line);
						}
					}
					else{
						priv = PRIV_MASTER;
						if(cl->authpriv) priv = cl->authpriv;
						else{
							loopv(clients) if(clients[i]->type != ST_EMPTY && clients[i]->priv == PRIV_MASTER){
								sendf(sender, 1, "ri3", N_REQPRIV, i, PRIV_MASTER | 0x40);
								priv = PRIV_NONE;
								break;
							}
						}
					}
					if(priv) setpriv(sender, priv);
				}
				else setpriv(sender, PRIV_NONE);
				break;
			}

			case N_AUTHREQ:
				reqauth(sender, getint(p));
				break;

			case N_AUTHCHAL:
			{
				int hash[5];
				loopi(5) hash[i] = getint(p);
				bool answered = answerchallenge(sender, hash);
				if(cl->connectauth && answered) cl->connected = true;
				else checkauthdisc(*cl);
				break;
			}

			case N_CALLVOTE:
			{
				voteinfo *vi = new voteinfo;
				vi->type = getint(p);
				switch(vi->type)
				{
					case SA_MAP:
					{
						getstring(text, p);
						filtertext(text, text);
						int mode = getint(p);
						if(mode==GMODE_DEMO) vi->action = new demoplayaction(text);
						else vi->action = new mapaction(newstring(text), mode, sender);
						break;
					}
					case SA_KICK:
					{
						getstring(text, p);
						filtertext(text, text, 1, 16);
						int cn = getint(p);
						vi->action = new kickaction(cn, text);
						break;
					}
					case SA_REVOKE:
						vi->action = new revokeaction(getint(p));
						break;
					case SA_SUBDUE:
						vi->action = new subdueaction(getint(p));
						break;
					case SA_BAN:
					{
						int m = getint(p), c = getint(p);
						m = clamp(m, 1, 60);
						if(cl->priv < PRIV_ADMIN && m >= 10) m = 10;
						vi->action = new banaction(c, m);
						break;
					}
					case SA_REMBANS:
						vi->action = new removebansaction();
						break;
					case SA_MASTERMODE:
						vi->action = new mastermodeaction(getint(p));
						break;
					case SA_AUTOTEAM:
						vi->action = new autoteamaction(getint(p) > 0);
						break;
					case SA_SHUFFLETEAMS:
						vi->action = new shuffleteamaction();
						break;
					case SA_FORCETEAM:
						vi->action = new forceteamaction(getint(p), sender);
						break;
					case SA_SPECT:
						vi->action = new spectaction(getint(p), sender);
						break;
					case SA_GIVEADMIN:
					{
						int c = getint(p), r = getint(p);
						vi->action = new giveadminaction(c, r, sender);
						break;
					}
					case SA_RECORDDEMO:
						vi->action = new recorddemoaction(getint(p)!=0);
						break;
					case SA_STOPDEMO:
						vi->action = new stopdemoaction();
						break;
					case SA_CLEARDEMOS:
						vi->action = new cleardemosaction(getint(p));
						break;
					case SA_BOTBALANCE:
					{
						int b = getint(p);
						b = clamp(b, -1, MAXBOTS);
						vi->action = new botbalanceaction(b);
						break;
					}
					case SA_SERVERDESC:
						getstring(text, p);
						filtertext(text, text);
						vi->action = new serverdescaction(newstring(text), sender);
						break;
					default:
						vi->type = SA_KICK;
						vi->action = new kickaction(-1, "<invalid type placeholder>");
						break;
				}
				vi->owner = sender;
				vi->callmillis = servmillis;
				if(!scallvote(vi)) delete vi;
				break;
			}

			case N_VOTE:
			{
				int vote = getint(p);
				if(!curvote || !curvote->action || vote < VOTE_YES || vote > VOTE_NO) break;
				if(cl->vote != VOTE_NEUTRAL){
					if(cl->vote == vote){
						if(cl->priv >= curvote->action->role && cl->priv >= curvote->action->vetorole) curvote->evaluate(true, vote);
						else sendf(sender, 1, "ri2", N_CALLVOTEERR, VOTEE_VETOPERM);
						break;
					}
					else logline(ACLOG_INFO,"[%s] %s changed vote to %s", clients[sender]->hostname, clients[sender]->name, vote == VOTE_NO ? "no" : "yes");
				}
				else logline(ACLOG_INFO,"[%s] %s voted %s", clients[sender]->hostname, clients[sender]->name, vote == VOTE_NO ? "no" : "yes");
				cl->vote = vote;
				sendf(-1, 1, "ri3x", N_VOTE, sender, vote, sender);
				curvote->evaluate();
				break;
			}

			case N_WHOIS:
				sendwhois(sender, getint(p));
				break;

			case N_LISTDEMOS:
				listdemos(sender);
				break;

			case N_DEMO:
				senddemo(sender, getint(p));
				break;

			case N_PHYS:
			{
				const int cn = getint(p), typ = getint(p);
				const bool isfall = typ == PHYS_HARDFALL || typ == PHYS_FALL;
				const float fall = isfall ? getint(p)/DMF : 0;
				if(!hasclient(cl, cn) || typ < 0 || typ >= PHYS_NUM) break;
				client *cp = clients[cn];
				if(isfall){
					if(cp->state.state != CS_ALIVE) break;
					// deal falling damage?
					int damage = (fall - 10) / (cp->state.perk == PERK_FALL ? 10 : 2);
					if(damage < 1) break; // don't heal the player
					else if(damage > 200) damage = 200;
					serverdamage(cp, cp, damage, WEAP_MAX+2, FRAG_NONE, cp->state.o);
				}
				else if(typ == PHYS_AKIMBOOUT){
					if(!cp->state.akimbomillis) break;
					cp->state.akimbomillis = 0;
				}
				else if(typ == PHYS_NOAMMO && cp->state.mag) break;
				QUEUE_INT(N_PHYS);
				QUEUE_INT(cn);
				QUEUE_INT(typ);
				break;
			}

			case N_EXT: // note that there is no guarantee that custom extensions will work in future AC versions
			{
				getstring(text, p, 64); // extension specifier, preferred to be in the form of OWNER::EXTENSION
				int n = getint(p);  // length of data after the specifier
				if(n > 50 || n < 1) return;

				// sample
				if(!strcmp(text, "official::writelog"))
				{
					// extension:   writelog
					// description: writes a custom string to the server log
					// access:	  requires admin privileges
					// usage:	/wl = [serverextension "official::writelog" $arg1]
					//			/wl "message to write to the log"
					getstring(text, p, n);
					if(valid_client(sender) && clients[sender]->priv >= PRIV_ADMIN) logline(ACLOG_INFO, "%s", text);
				}

				// add other extensions here

				else{
					logline(ACLOG_INFO, "[%s] sent unknown extension %s, length %d", cl->hostname, text, n);
					while(n-- > 0) getint(p); // ignore unknown extensions
				}

				break;
			}

			default: // unknown
			case -1: // tag type
				disconnect_client(sender, DISC_TAGT);
				return;

			case -2: // overflow
				disconnect_client(sender, DISC_OVERFLOW);
				return;
		}
	}

	if(p.overread() && sender>=0) disconnect_client(sender, DISC_EOP);

	#ifdef _DEBUG
	protocoldebug(false);
	#endif
}

void localclienttoserver(int chan, ENetPacket *packet){
	process(packet, 0, chan);
}

client &addclient(){
	client *c = NULL;
	loopv(clients){
		if(clients[i]->type==ST_EMPTY) { c = clients[i]; break; }
		else if(clients[i]->type==ST_AI) { deleteai(*clients[i]); c = clients[i]; break; }
	}
	if(!c)
	{
		c = new client;
		c->clientnum = clients.length();
		clients.add(c);
	}
	c->reset();
	return *c;
}

void checkintermission(){
	if(minremain>0)
	{
		minremain = gamemillis>=gamelimit || forceintermission ? 0 : (gamelimit - gamemillis + 60000 - 1)/60000;
		if(isdedicated) loopv(clients) if(valid_client(i, true)) sendf(i, 1, "ri3", N_ACCURACY, clients[i]->state.damage, clients[i]->state.shotdamage);
		if(minremain < 2){
			short nextmaptype = 0, nextmaptime = 0, nextmapmode = GMODE_TEAMDEATHMATCH;
			string nextmapnm = "unknown";
			if(*nextmapname){
				nextmaptype = 1;
				copystring(nextmapnm, nextmapname);
				nextmapmode = nextgamemode;
			}
			else if(configsets.length()){
				nextmaptype = 2;
				configset nextmaprot = configsets[nextcfgset(false, true)];
				copystring(nextmapnm, nextmaprot.mapname);
				nextmapmode = nextmaprot.mode;
				nextmaptime = nextmaprot.time;
			}
			else{
				nextmaptype = 3;
				copystring(nextmapnm, smapname);
				nextmapmode = smode;
			}
			if(nextmaptime < 1){
				int smode = nextmapmode;
				nextmaptime = m_team ? 15 : 10;
			}
			if(nextmaptype) sendf(-1, 1, "ri4s", N_CONFMSG, 14, nextmaptime, nextmapmode | ((nextmaptype & 3) << 6), nextmapnm);
		}
		sendf(-1, 1, "ri3", N_TIMEUP, gamemillis, gamelimit);
	}
	if(!interm && minremain<=0) interm = gamemillis+10000;
	forceintermission = false;
}

void resetserverifempty(){
	loopv(clients) if(clients[i]->type!=ST_EMPTY) return;
	resetserver("", 0, 10);
	mastermode = MM_OPEN;
	autoteam = true;
	nextmapname[0] = '\0';
}

void sendworldstate(){
	static enet_uint32 lastsend = 0;
	if(clients.empty()) return;
	enet_uint32 curtime = enet_time_get()-lastsend;
	if(curtime<40) return;
	bool flush = buildworldstate();
	lastsend += curtime - (curtime%40);
	if(flush) enet_host_flush(serverhost);
	if(demorecord) recordpackets = true; // enable after 'old' worldstate is sent
}

void rereadcfgs(void){
	readscfg(NULL);
	readpwdfile(NULL);
	readipblacklist(NULL);
	nbl.readnickblacklist(NULL);
}

void loggamestatus(const char *reason){
	int fragscore[2] = {0, 0}, flagscore[2] = {0, 0}, pnum[2] = {0, 0}, n;
	string text;
	formatstring(text)("%d minutes remaining", minremain);
	logline(ACLOG_INFO, "");
	logline(ACLOG_INFO, "Game status: %s on %s, %s, %s%c %s",
					  modestr(gamemode), smapname, reason ? reason : text, mmfullname(mastermode), custom_servdesc ? ',' : '\0', servdesc_current);
	logline(ACLOG_INFO, "cn name		    %s%sfrag death ping role	host", m_team ? "team " : "", m_flags ? "flag " : "");
	loopv(clients)
	{
		client &c = *clients[i];
		if(c.type == ST_EMPTY || !c.name[0]) continue;
		formatstring(text)("%2d %-16s ", c.clientnum, c.name);		 // cn name
		if(m_team) s_strcatf(text, "%-4s ", team_string(c.team)); // team
		if(m_flags) s_strcatf(text, "%4d ", c.state.flagscore);	 // flag
		s_strcatf(text, "%4d %5d", c.state.frags, c.state.deaths);  // frag death
		logline(ACLOG_INFO, "%s%5d %s %s", text, c.ping,
			c.priv == PRIV_NONE ? "normal " :
			c.priv == PRIV_MASTER ? "master " :
			c.priv == PRIV_ADMIN ? "admin  " :
			c.priv == PRIV_MAX ? "highest" :
			"unknown", c.hostname);
		n = c.team;
		flagscore[n] += c.state.flagscore;
		fragscore[n] += c.state.frags;
		pnum[n] += 1;
	}
	if(m_team)
	{
		loopi(2) logline(ACLOG_INFO, "Team %4s:%3d players,%5d frags%c%5d flags", team_string(i), pnum[i], fragscore[i], m_flags ? ',' : '\0', flagscore[i]);
	}
	logline(ACLOG_INFO, "");
}

int lastmillis = 0, totalmillis = 0;

void serverslice(uint timeout)   // main server update, called from cube main loop in sp, or dedicated server loop
{
#ifdef STANDALONE
	int nextmillis = (int)enet_time_get();
	if(svcctrl) svcctrl->keepalive();
#else
	int nextmillis = isdedicated ? (int)enet_time_get() : lastmillis;
#endif
	int diff = nextmillis - servmillis;
	gamemillis += diff;
	servmillis = nextmillis;

	if(m_demo) readdemo();

	if(minremain>0)
	{
		processevents();
		checkitemspawns(diff);
		bool ktfflagingame = false;
		if(m_flags) loopi(2)
		{
			sflaginfo &f = sflaginfos[i];
			if(f.state == CTFF_DROPPED && gamemillis-f.lastupdate > (m_ctf ? 30000 : 10000)) flagaction(i, FA_RESET, -1);
			if(m_htf && f.state == CTFF_INBASE && gamemillis-f.lastupdate > (smapstats.hasflags ? 10000 : 1000))
				htf_forceflag(i);
			if(m_ktf && f.state == CTFF_STOLEN && gamemillis-f.lastupdate > 15000)
				flagaction(i, FA_SCORE, -1);
			if(f.state == CTFF_INBASE || f.state == CTFF_STOLEN) ktfflagingame = true;
		}
		if(m_ktf && !ktfflagingame) flagaction(rnd(2), FA_RESET, -1); // ktf flag watchdog
		if(m_duel) arenacheck();
		if(scl.afktimelimit && mastermode == MM_OPEN && next_afk_check < servmillis && gamemillis > 20000 ) check_afk();
	}

	if(curvote)
	{
		if(!curvote->isalive()) curvote->evaluate(true);
		if(curvote->result!=VOTE_NEUTRAL) DELETEP(curvote);
	}

	int nonlocalclients = numnonlocalclients();

	if(forceintermission || ((m_fight(smode) || numnonlocalclients()) && gamemillis-diff>0 && gamemillis/60000!=(gamemillis-diff)/60000))
		checkintermission();
	if(interm && gamemillis>interm)
	{
		loggamestatus("game finished");
		if(demorecord) enddemorecord();
		interm = 0;

		//start next game
		if(nextmapname[0]) resetmap(nextmapname, nextgamemode);
		else if(configsets.length()) nextcfgset();
		else if(!isdedicated){
			loopv(clients) if(clients[i]->type!=ST_EMPTY){
				sendf(i, 1, "ri2", N_NEXTMAP, 0);	// ask a client for the next map
				mapreload = true;
				break;
			}
		}
		else resetmap(smapname, smode);
	}

	resetserverifempty();

	if(!isdedicated) return;	 // below is network only

	serverms(smode, numclients(), gamelimit-gamemillis, smapname, servmillis, serverhost->address, PROTOCOL_VERSION);

	if(autoteam && m_team && !m_duel && !interm && servmillis - lastfillup > 5000 && refillteams()) lastfillup = servmillis;

	loopv(clients) if(valid_client(i) && (!clients[i]->connected || clients[i]->connectauth) && clients[i]->connectmillis + 10000 <= servmillis) disconnect_client(i, DISC_TIMEOUT);

	if(servmillis-laststatus>60*1000)   // display bandwidth stats, useful for server ops
	{
		laststatus = servmillis;
		rereadcfgs();
		if(nonlocalclients || bsend || brec)
		{
			if(nonlocalclients) loggamestatus(NULL);
			logline(ACLOG_INFO, "Status at %s: %d remote clients, %.1f send, %.1f rec (KB/sec)", timestring(true, "%d-%m-%Y %H:%M:%S"), nonlocalclients, bsend/60.0f/1024, brec/60.0f/1024);
		}
		bsend = brec = 0;
	}

	ENetEvent event;
	bool serviced = false;
	while(!serviced)
	{
		if(enet_host_check_events(serverhost, &event) <= 0)
		{
			if(enet_host_service(serverhost, &event, timeout) <= 0) break;
			serviced = true;
		}
		switch(event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
			{
				client &c = addclient();
				c.type = ST_TCPIP;
				c.peer = event.peer;
				c.peer->data = (void *)(size_t)c.clientnum;
				c.connectmillis = servmillis;
				c.salt = rand()*((servmillis%1000)+1);
				char hn[1024];
				copystring(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");
				logline(ACLOG_INFO,"[%s] client #%d connected", c.hostname, c.clientnum);
				sendservinfo(c);
				checkai(); // reinit AI
				break;
			}

			case ENET_EVENT_TYPE_RECEIVE:
			{
				brec += (int)event.packet->dataLength;
				int cn = (int)(size_t)event.peer->data;
				if(valid_client(cn)) process(event.packet, cn, event.channelID);
				if(event.packet->referenceCount==0) enet_packet_destroy(event.packet);
				break;
			}

			case ENET_EVENT_TYPE_DISCONNECT:
			{
				int cn = (int)(size_t)event.peer->data;
				if(!valid_client(cn)) break;
				disconnect_client(cn);
				break;
			}

			default:
				break;
		}
	}
	sendworldstate();
}

void cleanupserver(){
	if(serverhost) enet_host_destroy(serverhost);
	if(svcctrl)
	{
		svcctrl->stop();
		DELETEP(svcctrl);
	}
	exitlogging();
}

int getpongflags(enet_uint32 ip){
	int flags = mastermode << PONGFLAG_MASTERMODE;
	flags |= scl.serverpassword[0] ? 1 << PONGFLAG_PASSWORD : 0;
	loopv(bans) if(bans[i].host == ip) { flags |= 1 << PONGFLAG_BANNED; break; }
	flags |= checkipblacklist(ip) ? 1 << PONGFLAG_BLACKLIST : 0;
	flags |= checkmasterbans(ip) ? 1 << PONGFLAG_MBLACKLIST : 0;
	return flags;
}

void extping_namelist(ucharbuf &p){
	loopv(clients)
	{
		if(clients[i]->type == ST_TCPIP && clients[i]->connected) sendstring(clients[i]->name, p);
	}
	sendstring("", p);
}

#define MAXINFOLINELEN 100  // including color codes

const char *readserverinfo(const char *lang){
	defformatstring(fname)("%s_%s.txt", scl.infopath, lang);
	path(fname);
	int len, n;
	char *c, *s, *t, *buf = loadfile(fname, &len);
	if(!buf) return NULL;
	char *nbuf = new char[len + 2];
	for(t = nbuf, s = strtok(buf, "\n\r"); s; s = strtok(NULL, "\n\r"))
	{
		c = strstr(s, "//");
		if(c) *c = '\0'; // strip comments
		for(n = strlen(s) - 1; n >= 0 && s[n] == ' '; n--) s[n] = '\0'; // strip trailing blanks
		filterrichtext(t, s + strspn(s, " "), MAXINFOLINELEN); // skip leading blanks
		n = strlen(t);
		if(n) t += n + 1;
	}
	*t = '\0';
	delete[] buf;
	if(!*nbuf) DELETEA(nbuf);
	return nbuf;
}

struct serverinfotext { char lang[3]; const char *info; };
vector<serverinfotext> serverinfotexts;

const char *getserverinfo(const char *lang){
	if(!islower(lang[0]) || !islower(lang[1])) return NULL;
	serverinfotext s;
	loopi(3) s.lang[i] = lang[i];
	loopv(serverinfotexts)
	{
		if(!strcmp(s.lang, serverinfotexts[i].lang) && serverinfotexts[i].info) return serverinfotexts[i].info;
	}
	s.info = readserverinfo(lang);
	serverinfotexts.add(s);
	return s.info;
}

void extping_serverinfo(ucharbuf &pi, ucharbuf &po){
	char lang[3];
	lang[0] = tolower(getint(pi)); lang[1] = tolower(getint(pi)); lang[2] = '\0';
	const char *reslang = lang, *buf = getserverinfo(lang); // try client language
	if(!buf) buf = getserverinfo(reslang = "en");	 // try english
	sendstring(buf ? reslang : "", po);
	if(buf)
	{
		for(const char *c = buf; *c && po.remaining() > MAXINFOLINELEN + 10; c += strlen(c) + 1) sendstring(c, po);
		sendstring("", po);
	}
}

void extping_maprot(ucharbuf &po){
	putint(po, CONFIG_MAXPAR);
	string text;
	bool abort = false;
	loopv(configsets)
	{
		if(po.remaining() < 100) abort = true;
		configset &c = configsets[i];
		filtertext(text, c.mapname, 0);
		text[30] = '\0';
		sendstring(abort ? "-- list truncated --" : text, po);
		loopi(CONFIG_MAXPAR) putint(po, c.par[i]);
		if(abort) break;
	}
	sendstring("", po);
}

void extinfo_cnbuf(ucharbuf &p, int cn){
	if(cn == -1) // add all available player ids
	{
		loopv(clients) if(clients[i]->type != ST_EMPTY)
			putint(p,clients[i]->clientnum);
	}
	else if(valid_client(cn)) // add single player only
	{
		putint(p,clients[cn]->clientnum);
	}
}

void extinfo_statsbuf(ucharbuf &p, int pid, int bpos, ENetSocket &pongsock, ENetAddress &addr, ENetBuffer &buf, int len){
	loopv(clients)
	{
		if(clients[i]->type != ST_TCPIP) continue;
		if(pid>-1 && clients[i]->clientnum!=pid) continue;

		putint(p,EXT_PLAYERSTATS_RESP_STATS);  // send player stats following
		putint(p,clients[i]->clientnum);  //add player id
		putint(p,clients[i]->ping);			 //Ping
		sendstring(clients[i]->name,p);		 //Name
		sendstring(team_string(clients[i]->team),p);//Team
		putint(p,clients[i]->state.frags);	  //Frags
		putint(p,clients[i]->state.assists);	  //Assists
		putint(p,clients[i]->state.flagscore);  //Flagscore
		putint(p,clients[i]->state.deaths);	 //Death
		putint(p,clients[i]->state.damage*100/max(clients[i]->state.shotdamage,1)); //Accuracy
		putint(p,clients[i]->state.health);	 //Health
		putint(p,clients[i]->state.armor);	 //armor
		putint(p,clients[i]->state.gunselect);  //Gun selected
		putint(p,clients[i]->priv ? 1 : 0);		 //Role
		putint(p,clients[i]->state.state);	  //State (Alive,Dead,Spawning,Lagged,Editing)
		putint(p,clients[i]->peer->address.host & 0xFF); // only 1 byte of the IP address (privacy protected)

		buf.dataLength = len + p.length();
		enet_socket_send(pongsock, &addr, &buf, 1);

		if(pid>-1) break;
		p.len=bpos;
	}
}

void extinfo_teamscorebuf(ucharbuf &p){
	putint(p, m_team ? EXT_ERROR_NONE : EXT_ERROR);
	putint(p, gamemode);
	putint(p, minremain);
	if(!m_team) return;

	ivector teams;
	bool addteam;
	loopv(clients) if(clients[i]->type!=ST_EMPTY)
	{
		addteam = true;
		loopvj(teams)
		{
			if(clients[i]->team == teams[j])
			{
				addteam = false;
				break;
			}
		}
		if(addteam) teams.add(clients[i]->team);
	}

	loopv(teams)
	{
		sendstring(team_string(teams[i]),p); //team
		int fragscore = 0;
		int flagscore = 0;
		loopvj(clients) if(clients[j]->type!=ST_EMPTY)
		{
			if(clients[j]->team != teams[i]) continue;
			fragscore += clients[j]->state.frags;
			flagscore += clients[j]->state.flagscore;
		}
		putint(p,fragscore); //add fragscore per team
		if(m_flags) //when capture mode
		{
			putint(p,flagscore); //add flagscore per team
		}
		else //all other team modes
		{
			putint(p,-1); //flagscore not available
		}
		putint(p,-1);
	}
}


#ifndef STANDALONE
void localdisconnect(){
	loopv(clients) if(clients[i]->type==ST_LOCAL) clients[i]->zap();
}

void localconnect(){
	client &c = addclient();
	c.type = ST_LOCAL;
	c.priv = PRIV_MAX;
	copystring(c.hostname, "local");
	sendservinfo(c);
}
#endif

void initserver(bool dedicated){
	srand(time(NULL));

	string identity;
	if(scl.logident[0]) filtertext(identity, scl.logident, 0);
	else formatstring(identity)("%s#%d", scl.ip[0] ? scl.ip : "local", scl.serverport);
	int conthres = scl.verbose > 1 ? ACLOG_DEBUG : (scl.verbose ? ACLOG_VERBOSE : ACLOG_INFO);
	if(dedicated && !initlogging(identity, scl.syslogfacility, conthres, scl.filethres, scl.syslogthres, scl.logtimestamp))
		printf("WARNING: logging not started!\n");
	logline(ACLOG_INFO, "logging local AssaultCube server (version %d, protocol %d/%d) now..", AC_VERSION, PROTOCOL_VERSION, EXT_VERSION);

	copystring(servdesc_current, scl.servdesc_full);
	servermsinit(scl.master ? scl.master : AC_MASTER_URI, scl.ip, scl.serverport + CUBE_SERVINFO_OFFSET, dedicated);

	if((isdedicated = dedicated))
	{
		ENetAddress address = { ENET_HOST_ANY, scl.serverport };
		if(scl.ip[0] && enet_address_set_host(&address, scl.ip)<0) logline(ACLOG_WARNING, "server ip not resolved!");
		serverhost = enet_host_create(&address, scl.maxclients+4, 0, scl.uprate);
		if(!serverhost) fatal("could not create server host");
		loopi(scl.maxclients) serverhost->peers[i].data = (void *)-1;

		readscfg(scl.maprot);
		readpwdfile(scl.pwdfile);
		readipblacklist(scl.blfile);
		nbl.readnickblacklist(scl.nbfile);
		getserverinfo("en"); // cache 'en' serverinfo
		if(scl.demoeverymatch) logline(ACLOG_VERBOSE, "recording demo of every game (holding up to %d in memory)", scl.maxdemos);
		if(scl.demopath[0]) logline(ACLOG_VERBOSE,"all recorded demos will be written to: \"%s\"", scl.demopath);
		if(scl.voteperm[0]) logline(ACLOG_VERBOSE,"vote permission string: \"%s\"", scl.voteperm);
		logline(ACLOG_VERBOSE,"server description: \"%s\"", scl.servdesc_full);
		if(scl.servdesc_pre[0] || scl.servdesc_suf[0]) logline(ACLOG_VERBOSE,"custom server description: \"%sCUSTOMPART%s\"", scl.servdesc_pre, scl.servdesc_suf);
		if(scl.master) logline(ACLOG_VERBOSE,"master server URL: \"%s\"", scl.master);
		if(scl.serverpassword[0]) logline(ACLOG_VERBOSE,"server password: \"%s\"", hiddenpwd(scl.serverpassword));
	}

	resetserverifempty();

	if(isdedicated)	   // do not return, this becomes main loop
	{
		#ifdef WIN32
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		#endif
		logline(ACLOG_INFO, "dedicated server started, waiting for clients...");
		logline(ACLOG_INFO, "Ctrl-C to exit");
		atexit(enet_deinitialize);
		atexit(cleanupserver);
		enet_time_set(0);
		for(;;) serverslice(5);
	}
}

#ifdef STANDALONE

void localservertoclient(int chan, uchar *buf, int len) {}
void fatal(const char *s, ...){
	defvformatstring(msg,s,s);
	defformatstring(out)("AssaultCube fatal error: %s", msg);
	if (logline(ACLOG_ERROR, "%s", out));
	else puts(out);
	cleanupserver();
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv){
	#ifdef WIN32
	//atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
	#ifndef _DEBUG
	#ifndef __GNUC__
	__try {
	#endif
	#endif
	#endif

	const char *service = NULL;

	for(int i = 1; i<argc; i++)
	{
		if(!scl.checkarg(argv[i]))
		{
			char *a = &argv[i][2];
			if(!scl.checkarg(argv[i]) && argv[i][0]=='-') switch(argv[i][1])
			{
				case '-':
					if(!strncmp(argv[i], "--wizard", 8))
					{
						return wizardmain(argc-1, argv+1);
					}
					break;
				case 'S': service = a; break;
				default: printf("WARNING: unknown commandline option\n");
			}
			else printf("WARNING: unknown commandline argument\n");
		}
	}

	if(service && !svcctrl)
	{
		#ifdef WIN32
		svcctrl = new winservice(service);
		#endif
		if(svcctrl)
		{
			svcctrl->argc = argc; svcctrl->argv = argv;
			svcctrl->start();
		}
	}

	if(enet_initialize()<0) fatal("Unable to initialise network module");
	initserver(true);
	return EXIT_SUCCESS;

	#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
	} __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
	#endif
}
#endif

