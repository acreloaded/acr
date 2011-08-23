// client.cpp, mostly network related client game code

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

// Global Noob-Switch
VARP(noob, 0, 0, 1);

VAR(connected, 1, 0, 0);

ENetHost *clienthost = NULL;
ENetPeer *curpeer = NULL, *connpeer = NULL;
int connmillis = 0, connattempts = 0, discmillis = 0;
bool watchingdemo = false;		  // flowtron : enables N_ITEMLIST in demos - req. because mapchanged == false by then

int getclientnum() { return player1 ? player1->clientnum : -1; }

bool multiplayer(bool msg)
{
	// check not correct on listen server?
	if(curpeer && msg) conoutf("operation not available in multiplayer");
	return curpeer!=NULL;
}

VAR(edithack, 0, 0, 1); // USE AT YOUR OWN RISK
bool allowedittoggle(){
	bool allow = edithack || !curpeer || m_edit;
	if(!allow) conoutf("editing in multiplayer requires coopedit mode (/coop or /mode 1)");
	return allow;
}

void throttle();

VARF(throttle_interval, 0, 5, 30, throttle());
VARF(throttle_accel,	0, 2, 32, throttle());
VARF(throttle_decel,	0, 2, 32, throttle());

void throttle()
{
	if(!curpeer) return;
	ASSERT(ENET_PEER_PACKET_THROTTLE_SCALE==32);
	enet_peer_throttle_configure(curpeer, throttle_interval*1000, throttle_accel, throttle_decel);
}

string clientpassword = "";

void abortconnect()
{
	if(!connpeer) return;
	clientpassword[0] = '\0';
	if(connpeer->state!=ENET_PEER_STATE_DISCONNECTED) enet_peer_reset(connpeer);
	connpeer = NULL;
#if 0
	if(!curpeer)
	{
		enet_host_destroy(clienthost);
		clienthost = NULL;
	}
#endif
}

void connectserv_(const char *servername, const char *serverport = NULL, const char *password = NULL, int role = PRIV_NONE)
{
	extern void enddemoplayback();
	if(watchingdemo) enddemoplayback();
	if(connpeer)
	{
		conoutf("aborting connection attempt");
		abortconnect();
	}

	copystring(clientpassword, password ? password : "");

	ENetAddress address;
	int p = 0;
	if(serverport && serverport[0]) p = atoi(serverport);
	address.port = p > 0 ? p : CUBE_DEFAULT_SERVER_PORT;

	if(servername)
	{
		addserver(servername, serverport, "0");
		conoutf("attempting to connect to %s%c%s", servername, address.port != CUBE_DEFAULT_SERVER_PORT ? ':' : 0, serverport);
		if(!resolverwait(servername, &address))
		{
			conoutf("\f3could not resolve server %s", servername);
			clientpassword[0] = '\0';
			return;
		}
	}
	else
	{
		conoutf("attempting to connect over LAN");
		address.host = ENET_HOST_BROADCAST;
	}

	if(!clienthost) clienthost = enet_host_create(NULL, 2, 0, 0);

	if(clienthost)
	{
		connpeer = enet_host_connect(clienthost, &address, 3);
		enet_host_flush(clienthost);
		connmillis = totalmillis;
		connattempts = 0;
		if(!m_fight(gamemode)) gamemode = GMODE_TEAMDEATHMATCH;
	}
	else
	{
		conoutf("\f3could not connect to server");
		clientpassword[0] = '\0';
	}
}

void connectserv(char *servername, char *serverport, char *password)
{
	connectserv_(servername, serverport, password);
}

void connectadmin(char *servername, char *serverport, char *password)
{
	if(!password[0]) return;
	connectserv_(servername, serverport, password, PRIV_MAX);
}

void lanconnect()
{
	connectserv_(NULL);
}

void disconnect(int onlyclean, int async)
{
	bool cleanup = onlyclean!=0;
	if(curpeer)
	{
		if(!discmillis)
		{
			enet_peer_disconnect(curpeer, DISC_NONE);
			enet_host_flush(clienthost);
			discmillis = totalmillis;
		}
		if(curpeer->state!=ENET_PEER_STATE_DISCONNECTED)
		{
			if(async) return;
			enet_peer_reset(curpeer);
		}
		curpeer = NULL;
		discmillis = 0;
		connected = 0;
		conoutf("disconnected");
		cleanup = true;
	}

	if(cleanup)
	{
		player1->clientnum = -1;
		player1->lifesequence = 0;
		player1->priv = PRIV_NONE;
		loopv(players) zapplayer(players[i]);
		clearvote();
		clearworldsounds(false);
		localdisconnect();
	}
#if 0
	if(!connpeer && clienthost)
	{
		enet_host_destroy(clienthost);
		clienthost = NULL;
	}
#endif
	if(!onlyclean) localconnect();
}

void trydisconnect()
{
	if(connpeer)
	{
		conoutf("aborting connection attempt");
		abortconnect();
		return;
	}
	if(!curpeer)
	{
		conoutf("not connected");
		return;
	}
	conoutf("attempting to disconnect...");
	disconnect(0, !discmillis);
}

VARP(hudchat, 0, 1, 1);
void saytext(playerent *d, char *text, int flags, int sound){
	string nametag; copystring(nametag, colorname(d));
	if(flags & SAY_TEAM) formatstring(nametag)("%s \f5(\f%d%s\f5)", nametag, d->team ? 1 : 3, team_string(d->team));
	if(sound > S_MAINEND && sound < S_NULL){
		d->addicon(eventicon::VOICECOM);
		playsound(sound, SP_HIGH);
	} else sound = 0;
	int textcolor = 0; // normal text
	if(flags&SAY_TEAM) textcolor = isteam(d, player1) ? 1 : 3; // friendly blue, enemy red
	if(flags&SAY_DENY){
		textcolor = 2; // denied yellow
		concatstring(text, " \f3Do not SPAM!");
	}
	string textout;
	const int col = d == player1 ? 1 : m_team ? d->team == player1->team ? 0 : 3 : 5;
	if(flags & SAY_ACTION) formatstring(textout)("\f5* \f%d%s \f6(%d)", col, d->name, d->clientnum);
	else formatstring(textout)("\f5<\f%d%s \f6(%d)\f5>", col, d->name, d->clientnum);
	if(sound) formatstring(textout)("%s \f4[\f6%d\f4]", textout, sound);
	formatstring(textout)("%s \f%d%s", textout, textcolor, text);
	(flags&SAY_DENY ? conoutf : chatoutf)("%s", textout);
}

void toserver(char *text, int voice, bool action){
	if(!text) return;
	bool toteam = *text == '%' && m_team && strlen(text) > 1;
	if(*text == '%') text++;
	addmsg(N_TEXT, "ris", (voice & 0x1F) | (((action ? SAY_ACTION : 0) | (toteam ? SAY_TEAM : 0)) << 5), text);
}

void toserver_(char *text){ toserver(text); }
void toserver_voice(char *text){
	extern int findvoice();
	int s = findvoice();
	string t;
	*t = t[1] = 0;
	if(s <= S_VOICETEAMEND) *t = '%';
	concatstring(t, text);
	toserver(t, s - S_MAINEND);
}
void toserver_me(char *text){ toserver(text, 0, true); }

COMMANDN(say, toserver_, ARG_CONC);
COMMANDN(sayvoice, toserver_voice, ARG_2STR);
COMMANDN(me, toserver_me, ARG_CONC);

void echo(char *text) { conoutf("%s", text); }

COMMAND(echo, ARG_CONC);
COMMANDN(connect, connectserv, ARG_3STR);
COMMAND(connectadmin, ARG_3STR);
COMMAND(lanconnect, ARG_NONE);
COMMANDN(disconnect, trydisconnect, ARG_NONE);

void cleanupclient()
{
	abortconnect();
	disconnect(1);
	if(clienthost)
	{
		enet_host_destroy(clienthost);
		clienthost = NULL;
	}
}

// collect c2s messages conveniently

vector<uchar> messages;
bool messagereliable = false;

void addmsg(int type, const char *fmt, ...)
{
	static uchar buf[MAXTRANS];
	ucharbuf p(buf, MAXTRANS);
	putint(p, type);
	bool reliable = false;
	if(fmt)
	{
		va_list args;
		va_start(args, fmt);
		while(*fmt) switch(*fmt++)
		{
			case 'r': reliable = true; break;
			/*case 'v':
			{
				int n = va_arg(args, int);
				int *v = va_arg(args, int *);
				loopi(n) putint(p, v[i]);
				break;
			}*/
			case 'i':
			{
				int n = isdigit(*fmt) ? *fmt++-'0' : 1;
				loopi(n) putint(p, va_arg(args, int));
				break;
			}
			case 'f':
			{
				int n = isdigit(*fmt) ? *fmt++-'0' : 1;
				loopi(n) putfloat(p, (float)va_arg(args, double));
				break;
			}
			case 's': sendstring(va_arg(args, const char *), p); break;
		}
		va_end(args);
	}
	if(reliable) messagereliable = true;
	loopi(p.length()) messages.add(buf[i]);
}

static int lastping = 0;
bool sendmapident = false;

void sendpackettoserv(int chan, ENetPacket *packet)
{
	if(curpeer) enet_peer_send(curpeer, chan, packet);
	else localclienttoserver(chan, packet);
	if(!packet->referenceCount) enet_packet_destroy(packet);
}

void c2skeepalive()
{
	if(clienthost && (curpeer || connpeer)) enet_host_service(clienthost, NULL, 0);
}

void sendposition(playerent *d){
	if(d->state != CS_ALIVE && d->state != CS_EDITING) return;
	ENetPacket *packet = enet_packet_create(NULL, 100, 0);
	ucharbuf q(packet->data, packet->dataLength);

	putint(q, N_POS);
	putint(q, d->clientnum);
	putfloat(q, d->o.x);
	putfloat(q, d->o.y);
	putfloat(q, d->o.z); // not subtracting the eyeheight
	putfloat(q, d->yaw);
	putfloat(q, d->pitch);
	putfloat(q, d->roll);
	putfloat(q, d->vel.x);
	putfloat(q, d->vel.y);
	putfloat(q, d->vel.z);
	putfloat(q, d->pitchvel);
	// pack rest in 1 int: strafe:2, move:2, onfloor:1, onladder: 1
	putuint(q, (d->strafe&3) | ((d->move&3)<<2) | (((int)d->onfloor)<<4) | (((int)d->onladder)<<5) | ((d->lifesequence&1)<<6) | (((int)d->crouching)<<7));

	enet_packet_resize(packet, q.length());
	sendpackettoserv(0, packet);
}

void sendpositions(){
	sendposition(player1);
	loopv(players){
		playerent *p = players[i];
		if(p && p->ownernum == getclientnum()) sendposition(p);
	}
}

void sendmessages(){
	ENetPacket *packet = enet_packet_create (NULL, MAXTRANS, 0);
	ucharbuf p(packet->data, packet->dataLength);
	if(sendmapident)
	{
		if(!curpeer) spawnallitems();
		messagereliable = true;
		putint(p, N_MAPIDENT);
		putint(p, maploaded);
		sendmapident = false;
	}
	if(messages.length())
	{
		p.put(messages.getbuf(), messages.length());
		messages.setsize(0);
	}
	if(totalmillis-lastping>250)
	{
		putint(p, N_PINGPONG);
		putint(p, totalmillis);
		lastping = totalmillis;
	}
	if(messagereliable) packet->flags |= ENET_PACKET_FLAG_RELIABLE;
	messagereliable = false;
	if(!p.length()) enet_packet_destroy(packet);
	else
	{
		enet_packet_resize(packet, p.length());
		sendpackettoserv(1, packet);
	}
}

void c2sinfo(bool force){				  // send update to the server
	static int lastupdate = -1000;
	if(!force && totalmillis-lastupdate<40) return;	// don't update faster than 25fps
	lastupdate = totalmillis;
	sendpositions();
	sendmessages();
	if(clienthost) enet_host_flush(clienthost);
}

int authtoken = -1;

void tryauth(){
	authtoken = rand();
	addmsg(N_AUTHREQ, "ri", authtoken);
}
COMMANDN(auth, tryauth, ARG_NONE);

VARP(connectauth, 0, 0, 1);

void sendintro()
{
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_CONNECT);
	sendstring(player1->name, p);
	putint(p, player1->skin);
	extern int level;
	putint(p, level);
	sendstring(genpwdhash(player1->name, clientpassword, sessionid), p);
	if(connectauth){
		authtoken = rand();
		if(!authtoken) authtoken = 1;
		putint(p, authtoken);
	}
	else putint(p, 0);
	*clientpassword = 0;
	putint(p, player1->nextprimweap->type);
	putint(p, AC_VERSION);
	putint(p, 
			#ifdef WIN32
				0x40 |
			#endif
			#ifdef __APPLE__
				0x20 |
				#endif
			#ifdef _DEBUG
				0x08 |
			#endif
			#ifdef __GNUC__
				0x04 |
			#endif
				1);
	// other post-connect stuff goes here
	enet_packet_resize(packet, p.length());
	sendpackettoserv(1, packet);
}

void gets2c()		   // get updates from the server
{
	ENetEvent event;
	if(!clienthost || (!curpeer && !connpeer)) return;
	if(connpeer && totalmillis/3000 > connmillis/3000)
	{
		conoutf("attempting to connect...");
		connmillis = totalmillis;
		++connattempts;
		if(connattempts > 3)
		{
			conoutf("\f3could not connect to server");
			abortconnect();
			return;
		}
	}
	while(clienthost!=NULL && enet_host_service(clienthost, &event, 0)>0)
	switch(event.type)
	{
		case ENET_EVENT_TYPE_CONNECT:
			disconnect(1);
			curpeer = connpeer;
			connpeer = NULL;
			connected = 1;
			conoutf("connected to server");
			throttle();
			if(editmode) toggleedit(true);
			break;

		case ENET_EVENT_TYPE_RECEIVE:
		{
			extern packetqueue pktlogger;
			pktlogger.queue(event.packet);

			if(discmillis) conoutf("attempting to disconnect...");
			else servertoclient(event.channelID, event.packet->data, (int)event.packet->dataLength);
			// destroyed in logger
			//enet_packet_destroy(event.packet);
			break;
		}

		case ENET_EVENT_TYPE_DISCONNECT:
		{
			if(event.data>=DISC_NUM) event.data = DISC_NONE;
			if(event.peer==connpeer)
			{
				conoutf("\f3could not connect to server");
				abortconnect();
			}
			else
			{
				if(!discmillis || event.data) conoutf("\f3server network error, disconnecting (%s) ...", disc_reason(event.data));
				disconnect();
			}
			return;
		}

		default:
			break;
	}
}

// sendmap/getmap commands, should be replaced by more intuitive map downloading

cvector securemaps;

void resetsecuremaps() { securemaps.deletearrays(); }
void securemap(char *map) { if(map) securemaps.add(newstring(map)); }
bool securemapcheck(char *map, bool msg)
{
	if(strstr(map, "maps/")==map || strstr(map, "maps\\")==map) map += strlen("maps/");
	loopv(securemaps) if(!strcmp(securemaps[i], map))
	{
		if(msg) conoutf("\f3map %s is secured, you can not send, receive or overwrite it", map);
		return true;
	}
	return false;
}


void sendmap(char *mapname)
{
	if(*mapname && gamemode==1)
	{
		save_world(mapname);
		changemap(mapname); // FIXME!!
	}
	else mapname = getclientmap();
	if(securemapcheck(mapname)) return;

	int mapsize, cfgsize, cfgsizegz;
	uchar *mapdata = readmap(path(mapname), &mapsize);
	uchar *cfgdata = readmcfggz(path(mapname), &cfgsize, &cfgsizegz);
	if(!mapdata) return;
	if(!cfgdata) { cfgsize = 0; cfgsizegz = 0; }

	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS + mapsize + cfgsizegz, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);

	putint(p, N_SENDMAP);
	sendstring(mapname, p);
	putint(p, mapsize);
	putint(p, cfgsize);
	putint(p, cfgsizegz);
	if(MAXMAPSENDSIZE - p.length() < mapsize + cfgsizegz || cfgsize > MAXCFGFILESIZE)
	{
		conoutf("map %s is too large to send", mapname);
		delete[] mapdata;
		if(cfgsize) delete[] cfgdata;
		enet_packet_destroy(packet);
		return;
	}
	p.put(mapdata, mapsize);
	delete[] mapdata;
	if(cfgsizegz)
	{
		p.put(cfgdata, cfgsizegz);
		delete[] cfgdata;
	}

	enet_packet_resize(packet, p.length());
	sendpackettoserv(2, packet);
	conoutf("sending map %s to server...", mapname);
}

void getmap()
{
	conoutf("requesting map from server...");
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, N_RECVMAP);
	enet_packet_resize(packet, p.length());
	sendpackettoserv(2, packet);
}

void getdemo(int i)
{
	if(i<=0) conoutf("getting demo...");
	else conoutf("getting demo %d...", i);
	addmsg(N_DEMO, "ri", i);
}

void listdemos()
{
	conoutf("listing demos...");
	addmsg(N_LISTDEMOS, "r");
}

COMMAND(sendmap, ARG_1STR);
COMMAND(getmap, ARG_NONE);
COMMAND(resetsecuremaps, ARG_NONE);
COMMAND(securemap, ARG_1STR);
COMMAND(getdemo, ARG_1INT);
COMMAND(listdemos, ARG_NONE);
