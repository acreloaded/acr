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
#include "server.h"
#include "servercontroller.h"

#define DEBUGCOND (true)

servercontroller *svcctrl = NULL;
struct servercommandline scl;

#define SERVERMAP_PATH		  "packages/maps/servermaps/"
#define SERVERMAP_PATH_BUILTIN  "packages/maps/official/"
#ifdef STANDALONE
#define SERVERMAP_PATH_INCOMING "packages/maps/servermaps/incoming/"
#else
#define SERVERMAP_PATH_INCOMING "home/packages/maps/"
#endif

string smapname, nextmapname;
int nextgamemode, nextgamemuts;
mapstats smapstats;

vector<client *> clients;
static vector<savedscore> scores;
static vector<savedlimit> savedlimits;
teamscore steamscores[TEAM_NUM-1] = { teamscore(TEAM_RED), teamscore(TEAM_BLUE) };
inline teamscore &getsteamscore(client *c){
	return steamscores[(m_team(gamemode, mutators) && c->team == TEAM_BLUE) ? TEAM_BLUE : TEAM_RED];
}
uint nextauthreq = 1;

vector<ban> bans;
vector<entity> sents;
vector<demofile> demos;

vector<configset> configsets;
int curcfgset = -1;

// throwing knives
vector<sknife> sknives;
int knifeseq = 0;
vector<sconfirm> sconfirms;
int confirmseq = 0;
void purgesknives(){
	loopv(sknives) sendf(-1, 1, "ri2", N_KNIFEREMOVE, sknives[i].id);
	sknives.setsize(0);
}
void purgesconfirms(){
	loopv(sconfirms) sendf(-1, 1, "ri2", N_CONFIRMREMOVE, sconfirms[i].id);
}

ssqr *maplayout = NULL;
int maplayout_factor;

bool valid_client(int cn, bool player){
	return clients.inrange(cn) && clients[cn]->type != ST_EMPTY && (!player || clients[cn]->type != ST_AI);
}

const char *gethostname(int i){ return valid_client(i) ? valid_client(clients[i]->state.ownernum) ? clients[clients[i]->state.ownernum]->hostname : clients[i]->hostname : "unknown"; }
bool hasclient(client *ci, int cn){
	if(!valid_client(cn)) return false;
	client *cp = clients[cn];
	return ci->clientnum == cn || cp->state.ownernum == ci->clientnum;
}

int nextstatus = 0, servmillis = 0, lastfillup = 0;

void recordpacket(int chan, void *data, int len);

void sendpacket(int n, int chan, ENetPacket *packet, int exclude = -1){
	const int realexclude = valid_client(exclude) ? clients[exclude]->type == ST_AI ? clients[exclude]->state.ownernum : exclude : -1;
	if(!valid_client(n)){
		if(n<0)
		{
			recordpacket(chan, packet->data, (int)packet->dataLength);
			loopv(clients)
				if(i!=realexclude && clients[i]->type != ST_EMPTY && clients[i]->type != ST_AI)
					sendpacket(i, chan, packet);
		}
		return;
	}

	switch(clients[n]->type)
	{
		case ST_AI:
		{
			// reroute packets
			const int owner = clients[n]->state.ownernum;
			if(valid_client(owner, true) && owner != n && owner != realexclude)
				sendpacket(owner, chan, packet, exclude);
			break;
		}

		case ST_TCPIP:
			enet_peer_send(clients[n]->peer, chan, packet);
			break;

		case ST_LOCAL:
			localservertoclient(chan, packet->data, (int)packet->dataLength);
			break;
	}
}

static bool reliablemessages = false;

bool buildworldstate(){ // WAY easier worldstates
	bool flush = false;
	loopvj(clients){ // broadcast the needed packets AND record at the same time!
		client &c = *clients[j];
		if(c.type == ST_EMPTY || !c.connected) continue;
		c.overflow = 0;

		if(c.position.length()){
			flush = true;
			// positions
			ENetPacket *positionpacket = enet_packet_create(NULL, MAXTRANS, 0);
			ucharbuf pos(positionpacket->data, positionpacket->dataLength);
			pos.put(c.position.getbuf(), c.position.length());

			enet_packet_resize(positionpacket, pos.length());
			// possibly inspect every packet recipient with <cheap occlusion checks here to prevent wall hacks>
			loopv(clients) if(clients[i]->type != ST_EMPTY && clients[i]->type != ST_AI && clients[i]->connected){
				if(j == i || (c.type == ST_AI && c.state.ownernum == i)) continue;
				sendpacket(i, 0, positionpacket);
			}
			recordpacket(0, pos.buf, pos.length()); // record positions
			if(!positionpacket->referenceCount) enet_packet_destroy(positionpacket);

			c.position.setsize(0);
		}

		if(c.messages.length()){
			flush = true;
			// messages
			ENetPacket *messagepacket = enet_packet_create(NULL, MAXTRANS, reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0);
			ucharbuf p(messagepacket->data, messagepacket->dataLength);
			putint(p, N_CLIENT);
			putint(p, j);
			//putuint(p, c.messages.length());
			p.put(c.messages.getbuf(), c.messages.length());

			enet_packet_resize(messagepacket, p.length());
			sendpacket(-1, 1, messagepacket, j); // recorded by broadcast
			if(!messagepacket->referenceCount) enet_packet_destroy(messagepacket);

			c.messages.setsize(0);
		}
	}
	reliablemessages = false;
	return flush;
}

int countclients(int type, bool exclude = false){
	int num = 0;
	loopv(clients) if((clients[i]->type!=type)==exclude && clients[i]->state.ownernum < 0) num++;
	return num;
}

int countplayers(bool includebots = true){
	int num = 0;
	loopv(clients)
		if(clients[i]->type != ST_EMPTY && clients[i]->team != TEAM_SPECT && (includebots || clients[i]->type != ST_AI))
			++num;
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

int chooseteam(client &ci, int suggest = -1){ // suggest doesn't do anything?
	// zombies override
	if(m_zombie(gamemode) && !m_convert(gamemode, mutators)) return ci.type == ST_AI ? TEAM_RED : TEAM_BLUE;
	// by team size, then by rank
	int teamsize[2] = {0};
	int teamscore[2] = {0};
	int sum = calcscores();
	loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->connected && clients[i] != &ci && (ci.type == ST_AI || clients[i]->type!=ST_AI) && clients[i]->team < 2)
	{
		++teamsize[clients[i]->team];
		teamscore[clients[i]->team] += clients[i]->at3_score;
	}
	if(teamsize[0] == teamsize[1]) return sum > 200 ? (teamscore[0] < teamscore[1] ? 0 : 1) : rnd(2);
	return teamsize[1] < teamsize[0] ? 1 : 0;
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

bool findlimit(client &c, bool insert){
	if(c.type!=ST_TCPIP) return false;
	if(insert){
		if(savedlimits.length() >= 32) savedlimits.remove(0, 16); // halve the saved limits before it reaches 33
		savedlimit &sl = savedlimits.add();
		sl.ip = c.peer->address.host;
		sl.save(c);
		return true;
	}
	else loopv(scores){
		savedlimit &sl = savedlimits[i];
		if(sl.ip == c.peer->address.host){
			sl.restore(c);
			return true;
		}
	}
	return false;
}

static bool mapreload = false, autoteam = true, forceintermission = false, nokills = true;
#define autobalance (autoteam && !m_zombie(gamemode) && !m_convert(gamemode, mutators))

string servdesc_current;
ENetAddress servdesc_caller;
bool custom_servdesc = false;

bool isdedicated = false;
ENetHost *serverhost = NULL;

void process(ENetPacket *packet, int sender, int chan);
void welcomepacket(ucharbuf &p, int n, ENetPacket *packet);
void sendwelcome(client *cl, int chan = 1);

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
inline void sendmsgs(int msg, const char *str, int client = -1){
	sendf(client, 1, "ri2s", N_CONFMSG, msg, str);
}
inline void sendmsgi(int msg, int num, int client = -1){
	sendf(client, 1, "ri3", N_CONFMSG, msg, num);
}

void streakready(client &c, int streak){
	if(streak < 0 || streak >= STREAK_NUM) return;
	if(streak == STREAK_AIRSTRIKE) ++c.state.airstrikes;
	sendf(-1, 1, "ri3", N_STREAKREADY, c.clientnum, streak);
}

void usestreak(client &c, int streak, client *actor = NULL, const vec &o = vec(0, 0, 0)){
	if(streak < 0 || streak >= STREAK_NUM) return;
	int info = 0;
	switch(streak){
		case STREAK_AIRSTRIKE:
			explosion(c, o, WEAP_GRENADE, false); // add a delay?
			sendf(-1, 1, "ri3f3", N_STREAKUSE, c.clientnum, STREAK_AIRSTRIKE, info, o.x, o.y, o.z);
			return; // special message
		case STREAK_RADAR:
			c.state.radarearned = gamemillis + (info = 15000);
			break;
		case STREAK_NUKE:
			c.state.nukemillis = gamemillis + (info = 30000);
			break;
		case STREAK_REVENGE:
		{
			suicidebomberevent *ev = new suicidebomberevent;
			ev->id = actor ? actor->clientnum : -1;
			c.events.insert(0, ev);
			// fallthrough
		}
		case STREAK_DROPNADE:
			info = rand();
			c.state.grenades.add(info);
			break;
	}
	sendf(-1, 1, "ri4", N_STREAKUSE, c.clientnum, streak, info);
}

void spawnstate(client *c){
	clientstate &gs = c->state;
	if(c->type == ST_AI){
		// random loadout settings
		const int weap1[] = {
			WEAP_SHOTGUN,
			WEAP_SUBGUN,
			WEAP_SNIPER,
			WEAP_BOLT,
			WEAP_ASSAULT,
			WEAP_SWORD,
		}, weap2[] = {
			WEAP_PISTOL,
			WEAP_HEAL,
			WEAP_RPG,
		};
		gs.nextprimary = weap1[rnd(sizeof(weap1)/sizeof(int))];
		gs.nextsecondary = weap2[rnd(sizeof(weap2)/sizeof(int))];
		gs.nextperk1 = PERK_NONE;
		gs.nextperk2 = (gs.nextprimary == WEAP_BOLT || m_sniper(gamemode, mutators)) ? PERK2_STEADY : PERK2_NONE;
	}
	gs.spawnstate(smode, smuts);
	// FIXME Move to entity.h
	if(m_zombie(gamemode)){
		switch(c->team){
			case TEAM_RED:
				gs.gunselect = gs.primary = !rnd(3) ? WEAP_SWORD : WEAP_KNIFE;
				memset(gs.mag, 0, sizeof(gs.mag));
				memset(gs.ammo, 0, sizeof(gs.ammo));
				gs.mag[gs.primary] = magsize(gs.primary);
				gs.perk1 = gs.nextperk1 = PERK1_AGILE;
				gs.perk2 = gs.nextperk2 = PERK2_STREAK;
				if(m_onslaught(gamemode, mutators)){
					gs.health = STARTHEALTH * ZOMBIEHEALTHFACTOR;
					gs.armor += 50;
				}
				else gs.health = STARTHEALTH + rnd(STARTHEALTH * ZOMBIEHEALTHFACTOR);
				break;
			case TEAM_BLUE:
				if(!m_onslaught(gamemode, mutators)) break;
				// humans for onslaught only
				if(gs.perk2 == PERK2_HEALTH) gs.health = STARTHEALTH * ZOMBIEHEALTHFACTOR;
				else gs.health = STARTHEALTH * (rnd(ZOMBIEHEALTHFACTOR - 1) + 1) + (STARTHEALTH/2); // 150 - 450
				gs.armor += 2000;
				break;
		}
	}
	// end fixme
	++gs.lifesequence;
	gs.state = CS_DEAD;
}

void sendspawn(client *c){
	clientstate &gs = c->state;
	if(gs.lastdeath) gs.respawn();
	spawnstate(c);
	sendf(c->clientnum, 1, "ri9i2vv", N_SPAWNSTATE, c->clientnum, gs.lifesequence, // 1-3
		gs.health, gs.armor, gs.perk1, gs.perk2, // 4-7
		gs.primary, gs.secondary, gs.gunselect, m_duke(gamemode, mutators) ? c->spawnindex : -1, // 8-9, 2
		WEAP_MAX, gs.ammo, WEAP_MAX, gs.mag);
	gs.lastspawn = gamemillis;

	int dstreak = gs.deathstreak + (gs.perk2 == PERK2_STREAK ? 1 : 0);
	if(dstreak >= 8) gs.streakondeath = STREAK_REVENGE;
	else if(dstreak >= 5 && c->type != ST_AI) gs.streakondeath = STREAK_DROPNADE;
	else gs.streakondeath = -1;
	streakready(*c, gs.streakondeath);
}

// demo
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
	formatstring(d.info)("%s: %s, %s, %.2f%s", asctime(), modestr(gamemode, mutators), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
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
	putint(p, c.state.level);
	sendstring(c.name, p);
	putint(p, c.state.ownernum);
}

void putinitclient(client &c, ucharbuf &p);

void setupdemorecord(){
	if(numlocalclients() || !m_valid(smode)) return;

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

#include "points.h"

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
	//* remove one preceeding slash to disable AFK checks
	next_afk_check = INT_MAX;
	return;
	// */
	next_afk_check = servmillis + 7 * 1000;
	// OLD LOGIC if we have less than five players or a non-teammode is not full: do nothing!
	// if (numclients() < 5 || (numnonlocalclients() < scl.maxclients && !m_team(gamemode, mutators))) return;
	loopv(clients){
		client &c = *clients[i];
		if (c.type != ST_TCPIP || c.connectmillis + 60 * 1000 > servmillis || c.team == TEAM_SPECT ||
			!c.state.movemillis || c.state.movemillis + scl.afktimelimit > servmillis || clienthasflag(c.clientnum) > -1 ) continue;
		if ( ( c.state.state == CS_DEAD && !m_duke(gamemode, mutators) && c.state.lastdeath + 45 * 1000 < gamemillis) ||
			(c.state.state == CS_ALIVE /*&& c.state.upspawnp */)) {
			logline(ACLOG_INFO, "[%s] %s is afk, forcing to spectator", gethostname(i), formatname(c));
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
	if(action == FA_PICKUP && f.drop_cn == actor && f.dropmillis + 2000 > servmillis) return;
	sflaginfo &of = sflaginfos[team_opposite(flag)];
	int score = 0;
	int message = -1;

	if(m_capture(gamemode) || m_hunt(gamemode) || m_ktf2(gamemode, mutators) || m_bomber(gamemode))
	{
		switch(action)
		{
			case FA_PICKUP:
				f.state = CTFF_STOLEN;
				f.actor_cn = actor;
				f.stolentime = gamemillis; // needed for KTF2
				break;
			case FA_LOST:
			case FA_DROP:
				if(actor == -1) actor = f.actor_cn;
				f.state = CTFF_DROPPED;
				loopi(3) f.pos[i] = clients[actor]->state.o[i];
				//if(f.pos[2] < smapstats.hdr.waterlevel) f.pos[2] = smapstats.hdr.waterlevel;
				break;
			case FA_RETURN:
				f.state = CTFF_INBASE;
				break;
			case FA_SCORE:  // ctf: f = carried by actor flag,  htf: f = hunted flag (run over by actor)
				if(m_capture(gamemode)) score = 1;
				else if(m_bomber(gamemode)) score = of.state == CTFF_INBASE ? 3 : of.state == CTFF_DROPPED ? 2 : 1;
				else if(m_ktf2(gamemode, mutators)){
					if(valid_client(f.actor_cn) && clients[f.actor_cn]->state.state == CS_ALIVE)
					{
						actor = f.actor_cn;
						score = 1;
						message = FA_KTFSCORE;
						break;
					}
				}
				else if(m_hunt(gamemode)){ // htf
					score = (of.state == CTFF_STOLEN) ? of.actor_cn == actor ? 2 : 1 : 0;
					message = score ? FA_SCORE : FA_SCOREFAIL;
				}
				f.state = CTFF_INBASE;
				break;
			case FA_RESET:
				f.state = CTFF_INBASE;
				break;
		}
	}
	else if(m_keep(gamemode))  // f: active flag, of: idle flag
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
				if(f.state == CTFF_STOLEN) actor = f.actor_cn;
				f.state = CTFF_IDLE;
				of.state = CTFF_INBASE;
				sendflaginfo(team_opposite(flag));
				break;
		}
	}
	if(message < 0) message = action;
	if(valid_client(actor)){
		if(score){
			clients[actor]->state.flagscore += score;
			sendf(-1, 1, "ri3", N_FLAGCNT, actor, clients[actor]->state.flagscore);
			getsteamscore(clients[actor]).flagscore += score;
			sendteamscore(clients[actor]->team);
		}

		getsteamscore(clients[actor]).points += max(0, flagpoints(clients[actor], message));
		sendteamscore(clients[actor]->team);

		client &c = *clients[actor];
		switch(message)
		{
			case FA_PICKUP:
				logline(ACLOG_INFO,"[%s] %s stole the flag", gethostname(actor), formatname(c));
				break;
			case FA_DROP:
				f.drop_cn = actor;
				f.dropmillis = servmillis;
				logline(ACLOG_INFO,"[%s] %s dropped the flag", gethostname(actor), formatname(c));
				break;
			case FA_LOST:
				logline(ACLOG_INFO,"[%s] %s lost the flag", gethostname(actor), formatname(c));
				break;
			case FA_RETURN:
				logline(ACLOG_INFO,"[%s] %s returned the flag", gethostname(actor), formatname(c));
				break;
			case FA_SCORE:
				logline(ACLOG_INFO, "[%s] %s scored with the flag for %s, new score %d", gethostname(actor), formatname(c), team_string(c.team), c.state.flagscore);
				break;
			case FA_KTFSCORE:
				logline(ACLOG_INFO, "[%s] %s scored, carrying for %d seconds, new score %d", gethostname(actor), formatname(c), (gamemillis - f.stolentime) / 1000, c.state.flagscore);
				break;
			case FA_SCOREFAIL:
				logline(ACLOG_INFO, "[%s] %s failed to score", gethostname(actor), formatname(c));
				break;
		}
	}
	else if(message == FA_RESET) logline(ACLOG_INFO, "the server reset the flag for team %s", team_string(flag));
	f.lastupdate = gamemillis;
	sendflaginfo(flag);
	flagmessage(flag, message, valid_client(actor) ? actor : -1);
}

int clienthasflag(int cn){
	if(m_affinity(gamemode) && valid_client(cn))
	{
		loopi(2) { if(sflaginfos[i].state==CTFF_STOLEN && sflaginfos[i].actor_cn==cn) return i; }
	}
	return -1;
}

void ctfreset(){
	int idleflag = m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? rnd(2) : -1;
	loopi(2)
	{
		sflaginfos[i].actor_cn = -1;
		sflaginfos[i].state = i == idleflag ? CTFF_IDLE : CTFF_INBASE;
		sflaginfos[i].lastupdate = -1;
	}
}

void sdropflag(int cn){
	int fl = clienthasflag(cn);
	while(fl >= 0){
		flagaction(fl, FA_LOST, cn);
		sdropflag(cn);
		fl = clienthasflag(cn);
	}
}

void resetflag(int cn){
	int fl = clienthasflag(cn);
	if(fl >= 0){
		flagaction(fl, FA_RESET, -1);
		resetflag(cn);
		fl = clienthasflag(cn);
	}
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
				f.drop_cn = -1; // force pickup
				flagmessage(flag, FA_PICKUP, i);
				logline(ACLOG_INFO,"[%s] %s got forced to pickup the flag", gethostname(i), clients[i]->name);
				break;
			}
		}
	}
	f.lastupdate = gamemillis;
}

int arenaround = 0;

inline bool canspawn(client *c, bool connecting = false){
	return maplayout && c->team != TEAM_SPECT && (!m_duke(gamemode, mutators) || (connecting && numauthedclients() <= 2));
}

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
	if(m_team(gamemode, mutators) && !m_zombie(gamemode)) // zombies uses FFA spawns
	{
		distributeteam(0);
		distributeteam(1);
	}
	else
	{
		distributeteam(100);
	}
}

void checkitemspawns(int);

void arenacheck(){
	if(!m_duke(gamemode, mutators) || interm || gamemillis<arenaround || !numclients()) return;

	if(arenaround){ // start new arena round
		arenaround = 0;
		distributespawns();
		purgesknives();
		checkitemspawns(60*1000); // spawn items now!
		loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->connected && clients[i]->team != TEAM_SPECT){
			clients[i]->removeexplosives();
			if(clients[valid_client(clients[i]->state.ownernum) ? clients[i]->state.ownernum : i]->isonrightmap){
				clients[i]->state.lastdeath = 1;
				sendspawn(clients[i]);
			}
		}
		nokills = true;
		return;
	}

	client *alive = NULL;
	bool dead = false;
	int lastdeath = 0;
	bool found = false; int ha = 0, hd = 0; // found a match to keep the round / humans alive / humans dead
	loopv(clients){
		client &c = *clients[i];
		if(c.type==ST_EMPTY || !c.connected || c.team == TEAM_SPECT) continue;
		if(c.state.lastspawn < 0 && c.state.state == CS_DEAD){ // dead... (and killed)
			if(c.type != ST_AI) ++hd;

			dead = true;
			lastdeath = max(lastdeath, c.state.lastdeath);
		}
		else if(c.state.state==CS_ALIVE){ // ...or alive?
			if(c.type != ST_AI) ++ha;

			if(!alive) alive = &c;
			else if(!m_team(gamemode, mutators) || alive->team != c.team) found = true;
		}
		// or something stupid?
	}
	if((found && (ha || !hd)) || !dead || gamemillis < lastdeath + 500) return;
	// what happened?
	const int cn = !ha && found ? -2 : // bots win
		alive ? alive->clientnum : // someone/a team wins
		-1 // everyone died
	;
	// send message
	sendf(-1, 1, "ri2", N_ARENAWIN, cn);
	// award points
	loopv(clients) if(clients[i]->type != ST_EMPTY){
		int pts = 0, pr = -1;
		if(cn < 0){ // he died with this team, or bots win
			pts = ARENALOSEPT;
			pr = PR_ARENA_LOSE;
		}
		else if(clients[i]->state.state == CS_ALIVE){ // he survives
			pts = ARENAWINPT;
			pr = PR_ARENA_WIN;
		}
		else if(alive && isteam(alive, clients[i])){ // his team wins, but he's dead
			pts = ARENAWINDPT;
			pr = PR_ARENA_WIND;
		}
		addpt(clients[i], pts, pr);
	}
	// arena intermission
	arenaround = gamemillis+5000;
	// check teams
	if(m_team(gamemode, mutators) && autobalance)
		refillteams(true);
}

#define SPAMREPEATINTERVAL  20    // detect doubled lines only if interval < 20 seconds
#define SPAMMAXREPEAT        3    // 4th time is SPAM
#define SPAMCHARPERMINUTE  220    // good typist
#define SPAMCHARINTERVAL    30    // allow 20 seconds typing at maxspeed

#define SPAMTHROTTLE       800    // disallow messages before this delay from the last message (milliseconds)

bool spamdetect(client *cl, char *text) // checks doubled lines and average typing speed
{
	bool spam = false;
	int pause = servmillis - cl->lastsay;
	if(pause < 0 || pause > 90*1000) pause = 90*1000;
	else if(pause < SPAMTHROTTLE) return true;
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
	defformatstring(logmsg)("<%s> ", formatname(cl));
	if(!m_team(gamemode, mutators) && cl.team != TEAM_SPECT) flags &= ~SAY_TEAM;
	if(flags & SAY_ACTION) formatstring(logmsg)("* %s ", formatname(cl));
	string logappend;
	if(flags & SAY_TEAM){
		formatstring(logappend)("(%s) ", team_string(cl.team));
		concatstring(logmsg, logappend);
	}
	if(voice){
		formatstring(logappend)("[%d] ", voice + S_MAINEND);
		concatstring(logmsg, logappend);
	}
	if(cl.type != ST_TCPIP || cl.priv >= PRIV_ADMIN);
	else if(spamdetect(&cl, text)){
		logline(ACLOG_VERBOSE, "%s, SPAM detected", logmsg);
		sendf(cl.clientnum, 1, "ri4s", N_TEXT, cl.clientnum, 0, SAY_DENY, text);
		return;
	}
	else if(cl.muted){
		logline(ACLOG_VERBOSE, "%s, MUTED", logmsg);
		sendf(cl.clientnum, 1, "ri4s", N_TEXT, cl.clientnum, 0, SAY_MUTE, text);
		return;
	}
	logline(ACLOG_INFO, "[%s] %s%s", gethostname(cl.clientnum), logmsg, text);
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_TEXT);
	putint(p, cl.clientnum);
	putint(p, voice);
	putint(p, flags);
	sendstring(text, p);
	enet_packet_resize(packet, p.length());
	loopv(clients) if(clients[i]->type != ST_AI && clients[i]->type != ST_EMPTY && (!(flags&SAY_TEAM) || clients[i]->team == cl.team || clients[i]->priv >= PRIV_ADMIN)) sendpacket(i, 1, packet);
	recordpacket(1, packet->data, (int)packet->dataLength);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

int spawntime(int type){
	int np = countplayers();
	np = np<3 ? 4 : (np>4 ? 2 : 3);		 // some spawn times are dependent on number of players
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

// server map geometry tools
ssqr &getsblock(int id){
	if(!maplayout || id < 2 || id >= ((1 << (maplayout_factor * 2)) - 2)){
		static ssqr dummy;
		dummy.type = SPACE;
		dummy.ceil = 16;
		dummy.floor = 0;
		dummy.vdelta = 0;
		return dummy;
	}
	return maplayout[id];
}

inline int getmaplayoutid(int x, int y){
	const int max = (1 << maplayout_factor) - 2;
	return clamp(x, 2, max) + (clamp(y, 2, max) << maplayout_factor);
}

inline char cornertype(int x, int y){
	if(!maplayout) return 0;
	ssqr &me = getsblock(getmaplayoutid(x, y));
	if(me.type != CORNER) return 0;
	ssqr &up = getsblock(getmaplayoutid(x, y-1));
	ssqr &left = getsblock(getmaplayoutid(x-1, y));
	ssqr &right = getsblock(getmaplayoutid(x+1, y));
	ssqr &down = getsblock(getmaplayoutid(x, y+1));
	const uchar mes = me.ceil - me.floor;
	const bool
		u = up.type == SOLID || uchar(up.ceil - up.floor) < mes,
		l = left.type == SOLID || uchar(left.ceil - left.floor) < mes,
		r = right.type == SOLID || uchar(right.ceil - right.floor) < mes,
		d = down.type == SOLID || uchar(down.ceil - down.floor) < mes;
	if((u && d) || (l && r)) return 0; // more than 2 cubes, or two adjecent ones
	if((u && !l) || (l && !u)) return 2; // topright
	return 1; // topleft
}

inline uchar maxvdelta(int id){
	if(!maplayout) return 0;
	ssqr *s = &getsblock(id);
	uchar vdelta = s++->vdelta;
	if(uchar(s->vdelta) > vdelta) vdelta = s->vdelta;
	s += (1 << maplayout_factor) - 1; // new row, left one
	if(uchar(s->vdelta) > vdelta) vdelta = s->vdelta;
	++s;
	if(uchar(s->vdelta) > vdelta) vdelta = s->vdelta;
	return vdelta;
}

float getblockfloor(int id){
	if(!maplayout || getsblock(id).type == SOLID) return 127;
	ssqr &s = getsblock(id);
	return s.floor - (s.type == FHF ? maxvdelta(id) / 4.f : 0);
}

float getblockceil(int id){
	if(!maplayout || getsblock(id).type == SOLID) return -128;
	ssqr &s = getsblock(id);
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
		ssqr s = getsblock(getmaplayoutid(x, y));
		float floor = getblockfloor(mapid), ceil = getblockceil(mapid);
		if(s.type == SOLID || v.z < floor || v.z > ceil){
			if((!dx && !dy) || s.wtex==DEFAULT_SKY || (s.type != SOLID && v.z > ceil && s.ctex==DEFAULT_SKY)) return dist;
			if(surface){
				int cornert = 0;
				if(s.type == CORNER && (cornert = cornertype(x, y))){
					float angle = atan2(v.y - o.y, v.x - o.x) / RAD;
					while(angle < 0) angle += 360;
					while(angle > 360) angle -= 360;
					// maybe there is a faster way?

					// topleft
					if(cornert == 1)
						surface->x = surface->y = (angle >= 135 && angle <= 315) ? -.7071f : .7071f;
					// topright
					else if(cornert == 2){
						surface->x = (angle >= 45 && angle <= 225) ? -.7071f : .7071f;
						surface->y = -surface->x;
					}
				}
				else{ // make one for heightfields?
					if(dx<dy) surface->x = ray.x>0 ? -1 : 1;
					else surface->y = ray.y>0 ? -1 : 1;
					ssqr n = getsblock(getmaplayoutid(x+surface->x, y+surface->y));
					if(n.type == SOLID || (v.z < floor && v.z < n.floor) || (v.z > ceil && v.z > n.ceil)){
						*surface = dx<dy ? vec(0, ray.y>0 ? -1 : 1, 0) : vec(ray.x>0 ? -1 : 1, 0, 0);
						n = getsblock(getmaplayoutid(x+surface->x, y+surface->y));
						if(n.type == SOLID || (v.z < floor && v.z < n.floor) || (v.z > ceil && v.z > n.ceil))
							*surface = vec(0, 0, ray.z>0 ? -1 : 1);
					}
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
			if(surface && (s.ctex!=DEFAULT_SKY || ray.z<0)){
				if(s.type != ((ray.z > 0) ? CHF : FHF)) // flat part
					surface->z = ray.z>0 ? -1 : 1;
				else{ // use top left surface
					// of a plane, n = (b - a) x (c - a)
					const char f = (ray.z > 0) ? 1 : -1;
					vec b(1, 0, getsblock(getmaplayoutid(x+1, y)).vdelta + s.vdelta * f), c(0, 1, getsblock(getmaplayoutid(x, y+1)).vdelta + s.vdelta * f);
					*surface = vec(0, 0, s.vdelta); // as a
					b.sub(*surface);
					c.sub(*surface);
					dz *= surface->cross(c, b).normalize().z;
				}
			/*
			(getsblock(getmaplayoutid(x, y)).vdelta ==
			getsblock(getmaplayoutid(x+1, y)).vdelta &&
			getsblock(getmaplayoutid(x, y)).vdelta ==
			getsblock(getmaplayoutid(x, y+1)).vdelta &&
			getsblock(getmaplayoutid(x, y)).vdelta ==
			getsblock(getmaplayoutid(x+1, y+1)).vdelta)
			*/
			}
			dist += dz;
			break;
		}
		float disttonext = 0.1f + min(dx, dy);
		v.add(vec(ray).mul(disttonext));
		dist += disttonext;
	}
	return dist;
}

void forcedeath(client *cl, bool gib = false){
	sdropflag(cl->clientnum);
	clientstate &cs = cl->state;
	cs.state = CS_DEAD;
	//cs.respawn();
	cs.lastdeath = gamemillis;
	cs.nukemillis = 0;
	sendf(-1, 1, "ri2", gib ? N_FORCEGIB : N_FORCEDEATH, cl->clientnum);
}

// needs major cleanup...
void serverdied(client *target, client *actor, int damage, int gun, int style, const vec &source){
	clientstate &ts = target->state;
	const bool gib = (style & FRAG_GIB) != 0;

	ts.damagelog.removeobj(target->clientnum);
	if(actor == target && ts.damagelog.length()){
		loopv(ts.damagelog)
			if(valid_client(ts.damagelog[i]) && !isteam(target, clients[ts.damagelog[i]])){
				actor = clients[ts.damagelog[i]];
				style = isheadshot(gun, style) ? FRAG_GIB : FRAG_NONE;
				gun = WEAP_MAX + 3;
				ts.damagelog.remove(i/*--*/);
				break;
			}
	}

	int targethasflag = clienthasflag(target->clientnum);
	bool suic = false;
	
	// only things on target team that changes
	if(!m_confirm(gamemode, mutators)){
		++getsteamscore(target).deaths;
		// commit, if needed
		if(actor->team != target->team)
			sendteamscore(target->team);
	}
	// apply to individual
	++target->state.deaths;
	addpt(target, DEATHPT);

	const int kills = (actor == target || isteam(target, actor)) ? -1 : gib ? 2 : 1;
	actor->state.frags += kills;
	if(target!=actor){
		if(actor->state.revengelog.find(target->clientnum) >= 0){
			style |= FRAG_REVENGE;
			actor->state.revengelog.removeobj(target->clientnum);
		}
		target->state.revengelog.add(actor->clientnum);

		// first blood (not for AI)
		if(actor->type != ST_AI && nokills){
			style |= FRAG_FIRST;
			nokills = false;
		}

		// type of scoping
		const int zoomtime = ADSTIME(actor->state.perk2 == PERK_TIME), scopeelapsed = min(gamemillis - actor->state.scopemillis, zoomtime + 500);
		if(actor->state.scoping){
			// quick/recent/full
			if(scopeelapsed >= zoomtime){
				style |= FRAG_SCOPE_FULL;
				if(scopeelapsed < zoomtime + 500)
					style |= FRAG_SCOPE_NONE; // recent, not hard
			}
		}
		else{
			// no/quick
			if(scopeelapsed >= zoomtime) style |= FRAG_SCOPE_NONE;
		}
	}
	else // suicide
		suic = true;

	// streak/assist
	actor->state.pointstreak += 5;
	++ts.deathstreak;
	actor->state.deathstreak = ts.pointstreak = ts.streakused = 0;
	ts.wounds.shrink(0);
	ts.damagelog.removeobj(ts.lastkiller = actor->clientnum);
	target->heals.shrink(0);
	loopv(ts.damagelog){
		if(valid_client(ts.damagelog[i])){
			const int factor = isteam(clients[ts.damagelog[i]], target) ? -1 : 1;
			clients[ts.damagelog[i]]->state.assists += factor;
			if(factor > 0)
				getsteamscore(actor).assists += factor; // add to assists
			clients[ts.damagelog[i]]->state.pointstreak += factor * 2;
		}
		else ts.damagelog.remove(i--);
	}
	// kills
	int virtualstreak = actor->state.pointstreak + (actor->state.perk2 == PERK2_STREAK ? 5 : 0);
	if(virtualstreak >= 7 * 5 && actor->state.streakused < 7 * 5){
		streakready(*actor, STREAK_AIRSTRIKE);
	}
	if(virtualstreak >= 9 * 5 && actor->state.streakused < 9 * 5){
		if(!m_noradar(gamemode, mutators)) usestreak(*actor, STREAK_RADAR);
	}
	if(virtualstreak >= 11 * 5 && actor->state.streakused < 11 * 5){
		if(!m_nonuke(gamemode, mutators)) usestreak(*actor, STREAK_NUKE);
		// restart streak
		// actor->state.pointstreak %= 11 * 5;
	}
	actor->state.streakused = virtualstreak;

	if(gamemillis >= actor->state.lastkill + COMBOTIME) actor->state.combo = 0;
	actor->state.lastkill = gamemillis;
	const float killdist = ts.o == source ? 0 : clamp<float>(ts.o.dist(source) / 4, -1, 1000);
	sendf(-1, 1, "ri9f4iv", N_KILL, // i9
		target->clientnum, // victim
		actor->clientnum, // actor
		actor->state.frags, // frags
		gun, // weap
		style & FRAG_VALID, // style
		damage, // finishing damage
		++actor->state.combo, // combo
		actor->state.pointstreak, // streak
		killdist, // distance (f4)
		source.x, // source
		source.y,
		source.z, // below: assists (iv)
		ts.damagelog.length(), ts.damagelog.length(), ts.damagelog.getbuf());
	int earnedpts = killpoints(target, actor, gun, style);
	if(m_confirm(gamemode, mutators)){
		// create confirm object?
		if(earnedpts > 0 || kills > 0){
			sconfirm &c = sconfirms.add();
			c.o = ts.o;
			sendf(-1, 1, "ri3f3", N_CONFIRMADD, c.id = ++confirmseq, c.team = actor->team, c.o.x, c.o.y, c.o.z);
			c.points = max(0, earnedpts);
			c.frag = max(0, kills);
			c.death = target->team;
		}
	}
	else{
		if(earnedpts > 0) getsteamscore(actor).points += earnedpts;
		if(kills > 0) getsteamscore(actor).frags += kills;
	}
	// assists/tk-deaths
	sendteamscore(actor->team); // last team score change

	if(suic && (m_hunt(gamemode) || m_keep(gamemode)) && targethasflag >= 0)
		sendf(-1, 1, "ri3", N_FLAGCNT, actor->clientnum, --actor->state.flagscore);
	target->position.setsize(0);
	ts.state = CS_DEAD;
	ts.lastdeath = gamemillis;
	const char *h = gethostname(actor->clientnum);
	const int logtype = actor->type == ST_AI && target->type == ST_AI ? ACLOG_VERBOSE : ACLOG_INFO;
	if(!suic) logline(logtype, "[%s] %s %s %s (%.2f m)", h, formatname(actor), killname(toobit(gun, style), isheadshot(gun, style)), formatname(target), killdist);
	else logline(logtype, "[%s] %s %s (%.2f m)", h, formatname(actor), suicname(obit_suicide(gun)), killdist);

	if(m_affinity(gamemode)){
		if(m_ktf2(gamemode, mutators) && // KTF2 only
			targethasflag >= 0 && //he has any flag
			sflaginfos[team_opposite(targethasflag)].state != CTFF_INBASE){ // other flag is not in base
			if(sflaginfos[0].actor_cn == sflaginfos[1].actor_cn){ // he has both
				// reset the far one
				const int farflag = ts.o.distxy(vec(sflaginfos[0].x, sflaginfos[0].y, 0)) > ts.o.distxy(vec(sflaginfos[1].x, sflaginfos[1].y, 0)) ? 0 : 1;
				flagaction(farflag, FA_RESET, -1);
				// drop the close one
				targethasflag = team_opposite(farflag);
			}
			else{ // he only has this one
				// reset this
				flagaction(targethasflag, FA_RESET, -1);
				targethasflag = -1;
			}
		}
		while(targethasflag >= 0)
		{
			flagaction(targethasflag, /*tk ? FA_RESET : */FA_LOST, -1);
			targethasflag = clienthasflag(target->clientnum);
		}
	}

	// target streaks
	if(target->state.nukemillis){ // nuke cancelled!
		target->state.nukemillis = 0;
		sendf(-1, 1, "ri4", N_STREAKUSE, target->clientnum, STREAK_NUKE, -2);
	}

	// put this here to prevent crash
	int deathstreak = ts.streakondeath;
	if((explosive_weap(gun) || isheadshot(gun, style)) && deathstreak == STREAK_REVENGE)
		deathstreak = STREAK_DROPNADE;
	usestreak(*target, deathstreak, m_zombie(gamemode) ? actor : NULL);

	// conversions
	if(!suic && m_convert(gamemode, mutators) && target->team != actor->team){
		updateclientteam(target->clientnum, actor->team, FTR_SILENT);
		if(!m_duke(gamemode, mutators)){
			bool found = false;
			loopv(clients)
				if(clients[i]->type != ST_EMPTY && clients[i]->team != TEAM_SPECT && clients[i]->team != actor->team){
					found = true;
					break;
				}
			// game ends if not arena, and all enemies are converted
			if(!found)
				forceintermission = true;
		}
	}
}

void serverdamage(client *target, client *actor, int damage, int gun, int style, const vec &source){
	if(!target || !actor || !damage) return;

	if(m_expert(gamemode, mutators))
	{
		if((gun == WEAP_GRENADE && (style & FRAG_FLAG)) || (style & FRAG_GIB) || melee_weap(gun)) damage *= 2;
		else if(gun == WEAP_GRENADE) damage /= 2;
		else damage /= 8;
	}
	else if(m_real(gamemode, mutators)){
		if(gun == WEAP_HEAL && target == actor) damage /= 2;
		else damage *= 2;
	}
	else if(m_classic(gamemode, mutators)) damage /= 2;

	clientstate &ts = target->state;
	if(ts.state != CS_ALIVE) return;

	if(target != actor)
	{
		if(isteam(actor, target))
		{ // friendly fire handler
			// no friendly return for classic and healing gun
			if(gun != WEAP_HEAL && !m_classic(gamemode, mutators)){
				actor->state.shotdamage += damage; // reduce his accuracy (more)
				// NEW way: no friendly fire, but tiny reflection
				serverdamage(actor, actor, damage * .1f, gun, style, source);
			}
			// return; // we don't want this
			damage = 0; // we want to show a hitmarker...
		}
		else if(m_vampire(gamemode, mutators) && actor->state.health < 300 * HEALTHSCALE){
			int hpadd = damage / (rnd(3) + 3);
			// cap at 300 HP
			if(actor->state.health + hpadd > 300 * HEALTHSCALE)
				hpadd = 300 * HEALTHSCALE - actor->state.health;
			sendf(-1, 1, "ri3", N_REGEN, actor->clientnum, actor->state.health += hpadd);
		}
	}

	ts.dodamage(damage, actor->state.perk1 == PERK_POWER);
	ts.lastregen = gamemillis + REGENDELAY - REGENINT;

	if(ts.health<=0) serverdied(target, actor, damage, gun, style, source);
	else
	{
		if(ts.damagelog.find(actor->clientnum) < 0)
			ts.damagelog.add(actor->clientnum);
		sendf(-1, 1, "ri8f3", N_DAMAGE, target->clientnum, actor->clientnum, damage, ts.armor, ts.health, gun, style & FRAG_VALID, source.x, source.y, source.z);
	}
}

void cheat(client *cl, const char *reason = "unknown"){
	logline(ACLOG_INFO, "[%s] %s cheat detected (%s)", gethostname(cl->clientnum), formatname(cl), reason);
	defformatstring(cheats)("\f2%s \fs\f6(%d) \f3cheat detected \f4(%s)", cl->name, cl->clientnum, reason);
	sendservmsg(cheats);
	cl->suicide(WEAP_MAX + 5, FRAG_GIB);
}

#include "serverevents.h"

void readbotnames(const char *name)
{
	static string botfilename;
	static int botfilesize;
	//const char *sep = " ";
	char *p, *l;
	int len, line = 0;

	if(!name && getfilesize(botfilename) == botfilesize) return;
	botnames.shrink(0);
	char *buf = loadcfgfile(botfilename, name, &len);
	botfilesize = len;
	if(!buf) return;
	p = buf;
	logline(ACLOG_VERBOSE,"reading bot names '%s'", botfilename);
	while(p < buf + len)
	{
		l = p; p += strlen(p) + 1; line++;
		l += strspn(l, " ");
		if(l && *l)
		{
			botname &bn = botnames.add();
			copystring(bn.storage, l, MAXNAMELEN+1);
		}
		/*
		char *n = strchr(l, *sep);
		if(*l && n)
		{
			*n++ = 0;
			botname &bn = botnames.add();
			copystring(bn.rank, l, MAXNAMELEN);
			copystring(bn.name, n, MAXNAMELEN);
		}
		*/
	}
	delete[] buf;
	logline(ACLOG_INFO,"read %d bot names from '%s'", botnames.length(), botfilename);
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
			for(i = 5; i < CONFIG_MAXPAR; i++) c.par[i] = 0;  // default values
			for(i = 0; i < CONFIG_MAXPAR; i++)
			{
				if((l = strtok(NULL, sep)) != NULL)
					c.par[i] = atoi(l);
				else
					break;
			}
			if(i > 4)
			{
				configsets.add(c);
				logline(ACLOG_VERBOSE," %s, %s, %d minutes, vote:%d, minplayer:%d, maxplayer:%d, skiplines:%d", c.mapname, modestr(c.mode, c.muts, false), c.time, c.vote, c.minplayer, c.maxplayer, c.skiplines);
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

vector<iprange> ipblacklist, ipmutelist;

int fixblacklist(vector<iprange> &target, const char *name){
	target.sort(cmpiprange); // or else bsearch fucks up
	const int orglength = target.length();
	loopv(target)
	{
		if(!i) continue;
		if(target[i].ur <= target[i - 1].ur)
		{
			if(target[i].lr == target[i - 1].lr && target[i].ur == target[i - 1].ur)
				logline(ACLOG_INFO, " %s entry %s got dropped (double entry)", name, iprtoa(target[i]));
			else
				logline(ACLOG_INFO, " %s entry %s got dropped (already covered by %s)", name, iprtoa(target[i]), iprtoa(target[i - 1]));
			target.remove(i--); continue;
		}
		if(target[i].lr <= target[i - 1].ur)
		{
			logline(ACLOG_INFO, " %s entries %s and %s are joined due to overlap", name, iprtoa(target[i - 1]), iprtoa(target[i]));
			target[i - 1].ur = target[i].ur;
			target.remove(i--); continue;
		}
	}
	loopv(target) logline(ACLOG_VERBOSE," %s", iprtoa(target[i]));
	return orglength;
}

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
	const int orglength = fixblacklist(ipblacklist, "blacklist");
	logline(ACLOG_INFO,"read %d (%d) blacklist entries from '%s', %d errors", ipblacklist.length(), orglength, blfilename, errors);
}

inline bool checkblacklist(enet_uint32 ip, vector<iprange> &ranges){ // ip: network byte order
	iprange t;
	t.lr = ENET_NET_TO_HOST_32(ip); // blacklist uses host byte order
	t.ur = 0;
	return ranges.search(&t, cmpipmatch) != NULL;
}

inline bool checkipblacklist(enet_uint32 ip) { return checkblacklist(ip, ipblacklist); }
inline bool checkmutelist(enet_uint32 ip) { return checkblacklist(ip, ipmutelist); }

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

bool updateclientteam(int cn, int team, int ftr){
	if(!valid_client(cn) || !team_valid(team)) return false;
	client &ci = *clients[cn];
	if(m_zombie(gamemode) && !m_convert(gamemode, mutators) && team != TEAM_SPECT) team = ci.type == ST_AI ? TEAM_RED : TEAM_BLUE;
	if(ci.team == team){
		if (ftr != FTR_AUTOTEAM) return false;
	}
	else ci.removeexplosives(); // no nade switch
	if(ci.team == TEAM_SPECT) ci.state.lastdeath = gamemillis;
	logline(ftr == FTR_SILENT ? ACLOG_DEBUG : ACLOG_INFO, "[%s] %s is now on team %s", gethostname(cn), formatname(ci), team_string(team));
	// force a death if needed
	if(ci.state.state != CS_DEAD && (m_team(gamemode, mutators) || team == TEAM_SPECT)){
		if(ftr == FTR_PLAYERWISH) serverdied(&ci, &ci, 0, WEAP_MAX + ((team == TEAM_SPECT) ? 8 : 7), FRAG_NONE, ci.state.o);
		else forcedeath(&ci);
	}
	// set new team
	sendf(-1, 1, "ri3", N_SETTEAM, cn, (ci.team = team) | (ftr << 4));
	// check bots
	if(ci.type != ST_AI) checkai();
	return true; // success!
}

int calcscores() // skill eval
{
	int fp12 = (m_capture(gamemode) || m_hunt(gamemode)) ? 55 : 33;
	int fp3 = (m_capture(gamemode) || m_hunt(gamemode)) ? 25 : 15;
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
	if(m_zombie(gamemode)) return; // in case a vote is called
	int numplayers = numclients();
	int team, sums = calcscores();
	if(gamemillis < 2 * 60 *1000){ // random
		int teamsize[2] = {0, 0};
		loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->team < 2){
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
		loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->team < 2) { clients[i]->at3_score += rnd(sums | 1); shuffle.add(i); }
		shuffle.sort(cmpscore);
		loopi(shuffle.length()){
			updateclientteam(shuffle[i], team, ftr);
			team = !team;
		}
	}
}


bool balanceteams(int ftr, bool aionly = true)  // pro vs noobs never more
{
    if(mastermode != MM_OPEN || numauthedclients() < 3 ) return true;
    int tsize[2] = {0, 0}, tscore[2] = {0, 0};
    int totalscore = 0, nplayers = 0;
    int flagmult = (m_capture(gamemode) ? 50 : (m_hunt(gamemode) ? 25 : 12));

    loopv(clients) if(clients[i]->type!=ST_EMPTY){
        client *c = clients[i];
        if(c->connected && c->team < 2 && (!aionly || c->type == ST_AI)){
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
	if(!aionly) return false;
	return balanceteams(ftr, false);
}

int lastbalance = 0, waitbalance = 2 * 60 * 1000;

bool refillteams(bool now, int ftr, bool aionly){ // force only minimal amounts of players
	if(m_zombie(gamemode) && !m_convert(gamemode, mutators)){ // force to zombie teams
		loopv(clients)
			if(clients[i]->type != ST_EMPTY && clients[i]->team != TEAM_SPECT)
				updateclientteam(i, clients[i]->type == ST_AI ? TEAM_RED : TEAM_BLUE, ftr);
		return false;
	}
	static int lasttime_eventeams = 0;
    int teamsize[2] = {0, 0}, teamscore[2] = {0, 0}, moveable[2] = {0, 0};
    bool switched = false;

    calcscores();
    loopv(clients) if(clients[i]->type!=ST_EMPTY){ // playerlist stocktaking
        client *c = clients[i];
        c->at3_dontmove = true;
        if(c->connected && c->team < 2 && (!aionly || c->type == ST_AI)){
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
    if(switched) return true;
	return aionly ? false : refillteams(now, ftr, false);
}

void resetserver(const char *newname, int newmode, int newmuts, int newtime){
	if(m_demo(gamemode)) enddemoplayback();
	else enddemorecord();

	modecheck(smode = newmode, smuts = newmuts);
	copystring(smapname, newname);

	minremain = m_edit(gamemode) ? 1440 : newtime >= 0 ? newtime : (m_team(gamemode, mutators) ? 15 : 10);
	gamemillis = 0;
	gamelimit = minremain*60000;
	gamemusicseed = rand();

	mapreload = false;
	interm = 0;
	nextstatus = servmillis;
	sents.shrink(0);
	scores.shrink(0);
	ctfreset();
}

inline void putmap(ucharbuf &p){
	putint(p, N_MAPCHANGE);
	putint(p, smode);
	putint(p, smuts);
	putint(p, mapavailable(smapname));
	sendstring(smapname, p);

	int n = 0;
	loopv(sents) if(sents[i].spawned) ++n;
	putint(p, n);
	loopv(sents) if(sents[i].spawned) putint(p, i);

	putint(p, sknives.length());
	loopv(sknives){
		putint(p, sknives[i].id);
		putint(p, KNIFETTL+sknives[i].millis-gamemillis);
		putfloat(p, sknives[i].o.x);
		putfloat(p, sknives[i].o.y);
		putfloat(p, sknives[i].o.z);
	}

	putint(p, sconfirms.length());
	loopv(sconfirms){
		putint(p, sconfirms[i].id);
		putint(p, sconfirms[i].team);
		putfloat(p, sconfirms[i].o.x);
		putfloat(p, sconfirms[i].o.y);
		putfloat(p, sconfirms[i].o.z);
	}
}

void resetmap(const char *newname, int newmode, int newmuts, int newtime, bool notify){
	bool lastteammode = m_team(gamemode, mutators) && !m_zombie(gamemode);
	resetserver(newname, newmode, newmuts, newtime);

	if(isdedicated) getservermap();

	mapstats *ms = getservermapstats(smapname, true);
	if(ms){
		smapstats = *ms;
		loopi(2)
		{
			sflaginfo &f = sflaginfos[i];
			if(smapstats.flags[i] == 1)	// don't check flag positions, if there is more than one flag per team
			{
				/*
				short *fe = smapstats.entposs + smapstats.flagents[i] * 4;
				f.x = *fe;
				f.y = *++fe;
				*/
				f.x = smapstats.ents[smapstats.flagents[i]].x;
				f.y = smapstats.ents[smapstats.flagents[i]].y;
			}
			else f.x = f.y = -1;
		}

		loopi(smapstats.hdr.numents)
		{
			entity &e = sents.add();
			persistent_entity &pe = smapstats.ents[i];
			e.type = pe.type;
			e.transformtype(smode, smuts);
			e.x = pe.x;
			e.y = pe.y;
			e.z = pe.z;
			e.attr1 = pe.attr1;
			e.attr2 = pe.attr2;
			e.attr3 = pe.attr3;
			e.attr4 = pe.attr4;
			e.spawned = e.fitsmode(smode, smuts);
			e.spawntime = 0;
		}
		// copyrevision = copymapsize == smapstats.cgzsize ? smapstats.hdr.maprevision : 0;
	}
	else sendmsg(11);

	clearai(); // re-init ai (clear)

	if(notify){
		// change map
		sknives.setsize(0);
		ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		ucharbuf p(packet->data, packet->dataLength);
		putmap(p);
		enet_packet_resize(packet, p.length());
		sendpacket(-1, 1, packet);
		if(!packet->referenceCount) enet_packet_destroy(packet);
		// time remaining
		if(m_valid(smode)) sendf(-1, 1, "ri4", N_TIMEUP, gamemillis, gamelimit, gamemusicseed);
	}
	logline(ACLOG_INFO, "");
	logline(ACLOG_INFO, "Game start: %s on %s, %d players, %d minutes remaining, mastermode %d, (%s'getmap' %sprepared)",
		modestr(smode, smuts), smapname, numclients(), minremain, mastermode, ms ? "" : "itemlist failed,", mapavailable(smapname) ? "" : "not ");
	arenaround = 0;
	nokills = true;
	if(m_duke(gamemode, mutators)) distributespawns();
	if(notify){
		if(m_team(gamemode, mutators)){
			if(!lastteammode && !m_zombie(gamemode)) // shuffle if previous mode wasn't a team-mode, or was a zombie mode
				shuffleteams(FTR_SILENT);
			else if(m_zombie(gamemode) || autobalance) // force teams for zombies
				refillteams(true, FTR_SILENT);
		}
		// prepare spawns; players will spawn, once they've loaded the correct map
		loopv(clients) if(clients[i]->type!=ST_EMPTY){
			client *c = clients[i];
			c->mapchange();
			forcedeath(c);
		}
	}
	checkai(); // re-init ai (init)
	// reset team scores
	loopi(TEAM_NUM - 1) steamscores[i] = teamscore(i);
	purgesknives();
	purgesconfirms(); // but leave the confirms for team modes in arena
	if(m_demo(gamemode)) setupdemoplayback();
	else if((demonextmatch || scl.demoeverymatch) && *newname && numnonlocalclients() > 0){
		demonextmatch = false;
		setupdemorecord();
	}
	if(notify && m_keep(gamemode)) sendflaginfo();

	*nextmapname = 0;
	forceintermission = false;
}

int nextcfgset(bool notify = true, bool nochange = false){ // load next maprotation set
	int n = countplayers(false);
	int csl = configsets.length();
	int ccs = curcfgset;
	if(ccs >= 0 && ccs < csl) ccs += configsets[ccs].skiplines;
	configset *c = NULL;
	loopi(csl)
	{
		++ccs;
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
		resetmap(c->mapname, c->mode, c->muts, c->time, notify);
	}
	return ccs;
}

bool isbanned(int cn){
	if(!valid_client(cn)) return false;
	client &c = *clients[cn];
	if(c.type==ST_LOCAL || c.authpriv >= PRIV_MASTER) return false;
	loopv(bans){
		ban &b = bans[i];
		if(b.millis >= 0 && b.millis < servmillis) bans.remove(i--);
		else if(b.host == c.peer->address.host) return true;
	}
	return checkipblacklist(c.peer->address.host);
}

void banclient(client *c, int minutes){
	ban b; 
	b.host = c->peer->address.host;
	b.millis = servmillis + minutes * 60000;
	bans.add(b);
}

void sendserveropinfo(int receiver = -1){ loopv(clients) if(valid_client(i)) sendf(receiver, 1, "ri3", N_SETPRIV, i, clients[i]->priv); }

#include "serveractions.h"
static voteinfo *curvote = NULL;

void scallvotesuc(voteinfo *v){
	if(!v->isvalid()) return;
	DELETEP(curvote);
	curvote = v;
	clients[v->owner]->lastvotecall = servmillis;
	logline(ACLOG_INFO, "[%s] client %s called a vote: %s", gethostname(v->owner), formatname(clients[v->owner]), v->action->desc ? v->action->desc : "[unknown]");
}

void scallvoteerr(voteinfo *v, int error){
	if(!valid_client(v->owner)) return;
	sendf(v->owner, 1, "ri2", N_CALLVOTEERR, error);
	logline(ACLOG_INFO, "[%s] client %s failed to call a vote: %s (%s)", gethostname(v->owner), formatname(clients[v->owner]), v->action->desc ? v->action->desc : "[unknown]", voteerrorstr(error));
}


void sendcallvote(int cl = -1){
	if(curvote && curvote->result == VOTE_NEUTRAL){
		ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
		ucharbuf p(packet->data, packet->dataLength);
		putint(p, N_CALLVOTE);
		putint(p, curvote->owner);
		putint(p, curvote->type);
		putint(p, curvote->action->length - servmillis + curvote->callmillis);
		switch(curvote->type)
		{
			case SA_MAP:
				putint(p, ((mapaction *)curvote->action)->muts);
				sendstring(((mapaction *)curvote->action)->map, p);
				putint(p, ((mapaction *)curvote->action)->mode);
				break;
			case SA_SERVERDESC:
				sendstring(((serverdescaction *)curvote->action)->sdesc, p);
				break;
			case SA_GIVEROLE:
				putint(p, ((giveadminaction *)curvote->action)->give);
				putint(p, ((playeraction *)curvote->action)->cn);
				break;
			case SA_STOPDEMO:
			case SA_REMBANS:
			case SA_SHUFFLETEAMS:
			default:
				break;
			case SA_KICK:
				if(curvote->type == SA_KICK) sendstring(((kickaction *)curvote->action)->reason, p);
				// fallthrough
			case SA_BAN:
				if(curvote->type == SA_BAN)
				{
					sendstring(((banaction *)curvote->action)->reason, p);
					putint(p, ((banaction *)curvote->action)->bantime);
				}
				// fallthrough
			case SA_SUBDUE:
			case SA_REVOKE:
			case SA_FORCETEAM:
			case SA_SPECT:
				putint(p, ((playeraction *)curvote->action)->cn);
				break;
			case SA_AUTOTEAM:
			case SA_RECORDDEMO:
				putint(p, ((enableaction *)curvote->action)->enable ? 1 : 0);
				break;
			case SA_CLEARDEMOS:
				putint(p, ((cleardemosaction *)curvote->action)->demo);
				break;
			case SA_BOTBALANCE:
				putint(p, ((botbalanceaction *)curvote->action)->bb);
				break;
			case SA_MASTERMODE:
				putint(p, ((mastermodeaction *)curvote->action)->mode);
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
	else if(v->action->reqpriv > clients[v->owner]->priv) error = VOTEE_PERMISSION;
	else if(!(area & v->action->area)) error = VOTEE_AREA;
	else if(curvote && curvote->result==VOTE_NEUTRAL) error = VOTEE_CUR;
	else if(clients[v->owner]->priv < PRIV_ADMIN && v->action->isdisabled()) error = VOTEE_DISABLED;
	else if(clients[v->owner]->lastvotecall && servmillis - clients[v->owner]->lastvotecall < 60*1000 && clients[v->owner]->priv < PRIV_ADMIN && numclients()>1)
		error = VOTEE_MAX;

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
		sendf(-1, 1, "ri4", N_CLAIMPRIV, cl, c.priv, 1);
		logline(ACLOG_INFO,"[%s] %s relinquished %s access", gethostname(cl), formatname(c), privname(c.priv));
		c.priv = PRIV_NONE;
		sendserveropinfo();
		return;
	}
	else if(c.priv >= priv){
		sendf(cl, 1, "ri4", N_CLAIMPRIV, cl, priv, 2);
		return;
	}
	/*
	else if(priv >= PRIV_ADMIN){
		loopv(clients) if(clients[i]->type != ST_EMPTY && clients[i]->authpriv < PRIV_MASTER && clients[i]->priv == PRIV_MASTER) setpriv(i, PRIV_NONE);
	}
	*/
	c.priv = priv;
	sendf(-1, 1, "ri4", N_CLAIMPRIV, cl, c.priv, 0);
	logline(ACLOG_INFO,"[%s] %s claimed %s access", gethostname(cl), formatname(c), privname(c.priv));
	sendserveropinfo();
	//if(curvote) curvote->evaluate();
}

#include "aiman.h"

void disconnect_client(int n, int reason){
	if(!clients.inrange(n) || clients[n]->type!=ST_TCPIP) return;
	sdropflag(n);
	// remove assists
	loopv(clients) if(i != n){
		clientstate &cs = clients[i]->state;
		cs.damagelog.removeobj(n);
		cs.revengelog.removeobj(n);
	}
	client &c = *clients[n];
	loopv(clients) if(clients[i]->state.ownernum == n) if(!shiftai(*clients[i], -1, n)) deleteai(*clients[i]);
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
		findlimit(c, true);
	}
	int sp = (servmillis - c.connectmillis) / 1000;
	if(reason>=0) logline(ACLOG_INFO, "[%s] disconnecting client %s (%s) cn %d, %d seconds played%s", gethostname(n), formatname(c), disc_reason(reason), n, sp, scoresaved);
	else logline(ACLOG_INFO, "[%s] disconnected client %s cn %d, %d seconds played%s", gethostname(n), formatname(c), n, sp, scoresaved);
	c.peer->data = (void *)-1;
	if(reason>=0) enet_peer_disconnect(c.peer, reason);
	clients[n]->zap();
	sendf(-1, 1, "ri3", N_DISC, n, reason);
	if(curvote) curvote->evaluate();
	freeconnectcheck(n);
	checkai();
}

void sendwhois(int sender, int cn){
	if(!valid_client(sender) || !valid_client(cn)) return;

	if(clients[cn]->type == ST_TCPIP){
		sendf(-1, 1, "ri3", N_WHOIS, cn, sender);
		uint ip = clients[cn]->peer->address.host;
		uchar mask = 0;
		if(cn == sender) mask = 32;
		else switch(clients[sender]->priv){
			// admins and server owner: f.f.f.f/32 full ip
			case PRIV_MAX: case PRIV_ADMIN: mask = 32; break;
			// masters and users: f.f.h/12 full, full, half, empty
			case PRIV_MASTER: case PRIV_NONE: default: mask = 20; break;
		}
		if(mask < 32) ip &= (1 << mask) - 1;

		sendf(sender, 1, "ri5", N_WHOISINFO, cn, ip, mask, clients[cn]->peer->address.port);
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
	putint(p, N_MAPS2C);
	sendstring(copyname, p);
	putint(p, copymapsize);
	putint(p, copycfgsize);
	putint(p, copycfgsizegz);
	p.put(copydata, copysize);
	enet_packet_resize(packet, p.length());
	return packet;
}

void recvmapserv(client *cl){
	const int sender = cl->clientnum;
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
		sendwelcome(cl, 2);
	}
	else{
		cl->isonrightmap = true;
		sendmsg(13, sender);
	}
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

void putteamscore(int team, ucharbuf &p){
	if(!team_valid(team) || team == TEAM_SPECT) return;
	teamscore &t = steamscores[team];
	putint(p, N_TEAMSCORE);
	putint(p, team);
	putint(p, t.points);
	putint(p, t.flagscore);
	putint(p, t.frags);
	putint(p, t.assists);
	putint(p, t.deaths);
}

void sendteamscore(int team, int reciever){
	if(!team_valid(team) || team == TEAM_SPECT) return;
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putteamscore((m_team(gamemode, mutators) && team == TEAM_BLUE) ? TEAM_BLUE : TEAM_RED, p);
	enet_packet_resize(packet, p.length());
	sendpacket(reciever, 1, packet);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

void welcomepacket(ucharbuf &p, int n, ENetPacket *packet){
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
	putint(p, smapname[0] && !m_demo(gamemode) ? numcl : -1);
	CHECKSPACE(5+2*(int)strlen(scl.motd)+1);
	sendstring(scl.motd, p);
	if(smapname[0] && !m_demo(gamemode)){
		putmap(p);
		if(m_valid(smode)){
			putint(p, N_TIMEUP);
			putint(p, gamemillis);
			putint(p, gamelimit);
			putint(p, gamemusicseed);
		}
		if(m_affinity(gamemode)){
			CHECKSPACE(256);
			loopi(2) putflaginfo(p, i);
		}
	}

	if(demorecord){
		putint(p, N_CONFMSG);
		putint(p, 20);
	}

	// sendservopinfo(*)
	loopv(clients) if(valid_client(i)){
		putint(p, N_SETPRIV);
		putint(p, i);
		putint(p, clients[i]->priv);
	}

	if(c){
		CHECKSPACE(256);
		putint(p, N_SETTEAM);
        putint(p, n);
        putint(p, c->team | (FTR_SILENT << 4));

		putint(p, N_FORCEDEATH);
        putint(p, n);
        sendf(-1, 1, "ri2x", N_FORCEDEATH, n, n);
	}
	if(!c || clients.length()>1)
	{
		// welcomeinitclient
		loopv(clients){
			client &c = *clients[i];
			if(c.type == ST_EMPTY || !c.connected || c.clientnum == n) continue;
			putinitclient(c, p);
		}

		loopi(TEAM_NUM-1)
		{
			putteamscore(i, p);
			if(!m_team(gamemode, mutators)) break;
		}
		putint(p, N_RESUME);
		loopv(clients)
		{
			client &c = *clients[i];
			if(c.type != ST_TCPIP && c.type != ST_AI) continue;
			CHECKSPACE(512);
			putint(p, c.clientnum);
			clientstate &cs = c.state;
			putint(p, cs.state == CS_WAITING ? CS_DEAD : cs.state);
			putint(p, cs.lifesequence);
			putint(p, cs.gunselect);
			putint(p, cs.primary);
			putint(p, cs.secondary);
			putint(p, cs.points);
			putint(p, cs.flagscore);
			putint(p, cs.frags);
			putint(p, cs.assists);
			putint(p, cs.pointstreak);
			putint(p, cs.deathstreak);
			putint(p, cs.deaths);
			putint(p, cs.health);
			putint(p, cs.armor);
			putint(p, cs.radarearned - gamemillis);
			putint(p, cs.airstrikes);
			putint(p, cs.nukemillis - gamemillis);
			putint(p, cs.spawnmillis - gamemillis);
			loopi(WEAP_MAX) putint(p, cs.ammo[i]);
			loopi(WEAP_MAX) putint(p, cs.mag[i]);
		}
		putint(p, -1);
	}

	#undef CHECKSPACE
}

void sendwelcome(client *cl, int chan){
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	welcomepacket(p, cl->clientnum, packet);
	enet_packet_resize(packet, p.length());
	sendpacket(cl->clientnum, chan, packet);
	if(!packet->referenceCount) enet_packet_destroy(packet);
	cl->haswelcome = true;
}

void checkmove(client &cp){
	const int sender = cp.clientnum;
	clientstate &cs = cp.state;
	// detect speedhack
	float cps = cs.lasto.dist(cs.o);
	if(cs.lasto.dist(cs.o) >= .001f){
		cs.movemillis = servmillis;
		if(cps && cs.lastomillis && gamemillis > cs.lastomillis){
			cps *= 1000 / (gamemillis - cs.lastomillis);
			if(cps > 32){ // 8 meters per second
				defformatstring(fastmsg)("%s moved at %.3f meters/second", formatname(cp), cps / 4);
				sendservmsg(fastmsg);
				if(cps > 64) // 16 meters per second
					cheat(&cp, "speedhack");
			}
		}
	}
	// out of map check
	if(/*cp.type != ST_LOCAL &&*/ !m_edit(gamemode) && checkpos(cs.o, false)){
		if(cp.type == ST_AI) cp.suicide(WEAP_MAX + 4);
		else{
			logline(ACLOG_INFO, "[%s] %s collides with the map (%d)", gethostname(sender), formatname(cp), ++cp.mapcollisions);
			sendmsgi(40, sender);
			sendf(sender, 1, "ri", N_MAPIDENT);
			forcedeath(&cp);
			cp.isonrightmap = false; // cannot spawn until you get the right map
		}
		return; // no pickups for you!
	}
	// item pickups
	if(!m_zombie(gamemode) || cp.team != TEAM_RED) loopv(sents){
		entity &e = sents[i];
		const bool cantake = (e.spawned && cs.canpickup(e.type)), canheal = (e.type == I_HEALTH && cs.wounds.length());
		if(!cantake && !canheal) continue;
		const int ls = (1 << maplayout_factor) - 2, maplayoutid = getmaplayoutid(e.x, e.y);
		const bool getmapz = maplayout && e.x > 2 && e.y > 2 && e.x < ls && e.y < ls;
		const char &mapz = getmapz ? getblockfloor(maplayoutid) : 0;
		vec v(e.x, e.y, getmapz ? (mapz + e.attr1 + PLAYERHEIGHT) : cs.o.z);
		float dist = cs.o.dist(v);
		if(dist > 3) continue;
		if(canheal){
			// healing station
			addpt(&cp, HEALWOUNDPT * cs.wounds.length(), PR_HEALWOUND);
			cs.wounds.shrink(0);
		}
		if(cantake){
			// server side item pickup, acknowledge first client that moves to the entity
			e.spawned = false;
			sendf(-1, 1, "ri4", N_ITEMACC, i, sender, e.spawntime = spawntime(e.type));
			cs.pickup(sents[i].type);
			if(sents[i].type == I_HEALTH && cs.health >= STARTHEALTH)
				cs.damagelog.setsize(0);
		}
	}
	// flags
	if(m_affinity(gamemode)) loopi(2){ // check flag pickup
		sflaginfo &f = sflaginfos[i];
		sflaginfo &of = sflaginfos[team_opposite(i)];
		bool forcez = false;
		vec v(-1, -1, cs.o.z);
		if(m_bomber(gamemode) && i == cp.team && f.state == CTFF_STOLEN && f.actor_cn == sender){
			v.x = of.x;
			v.y = of.y;
		}
		else switch(f.state){
			case CTFF_STOLEN:
				if(!m_return(gamemode, mutators) || i != cp.team) break;
			case CTFF_INBASE:
				v.x = f.x; v.y = f.y;
				break;
			case CTFF_DROPPED:
				v.x = f.pos[0]; v.y = f.pos[1];
				forcez = true;
				break;
		}
		if(v.x < 0) continue;
		if(forcez)
			v.z = f.pos[2];
		else
			v.z = getsblock(getmaplayoutid((int)v.x, (int)v.y)).floor + PLAYERHEIGHT;
		float dist = cs.o.dist(v);
		if(dist > 2) continue;
		if(m_capture(gamemode)){
			if(i == cp.team){ // it's our flag
				if(f.state == CTFF_DROPPED){
					if(m_return(gamemode, mutators) /*&& (of.state != CTFF_STOLEN || of.actor_cn != sender)*/) flagaction(i, FA_PICKUP, sender);
					else flagaction(i, FA_RETURN, sender);
				}
				else if(f.state == CTFF_STOLEN && sender == f.actor_cn) flagaction(i, FA_RETURN, sender);
				else if(f.state == CTFF_INBASE && of.state == CTFF_STOLEN && of.actor_cn == sender && gamemillis >= of.stolentime + 1000) flagaction(team_opposite(i), FA_SCORE, sender);
			}
			else{
				/*if(m_return && of.state == CTFF_STOLEN && of.actor_cn == sender) flagaction(team_opposite(i), FA_RETURN, sender);*/
				flagaction(i, FA_PICKUP, sender);
			}
		}
		else if(m_hunt(gamemode) || m_bomber(gamemode)){
			// BTF only: score their flag by bombing their base!
			if(f.state == CTFF_STOLEN){
				flagaction(i, FA_SCORE, sender);
				explosion(cp, v, WEAP_GRENADE); // identical to self-nades, replace with something else?
			}
			else if(i == cp.team){
				if(m_hunt(gamemode)) f.drop_cn = -1; // force pickup
				flagaction(i, FA_PICKUP, sender);
			}
			else if(f.state == CTFF_DROPPED && gamemillis >= of.stolentime + 500) flagaction(i, m_hunt(gamemode) ? FA_SCORE : FA_RETURN, sender);
		}
		else if(m_keep(gamemode) && f.state == CTFF_INBASE) flagaction(i, FA_PICKUP, sender);
		else if(m_ktf2(gamemode, mutators) && f.state != CTFF_STOLEN){
			bool cantake = of.state != CTFF_STOLEN || of.actor_cn != sender || !m_team(gamemode, mutators);
			if(!cantake){
				cantake = true;
				loopv(clients) if(i != sender && valid_client(i, true) && clients[i]->team == cp.team) { cantake = false; break; }
			}
			if(cantake) flagaction(i, FA_PICKUP, sender);
		}
	}
	// kill confirmed
	loopv(sconfirms) if(sconfirms[i].o.dist(cs.o) < 5){
		if(cp.team == sconfirms[i].team){
			addpt(&cp, KCKILLPTS, PR_KC);
			steamscores[sconfirms[i].team].points += sconfirms[i].points;
			steamscores[sconfirms[i].team].frags += sconfirms[i].frag;
			++steamscores[sconfirms[i].death].deaths;

			sendteamscore(sconfirms[i].team);
			if(sconfirms[i].team != sconfirms[i].death)
				sendteamscore(sconfirms[i].death);
		}
		else addpt(&cp, KCDENYPTS, PR_KD);

		sendf(-1, 1, "ri2", N_CONFIRMREMOVE, sconfirms[i].id);
		sconfirms.remove(i--);
	}
	// throwing knife pickup
	if(cp.type != ST_AI) loopv(sknives){
		const bool pickup = cs.o.dist(sknives[i].o) < 5 && cs.ammo[WEAP_KNIFE] < ammostats[WEAP_KNIFE].max, expired = gamemillis - sknives[i].millis > KNIFETTL;
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
	if(!m_edit(gamemode) && cl && cl->type == ST_TCPIP && (type >= N_EDITH && type <= N_NEWMAP)) return -1; // edit
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
		else if(chan!=1 || getint(p) != N_WELCOME) disconnect_client(sender, DISC_TAGT);
		else
		{
			getstring(text, p);
			filtername(text, text);
			if(!*text) copystring(text, "unarmed");
			copystring(cl->name, text, MAXNAMELEN+1);
			cl->skin = getint(p);
			cl->state.level = clamp(getint(p), 1, MAXLEVEL);
			getstring(text, p);
			copystring(cl->pwd, text);
			const int connectauth = getint(p);
			getstring(text, p); // authname
			cl->state.nextprimary = getint(p);
			cl->state.nextsecondary = getint(p);
			cl->state.nextperk1 = getint(p);
			cl->state.nextperk2 = getint(p);

			int clientversion = getint(p), clientdefs = getint(p), clientguid = getint(p);
			logversion(*cl, clientversion, clientdefs, clientguid);

			int disc = p.remaining() ? DISC_TAGT : allowconnect(*cl, cl->pwd, connectauth, text);

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

			// ask masterserver for connection verdict
			connectcheck(sender, cl->guid, cl->peer->address.host);
			// restore the score
			savedscore *sc = findscore(*cl, false);
			if(sc)
			{
				sc->restore(cl->state);
				// sendresume
				clientstate &cs = cl->state;
				sendf(-1, 1, "rii5i5i9i9vvi", N_RESUME,
					cs.state == CS_WAITING ? CS_DEAD : cs.state, // 1
					cs.lifesequence,
					cs.gunselect,
					cs.primary,
					cs.secondary,
					cs.points,
					cs.flagscore,
					cs.frags,
					cs.assists, // 9
					cs.pointstreak, // 1
					cs.deathstreak,
					cs.deaths,
					cs.health,
					cs.armor,
					cs.radarearned - gamemillis,
					cs.airstrikes,
					cs.nukemillis - gamemillis,
					cs.spawnmillis - gamemillis, // 9
					WEAP_MAX, cs.ammo,
					WEAP_MAX, cs.mag,
					-1
				);
			}
			// check teams
			cl->team = (mastermode >= MM_LOCKED) ? TEAM_SPECT : chooseteam(*cl);
		}

		sendwelcome(cl);
		sendinitclient(*cl);
		if(canspawn(cl, true)) sendspawn(cl);
		findlimit(*cl, false);

		if(curvote){
			sendcallvote(sender);
			curvote->evaluate();
		}
		checkai();
		while(reassignai());
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

	int curmsg;
	while((curmsg = p.length()) < p.maxlen)
	{
		type = checktype(getint(p), cl);

		#ifdef _DEBUG
		if(type!=N_POS && type!=N_SOUND && type!=N_PINGTIME && type!=N_PINGPONG)
		{
			DEBUGVAR(cl->name);
			DEBUGVAR(messagenames(type));
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
				filtertext(text, text, 1, 127);
				sendtext(text, *cl, flags, voice);
				break;
			}

			case N_NEWNAME:
				getstring(text, p);
				filtername(text, text);
				if(!*text) copystring(text, "unarmed");
				if(!strcmp(cl->name, text)) break; // same name!
				switch(const int nwl = nbl.checknickwhitelist(*cl)){
					case NWL_PWDFAIL:
					case NWL_IPFAIL:
						logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong %s", gethostname(sender), formatname(cl), nwl == NWL_IPFAIL ? "IP" : "PWD");
						disconnect_client(sender, nwl == NWL_IPFAIL ? DISC_NAME_IP : DISC_NAME_PWD);
						break;

					case NWL_UNLISTED:
					{
						int l = nbl.checknickblacklist(cl->name);
						if(l >= 0)
						{
							logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", gethostname(sender), formatname(cl), l);
							disconnect_client(sender, DISC_NAME);
						}
						break;
					}
				}
				logline(ACLOG_INFO,"[%s] %s is now called %s", gethostname(sender), formatname(cl), text);
				copystring(cl->name, text, MAXNAMELEN+1);
				sendf(-1, 1, "ri2s", N_NEWNAME, sender, cl->name);
				break;

			case N_SWITCHTEAM:
			{
				int t = getint(p);
				if(cl->team == t) break;
				
				if(cl->priv < PRIV_ADMIN && t < 2){
					if(mastermode >= MM_LOCKED && cl->team >= 2){
						sendf(sender, 1, "ri2", N_SWITCHTEAM, 1 << 4);
						break;
					}
					else if(m_zombie(gamemode)){
						sendf(sender, 1, "ri2", N_SWITCHTEAM, 1 << 5);
						break;
					}
					else if(m_team(gamemode, mutators)){
						int teamsizes[2] = {0};
						loopv(clients) if(i != sender && clients[i]->type!=ST_EMPTY && clients[i]->type!=ST_AI && clients[i]->connected && clients[i]->team < 2)
							++teamsizes[clients[i]->team];
						if(teamsizes[t] > teamsizes[t ^ 1]){
							sendf(sender, 1, "ri2", N_SWITCHTEAM, t);
							break;
						}
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
				if(!isdedicated || m_edit(gamemode) || !maplayout || !smapstats.cgzsize || smapstats.cgzsize == gzs){
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
				int nextprimary = getint(p), nextsecondary = getint(p), perk1 = getint(p), perk2 = getint(p);
				clientstate &cs = cl->state;
				cs.nextperk1 = perk1;
				cs.nextperk2 = perk2;
				if(nextprimary >= 0 && nextprimary < WEAP_MAX)
					cs.nextprimary = nextprimary;
				if(nextsecondary >= 0 && nextsecondary < WEAP_MAX)
					cs.nextsecondary = nextsecondary;
				break;
			}

			case N_TRYSPAWN:
			{
				client &cp = *cl;
				clientstate &cs = cp.state;
				if(cs.state == CS_WAITING){ // dequeue for spawning
					cs.state = CS_DEAD;
					sendf(sender, 1, "ri2", N_TRYSPAWN, 0);
					break;
				}
				if(!cl->isonrightmap || !maplayout){ // need the map for spawning
					sendf(sender, 1, "ri", N_MAPIDENT);
					break;
				}
				if(cs.state!=CS_DEAD || cs.lastspawn>=0) break; // not dead or already enqueued
				if(cp.team == TEAM_SPECT){ // need to unspectate
					if(mastermode < MM_LOCKED || cp.type != ST_TCPIP || cp.priv >= PRIV_ADMIN){
						updateclientteam(sender, chooseteam(cp), FTR_PLAYERWISH);
						if(!canspawn(&cp, true)) break;
					}
					else{
						sendf(sender, 1, "ri2", N_SWITCHTEAM, 1 << 4);
						break; // no enqueue
					}
				}
				else if(!canspawn(&cp)) break;
				// enqueue for spawning
				cs.state = CS_WAITING;
				sendf(sender, 1, "ri2", N_TRYSPAWN, 1);
				break;
			}

			case N_SPAWN:
			{
				int cn = getint(p), ls = getint(p), gunselect = getint(p);
				vec o;
				loopi(3) o[i] = getfloat(p);
				if(!hasclient(cl, cn)) break;
				clientstate &cs = clients[cn]->state;
				if((cs.state!=CS_ALIVE && cs.state!=CS_DEAD) || ls!=cs.lifesequence || cs.lastspawn<0 || gunselect<0 || gunselect>=WEAP_MAX) break;
				cs.lastspawn = -1;
				cs.spawnmillis = gamemillis;
				cs.state = CS_ALIVE;
				cs.gunselect = gunselect;
				cs.o = o;
				QUEUE_BUF(5*(9 + 2*WEAP_MAX) + 4*(3),
				{
					putint(buf, N_SPAWN);
					putint(buf, cn);
					putint(buf, ls);
					putint(buf, cs.health);
					putint(buf, cs.armor);
					putint(buf, cs.perk1);
					putint(buf, cs.perk2);
					putint(buf, gunselect);
					putint(buf, cs.secondary);
					loopi(WEAP_MAX) putint(buf, cs.ammo[i]);
					loopi(WEAP_MAX) putint(buf, cs.mag[i]);
					loopi(3) putfloat(buf, cs.o[i]);
				});
				break;
			}

			case N_SUICIDE:
			{
				const int cn = getint(p);
				if(!hasclient(cl, cn)) break;
				client *cp = clients[cn];
				if(cp->state.state != CS_DEAD) cp->suicide(cn == sender ? WEAP_MAX : WEAP_MAX + 4, cn == sender ? FRAG_GIB : FRAG_NONE);
				break;
			}

			case N_SHOOT: // cn id weap to.dx to.dy to.dz heads.length heads.v
			case N_SHOOTC: // cn id weap
			{
				shotevent *ev = new shotevent;
				const int cn = getint(p);
				ev->id = getint(p);
				ev->weap = getint(p);
				if(!(ev->compact = (type == N_SHOOTC)))
				{
					loopi(3) ev->to[i] = getfloat(p);
					const int heads = getint(p), maxheads = numclients();
					loopi(heads)
					{
						head_t h;
						h.cn = getint(p);
						loopj(3) h.delta[j] = getfloat(p);
						
						if(i < maxheads) ev->heads.add(h);
					}
				}

				client *cp = hasclient(cl, cn) ? clients[cn] : NULL;
				if(cp)
				{
					ev->millis = cp->getmillis(gamemillis, ev->id);
					cp->addevent(ev);
				}
				else delete ev;
				break;
			}

			case N_PROJ: // cn id weap flags x y z
			{
				const int cn = getint(p);
				destroyevent *ev = new destroyevent;
				ev->id = getint(p);
				ev->weap = getint(p);
				ev->flags = getint(p);
				loopi(3) ev->o[i] = getfloat(p);

				client *cp = hasclient(cl, cn) ? clients[cn] : NULL;
				if(cp)
				{
					ev->millis = cp->getmillis(gamemillis, ev->id);
					cp->addevent(ev);
				}
				else delete ev;
				break;
			}

			case N_AKIMBO: // cn id
			{
				const int cn = getint(p), id = getint(p);
				if(!hasclient(cl, cn)) break;
				client *cp = clients[cn];
				akimboevent *ev = new akimboevent;
				ev->millis = cp->getmillis(gamemillis, ev->id = id);
				cp->addevent(ev);
				break;
			}

			case N_RELOAD: // cn id weap
			{
				int cn = getint(p), id = getint(p), weap = getint(p);
				if(!hasclient(cl, cn)) break;
				client *cp = clients[cn];
				reloadevent *ev = new reloadevent;
				ev->millis = cp->getmillis(gamemillis, ev->id = id);
				ev->weap = weap;
				cp->addevent(ev);
				break;
			}

			case N_QUICKSWITCH: // cn
			{
				const int cn = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				cp.state.gunselect = cp.state.primary;
				sendf(-1, 1, "ri2x", N_QUICKSWITCH, cn, sender);
				cp.state.scoping = false;
				cp.state.scopemillis = gamemillis - ADSTIME(cp.state.perk2 == PERK_TIME);
				break;
			}

			case N_SWITCHWEAP: // cn weap
			{
				int cn = getint(p), weaponsel = getint(p);
				if(!hasclient(cl, cn)) break;
				client &cp = *clients[cn];
				if(weaponsel < 0 || weaponsel >= WEAP_MAX) break;
				cp.state.gunselect = weaponsel;
				sendf(-1, 1, "ri3x", N_SWITCHWEAP, cn, weaponsel, sender);
				cp.state.scoping = false;
				cp.state.scopemillis = gamemillis - ADSTIME(cp.state.perk2 == PERK_TIME);
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
				--cps.knives.throwable;
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
				--cps.grenades.throwable;
				checkpos(from);
				if(vel.magnitude() > NADEPOWER) vel.normalize().mul(NADEPOWER);
				sendf(-1, 1, "ri2f6ix", N_THROWNADE, cn, from.x, from.y, from.z, vel.x, vel.y, vel.z, cooked, sender);
				break;
			}

			case N_STREAKUSE:
			{
				vec o;
				loopi(3) o[i] = getfloat(p);
				// can't use streaks unless alive
				if(!cl->state.isalive(gamemillis)) break;
				// check how many airstrikes available first
				if(cl->state.airstrikes > 0){
					--cl->state.airstrikes;
					usestreak(*cl, STREAK_AIRSTRIKE, NULL, o);
				}
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
				if(!m_edit(gamemode) && cl->type == ST_TCPIP){ // unacceptable
					cheat(cl, "tried editmode");
					break;
				}
				if(cl->state.state != (editing ? CS_ALIVE : CS_EDITING)) break;
				cl->state.state = editing ? CS_EDITING : CS_ALIVE;
				cl->state.onfloor = true; // prevent falling damage
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
					{ \
						const int ssize = 1 << maplayout_factor; /* borrow the OUTBORD macro */ \
						loop(xx, xs) loop(yy, ys) if(!OUTBORD(x + xx, y + yy)) \
						{ \
							const int id = getmaplayoutid(x + xx, y + yy); \
							body \
						} \
					}
					case N_EDITH:
					{
						int offset = getint(p);
						seditloop({
							if(!v){ // ceil
								getsblock(id).ceil += offset;
								if(getsblock(id).ceil <= getsblock(id).floor) getsblock(id).ceil = getsblock(id).floor+1;
							}
							else{ // floor
								getsblock(id).floor += offset;
								if(getsblock(id).floor >= getsblock(id).ceil) getsblock(id).floor = getsblock(id).ceil-1;
							}
						});
						break;
					}
					case N_EDITS:
						seditloop({
							getsblock(id).type = v;
						});
						break;
					case N_EDITD:
						seditloop({
							getsblock(id).vdelta += v;
							if(getsblock(id).vdelta < 0) getsblock(id).vdelta = 0;
						});
						break;
					case N_EDITE:
					{
						int low = 127, hi = -128;
						seditloop({
							if(getsblock(id).floor<low) low = getsblock(id).floor;
							if(getsblock(id).ceil>hi) hi = getsblock(id).ceil;
						});
						seditloop({
							if(!v) getsblock(id).ceil = hi; else getsblock(id).floor = low;
							if(getsblock(id).floor >= getsblock(id).ceil) getsblock(id).floor = getsblock(id).ceil-1;
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
				int attr1 = getint(p), attr2 = getint(p), attr3 = getint(p), attr4 = getint(p);
				while(sents.length() <= id) sents.add().type = NOTUSED;
				entity &e = sents[max(id, 0)];
				// server entity
				e.type = type;
				e.transformtype(smode, smuts);
				e.x = o.x;
				e.y = o.y;
				e.z = o.z;
				e.attr1 = attr1;
				e.attr2 = attr2;
				e.attr3 = attr3;
				e.attr4 = attr4;
				// is it spawned?
				if(e.spawned = e.fitsmode(smode, smuts))
					sendf(-1, 1, "ri2", N_ITEMSPAWN, id);
				e.spawntime = 0;
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
				const bool broadcast = hasclient(cl, cn);
				vec newo, newaim, newvel;
				loopi(3) newo[i] = getfloat(p);
				loopi(3) newaim[i] = getfloat(p);
				loopi(3) newvel[i] = getfloat(p);
				const float newpitchvel = getfloat(p);
				const int f = getuint(p);
				if(!valid_client(cn)) break;
				client &cp = *clients[cn];
				clientstate &cs = cp.state;
				//if(broadcast && cs.state == CS_SPAWNING) cs.state = CS_ALIVE;
				if(interm || !broadcast || (cs.state!=CS_ALIVE && cs.state!=CS_EDITING) || ((f>>4)&1)!=(cs.lifesequence&1)) break;
				// store location
				cs.lasto = cs.o;
				cs.lastomillis = gamemillis;
				cs.o = newo;
				cs.aim = newaim;
				cs.vel = newvel;
				cs.pitchvel = newpitchvel;
				// crouch
				const bool newcrouching = (f>>5)&1;
				if(cs.crouching != newcrouching){
					cs.crouching = newcrouching;
					cs.crouchmillis = gamemillis - CROUCHTIME + min(gamemillis - cl->state.crouchmillis, CROUCHTIME);
				}
				// scoping
				const bool newscoping = (f>>6)&1;
				if(newscoping != cs.scoping){
					if(!newscoping || (ads_gun(cl->state.gunselect) && ads_classic_allowed(cs.gunselect))){
						cs.scoping = newscoping;
						cs.scopemillis = gamemillis - ADSTIME(cs.perk2 == PERK_TIME) + min(gamemillis - cl->state.scopemillis, ADSTIME(cs.perk2 == PERK_TIME));
					}
					// else
					// clear the scope from the packet?
				}
				// alive block
				if(cs.state==CS_ALIVE)
				{
					// deal damaage
					if(!cs.protect(gamemillis, gamemode, mutators)){
						// medium transfer (falling damage)
						const bool newonfloor = (f>>7)&1, newonladder = (f>>8)&1, newunderwater = newo.z < hdr.waterlevel;
						if((newonfloor || newonladder || newunderwater) && !cs.onfloor){
							if(newonfloor){ // air to solid
								// 4 meters without damage + 2/0.5 HP/meter
								//int damage = ((cs.fallz - newo.z) - 16) * HEALTHSCALE / (cs.perk1 == PERK1_LIGHT ? 8 : 2);
								// 2 meters without damage, then square up to 10^2 = 100 for up to 20m (50m with lightweight)
								int damage = 0;
								if((cs.fallz - newo.z) > 8){
									damage = powf(min<float>((cs.fallz - newo.z - 8) / 4 / (cs.perk1 == PERK1_LIGHT ? 5 : 2), 10), 2.f) * HEALTHSCALE; // 10 * 10 = 100
								}
								if(damage >= 1*HEALTHSCALE){ // don't heal the player
									// maximum damage is 99 for balance purposes
									serverdamage(&cp, &cp, min(damage, 99 * HEALTHSCALE), WEAP_MAX + 2, FRAG_NONE, cs.o);
								}
							}
							else if(newunderwater && (cs.fallz - newo.z) > 32){ // air to liquid, more than 8 meters
								serverdamage(&cp, &cp, 35 * HEALTHSCALE, WEAP_MAX + 9, FRAG_NONE, cs.o); // fixed damage @ 35
							}
							cs.onfloor = true;
						}
						else if(!newonfloor){ // airborne
							if(cs.onfloor || cs.fallz < newo.z) cs.fallz = newo.z;
							cs.onfloor = false;
						}
					}
					// continue if and only if still alive
					if(cs.state!=CS_ALIVE) break;
					// check movement
					checkmove(cp);
				}
				// relay
				cp.position.setsize(0);
 				while(curmsg < p.length()) cp.position.add(p.buf[curmsg++]);
				break;
			}

			case N_NEXTMAP:
			{
				getstring(text, p);
				filtertext(text, text);
				const int mode = getint(p), muts = getint(p);
				if(mapreload || numclients() == 1) resetmap(text, max<const int>(G_DM, mode), muts);
				break;
			}

			case N_MAPC2S:
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
				bool found = mapavailable(text) > 0;
				if((!found || cl->priv >= PRIV_ADMIN) && sendmapserv(sender, text, mapsize, cfgsize, cfgsizegz, &p.buf[p.len]))
				{
					sendf(-1, 1, "ri2s", N_MAPC2S, sender, text);
					logline(ACLOG_INFO,"[%s] %s sent map %s, %d + %d(%d) bytes written",
								gethostname(sender), formatname(clients[sender]), text, mapsize, cfgsize, cfgsizegz);
					// reset
					//loopv(clients) if(i != sender && clients[i]->type == ST_TCPIP) recvmapserv(clients[i]);
					if(!found) resetmap(smapname, smode, smuts);
				}
				else
				{
					logline(ACLOG_INFO,"[%s] %s sent map %s, not written to file",
								gethostname(sender), formatname(clients[sender]), text);
					// could not write
					sendmsg(16);
				}
				p.len += mapsize + cfgsizegz;
				break;
			}

			case N_MAPS2C:
				recvmapserv(cl);
				break;

			case N_DROPFLAG:
			{
				int fl = clienthasflag(sender);
				flagaction(fl, FA_DROP, sender);
				/*
				while(fl >= 0){
					flagaction(fl, FA_DROP, sender);
					fl = clienthasflag(sender);
				}
				*/
				break;
			}

			case N_CLAIMPRIV: // claim
			{
				getstring(text, p);
				pwddetail pd;
				pd.line = -1;
				if(cl->type == ST_LOCAL) setpriv(sender, PRIV_MAX);
				else if(!checkadmin(cl->name, text, cl->salt, &pd) || !pd.priv){
					if(cl->authpriv >= PRIV_MASTER){
						logline(ACLOG_INFO,"[%s] %s was already authed for %s", gethostname(sender), formatname(cl), privname(cl->authpriv));
						setpriv(sender, cl->authpriv);
					}
					else if(cl->priv < PRIV_ADMIN && text){
						disconnect_client(sender, DISC_LOGINFAIL); // avoid brute-force
						return;
					}
				} else {
					setpriv(sender, pd.priv);
					if(pd.line >= 0) logline(ACLOG_INFO,"[%s] %s used %s password in line %d", gethostname(sender), formatname(cl), privname(pd.priv), pd.line);
				}
				break;
			}

			case N_SETPRIV: // relinquish
			{
				setpriv(sender, PRIV_NONE);
				break;
			}

			case N_AUTHREQ:
				getstring(text, p);
				reqauth(sender, text, getint(p));
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
						int mode = getint(p), muts = getint(p);
						modecheck(mode, muts);
						if(mode==G_DEMO) vi->action = new demoplayaction(text);
						else{
							modecheck(mode, muts);
							vi->action = new mapaction(newstring(text), mode, muts, sender);
						}
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
						getstring(text, p);
						filtertext(text, text, 1, 16);
						int m = getint(p), c = getint(p);
						m = clamp(m, 1, 60);
						if(cl->priv < PRIV_ADMIN && m >= 10) m = 10;
						vi->action = new banaction(c, m, text);
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
					case SA_GIVEROLE:
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
						b = clamp(b, -1, MAXBOTBALANCE);
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
						if(cl->priv >= curvote->action->reqpriv && cl->priv >= curvote->action->reqveto) curvote->evaluate(true, vote, sender);
						else sendf(sender, 1, "ri2", N_CALLVOTEERR, VOTEE_VETOPERM);
						break;
					}
					else logline(ACLOG_INFO,"[%s] %s now votes %s", gethostname(sender), formatname(clients[sender]), vote == VOTE_NO ? "no" : "yes");
				}
				else logline(ACLOG_INFO,"[%s] %s voted %s", gethostname(sender), formatname(clients[sender]), vote == VOTE_NO ? "no" : "yes");
				cl->vote = vote;
				sendf(-1, 1, "ri3", N_VOTE, sender, vote);
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

			case N_SOUND: // simple physics related stuff (jump/hit the ground) mostly sounds
			{
				int cn = getint(p), snd = getint(p);
				if(!valid_client(cn)) // fix for them...
					cn = sender;
				if(!hasclient(cl, cn)) break;
				switch(snd){
					case S_NOAMMO:
						if(clients[cn]->state.mag) break;
						// INTENTIONAL FALLTHROUGH
					case S_JUMP:
					case S_SOFTLAND:
					case S_HARDLAND:
						QUEUE_MSG;
						break;
				}
				QUEUE_MSG;
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
					logline(ACLOG_INFO, "[%s] sent unknown extension %s, length %d", gethostname(sender), text, n);
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
			short nextmaptype = 0, nextmaptime = 0, nextmapmode = G_DM, nextmapmuts = G_M_NONE;
			string nextmapnm = "unknown";
			if(*nextmapname){ // map vote
				nextmaptype = 1;
				copystring(nextmapnm, nextmapname);
				nextmapmode = nextgamemode;
				nextmapmuts = nextgamemuts;
			}
			else if(configsets.length()){ // next map rotation
				nextmaptype = 2;
				configset nextmaprot = configsets[nextcfgset(false, true)];
				copystring(nextmapnm, nextmaprot.mapname);
				nextmapmode = nextmaprot.mode;
				nextmapmuts = nextmaprot.muts;
				nextmaptime = nextmaprot.time;
			}
			else{ // no map rotation entries
				nextmaptype = 3;
				copystring(nextmapnm, smapname);
				nextmapmode = smode;
				nextmapmuts = smuts;
			}
			if(nextmaptime < 1) nextmaptime = m_team(nextmapmode, nextmapmuts) ? 15 : 10;
			if(nextmaptype) sendf(-1, 1, "ri6s", N_CONFMSG, 14, nextmaptime, nextmapmode, nextmapmuts, nextmaptype, nextmapnm);
		}
		if(!minremain) sendf(-1, 1, "ri4", N_TIMEUP, gamelimit, gamelimit - 60000 + 1, gamemusicseed); // force intermission
		else sendf(-1, 1, "ri4", N_TIMEUP, gamemillis, gamelimit, gamemusicseed);
	}
	if(!interm && minremain<=0) interm = gamemillis+10000;
	forceintermission = false;
}

void resetserverifempty(){
	loopv(clients) if(clients[i]->type!=ST_EMPTY) return;
	resetserver("", 0, G_M_NONE, 10);
	nextmapname[0] = 0;

	#ifdef STANDALONE
	botbalance = -1;
	#else
	botbalance = 0;
	#endif
	mastermode = MM_OPEN;
	autoteam = true;
	savedlimits.shrink(0);
}

void sendworldstate(){
	static enet_uint32 lastsend = 0;
	if(clients.empty()) return;
	enet_uint32 curtime = enet_time_get()-lastsend;
	if(curtime<25) return; // 40fps
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
	readbotnames(NULL);
}

void loggamestatus(const char *reason){
	int fragscore[TEAM_NUM] = {0}, flagscore[TEAM_NUM] = {0}, pnum[TEAM_NUM] = {0};
	string text;
	formatstring(text)("%d minutes remaining", minremain);
	logline(ACLOG_INFO, "");
	logline(ACLOG_INFO, "Game status: %s on %s, %s, %s%c %s",
					  modestr(gamemode, mutators), smapname, reason ? reason : text, mmfullname(mastermode), custom_servdesc ? ',' : '\0', servdesc_current);
	logline(ACLOG_INFO, "cn  name             %s%sfrag death ping priv    host", m_team(gamemode, mutators) ? "team  " : "", m_affinity(gamemode) ? "flag " : "");
	loopv(clients)
	{
		client &c = *clients[i];
		if(c.type == ST_EMPTY) continue;
		formatstring(text)("%2d%c %-16s ", c.clientnum, c.state.ownernum < 0 ? ' ' : '*', c.name); // cn* name
		if(m_team(gamemode, mutators)) concatformatstring(text, "%-5s ", team_string(c.team)); // team
		if(m_affinity(gamemode)) concatformatstring(text, "%4d ", c.state.flagscore);	 // flag
		concatformatstring(text, "%4d %5d", c.state.frags, c.state.deaths);  // frag death
		logline(ACLOG_INFO, "%s%5d %s %s", text, c.ping,
			c.priv == PRIV_NONE ? "normal " :
			c.priv == PRIV_MASTER ? "master " :
			c.priv == PRIV_ADMIN ? "admin  " :
			c.priv == PRIV_MAX ? "highest" :
			"unknown", gethostname(i));
		flagscore[c.team] += c.state.flagscore;
		fragscore[c.team] += c.state.frags;
		pnum[c.team] += 1;
	}
	if(m_team(gamemode, mutators))
	{
		loopi(TEAM_NUM)
		if(i == TEAM_SPECT)
			{ if(pnum[i]) logline(ACLOG_INFO, "Team SPECT: %d spectators", pnum[i]); }
		else
			logline(ACLOG_INFO, "Team %4s:%3d players,%5d frags%c%5d flags", team_string(i), pnum[i], fragscore[i], m_affinity(gamemode) ? ',' : '\0', flagscore[i]);
	}
	logline(ACLOG_INFO, "");
}

static unsigned char chokelog[MAXCLIENTS + 1] = { 0 };

void linequalitystats(int elapsed)
{
    static unsigned int chokes[MAXCLIENTS + 1] = { 0 }, spent[MAXCLIENTS + 1] = { 0 }, chokes_raw[MAXCLIENTS + 1] = { 0 }, spent_raw[MAXCLIENTS + 1] = { 0 };
    if(elapsed)
    { // collect data
        int c1 = 0, c2 = 0, r1 = 0, numc = 0;
        loopv(clients)
        {
            client &c = *clients[i];
            if(c.type != ST_TCPIP) continue;
            numc++;
            enet_uint32 &rtt = c.peer->lastRoundTripTime, &throttle = c.peer->packetThrottle;
            if(rtt < c.bottomRTT + c.bottomRTT / 3)
            {
                if(servmillis - c.connectmillis < 5000)
                    c.bottomRTT = rtt;
                else
                    c.bottomRTT = (c.bottomRTT * 15 + rtt) / 16; // simple IIR
            }
            if(throttle < 22) c1++;
            if(throttle < 11) c2++;
            if(rtt > c.bottomRTT * 2 && rtt - c.bottomRTT > 300) r1++;
        }
        spent_raw[numc] += elapsed;
        int t = numc < 7 ? numc : (numc + 1) / 2 + 3;
        chokes_raw[numc] +=  ((c1 >= t ? c1 + c2 : 0) + (r1 >= t ? r1 : 0)) * elapsed;
    }
    else
    { // calculate compressed statistics
        defformatstring(msg)("Uplink quality [ ");
        int ncs = 0;
        loopj(scl.maxclients)
        {
            int i = j + 1;
            int nc = chokes_raw[i] / 1000 / i;
            chokes[i] += nc;
            ncs += nc;
            spent[i] += spent_raw[i] / 1000;
            chokes_raw[i] = spent_raw[i] = 0;
            int s = 0, c = 0;
            if(spent[i])
            {
                frexp((double)spent[i] / 30, &s);
                if(s < 0) s = 0;
                if(s > 15) s = 15;
                if(chokes[i])
                {
                    frexp(((double)chokes[i]) / spent[i], &c);
                    c = 15 + c;
                    if(c < 0) c = 0;
                    if(c > 15) c = 15;
                }
            }
            chokelog[i] = (s << 4) + c;
            concatformatstring(msg, "%02X ", chokelog[i]);
        }
        logline(ACLOG_DEBUG, "%s] +%d", msg, ncs);
    }
}

int lastmillis = 0, totalmillis = 0;

void serverslice(uint timeout)   // main server update, called from cube main loop in sp, or dedicated server loop
{
	static int pnum = 0, psend = 0, prec = 0;
#ifdef STANDALONE
	int nextmillis = (int)enet_time_get();
	if(svcctrl) svcctrl->keepalive();
#else
	int nextmillis = isdedicated ? (int)enet_time_get() : lastmillis;
#endif
	int diff = nextmillis - servmillis;
	gamemillis += diff;
	servmillis = nextmillis;

	if(m_demo(gamemode)) readdemo();

	if(minremain>0)
	{
		processevents();
		checkitemspawns(diff);
		bool ktfflagingame = false;
		if(m_affinity(gamemode)) loopi(2)
		{
			sflaginfo &f = sflaginfos[i];
			if(f.state == CTFF_DROPPED && gamemillis-f.lastupdate > (m_capture(gamemode) ? 30000 : (m_ktf2(gamemode, mutators) || m_bomber(gamemode)) ? 20000 : 10000)) flagaction(i, FA_RESET, -1);
			if(m_hunt(gamemode) && f.state == CTFF_INBASE && gamemillis-f.lastupdate > (smapstats.hasflags ? 10000 : 1000))
				htf_forceflag(i);
			if(m_keep(gamemode) && f.state == CTFF_STOLEN && gamemillis-f.lastupdate > 15000)
				flagaction(i, FA_SCORE, -1);
			if(f.state == CTFF_INBASE || f.state == CTFF_STOLEN) ktfflagingame = true;
		}
		if(m_keep(gamemode) && !m_ktf2(gamemode, mutators) && !ktfflagingame) flagaction(rnd(2), FA_RESET, -1); // ktf flag watchdog
		if(m_duke(gamemode, mutators)) arenacheck();
		if(scl.afktimelimit && mastermode == MM_OPEN && next_afk_check < servmillis && gamemillis > 20000 ) check_afk();
	}

	if(curvote)
	{
		if(!curvote->isalive()) curvote->evaluate(true);
		if(curvote->result!=VOTE_NEUTRAL) DELETEP(curvote);
	}

	int nonlocalclients = numnonlocalclients();

	if(forceintermission || (m_valid(smode) && !m_edit(gamemode) && gamemillis-diff>0 && gamemillis/60000!=(gamemillis-diff)/60000))
		checkintermission();
	if(interm && gamemillis>interm)
	{
		loggamestatus("game finished");
		if(demorecord) enddemorecord();
		interm = 0;

		//start next game
		if(*nextmapname) resetmap(nextmapname, nextgamemode, nextgamemuts);
		else if(configsets.length()) nextcfgset();
		else if(isdedicated && *smapname) resetmap(smapname, smode, smuts);
		else{
			loopv(clients) if(clients[i]->type!=ST_EMPTY){
				sendf(i, 1, "ri2", N_NEXTMAP, 0);	// ask a client for the next map
				mapreload = true;
				break;
			}
		}
	}

	resetserverifempty();

	if(!isdedicated) return;	 // below is network only

	serverms(smode, smuts, numclients(), gamelimit-gamemillis, smapname, servmillis, serverhost->address, pnum, psend, prec, PROTOCOL_VERSION);

	if(autobalance && m_team(gamemode, mutators) && !m_zombie(gamemode) && !m_duke(gamemode, mutators) && !interm && servmillis - lastfillup > 5000 && refillteams())
		lastfillup = servmillis;

	loopv(clients) if(valid_client(i) && (!clients[i]->connected || clients[i]->connectauth) && clients[i]->connectmillis + 10000 <= servmillis) disconnect_client(i, DISC_TIMEOUT);

	static unsigned int lastThrottleEpoch = 0;
    if(serverhost->bandwidthThrottleEpoch != lastThrottleEpoch)
    {
        if(lastThrottleEpoch) linequalitystats(serverhost->bandwidthThrottleEpoch - lastThrottleEpoch);
        lastThrottleEpoch = serverhost->bandwidthThrottleEpoch;
    }

	if(servmillis>nextstatus)   // display bandwidth stats, useful for server ops
	{
		nextstatus = servmillis + 60 * 1000;
		rereadcfgs();
		if(nonlocalclients || serverhost->totalSentData || serverhost->totalReceivedData)
		{
			if(nonlocalclients) loggamestatus(NULL);
			logline(ACLOG_INFO, "Status at %s: %d remote client%s, %.1f send, %.1f rec (KiB/s); %d ping%s: %d sent %d recieved", timestring(true, "%d-%m-%Y %H:%M:%S"), nonlocalclients, nonlocalclients==1?"":"s", serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024, pnum, pnum==1?"":"s", psend, prec);
			pnum = psend = prec = 0;
			linequalitystats(0);
		}
		serverhost->totalSentData = serverhost->totalReceivedData = 0;
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
				copystring(c.hostname, (!enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))) ? hn : "unknown");
				logline(ACLOG_INFO,"[%s] client #%d connected", c.hostname, c.clientnum);
				sendservinfo(c);
				break;
			}

			case ENET_EVENT_TYPE_RECEIVE:
			{
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
	if(*scl.serverpassword)
		flags |= PONGFLAG_PASSWORD;
	loopv(bans) if(bans[i].host == ip) { flags |= 1 << PONGFLAG_BANNED; break; }
	if(checkipblacklist(ip))
		flags |= 1 << PONGFLAG_BLACKLIST;
	if(checkmutelist(ip))
		flags |= 1 << PONGFLAG_MUTE;
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

void extping_uplinkstats(ucharbuf &po)
{
	po.put(chokelog, scl.maxclients); // send logs for every used slot
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

void extinfo_statsbuf(ucharbuf &p, int pid, int bpos, ENetSocket &pongsock, ENetAddress &addr, ENetBuffer &buf, int len, int &psend){
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
		psend += buf.dataLength;

		if(pid>-1) break;
		p.len=bpos;
	}
}

void extinfo_teamscorebuf(ucharbuf &p){
	putint(p, m_team(gamemode, mutators) ? EXT_ERROR_NONE : EXT_ERROR);
	putint(p, gamemode);
	putint(p, minremain);
	if(!m_team(gamemode, mutators)) return;

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
		if(m_affinity(gamemode)) //when capture mode
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

	if(dedicated && !initlogging(&scl))
		printf("WARNING: logging not started!\n");
	logline(ACLOG_INFO, "logging local AssaultCube server (version %d, protocol %d/%d) now..", AC_VERSION, PROTOCOL_VERSION, EXT_VERSION);

	copystring(servdesc_current, scl.servdesc_full);
	servermsinit(scl.master ? scl.master : AC_MASTER_URI, scl.ip, scl.serverport + CUBE_SERVINFO_OFFSET, dedicated);

	if((isdedicated = dedicated))
	{
		ENetAddress address = { ENET_HOST_ANY, scl.serverport };
		if(scl.ip[0] && enet_address_set_host(&address, scl.ip)<0) logline(ACLOG_WARNING, "server ip not resolved!");
		serverhost = enet_host_create(&address, scl.maxclients+4, 3, 0, scl.uprate);
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
	readbotnames(scl.botfile);

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
	defformatstring(out)("ACR fatal error: %s", msg);
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
