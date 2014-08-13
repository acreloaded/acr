// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "cube.h"

#define DEBUGCOND (true)

#include "server.h"
#include "servercontroller.h"
#include "serverfiles.h"
// 2011feb05:ft: quitproc
#include "signal.h"
// config
servercontroller *svcctrl = NULL;
servercommandline scl;
servermaprot maprot;
serveripblacklist ipblacklist;
servernickblacklist nickblacklist;
serverforbiddenlist forbiddenlist;
serverpasswords passwords;
serverinfofile infofiles;

// server state
bool isdedicated = false;
ENetHost *serverhost = NULL;

int nextstatus = 0, servmillis = 0, lastfillup = 0;

vector<client *> clients;
vector<worldstate *> worldstates;
vector<savedscore> savedscores;
vector<savedlimit> savedlimits;
vector<ban> bans;
vector<demofile> demofiles;

int mastermode = MM_OPEN, botbalance = -1, progressiveround = 1, zombiebalance = 1, zombiesremain = 1;
static bool autoteam = true;
#define autobalance_mode (!m_zombie(gamemode) && !m_convert(gamemode, mutators))
#define autobalance (autoteam && autobalance_mode)
int matchteamsize = 0;

long int incoming_size = 0;

static bool forceintermission = false, nokills = false;

string servdesc_current;
ENetAddress servdesc_caller;
bool custom_servdesc = false;

// current game
string smapname, nextmapname;
int smode = G_DM, nextgamemode, smuts = G_M_TEAM, nextmutators;
int interm = 0;
static int minremain = 0, gamemillis = 0, gamelimit = 0, /*lmsitemtype = 0,*/ nextsendscore = 0;
mapstats smapstats;
vector<entity> sents;
ssqr *maplayout = NULL, *testlayout = NULL;
int maplayout_factor, testlayout_factor, maplayoutssize;
persistent_entity *mapents = NULL;
servermapbuffer mapbuffer;

vector<sconfirm> sconfirms;
int confirmseq = 0;
vector<sknife> sknives;
int knifeseq = 0;
void purgesconfirms()
{
    loopv(sconfirms)
        sendf(NULL, 1, "ri2", SV_CONFIRMREMOVE, sconfirms[i].id);
}
void purgesknives()
{
    loopv(sknives)
        sendf(NULL, 1, "ri2", SV_KNIFEREMOVE, sknives[i].id);
    sknives.setsize(0);
}

// cmod
int totalclients = 0;
int servertime = 0, serverlagged = 0;

#include "serverworld.h"

bool valid_client(int cn)
{
    return clients.inrange(cn) && clients[cn]->type != ST_EMPTY;
}

const char *client::formatname()
{
    static string cname[3];
    static int idx = 0;
    if (idx >= 3) idx %= 3;
    if (type == ST_AI) formatstring(cname[idx])("%s [%d-%d]", name, clientnum, ownernum);
    else formatstring(cname[idx])("%s (%d)", name, clientnum);
    return cname[idx++];
}

const char *client::gethostname()
{
    if (ownernum < 0)
        return hostname;
    return clients[ownernum]->hostname;
}

bool client::hasclient(int cn)
{
    if(!valid_client(cn)) return false;
    return clientnum == cn || clients[cn]->ownernum == clientnum;
}

void client::removeexplosives()
{
    state.grenades.reset(); // remove active/flying nades
    state.knives.reset(); // remove active/flying knives (usually useless, since knives are fast)
}

void client::cheat(const char *reason)
{
    logline(ACLOG_INFO, "[%s] %s cheat detected (%s)", this->gethostname(), this->formatname(), reason);
    defformatstring(cheatstr)("\f2%s \fs\f6(%d) \f3cheat detected \f4(%s)", this->name, this->clientnum, reason);
    sendservmsg(cheatstr);
    this->suicide(this->type == ST_AI ? OBIT_BOT : OBIT_CHEAT, FRAG_GIB);
}

void clientstate::addwound(int owner, const vec &woundloc)
{
    wound &w = wounds.length() >= 8 ? wounds[0] : wounds.add();
    w.inflictor = owner;
    w.lastdealt = gamemillis;
    w.offset = woundloc;
    w.offset.sub(o);
}

void cleanworldstate(ENetPacket *packet)
{
   loopv(worldstates)
   {
       worldstate *ws = worldstates[i];
       if(ws->positions.inbuf(packet->data) || ws->messages.inbuf(packet->data)) ws->uses--;
       else continue;
       if(!ws->uses)
       {
           delete ws;
           worldstates.remove(i);
       }
       break;
   }
}

void sendpacket(client *cl, int chan, ENetPacket *packet, int exclude, bool demopacket)
{
    // fix exclude
    if(valid_client(exclude) && clients[exclude]->type == ST_AI)
        exclude = clients[exclude]->ownernum;
    if(!cl)
    {
        // broadcast
        recordpacket(chan, packet->data, (int)packet->dataLength);
        loopv(clients)
            if(i!=exclude && clients[i]->type != ST_AI && (clients[i]->type!=ST_TCPIP || clients[i]->isauthed))
                sendpacket(clients[i], chan, packet, -1, demopacket);
        return;
    }
    if(cl->type == ST_AI)
    {
        // reroute packets
        if (!valid_client(cl->ownernum) || cl->ownernum == exclude || clients[cl->ownernum]->type == ST_AI)
            return;
        cl = clients[cl->ownernum];
    }
    switch(cl->type)
    {
        case ST_TCPIP:
        {
            enet_peer_send(cl->peer, chan, packet);
            break;
        }

        case ST_LOCAL:
            localservertoclient(chan, packet->data, (int)packet->dataLength, demopacket);
            break;
    }
}

static bool reliablemessages = false;

bool buildworldstate()
{
    static struct { int posoff, poslen, msgoff, msglen; } pkt[MAXCLIENTS];
    worldstate &ws = *new worldstate;
    loopvj(clients)
    {
        if(clients[j]->type!=ST_TCPIP || !clients[j]->isauthed) continue;
        pkt[j].posoff = ws.positions.length();
        pkt[j].msgoff = ws.messages.length();
        loopv(clients)
        {
            client &c = *clients[i];
            if(i != j && (c.type!=ST_AI || c.ownernum != j)) continue;
            c.overflow = 0;
            if(!c.position.empty())
            {
                ws.positions.put(c.position.getbuf(), c.position.length());
                c.position.setsize(0);
            }
            if(!c.messages.empty())
            {
                putint(ws.messages, SV_CLIENT);
                putint(ws.messages, i);
                ws.messages.put(c.messages.getbuf(), c.messages.length());
                c.messages.setsize(0);
            }
        }
        pkt[j].poslen = ws.positions.length() - pkt[j].posoff;
        pkt[j].msglen = ws.messages.length() - pkt[j].msgoff;
    }
    int psize = ws.positions.length(), msize = ws.messages.length();
    if(psize)
    {
        recordpacket(0, ws.positions.getbuf(), psize);
        ucharbuf p = ws.positions.reserve(psize);
        p.put(ws.positions.getbuf(), psize);
        ws.positions.addbuf(p);
    }
    if(msize)
    {
        recordpacket(1, ws.messages.getbuf(), msize);
        ucharbuf p = ws.messages.reserve(msize);
        p.put(ws.messages.getbuf(), msize);
        ws.messages.addbuf(p);
    }
    ws.uses = 0;
    if(psize || msize)
        loopv(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP || !c.isauthed) continue;
        ENetPacket *packet;
        if(psize && psize>pkt[i].poslen)
        {
            packet = enet_packet_create(&ws.positions[!pkt[i].poslen ? 0 : pkt[i].posoff+pkt[i].poslen],
                                        psize-pkt[i].poslen,
                                        ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(&c, 0, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            else { ++ws.uses; packet->freeCallback = cleanworldstate; }
        }

        if(msize && msize>pkt[i].msglen)
        {
            packet = enet_packet_create(&ws.messages[!pkt[i].msglen ? 0 : pkt[i].msgoff+pkt[i].msglen],
                                        msize-pkt[i].msglen,
                                        (reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0) | ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(&c, 1, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            else { ++ws.uses; packet->freeCallback = cleanworldstate; }
        }
    }
    reliablemessages = false;
    if(!ws.uses)
    {
        delete &ws;
        return false;
    }
    else
    {
        worldstates.add(&ws);
        return true;
    }
}

int countclients(int type, bool exclude = false)
{
    int num = 0;
    loopv(clients) if((clients[i]->type!=type)==exclude) num++;
    return num;
}

int numclients() { return countclients(ST_EMPTY, true); }
int numlocalclients() { return countclients(ST_LOCAL); }
int numnonlocalclients() { return countclients(ST_TCPIP); }

int numauthedclients()
{
    int num = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed) num++;
    return num;
}

int numactiveclients()
{
    int num = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed && clients[i]->isonrightmap && team_isactive(clients[i]->team)) num++;
    return num;
}

int *numteamclients(int exclude = -1, bool include_bots = false)
{
    static int num[TEAM_NUM];
    loopi(TEAM_NUM) num[i] = 0;
    loopv(clients) if(i != exclude && clients[i]->type!=ST_EMPTY && (include_bots || clients[i]->type!=ST_AI) && clients[i]->isauthed && clients[i]->isonrightmap && team_isvalid(clients[i]->team)) num[clients[i]->team]++;
    return num;
}

int sendservermode(bool send = true)
{
    int sm = (autoteam & 1) | ((mastermode & MM_MASK) << 2) | (matchteamsize << 4);
    if(send) sendf(NULL, 1, "ri2", SV_SERVERMODE, sm);
    return sm;
}

void changematchteamsize(int newteamsize)
{
    if(newteamsize < 0) return;
    if(matchteamsize != newteamsize)
    {
        matchteamsize = newteamsize;
        sendservermode();
    }
    if(mastermode == MM_MATCH && matchteamsize && m_team(gamemode, mutators))
    {
        int size[2] = { 0 };
        loopv(clients)
        {
            client &cl = *clients[i];
            if (cl.type != ST_EMPTY && cl.isauthed && cl.isonrightmap && team_isactive(cl.team))
            {
                if (++size[cl.team] > matchteamsize) updateclientteam(cl, team_tospec(cl.team), FTR_SILENT);
            }
        }
    }
}

void changemastermode(int newmode)
{
    if(mastermode != newmode)
    {
        mastermode = newmode;
        senddisconnectedscores();
        if(mastermode != MM_MATCH)
        {
            loopv(clients)
            {
                client &cl = *clients[i];
                if (cl.type != ST_EMPTY && cl.isauthed && (cl.team == TEAM_CLA_SPECT || cl.team == TEAM_RVSF_SPECT))
                    updateclientteam(cl, TEAM_SPECT, FTR_SILENT);
            }
        }
        else if(matchteamsize) changematchteamsize(matchteamsize);
    sendservermode();
    }
}

savedscore *findscore(client &c, bool insert)
{
    if(c.type!=ST_TCPIP) return NULL;
    enet_uint32 mask = ENET_HOST_TO_NET_32(mastermode == MM_MATCH ? 0xFFFF0000 : 0xFFFFFFFF); // in match mode, reconnecting from /16 subnet is allowed
    if(!insert)
    {
        loopv(clients)
        {
            client &o = *clients[i];
            if(o.type!=ST_TCPIP || !o.isauthed) continue;
            if(o.clientnum!=c.clientnum && o.peer->address.host==c.peer->address.host && !strcmp(o.name, c.name))
            {
                static savedscore curscore;
                curscore.save(o.state, o.team);
                return &curscore;
            }
        }
    }
    loopv(savedscores)
    {
        savedscore &sc = savedscores[i];
        if(!strcmp(sc.name, c.name) && (sc.ip & mask) == (c.peer->address.host & mask)) return &sc;
    }
    if(!insert) return NULL;
    savedscore &sc = savedscores.add();
    copystring(sc.name, c.name);
    sc.ip = c.peer->address.host;
    return &sc;
}

bool findlimit(client &c, bool insert)
{
    if (c.type != ST_TCPIP) return false;
    if (insert)
    {
        if (savedlimits.length() >= 32) savedlimits.remove(0, 16); // halve the saved limits before it reaches 33
        savedlimit &sl = savedlimits.add();
        sl.ip = c.peer->address.host;
        sl.save(c);
        return true;
    }
    loopv(savedlimits)
    {
        savedlimit &sl = savedlimits[i];
        if (sl.ip == c.peer->address.host)
        {
            sl.restore(c);
            return true;
        }
    }
    return false;
}

void sendf(client *cl, int chan, const char *format, ...)
{
    int exclude = -1;
    bool reliable = false;
    if(*format=='r') { reliable = true; ++format; }
    packetbuf p(MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
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
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'm':
        {
            int n = va_arg(args, int);
            p.put(va_arg(args, uchar *), n);
            break;
        }
    }
    va_end(args);
    sendpacket(cl, chan, p.finalize(), exclude);
}

void sendservmsg(const char *msg, client *cl)
{
    sendf(cl, 1, "ris", SV_SERVMSG, msg);
}

#define SECURESPAWNDIST 15
int spawncycle = -1;
int fixspawn = 2;

int findspawn(int index)
{
    for (int i = index; i<smapstats.hdr.numents; i++) if (mapents[i].type == PLAYERSTART) return i;
    loopj(index) if (mapents[j].type == PLAYERSTART) return j;
    return -1;
}

int findspawn(int index, uchar attr2)
{
    for (int i = index; i<smapstats.hdr.numents; i++) if (mapents[i].type == PLAYERSTART && mapents[i].attr2 == attr2) return i;
    loopj(index) if (mapents[j].type == PLAYERSTART && mapents[j].attr2 == attr2) return j;
    return -1;
}

// returns -1 for a free place, else dist to the nearest enemy
float nearestenemy(vec place, int team)
{
    float nearestenemydist = -1;
    loopv(clients)
    {
        client &other = *clients[i];
        if (other.type == ST_EMPTY || other.team == TEAM_SPECT || (m_team(gamemode, mutators) && team == other.team)) continue;
        float dist = place.dist(other.state.o);
        if (dist < nearestenemydist || nearestenemydist == -1) nearestenemydist = dist;
    }
    if (nearestenemydist >= SECURESPAWNDIST || nearestenemydist == -1) return -1;
    else return nearestenemydist;
}

void sendspawn(client &c)
{
    if(team_isspect(c.team)) return;
    clientstate &gs = c.state;
    if (gs.lastdeath) gs.respawn();
    // spawnstate
    if(c.type == ST_AI)
    {
        // random loadout settings
        const int weap1[] = {
            // insta/sniping
            GUN_BOLT,
            GUN_SNIPER2,
            GUN_SNIPER, // only for sniping
            // non-sniping below
            GUN_SHOTGUN,
            GUN_SUBGUN,
            GUN_ASSAULT,
            GUN_SWORD,
            GUN_ASSAULT2,
        }, weap2[] = {
            GUN_PISTOL,
            GUN_HEAL,
            GUN_RPG,
        };
        gs.nextprimary = weap1[rnd(m_insta(gamemode, mutators) ? 2 : m_sniper(gamemode, mutators) ? 3 : sizeof(weap1) / sizeof(int))];
        gs.nextsecondary = weap2[rnd(sizeof(weap2) / sizeof(int))];
        gs.nextperk1 = PERK_NONE;
        gs.nextperk2 = (gs.nextprimary == GUN_BOLT || m_sniper(gamemode, mutators)) ? PERK2_STEADY : PERK2_NONE;
    }
#if (SERVER_BUILTIN_MOD & 8)
    gs.nextprimary = gs.nextsecondary = gungame[gs.gungame];
#endif
    gs.spawnstate(c.team, smode, smuts);
    gs.lifesequence++;
    gs.state = CS_DEAD;
    // spawnpos
    persistent_entity *spawn_ent = NULL;
    int r = fixspawn-->0 ? 4 : rnd(10) + 1;
    const int type = m_spawn_team(gamemode, mutators) ? (c.team ^ ((m_spawn_reversals(gamemode, mutators) && gamemillis > gamelimit / 2) ? 1 : 0)) : 100;
    if (m_duke(gamemode, mutators) && c.spawnindex >= 0)
    {
        int x = -1;
        loopi(c.spawnindex + 1) x = findspawn(x + 1, type);
        if (x >= 0) spawn_ent = &mapents[x];
    }
    else if (m_team(gamemode, mutators) || m_duke(gamemode, mutators))
    {
        loopi(r) spawncycle = findspawn(spawncycle + 1, type);
        if (spawncycle >= 0) spawn_ent = &mapents[spawncycle];
    }
    else
    {
        float bestdist = -1;

        loopi(r)
        {
            spawncycle = !m_spawn_team(gamemode, mutators) && smapstats.spawns[2] > 5 ? findspawn(spawncycle + 1, 100) : findspawn(spawncycle + 1);
            if (spawncycle < 0) continue;
            float dist = nearestenemy(vec(mapents[spawncycle].x, mapents[spawncycle].y, mapents[spawncycle].z), c.team);
            if (!spawn_ent || dist < 0 || (bestdist >= 0 && dist > bestdist)) { spawn_ent = &mapents[spawncycle]; bestdist = dist; }
        }
    }
    if (spawn_ent)
    {
        gs.o.x = spawn_ent->x;
        gs.o.y = spawn_ent->y;
        c.y = spawn_ent->attr1; // yaw
    }
    else
    {
        // try to spawn in a random place (might be solid)
        gs.o.x = rnd((1 << maplayout_factor) - MINBORD) + MINBORD;
        gs.o.y = rnd((1 << maplayout_factor) - MINBORD) + MINBORD;
        c.y = 0; // yaw
    }
    extern float getblockfloor(int id, bool check_vdelta = true);
    gs.o.z = getblockfloor(getmaplayoutid(gs.o.x, gs.o.y));
    extern bool checkpos(vec &p, bool alter = true);
    checkpos(gs.o); // fix spawn being stuck
    gs.o.z += PLAYERHEIGHT;
    checkpos(gs.o); // fix spawn being too high
    c.p = 0; // pitch
    // send spawn state
    sendf(&c, 1, "ri9vvi4", SV_SPAWNSTATE, c.clientnum, gs.lifesequence,
        gs.health, gs.armour, gs.perk1, gs.perk2, gs.primary, gs.secondary,
        NUMGUNS, gs.ammo, NUMGUNS, gs.mag,
        (int)(gs.o.x*DMF), (int)(gs.o.y*DMF), (int)(gs.o.z*DMF), c.y);
    gs.lastspawn = gamemillis;
}

// demo
stream *demotmp = NULL, *demorecord = NULL, *demoplayback = NULL;
bool recordpackets = false;
int nextplayback = 0;

void writedemo(int chan, void *data, int len)
{
    if(!demorecord) return;
    int stamp[3] = { gamemillis, chan, len };
    lilswap(stamp, 3);
    demorecord->write(stamp, sizeof(stamp));
    demorecord->write(data, len);
}

void recordpacket(int chan, void *data, int len)
{
    if(recordpackets) writedemo(chan, data, len);
}

void recordpacket(int chan, ENetPacket *packet)
{
    if(recordpackets) writedemo(chan, packet->data, (int)packet->dataLength);
}

#ifdef STANDALONE
const char *currentserver(int i)
{
    static string curSRVinfo;
    string r;
    r[0] = '\0';
    switch(i)
    {
        case 1: { copystring(r, scl.ip[0] ? scl.ip : "local"); break; } // IP
        case 2: { copystring(r, scl.logident[0] ? scl.logident : "local"); break; } // HOST
        case 3: { formatstring(r)("%d", scl.serverport); break; } // PORT
        // the following are used by a client, a server will simply return empty strings for them
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        {
            break;
        }
        default:
        {
            formatstring(r)("%s %d", scl.ip[0] ? scl.ip : "local", scl.serverport);
            break;
        }
    }
    copystring(curSRVinfo, r);
    return curSRVinfo;
}
#endif

// these are actually the values used by the client, the server ones are in "scl".
string demofilenameformat = DEFDEMOFILEFMT;
string demotimestampformat = DEFDEMOTIMEFMT;
int demotimelocal = 0;

#ifdef STANDALONE
#define DEMOFORMAT scl.demofilenameformat
#define DEMOTSFORMAT scl.demotimestampformat
#else
#define DEMOFORMAT demofilenameformat
#define DEMOTSFORMAT demotimestampformat
#endif

const char *getDemoFilename(int gmode, int gmuts, int mplay, int mdrop, int tstamp, char *srvmap)
{
    // we use the following internal mapping of formatchars:
    // %g : gamemode (int)      %G : gamemode (chr)             %F : gamemode (full)
    // %m : minutes remaining   %M : minutes played
    // %s : seconds remaining   %S : seconds played
    // %h : IP of server        %H : hostname of server
    // %n : mapName
    // %w : timestamp "when"
    static string dmofn;
    copystring(dmofn, "");

    int cc = 0;
    int mc = strlen(DEMOFORMAT);

    while(cc<mc)
    {
        switch(DEMOFORMAT[cc])
        {
            case '%':
            {
                if(cc<(mc-1))
                {
                    string cfspp;
                    switch(DEMOFORMAT[cc+1])
                    {
                        case 'F': formatstring(cfspp)("%s", modestr(gmode, gmuts, false)); break;
                        case 'g': formatstring(cfspp)("%d-%d", gmode, gmuts); break;
                        case 'G': formatstring(cfspp)("%s", modestr(gmode, gmuts, true)); break;
                        case 'h': formatstring(cfspp)("%s", currentserver(1)); break; // client/server have different implementations
                        case 'H': formatstring(cfspp)("%s", currentserver(2)); break; // client/server have different implementations
                        case 'm': formatstring(cfspp)("%d", mdrop/60); break;
                        case 'M': formatstring(cfspp)("%d", mplay/60); break;
                        case 'n': formatstring(cfspp)("%s", srvmap); break;
                        case 's': formatstring(cfspp)("%d", mdrop); break;
                        case 'S': formatstring(cfspp)("%d", mplay); break;
                        case 'w':
                        {
                            time_t t = tstamp;
                            struct tm * timeinfo;
                            timeinfo = demotimelocal ? localtime(&t) : gmtime (&t);
                            strftime(cfspp, sizeof(string) - 1, DEMOTSFORMAT, timeinfo);
                            break;
                        }
                        default: logline(ACLOG_INFO, "bad formatstring: demonameformat @ %d", cc); cc-=1; break; // don't drop the bad char
                    }
                    concatstring(dmofn, cfspp);
                }
                else
                {
                    logline(ACLOG_INFO, "trailing %%-sign in demonameformat");
                }
                cc+=1;
                break;
            }
            default:
            {
                defformatstring(fsbuf)("%s%c", dmofn, DEMOFORMAT[cc]);
                copystring(dmofn, fsbuf);
                break;
            }
        }
        cc+=1;
    }
    return dmofn;
}
#undef DEMOFORMAT
#undef DEMOTSFORMAT

void enddemorecord()
{
    if(!demorecord) return;

    delete demorecord;
    recordpackets = false;
    demorecord = NULL;

    if(!demotmp) return;

    if(gamemillis < DEMO_MINTIME)
    {
        delete demotmp;
        demotmp = NULL;
        logline(ACLOG_INFO, "Demo discarded.");
        return;
    }

    int len = demotmp->size();
    demotmp->seek(0, SEEK_SET);
    if(demofiles.length() >= scl.maxdemos)
    {
        delete[] demofiles[0].data;
        demofiles.remove(0);
    }
    int mr = gamemillis >= gamelimit ? 0 : (gamelimit - gamemillis + 60000 - 1)/60000;
    demofile &d = demofiles.add();

    //2010oct10:ft: suggests : formatstring(d.info)("%s, %s, %.2f%s", modestr(gamemode, mutators), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB"); // the datetime bit is pretty useless in the servmesg, no?!
    formatstring(d.info)("%s: %s, %s, %.2f%s", asctime(), modestr(gamemode, mutators), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
    if(mr) { concatformatstring(d.info, ", %d mr", mr); concatformatstring(d.file, "_%dmr", mr); }
    defformatstring(msg)("Demo \"%s\" recorded\nPress F10 to download it from the server..", d.info);
    sendservmsg(msg);
    logline(ACLOG_INFO, "Demo \"%s\" recorded.", d.info);

    // 2011feb05:ft: previously these two static formatstrings were used ..
    //formatstring(d.file)("%s_%s_%s", timestring(), behindpath(smapname), modestr(gamemode, mutators, true)); // 20100522_10.08.48_ac_mines_DM.dmo
    //formatstring(d.file)("%s_%s_%s", modestr(gamemode, mutators, true), behindpath(smapname), timestring( true, "%Y.%m.%d_%H%M")); // DM_ac_mines.2010.05.22_1008.dmo
    // .. now we use client-side parseable fileattribs
    int mPLAY = gamemillis >= gamelimit ? gamelimit/1000 : gamemillis/1000;
    int mDROP = gamemillis >= gamelimit ? 0 : (gamelimit - gamemillis)/1000;
    int iTIME = time(NULL);
    const char *mTIME = numtime();
    const char *sMAPN = behindpath(smapname);
    string iMAPN;
    copystring(iMAPN, sMAPN);
    formatstring(d.file)( "%d:%d:%d:%s:%s", gamemode, mPLAY, mDROP, mTIME, iMAPN);

    d.data = new uchar[len];
    d.len = len;
    demotmp->read(d.data, len);
    delete demotmp;
    demotmp = NULL;
    if(scl.demopath[0])
    {
        formatstring(msg)("%s%s.dmo", scl.demopath, getDemoFilename(gamemode, mutators, mPLAY, mDROP, iTIME, iMAPN)); //d.file);
        path(msg);
        stream *demo = openfile(msg, "wb");
        if(demo)
        {
            int wlen = (int) demo->write(d.data, d.len);
            delete demo;
            logline(ACLOG_INFO, "demo written to file \"%s\" (%d bytes)", msg, wlen);
        }
        else
        {
            logline(ACLOG_INFO, "failed to write demo to file \"%s\"", msg);
        }
    }
}

void setupdemorecord()
{
    if(numlocalclients() || m_edit(gamemode)) return;

    defformatstring(demotmppath)("demos/demorecord_%s_%d", scl.ip[0] ? scl.ip : "local", scl.serverport);
    demotmp = opentempfile(demotmppath, "w+b");
    if(!demotmp) return;

    stream *f = opengzfile(NULL, "wb", demotmp);
    if(!f)
    {
        delete demotmp;
        demotmp = NULL;
        return;
    }

    sendservmsg("recording demo");
    logline(ACLOG_INFO, "Demo recording started.");

    demorecord = f;
    recordpackets = false;

    demoheader hdr;
    memcpy(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic));
    hdr.version = DEMO_VERSION;
    hdr.protocol = SERVER_PROTOCOL_VERSION;
    lilswap(&hdr.version, 1);
    lilswap(&hdr.protocol, 1);
    memset(hdr.desc, 0, DHDR_DESCCHARS);
    defformatstring(desc)("%s, %s, %s %s", modestr(gamemode, mutators, false), behindpath(smapname), asctime(), servdesc_current);
    if(strlen(desc) > DHDR_DESCCHARS)
        formatstring(desc)("%s, %s, %s %s", modestr(gamemode, mutators, true), behindpath(smapname), asctime(), servdesc_current);
    desc[DHDR_DESCCHARS - 1] = '\0';
    strcpy(hdr.desc, desc);
    memset(hdr.plist, 0, DHDR_PLISTCHARS);
    const char *bl = "";
    loopv(clients)
    {
        client *ci = clients[i];
        if(ci->type==ST_EMPTY) continue;
        if(strlen(hdr.plist) + strlen(ci->name) < DHDR_PLISTCHARS - 2) { strcat(hdr.plist, bl); strcat(hdr.plist, ci->name); }
        bl = " ";
    }
    demorecord->write(&hdr, sizeof(demoheader));

    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    welcomepacket(p, NULL);
    writedemo(1, p.buf, p.len);
}

void listdemos(client *cl)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_SENDDEMOLIST);
    putint(p, demofiles.length());
    loopv(demofiles) sendstring(demofiles[i].info, p);
    sendpacket(cl, 1, p.finalize());
}

static void cleardemos(int n)
{
    if(!n)
    {
        loopv(demofiles) delete[] demofiles[i].data;
        demofiles.shrink(0);
        sendservmsg("cleared all demos");
    }
    else if(demofiles.inrange(n-1))
    {
        delete[] demofiles[n-1].data;
        demofiles.remove(n-1);
        defformatstring(msg)("cleared demo %d", n);
        sendservmsg(msg);
    }
}

bool sending_demo = false;

void senddemo(client &cl, int num)
{
    bool is_admin = cl.role >= CR_ADMIN;
    if(scl.demo_interm && (!interm || totalclients > 2) && !is_admin)
    {
        sendservmsg("\f3sorry, but this server only sends demos at intermission.\n wait for the end of this game, please", &cl);
        return;
    }
    if(!num) num = demofiles.length();
    if(!demofiles.inrange(num-1))
    {
        if (demofiles.empty()) sendservmsg("no demos available", &cl);
        else
        {
            defformatstring(msg)("no demo %d available", num);
            sendservmsg(msg, &cl);
        }
        return;
    }
    demofile &d = demofiles[num-1];
    loopv(d.clientssent) if(d.clientssent[i].ip == cl.peer->address.host && d.clientssent[i].clientnum == cl.clientnum)
    {
        sendservmsg("\f3Sorry, you have already downloaded this demo.", &cl);
        return;
    }
    clientidentity &ci = d.clientssent.add();
    ci.ip = cl.peer->address.host;
    ci.clientnum = cl.clientnum;

    if (interm) sending_demo = true;
    packetbuf p(MAXTRANS + d.len, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_SENDDEMO);
    sendstring(d.file, p);
    putint(p, d.len);
    p.put(d.data, d.len);
    sendpacket(&cl, 2, p.finalize());
}

int demoprotocol;
bool watchingdemo = false;

void enddemoplayback()
{
    if(!demoplayback) return;
    delete demoplayback;
    demoplayback = NULL;
    watchingdemo = false;

    loopv(clients) sendf(clients[i], 1, "risi", SV_DEMOPLAYBACK, "", i);

    sendservmsg("demo playback finished");

    loopv(clients) sendwelcome(*clients[i]);
}

void setupdemoplayback()
{
    demoheader hdr;
    string msg;
    msg[0] = '\0';
    defformatstring(file)("demos/%s.dmo", smapname);
    path(file);
    demoplayback = opengzfile(file, "rb");
    if(!demoplayback) formatstring(msg)("could not read demo \"%s\"", file);
    else if(demoplayback->read(&hdr, sizeof(demoheader))!=sizeof(demoheader) || memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)))
        formatstring(msg)("\"%s\" is not a demo file", file);
    else
    {
        lilswap(&hdr.version, 1);
        lilswap(&hdr.protocol, 1);
        if(hdr.version!=DEMO_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of AssaultCube", file, hdr.version<DEMO_VERSION ? "older" : "newer");
        else if(hdr.protocol != PROTOCOL_VERSION && !(hdr.protocol < 0 && hdr.protocol == -PROTOCOL_VERSION)) formatstring(msg)("demo \"%s\" requires an %s version of AssaultCube", file, hdr.protocol<PROTOCOL_VERSION ? "older" : "newer");
        demoprotocol = hdr.protocol;
    }
    if(msg[0])
    {
        if(demoplayback) { delete demoplayback; demoplayback = NULL; }
        sendservmsg(msg);
        return;
    }

    formatstring(msg)("playing demo \"%s\"", file);
    sendservmsg(msg);
    sendf(NULL, 1, "risi", SV_DEMOPLAYBACK, smapname, -1);
    watchingdemo = true;

    if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
    {
        enddemoplayback();
        return;
    }
    lilswap(&nextplayback, 1);
}

void readdemo()
{
    if(!demoplayback) return;
    while(gamemillis>=nextplayback)
    {
        int chan, len;
        if(demoplayback->read(&chan, sizeof(chan))!=sizeof(chan) ||
           demoplayback->read(&len, sizeof(len))!=sizeof(len))
        {
            enddemoplayback();
            return;
        }
        lilswap(&chan, 1);
        lilswap(&len, 1);
        ENetPacket *packet = enet_packet_create(NULL, len, 0);
        if(!packet || demoplayback->read(packet->data, len)!=len)
        {
            if(packet) enet_packet_destroy(packet);
            enddemoplayback();
            return;
        }
        sendpacket(NULL, chan, packet, -1, true);
        if(!packet->referenceCount) enet_packet_destroy(packet);
        if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
        {
            enddemoplayback();
            return;
        }
        lilswap(&nextplayback, 1);
    }
}

void putflaginfo(packetbuf &p, int flag)
{
    sflaginfo &f = sflaginfos[flag];
    putint(p, SV_FLAGINFO);
    putint(p, flag);
    putint(p, f.state);
    switch(f.state)
    {
        case CTFF_STOLEN:
            putint(p, f.actor_cn);
            break;
        case CTFF_DROPPED:
            loopi(3) putuint(p, (int)(f.pos[i]*DMF));
            break;
    }
}

void putsecureflaginfo(ucharbuf &p, ssecure &s)
{
    putint(p, SV_FLAGSECURE);
    putint(p, s.id);
    putint(p, s.team);
    putint(p, s.enemy);
    putint(p, s.overthrown);
}

#include "serverchecks.h"

void sendflaginfo(int flag = -1, client *cl = NULL)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    if(flag >= 0) putflaginfo(p, flag);
    else loopi(2) putflaginfo(p, i);
    sendpacket(cl, 1, p.finalize());
}

void sendsecureflaginfo(ssecure *s = NULL, client *cl = NULL)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    if (s) putsecureflaginfo(p, *s);
    else loopv(ssecures) putsecureflaginfo(p, ssecures[i]);
    sendpacket(cl, 1, p.finalize());
}

void flagmessage(int flag, int message, int actor)
{
    if(message == FA_KTFSCORE)
        sendf(NULL, 1, "ri5", SV_FLAGMSG, flag, message, actor, (gamemillis - sflaginfos[flag].stolentime) / 1000);
    else
        sendf(NULL, 1, "ri4", SV_FLAGMSG, flag, message, actor);
}

void flagaction(int flag, int action, int actor)
{
    if(!valid_flag(flag)) return;
    sflaginfo &f = sflaginfos[flag];
    sflaginfo &of = sflaginfos[team_opposite(flag)];
    int score = 0;
    int message = action;

    if (m_capture(gamemode) || m_hunt(gamemode) || m_ktf2(gamemode, mutators) || m_bomber(gamemode))
    {
        switch(action)
        {
            case FA_PICKUP:
            case FA_STEAL:
            {
                f.state = CTFF_STOLEN;
                f.actor_cn = actor;
                f.stolentime = gamemillis; // needed for KTF2
                break;
            }
            case FA_LOST:
            case FA_DROP:
                if (actor == -1) actor = f.actor_cn;
                f.state = CTFF_DROPPED;
                loopi(3) f.pos[i] = clients[actor]->state.o[i];
                //if(f.pos[2] < smapstats.hdr.waterlevel) f.pos[2] = smapstats.hdr.waterlevel; // float to top of water
                break;
            case FA_RETURN:
                f.state = CTFF_INBASE;
                break;
            case FA_SCORE:  // ctf: f = carried by actor flag,  htf: f = hunted flag (run over by actor)
                if (m_capture(gamemode)) score = 1;
                else if (m_bomber(gamemode)) score = of.state == CTFF_INBASE ? 3 : of.state == CTFF_DROPPED ? 2 : 1;
                else if (m_ktf2(gamemode, mutators))
                {
                    if (valid_client(f.actor_cn) && clients[f.actor_cn]->state.state == CS_ALIVE)
                    {
                        actor = f.actor_cn;
                        score = 1;
                        message = FA_KTFSCORE;
                        break; // do not set to INBASE
                    }
                }
                else if (m_hunt(gamemode))
                {
                    // strict: must have flag to score
                    if (!m_gsp1(gamemode, mutators) || of.state == CTFF_STOLEN)
                    {
                        if (!m_gsp1(gamemode, mutators))
                            ++score;
                        if (of.state == CTFF_STOLEN)
                        {
                            ++score;
                            if (of.actor_cn == actor) ++score;
                        }
                    }
                    if (!score) message = FA_SCOREFAIL;
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
            case FA_STEAL:
                f.state = CTFF_STOLEN;
                f.actor_cn = actor;
                f.stolentime = gamemillis;
                break;
            case FA_SCORE:  // f = carried by actor flag
                if(valid_client(f.actor_cn) && clients[f.actor_cn]->state.state == CS_ALIVE && !team_isspect(clients[f.actor_cn]->team))
                {
                    actor = f.actor_cn;
                    score = 1;
                    message = FA_KTFSCORE;
                    break;
                }
            case FA_LOST:
            case FA_DROP:
                if (actor == -1) actor = f.actor_cn;
            case FA_RESET:
                if(f.state == CTFF_STOLEN)
                    actor = f.actor_cn;
                f.state = CTFF_IDLE;
                of.state = CTFF_INBASE;
                sendflaginfo(team_opposite(flag));
                break;
        }
    }
    if (valid_client(actor))
    {
        client &c = *clients[actor];
        if (score)
        {
            c.state.invalidate().flagscore += score;
            // usesteamscore(c.team).flagscore += score;
        }
        /*usesteamscore(c.team).points += max(0,*/ ( flagpoints(c, message));

        switch (message)
        {
            case FA_PICKUP:
            case FA_STEAL:
                logline(ACLOG_INFO, "[%s] %s %s the flag", c.gethostname(), c.formatname(), action == FA_STEAL ? "stole" : "took");
                break;
            case FA_DROP:
                f.drop_cn = actor;
                f.dropmillis = servmillis;
            case FA_LOST:
                logline(ACLOG_INFO, "[%s] %s %s the flag", c.gethostname(), c.formatname(), message == FA_LOST ? "lost" : "dropped");
                break;
            case FA_RETURN:
                logline(ACLOG_INFO, "[%s] %s returned the flag", c.gethostname(), c.formatname());
                break;
            case FA_SCORE:
                if (m_hunt(gamemode))
                    logline(ACLOG_INFO, "[%s] %s hunted the flag for %s, new score %d", c.gethostname(), c.formatname(), team_string(c.team), c.state.flagscore);
                else
                    logline(ACLOG_INFO, "[%s] %s scored with the flag for %s, new score %d", c.gethostname(), c.formatname(), team_string(c.team), c.state.flagscore);
                break;
            case FA_KTFSCORE:
                logline(ACLOG_INFO, "[%s] %s scored, carrying for %d seconds, new score %d", c.gethostname(), c.formatname(), (gamemillis - f.stolentime) / 1000, c.state.flagscore);
                break;
            case FA_SCOREFAIL:
                logline(ACLOG_INFO, "[%s] %s failed to score", c.gethostname(), c.formatname());
                break;
            default:
                logline(ACLOG_INFO, "flagaction %d, actor %d, flag %d, message %d", action, actor, flag, message);
                break;
        }
    }
    else if (message == FA_RESET) logline(ACLOG_INFO, "the server reset the flag for team %s", team_string(flag));
    else logline(ACLOG_INFO, "flagaction %d, actor %d, flag %d, message %d", action, actor, flag, message);

    f.lastupdate = gamemillis;
    sendflaginfo(flag);
    flagmessage(flag, message, actor);
}

int clienthasflag(int cn)
{
    if (m_flags(gamemode) && !m_secure(gamemode) && valid_client(cn))
    {
        loopi(2) { if(sflaginfos[i].state==CTFF_STOLEN && sflaginfos[i].actor_cn==cn) return i; }
    }
    return -1;
}

void ctfreset()
{
    int idleflag = m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? rnd(2) : -1;
    loopi(2)
    {
        sflaginfos[i].actor_cn = -1;
        sflaginfos[i].state = i == idleflag ? CTFF_IDLE : CTFF_INBASE;
        sflaginfos[i].lastupdate = -1;
    }
}

void sdropflag(int cn)
{
    int fl = clienthasflag(cn);
    if(fl >= 0) flagaction(fl, FA_LOST, cn);
}

void resetflag(int cn)
{
    int fl = clienthasflag(cn);
    if(fl >= 0) flagaction(fl, FA_RESET, -1);
}

void htf_forceflag(int flag)
{
    sflaginfo &f = sflaginfos[flag];
    int besthealth = 0;
    vector<int> clientnumbers;
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        if(clients[i]->state.state == CS_ALIVE && team_base(clients[i]->team) == flag)
        {
            if(clients[i]->state.health == besthealth)
                clientnumbers.add(i);
            else
            {
                if(clients[i]->state.health > besthealth)
                {
                    besthealth = clients[i]->state.health;
                    clientnumbers.shrink(0);
                    clientnumbers.add(i);
                }
            }
        }
    }

    if(clientnumbers.length())
    {
        int pick = rnd(clientnumbers.length());
        client *cl = clients[clientnumbers[pick]];
        f.state = CTFF_STOLEN;
        f.actor_cn = cl->clientnum;
        sendflaginfo(flag);
        flagmessage(flag, FA_PICKUP, cl->clientnum);
        logline(ACLOG_INFO, "[%s] %s got forced to pickup the flag", cl->gethostname(), cl->formatname());
    }
    f.lastupdate = gamemillis;
}

int arenaround = 0, arenaroundstartmillis = 0;

struct twoint { int index, value; };
int cmpscore(const int *a, const int *b) { return clients[*a]->at3_score - clients[*b]->at3_score; }
int cmptwoint(const struct twoint *a, const struct twoint *b) { return a->value - b->value; }
vector<int> tdistrib;
vector<twoint> sdistrib;

void distributeteam(int team)
{
    int numsp = team == 100 ? smapstats.spawns[2] : smapstats.spawns[team];
    if(!numsp) numsp = 30; // no spawns: try to distribute anyway
    twoint ti;
    tdistrib.shrink(0);
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        if(team == 100 || team == clients[i]->team)
        {
            tdistrib.add(i);
            clients[i]->at3_score = rnd(0x1000000);
        }
    }
    tdistrib.sort(cmpscore); // random player order
    sdistrib.shrink(0);
    loopi(numsp)
    {
        ti.index = i;
        ti.value = rnd(0x1000000);
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

void distributespawns()
{
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        clients[i]->spawnindex = -1;
    }
    if(m_team(gamemode, mutators))
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

void arenanext(bool forcespawn = true)
{
    // start new arena round
    arenaround = 0;
    arenaroundstartmillis = gamemillis;
    distributespawns();
    // purgesknives();
    checkitemspawns(60 * 1000); // the server will respawn all items now
    loopi(2) if (sflaginfos[i].state == CTFF_DROPPED || sflaginfos[i].state == CTFF_STOLEN) flagaction(i, FA_RESET, -1);
    loopv(clients) if (clients[i]->type != ST_EMPTY && clients[i]->isauthed)
    {
        if (clients[i]->isonrightmap && team_isactive(clients[i]->team))
            sendspawn(*clients[i]);
    }
    nokills = true;
}

void arenacheck()
{
    if(!m_duke(gamemode, mutators) || interm || gamemillis<arenaround || !numactiveclients()) return;

    if(arenaround)
    {
        if (m_progressive(gamemode, mutators) && progressiveround <= MAXZOMBIEROUND)
        {
            defformatstring(zombiemsg)("\f1Wave #\f0%d \f3has started\f2!", progressiveround);
            sendservmsg(zombiemsg);
            return arenanext(false); // bypass forced spawning
        }
        return arenanext();
    }

    client *alive = NULL;
    bool dead = false;
    int lastdeath = 0;
    bool found = false; int ha = 0, hd = 0; // found a match to keep the round / humans alive / humans dead
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type==ST_EMPTY || !c.isauthed || !c.isonrightmap || team_isspect(c.team)) continue;
        if (c.state.lastspawn < 0 && c.state.state==CS_DEAD)
        {
            if (c.type != ST_AI) ++hd;
            dead = true;
            lastdeath = max(lastdeath, c.state.lastdeath);
        }
        else if(c.state.state==CS_ALIVE)
        {
            if (c.type != ST_AI) ++ha;
            if(!alive) alive = &c;
            else if(!m_team(gamemode, mutators) || alive->team != c.team) found = true;
        }
    }

    if ((found && (ha || !hd)) || !dead || gamemillis < lastdeath + 500) return;
    // what happened?
    if (m_progressive(gamemode, mutators))
    {
        const bool humanswin = !alive || alive->team == TEAM_RVSF;
        progressiveround += humanswin ? 1 : -1;
        if (progressiveround < 1) progressiveround = 1; // epic fail
        else if (progressiveround > MAXZOMBIEROUND)
        {
            sendservmsg("\f0Good work! \f1The zombies have been defeated!");
            forceintermission = true;
        }
        checkai(); // progressive zombies
        // convertcheck();
        sendf(NULL, 1, "ri2", SV_ZOMBIESWIN, (progressiveround << 1) | (humanswin ? 1 : 0));
        loopv(clients) if (clients[i]->type != ST_EMPTY && clients[i]->isauthed && clients[i]->team != TEAM_SPECT)
        {
            if (clients[i]->team == TEAM_CLA || progressiveround == MAXZOMBIEROUND)
            {
                // give humans time to prepare, except wave 30
                clients[i]->removeexplosives();
                if (clients[i]->state.state != CS_DEAD)
                    forcedeath(*clients[i]);
            }
            else if (clients[i]->isonrightmap && clients[i]->state.state == CS_DEAD)
            {
                // early repawn for humans, except wave 30
                clients[i]->state.lastdeath = 1;
                sendspawn(*clients[i]);
            }
            int pts = 0, pr = -1;
            if ((clients[i]->team == TEAM_CLA) == humanswin)
            {
                // he died
                pts = ARENALOSEPT;
                pr = PR_ARENA_LOSE;
            }
            else
            {
                // he survives
                pts = ARENAWINPT;
                pr = PR_ARENA_WIN;
            }
            addpt(*clients[i], pts, pr);
        }
    }
    else // duke
    {
        const int cn = !ha && found ? -2 : // bots win
            alive ? alive->clientnum : // someone/a team wins
            -1 // everyone died
            ;
        // send message
        sendf(NULL, 1, "ri2", SV_ARENAWIN, cn);
        // award points
        loopv(clients) if (clients[i]->type != ST_EMPTY && clients[i]->isauthed && team_isactive(clients[i]->team))
        {
            int pts = ARENALOSEPT, pr = PR_ARENA_LOSE; // he died with this team, or bots win
            if (clients[i]->state.state == CS_ALIVE) // he survives
            {
                pts = ARENAWINPT;
                pr = PR_ARENA_WIN;
            }
            else if (alive && isteam(alive, clients[i])) // his team wins, but he is dead
            {
                pts = ARENAWINDPT;
                pr = PR_ARENA_WIND;
            }
            addpt(*clients[i], pts, pr);
        }
    }
    // arena intermission
    arenaround = gamemillis+5000;
    // check team
    if(autobalance && m_team(gamemode, mutators))
        refillteams(true);
}

void convertcheck(bool quick)
{
    if (!m_convert(gamemode, mutators) || interm || gamemillis < arenaround || !numactiveclients()) return;
    if (arenaround)
    {
        // start new convert round
        shuffleteams(FTR_SILENT);
        return arenanext(true);
    }
    if (quick) return;
    // check if converted
    int bigteam = -1, found = 0;
    loopv(clients) if (clients[i]->type != ST_EMPTY && team_isactive(clients[i]->team))
    {
        if (!team_isvalid(bigteam)) bigteam = clients[i]->team;
        if (clients[i]->team == bigteam) ++found;
        else return; // nope
    }
    // game ends if not arena, and all enemies are converted
    if (found >= 2)
    {
        sendf(NULL, 1, "ri", SV_CONVERTWIN);
        arenaround = gamemillis + 5000;
    }
}

#define SPAMREPEATINTERVAL  20   // detect doubled lines only if interval < 20 seconds
#define SPAMMAXREPEAT       3    // 4th time is SPAM
#define SPAMCHARPERMINUTE   220  // good typist
#define SPAMCHARINTERVAL    30   // allow 20 seconds typing at maxspeed

bool spamdetect(client &cl, char *text) // checks doubled lines and average typing speed
{
    if(cl.type != ST_TCPIP || cl.role >= CR_ADMIN) return false;
    bool spam = false;
    int pause = servmillis - cl.lastsay;
    if(pause < 0 || pause > 90*1000) pause = 90*1000;
    cl.saychars -= (SPAMCHARPERMINUTE * pause) / (60*1000);
    cl.saychars += (int)strlen(text);
    if(cl.saychars < 0) cl.saychars = 0;
    if(text[0] && !strcmp(text, cl.lastsaytext) && servmillis - cl.lastsay < SPAMREPEATINTERVAL*1000)
    {
        spam = ++cl.spamcount > SPAMMAXREPEAT;
    }
    else
    {
         copystring(cl.lastsaytext, text);
         cl.spamcount = 0;
    }
    cl.lastsay = servmillis;
    if(cl.saychars > (SPAMCHARPERMINUTE * SPAMCHARINTERVAL) / 60)
        spam = true;
    return spam;
}

// chat message distribution matrix:
//
// /------------------------ common chat          C c C c c C C c C
// |/----------------------- RVSF chat            T
// ||/---------------------- CLA chat                 T
// |||/--------------------- spect chat             t   t t T   t T
// ||||                                           | | | | | | | | |
// ||||                                           | | | | | | | | |      C: normal chat
// ||||   team modes:                chat goes to | | | | | | | | |      T: team chat
// XX     -->   RVSF players                >-----/ | | | | | | | |      c: normal chat in all mastermodes except 'match'
// XX X   -->   RVSF spect players          >-------/ | | | | | | |      t: all chat in mastermode 'match', otherwise only team chat
// X X    -->   CLA players                 >---------/ | | | | | |
// X XX   -->   CLA spect players           >-----------/ | | | | |
// X  X   -->   SPECTATORs                  >-------------/ | | | |
// XXXX   -->   SPECTATORs (admin)          >---------------/ | | |
//        ffa modes:                                          | | |
// X      -->   any player (ffa mode)       >-----------------/ | |
// X  X   -->   any spectator (ffa mode)    >-------------------/ |
// X  X   -->   admin spectator             >---------------------/
//        -->   any admin (ACR addition) reads all

// purpose:
//  a) give spects a possibility to chat without annoying the players (even in ffa),
//  b) no hidden messages from spects to active teams,
//  c) no spect talk to players during 'match'

void sendtext(char *text, client &cl, int flags, int voice)
{
    // voicecom spam filter
    if(voice >= S_AFFIRMATIVE && voice <= S_AWESOME2 && servmillis > cl.mute) // valid and client is not muted
    {
        if ( cl.lastvc + 4000 < servmillis ) { if ( cl.spam > 0 ) cl.spam -= (servmillis - cl.lastvc) / 4000; } // no vc in the last 4 seconds
        else cl.spam++; // the guy is spamming
        if ( cl.spam < 0 ) cl.spam = 0;
        cl.lastvc = servmillis; // register
        if ( cl.spam > 4 ) { cl.mute = servmillis + 10000; voice = 0; } // 5 vcs in less than 20 seconds... shut up please
    }
    else voice = 0;
    string logmsg;
    if (flags & SAY_ACTION) formatstring(logmsg)("[%s] * %s '%s' ", cl.gethostname(), cl.formatname(), text);
    else formatstring(logmsg)("[%s] <%s> '%s' ", cl.gethostname(), cl.formatname(), text);
    // Check team flag
    if(cl.team == TEAM_VOID || (!m_team(gamemode, mutators) && cl.team != TEAM_SPECT))
        flags &= ~SAY_TEAM; // forced common chat
    else if(mastermode != MM_MATCH || !matchteamsize || team_isactive(cl.team) || (cl.team == TEAM_SPECT && cl.role >= CR_ADMIN));
    else // forced team chat
        flags |= SAY_TEAM;
    if(flags & SAY_TEAM)
        concatformatstring(logmsg, "(%s) ", team_basestring(cl.team));
    if(voice)
        concatformatstring(logmsg, "[%d] ", voice);
    if(cl.type == ST_TCPIP && cl.role < CR_ADMIN)
    {
        if(const char *forbidden = forbiddenlist.forbidden(text))
        {
            logline(ACLOG_VERBOSE, "%s, forbidden speech (%s)", logmsg, forbidden);
            defformatstring(forbiddenmessage)("\f2forbidden speech: \f3%s \f2was detected", forbidden);
            sendservmsg(forbiddenmessage, &cl);
            sendf(&cl, 1, "ri4s", SV_TEXT, cl.clientnum, 0, flags | SAY_FORBIDDEN, text);
            return;
        }
        else if(spamdetect(cl, text))
        {
            logline(ACLOG_VERBOSE, "%s, SPAM detected", logmsg);
            sendf(&cl, 1, "ri4s", SV_TEXT, cl.clientnum, 0, flags | SAY_SPAM, text);
            return;
        }
    }
    logline(ACLOG_INFO, "%s", logmsg);
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_TEXT);
    putint(p, cl.clientnum);
    putint(p, voice);
    putint(p, flags);
    sendstring(text, p);
    ENetPacket *packet = p.finalize();
    recordpacket(1, packet);
    const int &st = cl.team;
    loopv(clients)
    {
        if (clients[i]->type == ST_EMPTY || clients[i]->type == ST_AI)
            continue;
        const int &rt = clients[i]->team;
        if( !(flags & SAY_TEAM) || (i == cl.clientnum) ||         // common chat or same player
            (clients[i]->role >= CR_ADMIN) ||                     // admin reads all
           (team_isactive(st) && st == team_group(rt)) ||         // player to own team + own spects
           (team_isspect(st) && team_isspect(rt)))                // spectator to other spectators
            sendpacket(clients[i], 1, packet);
    }
}

int numplayers(bool include_bots = true)
{
    // Count every client
    if(include_bots)
        return numclients();
    // Count every client that is not a bot
    int count = 0;
    loopv(clients)
        if(clients[i]->type != ST_EMPTY && clients[i]->type != ST_AI)
            ++count;
    return count;
}

int spawntime(int type)
{
    int np = numplayers();
    np = np<3 ? 4 : (np>4 ? 2 : 3);    // Some spawn times are dependent on the number of players.
    int sec = 0;
    switch(type)
    {
    // Please update ./ac_website/htdocs/docs/introduction.html if these times change.
        case I_CLIPS:
        case I_AMMO: sec = np*2; break;
        case I_GRENADE: sec = np + 5; break;
        case I_HEALTH: sec = np*5; break;
        case I_HELMET:
        case I_ARMOUR: sec = 25; break;
        case I_AKIMBO: sec = 60; break;
    }
    return sec*1000;
}

void checkitemspawns(int diff)
{
    if(!diff) return;
    loopv(sents) if(sents[i].spawntime)
    {
        sents[i].spawntime -= diff;
        if(sents[i].spawntime<=0)
        {
            sents[i].spawntime = 0;
            sents[i].spawned = true;
            sendf(NULL, 1, "ri2", SV_ITEMSPAWN, i);
        }
    }
}

void serverdied(client &target, client &actor, int damage, int gun, int style, const vec &source, float killdist)
{
    clientstate &ts = target.state;

    const bool suic = (&target == &actor);
    const bool tk = !suic && isteam(&target, &actor);
    int targethasflag = clienthasflag(target.clientnum);

    ts.damagelog.removeobj(target.clientnum);
    // detect assisted suicide
    if (suic && ts.damagelog.length() && gun != OBIT_NUKE)
    {
        loopv(ts.damagelog)
            if (valid_client(ts.damagelog[i]) && !isteam(&target, clients[ts.damagelog[i]]))
            {
                actor = *clients[ts.damagelog[i]];
                style = isheadshot(gun, style) ? FRAG_GIB : FRAG_NONE;
                gun = OBIT_ASSIST;
                ts.damagelog.remove(i/*--*/);
                break;
            }
    }
    // only things on target team that changes
    //if (!m_confirm(gamemode, mutators)) ++usesteamscore(target->team).deaths;
    // apply to individual
    ++ts.invalidate().deaths;
    addpt(target, DEATHPT);
    const int kills = (suic || tk) ? -1 : ((style & FRAG_GIB) ? 2 : 1);
    actor.state.invalidate().frags += kills;

    if (!suic)
    {
        // revenge
        if (actor.state.revengelog.find(target.clientnum) >= 0)
        {
            style |= FRAG_REVENGE;
            actor.state.revengelog.removeobj(target.clientnum);
        }
        ts.revengelog.add(actor.clientnum);
        // first blood (not for AI)
        if (actor.type != ST_AI && nokills)
        {
            style |= FRAG_FIRST;
            nokills = false;
        }
        // type of scoping
        const int zoomtime = ADSTIME(actor.state.perk2 == PERK_TIME), scopeelapsed = gamemillis - actor.state.scopemillis;
        if (actor.state.scoping)
        {
            // quick/recent/full
            if (scopeelapsed >= zoomtime)
            {
                style |= FRAG_SCOPE_FULL;
                if (scopeelapsed < zoomtime + 300)
                    style |= FRAG_SCOPE_NONE; // recent, not hard
            }
        }
        else
        {
            // no/quick
            if (scopeelapsed >= zoomtime) style |= FRAG_SCOPE_NONE;
        }
        // buzzkill check
        //if (buzzkilled)
        if (false)
        {
            addptreason(actor, PR_BUZZKILL);
            addptreason(target, PR_BUZZKILLED);
        }
        // streak
        /*
        if (gun != OBIT_NUKE)
            actor->state.pointstreak += 5;
        ++ts.deathstreak;
        actor->state.deathstreak = ts.streakused = 0;
        */
        // teamkilling a flag carrier is bad
        if ((m_hunt(gamemode) || m_keep(gamemode)) && tk && targethasflag >= 0)
            --actor.state.invalidate().flagscore;
    }

    //ts.pointstreak = 0;
    ts.wounds.shrink(0);
    ts.damagelog.removeobj(actor.clientnum);
    target.invalidateheals();

    // assists
    loopv(ts.damagelog)
    {
        if (valid_client(ts.damagelog[i]))
        {
            /*
            const int factor = isteam(clients[ts.damagelog[i]], target) ? -1 : 1;
            clients[ts.damagelog[i]]->state.invalidate().assists += factor;
            if (factor > 0)
                usesteamscore(actor->team).assists += factor; // add to assists
            clients[ts.damagelog[i]]->state.pointstreak += factor * 2;
            */
        }
        else ts.damagelog.remove(i--);
    }

    // killstreak rewards
    // TODO

    // combo reset check
    if (gamemillis >= actor.state.lastkill + COMBOTIME) actor.state.combo = 0;
    actor.state.lastkill = gamemillis;

    // team points
    int earnedpts = killpoints(target, actor, gun, style);
    if (m_confirm(gamemode, mutators))
    {
        // create confirm object if necessary
        if (earnedpts > 0 || kills > 0)
        {
            sconfirm &c = sconfirms.add();
            c.o = ts.o;
            sendf(NULL, 1, "ri6", SV_CONFIRMADD, c.id = ++confirmseq, c.team = actor.team, (int)(c.o.x*DMF), (int)(c.o.y*DMF), (int)(c.o.z*DMF));
            c.actor = actor.clientnum;
            c.target = target.clientnum;
            c.points = max(0, earnedpts);
            c.frag = max(0, kills);
            c.death = target.team;
        }
    }
    else
    {
        //if (earnedpts > 0) usesteamscore(actor->team).points += earnedpts;
        //if (kills > 0) usesteamscore(actor->team).frags += kills;
    }

    // automatic zombie count
    if (m_zombie(gamemode) && !m_progressive(gamemode, mutators) && (botbalance == 0 || botbalance == -1) && target.team != actor.team)
    {
        if (target.team == TEAM_CLA)
        {
            // zombie was killed
            --zombiesremain;
            if (zombiesremain <= 0)
            {
                zombiesremain = (++zombiebalance + 1) >> 1;
                checkai();
            }
        }
        // human died
        else if (++zombiesremain > zombiebalance)
            zombiesremain = zombiebalance;
    }

    // send message
    sendf(NULL, 1, "ri9i3", SV_KILL, target.clientnum, actor.clientnum, gun, style, damage, ++actor.state.combo, ts.damagelog.length(),
        (int)(source.x*DMF), (int)(source.y*DMF), (int)(source.z*DMF), (int)(killdist*DMF));

    target.position.setsize(0);
    ts.state = CS_DEAD;
    ts.lastdeath = gamemillis;
    ts.lastspawn = -1;
    // don't issue respawn yet until DEATHMILLIS has elapsed
    // ts.respawn();
    
    // log message
    const int logtype = actor.type == ST_AI && target.type == ST_AI ? ACLOG_VERBOSE : ACLOG_INFO;
    if (suic)
        logline(logtype, "[%s] %s [%s] (%.2f m)", actor.gethostname(), actor.formatname(), suicname(gun), killdist / 4.f);
    else
        logline(logtype, "[%s] %s [%s] %s (%.2f m)", actor.gethostname(), actor.formatname(), killname(gun, style), target.formatname(), killdist / 4.f);

    // drop flags
    if (targethasflag >= 0 && m_flags(gamemode) && !m_secure(gamemode))
    {
        if (m_ktf2(gamemode, mutators) && // KTF2 only
            sflaginfos[team_opposite(targethasflag)].state != CTFF_INBASE) // other flag is not in base
        {
            if (sflaginfos[0].actor_cn == sflaginfos[1].actor_cn) // he has both
            {
                // reset the far one
                const int farflag = ts.o.distxy(vec(sflaginfos[0].x, sflaginfos[0].y, 0)) > ts.o.distxy(vec(sflaginfos[1].x, sflaginfos[1].y, 0)) ? 0 : 1;
                flagaction(farflag, FA_RESET, -1);
                // drop the close one
                targethasflag = team_opposite(farflag);
            }
            else // he only has this one
            {
                // reset this one
                flagaction(targethasflag, FA_RESET, -1);
                targethasflag = -1;
            }
        }
        // drop all flags
        while (targethasflag >= 0)
        {
            flagaction(targethasflag, (tk && m_capture(gamemode)) ? FA_RESET : FA_LOST, -1);
            targethasflag = clienthasflag(target.clientnum);
        }
    }

    // target streaks
    /*
    if (target->state.nukemillis)
    {
        // nuke cancelled!
        target->state.nukemillis = 0;
        sendf(NULL, 1, "ri4", SV_STREAKUSE, target->clientnum, STREAK_NUKE, -2);
    }
    */
    // deathstreaks MUST be processed after setting to CS_DEAD
    /*
    if ((explosive_weap(gun) || isheadshot(gun, style)) && ts.streakondeath == STREAK_REVENGE)
        ts.streakondeath = STREAK_DROPNADE;
    usestreak(*target, ts.streakondeath, m_zombie(gamemode) ? actor : NULL);
    */

    if (!suic)
    {
#if (SERVER_BUILTIN_MOD & 8)
        // gungame advance
        if (gun != OBIT_NUKE)
        {
            // gungame maxed out
            if (++actor->state.gungame >= GUNGAME_MAX)
            {
                actor->state.nukemillis = gamemillis; // deploy a nuke
                actor->state.gungame = 0; // restart gungame
            }
            const int newprimary = actor->state.primary = actor->state.secondary = actor->state.gunselect = gungame[actor->state.gungame];
            sendf(NULL, 1, "ri5", SV_RELOAD, actor->clientnum, newprimary, actor->state.mag[newprimary] = magsize(newprimary), actor->state.ammo[newprimary] = (ammostats[newprimary].start - 1));
            sendf(NULL, 1, "ri3", SV_WEAPCHANGE, actor->clientnum, newprimary);
        }
#endif
        // conversions
        if (m_convert(gamemode, mutators) && target.team != actor.team)
        {
            updateclientteam(target, actor.team, FTR_SILENT);
            // checkai(); // DO NOT balance bots here
            convertcheck(true);
        }
    }
}

void client::suicide(int gun, int style)
{
    if (state.state != CS_DEAD)
        serverdied(*this, *this, 0, gun, style, state.o);
}

void serverdamage(client &target, client &actor, int damage, int gun, int style, const vec &source, float dist)
{
    // moon jump mario = no damage during gib
#if (SERVER_BUILTIN_MOD & 4)
#if !(SERVER_BUILTIN_MOD & 2)
    if (m_gib(gamemode, mutators))
#endif
        return;
#endif
    if (!damage) return;

    if (&target != &actor)
    {
        if (isteam(&actor, &target))
        {
            // for hardcore modes only
            if (actor.state.protect(gamemillis, gamemode, mutators))
                return;
            damage /= 2;
            target = actor;
        }
        else if (m_vampire(gamemode, mutators) && actor.state.health < VAMPIREMAX)
        {
            int hpadd = damage / (rnd(3) + 3);
            // cap at 300 HP
            if (actor.state.health + hpadd > VAMPIREMAX)
                hpadd = VAMPIREMAX - actor.state.health;
            sendf(NULL, 1, "ri3", SV_REGEN, actor.clientnum, actor.state.health += hpadd);
        }
    }

    clientstate &ts = target.state;
    if (ts.state != CS_ALIVE) return;

    // damage changes
    if (m_expert(gamemode, mutators))
    {
        if (gun == GUN_RPG)
            damage /= ((style & (FRAG_GIB | FRAG_FLAG)) == (FRAG_GIB | FRAG_FLAG)) ? 1 : 3;
        else if ((gun == GUN_GRENADE && (style & FRAG_FLAG)) || (style & FRAG_GIB) || melee_weap(gun))
            damage *= 2;
        else if (gun == GUN_GRENADE) damage /= 2;
        else damage /= 8;
    }
    else if (m_real(gamemode, mutators))
    {
        if (gun == GUN_HEAL && &target == &actor) damage /= 2;
        else damage *= 2;
    }
    else if (m_classic(gamemode, mutators)) damage /= 2;

    ts.dodamage(damage, gun);
    // ts.dodamage(damage, actor->state.perk1 == PERK_POWER);
    ts.lastregen = gamemillis + REGENDELAY - REGENINT;
    //ts.allowspeeding(gamemillis, 2000);

    if (ts.health <= 0)
        serverdied(target, actor, damage, gun, style, source, dist);
    else
    {
        if (ts.damagelog.find(actor.clientnum) < 0)
            ts.damagelog.add(actor.clientnum);
        sendf(NULL, 1, "ri8i3", SV_DAMAGE, target.clientnum, actor.clientnum, damage, ts.armour, ts.health, gun, style,
            (int)(source.x*DMF), (int)(source.y*DMF), (int)(source.z*DMF));
    }
}

#include "serverevents.h"

bool updatedescallowed(void) { return scl.servdesc_pre[0] || scl.servdesc_suf[0]; }

void updatesdesc(const char *newdesc, ENetAddress *caller = NULL)
{
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

inline bool canspawn(client &c, bool connecting)
{
    if (!maplayout)
        return false;
    if (c.team == TEAM_SPECT || (team_isspect(c.team) && !m_team(gamemode, mutators)))
        return false; // SP_SPECT
    if (m_duke(gamemode, mutators))
        return (connecting && totalclients <= 2) || (arenaround && m_zombie(gamemode) && c.team == TEAM_RVSF && progressiveround != MAXZOMBIEROUND);
    return true;
}

int chooseteam(client &cl, int def = rnd(2))
{
    // zombies override
    if (m_zombie(gamemode) && !m_convert(gamemode, mutators))
        return cl.type == ST_AI ? TEAM_CLA : TEAM_RVSF;
    // match base team
    if(mastermode == MM_MATCH && cl.team < TEAM_SPECT)
        return team_base(cl.team);
    // team sizes
    int *teamsizes = numteamclients(cl.clientnum, cl.type == ST_AI);
    if(autoteam && teamsizes[TEAM_CLA] != teamsizes[TEAM_RVSF]) return teamsizes[TEAM_CLA] < teamsizes[TEAM_RVSF] ? TEAM_CLA : TEAM_RVSF;
    else
    { // join weaker team
        int teamscore[2] = {0, 0}, sum = calcscores();
        loopv(clients) if(clients[i]->type!=ST_EMPTY && i != cl.clientnum && clients[i]->isauthed && clients[i]->team != TEAM_SPECT)
        {
            teamscore[team_base(clients[i]->team)] += clients[i]->at3_score;
        }
        return sum > 200 ? (teamscore[TEAM_CLA] < teamscore[TEAM_RVSF] ? TEAM_CLA : TEAM_RVSF) : def;
    }
}

bool updateclientteam(client &cl, int newteam, int ftr)
{
    if (!team_isvalid(newteam)) return false;
    // zombies override
    if (m_zombie(gamemode) && !m_convert(gamemode, mutators) && newteam != TEAM_SPECT) newteam = cl.type == ST_AI ? TEAM_CLA : TEAM_RVSF;

    if (cl.team == newteam)
    {
        // only allow to notify
        if (ftr != FTR_AUTO) return false;
    }
    else cl.removeexplosives(); // no nade switch

    // if (cl.team == TEAM_SPECT) cl.state.lastdeath = gamemillis;

    // log message
    logline(ftr == FTR_SILENT ? ACLOG_DEBUG : ACLOG_INFO, "[%s] %s is now on team %s", cl.gethostname(), cl.formatname(), team_string(newteam));
    // send message
    sendf(NULL, 1, "ri3", SV_SETTEAM, cl, (cl.team = newteam) | (ftr << 4));

    // force a death if necessary
    if (cl.state.state != CS_DEAD && (m_team(gamemode, mutators) || newteam == TEAM_SPECT))
    {
        if (ftr == FTR_PLAYERWISH) cl.suicide(team_isspect(newteam) ? OBIT_SPECT : OBIT_TEAM, FRAG_NONE);
        else forcedeath(cl);
    }
    return true;
}

int calcscores() // skill eval
{
    int sum = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        clientstate &cs = clients[i]->state;
        sum += clients[i]->at3_score = cs.points > 0 ? ufSqrt((float)cs.points) : -ufSqrt((float)-cs.points);
    }
    return sum;
}

vector<int> shuffle;

void shuffleteams(int ftr)
{
    int numplayers = numclients();
    int team, sums = calcscores();
    if(gamemillis < 2 * 60 *1000)
    { // random
        int teamsize[2] = {0, 0};
        loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isonrightmap && !team_isspect(clients[i]->team)) // only shuffle active players
        {
            sums += rnd(1000);
            team = sums & 1;
            if(teamsize[team] >= numplayers/2) team = team_opposite(team);
            updateclientteam(*clients[i], team, ftr);
            teamsize[team]++;
            sums >>= 1;
        }
    }
    else
    { // skill sorted
        shuffle.shrink(0);
        sums /= 4 * numplayers + 2;
        team = rnd(2);
        loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isonrightmap && !team_isspect(clients[i]->team))
        {
            clients[i]->at3_score += rnd(sums | 1);
            shuffle.add(i);
        }
        shuffle.sort(cmpscore);
        loopi(shuffle.length())
        {
            updateclientteam(*clients[shuffle[i]], team, ftr);
            team = !team;
        }
    }
    checkai(); // end of shuffle
    // convertcheck();
}

bool balanceteams(int ftr)  // pro vs noobs never more
{
    if(mastermode != MM_OPEN || totalclients < 3 ) return true;
    int tsize[2] = {0, 0}, tscore[2] = {0, 0};
    int totalscore = 0, nplayers = 0;
    int flagmult = (m_capture(gamemode) ? 50 : (m_hunt(gamemode) ? 25 : 12));

    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        client *c = clients[i];
        if(c->isauthed && team_isactive(c->team))
        {
            int time = servmillis - c->connectmillis + 5000;
            if ( time > gamemillis ) time = gamemillis + 5000;
            tsize[c->team]++;
            // effective score per minute, thanks to wtfthisgame for the nice idea
            // in a normal game, normal players will do 500 points in 10 minutes
            c->eff_score = c->state.points * 60 * 1000 / time + c->state.points / 6 + c->state.flagscore * flagmult;
            tscore[c->team] += c->eff_score;
            nplayers++;
            totalscore += c->state.points;
        }
    }

    int h = 0, l = 1;
    if ( tscore[1] > tscore[0] ) { h = 1; l = 0; }
    if ( 2 * tscore[h] < 3 * tscore[l] || totalscore < nplayers * 100 ) return true;
    if ( tscore[h] > 3 * tscore[l] && tscore[h] > 150 * nplayers )
    {
        shuffleteams();
        return true;
    }

    float diffscore = tscore[h] - tscore[l];

    int besth = 0, hid = -1;
    int bestdiff = 0;
    client *bestpair[2] = { NULL, NULL };
    if ( tsize[h] - tsize[l] > 0 ) // the h team has more players, so we will force only one player
    {
        loopv(clients) if( clients[i]->type!=ST_EMPTY )
        {
            client *c = clients[i]; // loop for h
            // client from the h team, not forced in this game, and without the flag
            if( c->isauthed && c->team == h && !c->state.forced && clienthasflag(i) < 0 )
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
            updateclientteam(*clients[hid], l, ftr);
            // checkai(); // balance big to small
            // convertcheck();
            clients[hid]->at3_lastforce = gamemillis;
            clients[hid]->state.forced = true;
            return true;
        }
    } else { // the h score team has less or the same player number, so, lets exchange
        loopv(clients) if(clients[i]->type!=ST_EMPTY)
        {
            client &ci = *clients[i]; // loop for h
            if( ci.isauthed && ci.team == h && !ci.state.forced && clienthasflag(i) < 0 )
            {
                loopvj(clients) if(clients[j]->type!=ST_EMPTY && j != i )
                {
                    client &cj = *clients[j]; // loop for l
                    if( cj.isauthed && cj.team == l && !cj.state.forced && clienthasflag(j) < 0 )
                    {
                        int pairdiff = 2 * (ci.eff_score - cj.eff_score);
                        if ( pairdiff <= diffscore && 5 * pairdiff >= diffscore && pairdiff > bestdiff )
                        {
                            bestdiff = pairdiff;
                            bestpair[h] = &ci;
                            bestpair[l] = &cj;
                        }
                    }
                }
            }
        }
        if ( bestpair[h] && bestpair[l] )
        {
            updateclientteam(*bestpair[h], l, ftr);
            updateclientteam(*bestpair[l], h, ftr);
            bestpair[h]->at3_lastforce = bestpair[l]->at3_lastforce = gamemillis;
            bestpair[h]->state.forced = bestpair[l]->state.forced = true;
            checkai(); // balance switch
            // convertcheck();
            return true;
        }
    }
    return false;
}

int lastbalance = 0, waitbalance = 2 * 60 * 1000;

bool refillteams(bool now, int ftr)  // force only minimal amounts of players
{
    if (m_zombie(gamemode) && !m_convert(gamemode, mutators))
    {
        // force to zombie teams
        loopv(clients)
            if (clients[i]->type != ST_EMPTY && !team_isspect(clients[i]->team))
                updateclientteam(*clients[i], clients[i]->type == ST_AI ? TEAM_CLA : TEAM_RVSF, ftr);
        return false;
    }
    if(mastermode == MM_MATCH) return false;
    static int lasttime_eventeams = 0;
    int teamsize[2] = {0, 0}, teamscore[2] = {0, 0}, moveable[2] = {0, 0};
    bool switched = false;

    calcscores();
    loopv(clients) if(clients[i]->type!=ST_EMPTY)     // playerlist stocktaking
    {
        client *c = clients[i];
        c->at3_dontmove = true;
        if(c->isauthed)
        {
            if(team_isactive(c->team)) // only active players count
            {
                teamsize[c->team]++;
                teamscore[c->team] += c->at3_score;
                if(clienthasflag(i) < 0)
                {
                    c->at3_dontmove = false;
                    moveable[c->team]++;
                }
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
                client *pick = NULL;
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
                        pick = clients[i];
                    }
                }
                if(!pick) break; // should really never happen
                // move picked player
                pick->at3_dontmove = true;
                moveable[bigteam]--;
                if(updateclientteam(*pick, !bigteam, ftr))
                {
                    diffnum -= 2;
                    diffscore -= 2 * pick->at3_score;
                    pick->at3_lastforce = gamemillis;  // try not to force this player again for the next 5 minutes
                    switched = true;
                    // checkai(); // refill
                    // convertcheck();
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

void resetserver(const char *newname, int newmode, int newmuts, int newtime)
{
    if(m_demo(gamemode)) enddemoplayback();
    else enddemorecord();

    smode = newmode;
    copystring(smapname, newname);
    smuts = newmuts;

    minremain = newtime > 0 ? newtime : defaultgamelimit(newmode, newmuts);
    gamemillis = 0;
    gamelimit = minremain*60000;
    arenaround = arenaroundstartmillis = 0;
    memset(&smapstats, 0, sizeof(smapstats));

    interm = nextsendscore = 0;
    lastfillup = servmillis;
    sents.shrink(0);
    if(mastermode == MM_PRIVATE)
    {
        loopv(savedscores) savedscores[i].valid = false;
    }
    else savedscores.shrink(0);
    ctfreset();

    nextmapname[0] = '\0';
    forceintermission = false;
}

void startdemoplayback(const char *newname)
{
    if(isdedicated) return;
    resetserver(newname, G_DEMO, G_M_NONE, -1);
    setupdemoplayback();
}

inline void putmap(ucharbuf &p)
{
    putint(p, SV_MAPCHANGE);
    sendstring(smapname, p);
    putint(p, smode);
    putint(p, smuts);
    putint(p, mapbuffer.available());
    putint(p, mapbuffer.revision);

    loopv(sents)
        if (sents[i].spawned)
            putint(p, i);
    putint(p, -1);

    putint(p, sknives.length());
    loopv(sknives)
    {
        putint(p, sknives[i].id);
        putint(p, KNIFETTL + sknives[i].millis - gamemillis);
        putint(p, (int)(sknives[i].o.x*DMF));
        putint(p, (int)(sknives[i].o.y*DMF));
        putint(p, (int)(sknives[i].o.z*DMF));
    }

    putint(p, sconfirms.length());
    loopv(sconfirms)
    {
        putint(p, sconfirms[i].id);
        putint(p, sconfirms[i].team);
        putint(p, (int)(sconfirms[i].o.x*DMF));
        putint(p, (int)(sconfirms[i].o.y*DMF));
        putint(p, (int)(sconfirms[i].o.z*DMF));
    }
}

void startgame(const char *newname, int newmode, int newmuts, int newtime, bool notify)
{
    if(!newname || !*newname || (newmode == G_DEMO && isdedicated)) fatal("startgame() abused");
    if(newmode == G_DEMO)
    {
        startdemoplayback(newname);
    }
    else
    {
        bool lastteammode = m_team(gamemode, mutators);
        resetserver(newname, newmode, newmuts, newtime);   // beware: may clear *newname

        int maploc = MAP_VOID;
        mapstats *ms = getservermapstats(smapname, true, &maploc);
        mapbuffer.clear();
        if(isdedicated && distributablemap(maploc)) mapbuffer.load();
        if(ms)
        {
            smapstats = *ms;
            loopi(2)
            {
                sflaginfo &f = sflaginfos[i];
                if(smapstats.flags[i] == 1)    // don't check flag positions, if there is more than one flag per team
                {
                    f.x = mapents[smapstats.flagents[i]].x;
                    f.y = mapents[smapstats.flagents[i]].y;
                }
                else f.x = f.y = -1;
            }
            if (smapstats.flags[0] == 1 && smapstats.flags[1] == 1)
            {
                sflaginfo &f0 = sflaginfos[0], &f1 = sflaginfos[1];
                FlagFlag = pow2(f0.x - f1.x) + pow2(f0.y - f1.y);
            }
            ssecures.shrink(0);
            entity e;
            loopi(smapstats.hdr.numents)
            {
                entity &e = sents.add();
                persistent_entity &pe = mapents[i];
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
                if (m_secure(newmode) && e.type == CTF_FLAG && e.attr2 >= 2)
                {
                    ssecure &s = ssecures.add();
                    s.id = i;
                    s.team = TEAM_SPECT;
                    s.enemy = TEAM_SPECT;
                    s.overthrown = 0;
                    s.o = vec(e.x, e.y, getblockfloor(getmaplayoutid(e.x, e.y), false));
                    s.last_service = 0;
                }
            }
            mapbuffer.setrevision();
            logline(ACLOG_INFO, "Map height density information for %s: H = %.2f V = %d, A = %d and MA = %d", smapname, Mheight, Mvolume, Marea, Mopen);
        }
        else if(isdedicated) sendservmsg("\f3server error: map not found - please start another map or send this map to the server");
        if(notify)
        {
            // change map
            // sknives.setsize(0);
            packetbuf q(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
            putmap(q);
            sendpacket(NULL, 1, q.finalize());
            // time remaining
            if(smode>1 || (smode==0 && numnonlocalclients()>0)) sendf(NULL, 1, "ri3", SV_TIMEUP, gamemillis, gamelimit);
        }
        defformatstring(gsmsg)("Game start: %s on %s, %d players, %d minutes, mastermode %d, ", modestr(smode, smuts), smapname, numclients(), minremain, mastermode);
        if(mastermode == MM_MATCH) concatformatstring(gsmsg, "teamsize %d, ", matchteamsize);
        if(ms) concatformatstring(gsmsg, "(map rev %d/%d, %s, 'getmap' %sprepared)", smapstats.hdr.maprevision, smapstats.cgzsize, maplocstr[maploc], mapbuffer.available() ? "" : "not ");
        else concatformatstring(gsmsg, "error: failed to preload map (map: %s)", maplocstr[maploc]);
        logline(ACLOG_INFO, "\n%s", gsmsg);
        arenaround = 0;
        nokills = true;
        if(m_duke(gamemode, mutators)) distributespawns();
        if (m_progressive(gamemode, mutators)) progressiveround = 1;
        else if (m_zombie(gamemode)) zombiebalance = zombiesremain = 1;
        if(notify)
        {
            // shuffle if previous mode wasn't a team-mode
            if(m_team(gamemode, mutators))
            {
                if (m_zombie(gamemode) || (lastteammode && autoteam))
                    refillteams(true, FTR_SILENT); // force teams for zombies
                else if (!lastteammode)
                    shuffleteams(FTR_SILENT);
            }
            // prepare spawns; players will spawn, once they've loaded the correct map
            loopv(clients) if(clients[i]->type!=ST_EMPTY)
            {
                client &c = *clients[i];
                c.mapchange();
                forcedeath(c);
            }
        }
        checkai(); // re-init ai (init)
        // convertcheck();
        // reset team scores
        // loopi(TEAM_NUM - 1) steamscores[i] = steamscore(i);
        purgesknives();
        purgesconfirms(); // but leave the confirms for team modes in arena
        if(numnonlocalclients() > 0) setupdemorecord();
        if (notify)
        {
            if (m_keep(gamemode)) sendflaginfo();
            else if (m_secure(gamemode)) sendsecureflaginfo();
            senddisconnectedscores();
        }
    }
}

struct gbaninfo
{
    enet_uint32 ip, mask;
};

vector<gbaninfo> gbans;

void cleargbans()
{
    gbans.shrink(0);
}

bool checkgban(uint ip)
{
    loopv(gbans) if((ip & gbans[i].mask) == gbans[i].ip) return true;
    return false;
}

void addgban(const char *name)
{
    union { uchar b[sizeof(enet_uint32)]; enet_uint32 i; } ip, mask;
    ip.i = 0;
    mask.i = 0;
    loopi(4)
    {
        char *end = NULL;
        int n = strtol(name, &end, 10);
        if(!end) break;
        if(end > name) { ip.b[i] = n; mask.b[i] = 0xFF; }
        name = end;
        while(*name && *name++ != '.');
    }
    gbaninfo &ban = gbans.add();
    ban.ip = ip.i;
    ban.mask = mask.i;

    loopvrev(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP) continue;
        if(checkgban(c.peer->address.host)) disconnect_client(c, DISC_BANREFUSE);
    }
}

inline void addban(client &cl, int reason, int type)
{
    ban b = { cl.peer->address, servmillis+scl.ban_time, type };
    bans.add(b);
    disconnect_client(cl, reason);
}

int getbantype(client &c)
{
    if(c.type==ST_LOCAL) return BAN_NONE;
    if(checkgban(c.peer->address.host)) return BAN_MASTER;
    if(ipblacklist.check(c.peer->address.host)) return BAN_BLACKLIST;
    loopv(bans)
    {
        ban &b = bans[i];
        if(b.millis < servmillis) { bans.remove(i--); }
        if(b.address.host == c.peer->address.host) return b.type;
    }
    return BAN_NONE;
}

void sendserveropinfo(client *receiver = NULL)
{
    loopv(clients)
        if(clients[i]->type!=ST_EMPTY)
            sendf(receiver, 1, "ri3", SV_SETPRIV, i, clients[i]->role);
}

#include "serveractions.h"

struct voteinfo
{
    int owner, callmillis, result, type;
    serveraction *action;

    voteinfo() : owner(0), callmillis(0), result(VOTE_NEUTRAL), type(SA_NUM), action(NULL) {}
    ~voteinfo() { delete action; }

    void end(int result, int veto)
    {
        if(action && !action->isvalid()) result = VOTE_NO; // don't perform() invalid votes
        if (valid_client(veto)) logline(ACLOG_INFO, "[%s] vote %s, forced by %s (%d)", clients[owner]->gethostname(), result == VOTE_YES ? "passed" : "failed", clients[veto]->formatname(), veto);
        else logline(ACLOG_INFO, "[%s] vote %s (%s)", clients[owner]->gethostname(), result == VOTE_YES ? "passed" : "failed", veto == -2 ? "enough votes" : veto == -3 ? "expiry" : "unknown");
        sendf(NULL, 1, "ri3", SV_VOTERESULT, result, veto);
        this->result = result;
        if(result == VOTE_YES)
        {
            if(valid_client(owner)) clients[owner]->lastvotecall = 0;
            if(action) action->perform();
        }
        loopv(clients) clients[i]->vote = VOTE_NEUTRAL;
    }

    bool isvalid() { return valid_client(owner) && action != NULL && action->isvalid(); }
    bool isalive() { return servmillis - callmillis < action->length; }

    void evaluate(bool forceend = false, int veto = VOTE_NEUTRAL, int vetoowner = -1)
    {
        if(result!=VOTE_NEUTRAL) return; // block double action
        if(action && !action->isvalid()) end(VOTE_NO, -1);
        int stats[VOTE_NUM+1] = {0};
        loopv(clients)
        {
            if (clients[i]->type == ST_EMPTY || clients[i]->type == ST_AI) continue;
            ++stats[clients[i]->vote%VOTE_NUM];
            ++stats[VOTE_NUM];
        }
        const int expireresult = stats[VOTE_YES] / (float)(stats[VOTE_NO] + stats[VOTE_YES]) > action->passratio ? VOTE_YES : VOTE_NO;
        sendf(NULL, 1, "ri4", SV_VOTEREMAIN, expireresult,
            (int)(stats[VOTE_NUM] * action->passratio) + 1 - stats[VOTE_YES],
            (int)(stats[VOTE_NUM] * action->passratio) + 1 - stats[VOTE_NO]);
        // can it end?
        if (forceend)
        {
            if (veto == VOTE_NEUTRAL) end(expireresult, -3);
            else end(veto, vetoowner);
        }
        else if (stats[VOTE_YES] / (float)stats[VOTE_NUM] > action->passratio || (!isdedicated && clients[owner]->type == ST_LOCAL))
            end(VOTE_YES, -2);
        else if (stats[VOTE_NO] / (float)stats[VOTE_NUM] > action->passratio)
            end(VOTE_NO, -2);
    }
};

static voteinfo *curvote = NULL;

void scallvotesuc(voteinfo *v)
{
    if(!v->isvalid()) return;
    DELETEP(curvote);
    curvote = v;
    clients[v->owner]->lastvotecall = servmillis;
    clients[v->owner]->nvotes--; // successful votes do not count as abuse
    logline(ACLOG_INFO, "[%s] %s called a vote: %s", clients[v->owner]->gethostname(), clients[v->owner]->formatname(), v->action && v->action->desc ? v->action->desc : "[unknown]");
}

void scallvoteerr(voteinfo *v, int error)
{
    if(!valid_client(v->owner)) return;
    client &owner = *clients[v->owner];
    sendf(&owner, 1, "ri2", SV_CALLVOTEERR, error);
    logline(ACLOG_INFO, "[%s] %s failed to call a vote: %s (%s)", owner.gethostname(), owner.formatname(), v->action && v->action->desc ? v->action->desc : "[unknown]", voteerrorstr(error));
}

void sendcallvote(client *cl = NULL);

bool scallvote(voteinfo *v) // true if a regular vote was called
{
    ASSERT(v);
    int area = isdedicated ? EE_DED_SERV : EE_LOCAL_SERV;
    int error = -1;
    client *c = clients[v->owner];

    int time = servmillis - c->lastvotecall;
    if ( c->nvotes > 0 && time > 4*60*1000 ) c->nvotes -= time/(4*60*1000);
    if ( c->nvotes < 0 || c->role >= CR_ADMIN ) c->nvotes = 0;
    c->nvotes++;

    if( !v || !v->isvalid() ) error = VOTEE_INVALID;
    else if( v->action->reqcall > c->role ) error = VOTEE_PERMISSION;
    else if( !(area & v->action->area) ) error = VOTEE_AREA;
    else if( curvote && curvote->result==VOTE_NEUTRAL ) error = VOTEE_CUR;
    else if( c->role == CR_DEFAULT && v->action->isdisabled() ) error = VOTEE_DISABLED;
    else if( (c->lastvotecall && servmillis - c->lastvotecall < 60*1000 && c->role < CR_ADMIN && numclients()>1) || c->nvotes > 3 ) error = VOTEE_MAX;

    if(error>=0)
    {
        scallvoteerr(v, error);
        return false;
    }
    else
    {
        scallvotesuc(v);
        sendcallvote();
        // owner auto votes yes
        sendf(NULL, 1, "ri3", SV_VOTE, v->owner, (c->vote = VOTE_YES));
        curvote->evaluate();
        return true;
    }
}

void sendcallvote(client *cl)
{
    if (!curvote || curvote->result != VOTE_NEUTRAL)
        return;
    packetbuf q(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(q, SV_CALLVOTE);
    putint(q, curvote->owner);
    putint(q, curvote->type);
    putint(q, curvote->action->length - servmillis + curvote->callmillis);
    switch (curvote->type)
    {
        case SA_KICK:
            putint(q, ((kickaction *)curvote->action)->cn);
            sendstring(((kickaction *)curvote->action)->reason, q);
            break;
        case SA_BAN:
            putint(q, ((banaction *)curvote->action)->minutes);
            putint(q, ((banaction *)curvote->action)->cn);
            sendstring(((banaction *)curvote->action)->reason, q);
            break;
        case SA_MASTERMODE:
            putint(q, ((mastermodeaction *)curvote->action)->mode);
            break;
        case SA_AUTOTEAM:
            putint(q, ((autoteamaction *)curvote->action)->enable ? 1 : 0);
            break;
        case SA_FORCETEAM:
            putint(q, ((forceteamaction *)curvote->action)->team);
            putint(q, ((forceteamaction *)curvote->action)->cn);
            break;
        case SA_GIVEADMIN:
            putint(q, ((giveadminaction *)curvote->action)->give);
            putint(q, ((giveadminaction *)curvote->action)->cn);
            break;
        case SA_MAP:
            putint(q, ((mapaction *)curvote->action)->muts);
            putint(q, ((mapaction *)curvote->action)->mode + (((mapaction *)curvote->action)->queue ? G_MAX : 0));
            sendstring(((mapaction *)curvote->action)->map, q);
            break;
        case SA_RECORDDEMO:
            putint(q, ((recorddemoaction *)curvote->action)->enable ? 1 : 0);
            break;
        case SA_CLEARDEMOS:
            putint(q, ((cleardemosaction *)curvote->action)->demo);
            break;
        case SA_SERVERDESC:
            sendstring(((serverdescaction *)curvote->action)->desc, q);
            break;
        case SA_BOTBALANCE:
            putint(q, ((botbalanceaction *)curvote->action)->balance);
            break;
        case SA_SUBDUE:
            putint(q, ((subdueaction *)curvote->action)->cn);
            break;
        case SA_REVOKE:
            putint(q, ((revokeaction *)curvote->action)->cn);
            break;
        case SA_STOPDEMO:
            // compatibility
        default:
        case SA_REMBANS:
        case SA_SHUFFLETEAMS:
            break;
    }
    // send vote states
    loopv(clients) if (clients[i]->vote != VOTE_NEUTRAL)
    {
        putint(q, SV_VOTE);
        putint(q, i);
        putint(q, clients[i]->vote);
    }
    sendpacket(cl, 1, q.finalize());
}

void setpriv(client &cl, int priv)
{
    if (!priv) // relinquish
    {
        if (!cl.role) return; // no privilege to relinquish
        sendf(NULL, 1, "ri4", SV_CLAIMPRIV, cl.clientnum, cl.role, 1);
        logline(ACLOG_INFO, "[%s] %s relinquished %s access", cl.gethostname(), cl.formatname(), privname(cl.role));
        cl.role = CR_DEFAULT;
        sendserveropinfo();
        return;
    }
    else if (cl.role >= priv)
    {
        sendf(&cl, 1, "ri4", SV_CLAIMPRIV, cl.clientnum, priv, 2);
        return;
    }
    /*
    else if(priv >= PRIV_ADMIN)
    {
        loopv(clients)
            if(clients[i]->type != ST_EMPTY && clients[i]->authpriv < PRIV_MASTER && clients[i]->priv == PRIV_MASTER)
                setpriv(*clients[i], PRIV_NONE);
    }
    */
    cl.role = priv;
    sendf(NULL, 1, "ri4", SV_CLAIMPRIV, cl.clientnum, cl.role, 0);
    logline(ACLOG_INFO, "[%s] %s claimed %s access", cl.gethostname(), cl.formatname(), privname(cl.role));
    sendserveropinfo();
    //if(curvote) curvote->evaluate();
}

void senddisconnectedscores(client *cl)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_DISCSCORES);
    if(mastermode == MM_MATCH)
    {
        loopv(savedscores)
        {
            savedscore &sc = savedscores[i];
            if(sc.valid)
            {
                putint(p, sc.team);
                sendstring(sc.name, p);
                putint(p, sc.flagscore);
                putint(p, sc.frags);
                putint(p, sc.deaths);
                putint(p, sc.points);
            }
        }
    }
    putint(p, -1);
    sendpacket(cl, 1, p.finalize());
}

const char *disc_reason(int reason)
{
    static const char *disc_reasons[DISC_NUM] = {
        "normal", "end of packet/overread", "vote-kicked", "vote-banned", "tag type", "connection refused - banned", "incorrect password", "failed login", "the server is FULL - try again later", "mastermode is \"private\" - must be \"open\"",
        "bad nickname", "nickname is IP protected", "nickname requires password", "duplicate connection", "error - packet flood", "timeout",
        "extension", "ext2", "ext3"
    };
    return reason >= 0 && (size_t)reason < sizeof(disc_reasons)/sizeof(disc_reasons[0]) ? disc_reasons[reason] : "unknown";
}

void clientdisconnect(client &cl)
{
    const int n = cl.clientnum;
    sdropflag(n);
    // remove assists/revenge
    loopv(clients) if(valid_client(i) && i != n)
    {
        clientstate &cs = clients[i]->state;
        cs.damagelog.removeobj(n);
        cs.revengelog.removeobj(n);
    }
    // delete kill confirmed references
    loopv(sconfirms)
    {
        if(sconfirms[i].actor == n) sconfirms[i].actor = -1;
        if(sconfirms[i].target == n) sconfirms[i].target = -1;
    }
    cl.zap();
}

#include "serverai.h"

void disconnect_client(client &c, int reason)
{
    if(c.type!=ST_TCPIP) return;
    sdropflag(c.clientnum);
    // reassign/delete AI
    loopv(clients)
        if(clients[i]->ownernum == c.clientnum)
            if(!shiftai(*clients[i], -1, c.clientnum))
                deleteai(*clients[i]);
    // remove privilege
    if(c.role) setpriv(c, CR_DEFAULT);
    c.state.lastdisc = servmillis;
    const char *scoresaved = "";
    if(c.haswelcome)
    {
        // save score
        savedscore *sc = findscore(c, true);
        if(sc)
        {
            sc->save(c.state, c.team);
            scoresaved = ", score saved";
        }
        // save limits
        findlimit(c, true);
    }
    int sp = (servmillis - c.connectmillis) / 1000;
    if (reason >= 0) logline(ACLOG_INFO, "[%s] disconnecting client %s (%s) cn %d, %d seconds played%s", c.gethostname(), c.name, disc_reason(reason), c.clientnum, sp, scoresaved);
    else logline(ACLOG_INFO, "[%s] disconnected client %s cn %d, %d seconds played%s", c.gethostname(), c.name, c.clientnum, sp, scoresaved);
    totalclients--;
    c.peer->data = (void *)-1;
    if(reason>=0) enet_peer_disconnect(c.peer, reason);
    sendf(NULL, 1, "ri3", SV_CDIS, c.clientnum, max(reason, 0));
    if(curvote) curvote->evaluate();
    // do cleanup
    clientdisconnect(c);
    extern void freeconnectcheck(int cn);
    freeconnectcheck(c.clientnum); // disconnect - ms check is void
    if(*scoresaved && mastermode == MM_MATCH) senddisconnectedscores();
    checkai(); // disconnect
    convertcheck();
}

#include "serverauth.h"

void sendresume(client &c, bool broadcast)
{
    sendf(broadcast ? NULL : &c, 1, "ri9i6vvi", SV_RESUME,
            c.clientnum,
            c.state.state == CS_WAITING ? CS_DEAD : c.state.state,
            c.state.lifesequence,
            c.state.primary,
            c.state.secondary,
            c.state.perk1,
            c.state.perk2,
            c.state.gunselect,
            c.state.flagscore,
            c.state.frags,
            c.state.deaths,
            c.state.health,
            c.state.armour,
            c.state.points,
            NUMGUNS, c.state.ammo,
            NUMGUNS, c.state.mag,
            -1);
}

bool restorescore(client &c)
{
    //if(ci->local) return false;
    savedscore *sc = findscore(c, false);
    if(sc && sc->valid)
    {
        sc->restore(c.state);
        sc->valid = false;
        if ( c.connectmillis - c.state.lastdisc < 5000 ) c.state.reconnections++;
        else if ( c.state.reconnections ) c.state.reconnections--;
        return true;
    }
    return false;
}

void sendservinfo(client &c)
{
    sendf(&c, 1, "ri5", SV_SERVINFO, c.clientnum, isdedicated ? SERVER_PROTOCOL_VERSION : PROTOCOL_VERSION, c.salt, scl.serverpassword[0] ? 1 : 0);
}

void putinitai(client &c, ucharbuf &p)
{
    // cn, b.ownernum, b.bot_seed = randomMT(), b.skin[0], b.skin[1], b.team, b.level
    putint(p, SV_INITAI);
    putint(p, c.clientnum);
    putint(p, c.ownernum);
    putint(p, c.bot_seed);
    putint(p, c.skin[0]);
    putint(p, c.skin[1]);
    putint(p, c.team);
    putint(p, c.level);
}

void putinitclient(client &c, packetbuf &p)
{
    if(c.type == ST_AI) return putinitai(c, p);
    putint(p, SV_INITCLIENT);
    putint(p, c.clientnum);
    sendstring(c.name, p);
    putint(p, c.skin[TEAM_CLA]);
    putint(p, c.skin[TEAM_RVSF]);
    putint(p, c.level);
    putint(p, c.team);
    putint(p, c.acbuildtype | (c.authpriv > -1 ? 0x02 : 0));
    putint(p, c.acthirdperson);
}

void sendinitclient(client &c)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putinitclient(c, p);
    sendpacket(NULL, 1, p.finalize(), c.clientnum);
}

inline void welcomeinitclient(packetbuf &p, int exclude = -1)
{
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type==ST_EMPTY || !c.isauthed || c.clientnum == exclude) continue;
        putinitclient(c, p);
    }
}

void welcomepacket(packetbuf &p, client *c)
{
    if(!smapname[0]) maprot.next(false);

    const int n = c ? c->clientnum : -1;
    const int numcl = numclients();

    putint(p, SV_WELCOME);
    putint(p, smapname[0] && !m_demo(gamemode) ? numcl : -1);
    if(smapname[0] && !m_demo(gamemode))
    {
        putmap(p);
        if(smode>1 || (smode==0 && numnonlocalclients()>0))
        {
            putint(p, SV_TIMEUP);
            putint(p, (gamemillis>=gamelimit || forceintermission) ? gamelimit : gamemillis);
            putint(p, gamelimit);
            //putint(p, minremain*60);
        }
        if (m_flags(gamemode))
        {
            if (m_secure(gamemode))
            {
                loopv(ssecures) putsecureflaginfo(p, ssecures[i]);
            }
            else
            {
                loopi(2) putflaginfo(p, i);
            }
        }
    }
    savedscore *sc = NULL;
    if(c)
    {
        if(c->type == ST_TCPIP) sendserveropinfo(c);
        c->team = mastermode == MM_MATCH && sc ? team_tospec(sc->team) : TEAM_SPECT;
        putint(p, SV_SETTEAM);
        putint(p, n);
        putint(p, c->team | (FTR_SILENT << 4));

        putint(p, SV_FORCEDEATH);
        putint(p, n);
        sendf(NULL, 1, "ri2x", SV_FORCEDEATH, n, n);
    }
    if(!c || clients.length()>1)
    {
        putint(p, SV_RESUME);
        loopv(clients)
        {
            client &c = *clients[i];
            if(c.type!=ST_TCPIP || c.clientnum==n) continue;
            putint(p, c.clientnum);
            putint(p, c.state.state == CS_WAITING ? CS_DEAD : c.state.state);
            putint(p, c.state.lifesequence);
            putint(p, c.state.primary);
            putint(p, c.state.secondary);
            putint(p, c.state.perk1);
            putint(p, c.state.perk2);
            putint(p, c.state.gunselect);
            putint(p, c.state.flagscore);
            putint(p, c.state.frags);
            putint(p, c.state.deaths);
            putint(p, c.state.health);
            putint(p, c.state.armour);
            putint(p, c.state.points);
            loopi(NUMGUNS) putint(p, c.state.ammo[i]);
            loopi(NUMGUNS) putint(p, c.state.mag[i]);
        }
        putint(p, -1);
        welcomeinitclient(p, n);
    }
    putint(p, SV_SERVERMODE);
    putint(p, sendservermode(false));
    const char *motd = scl.motd[0] ? scl.motd : infofiles.getmotd(c ? c->lang : "");
    if(motd)
    {
        putint(p, SV_TEXT);
        putint(p, -1);
        putint(p, 0);
        putint(p, 0);
        sendstring(motd, p);
    }
}

void sendwelcome(client &cl, int chan)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    welcomepacket(p, &cl);
    sendpacket(&cl, chan, p.finalize());
    cl.haswelcome = true;
}

void forcedeath(client &cl, bool gib)
{
    sdropflag(cl.clientnum);
    cl.state.state = CS_DEAD;
    cl.state.respawn();
    sendf(NULL, 1, "ri2", gib ? SV_FORCEGIB : SV_FORCEDEATH, cl.clientnum);
}

bool movechecks(client &cp, const vec &newo, const int newf, const int newg)
{
    clientstate &cs = cp.state;
    // Only check alive players (skip editmode users)
    if(cs.state != CS_ALIVE) return true;
    // new crouch and scope
    const bool newcrouching = (newf >> 7) & 1;
    if (cs.crouching != newcrouching)
    {
        cs.crouching = newcrouching;
        cs.crouchmillis = gamemillis - CROUCHTIME + min(gamemillis - cs.crouchmillis, CROUCHTIME);
    }
    const bool newscoping = (newg >> 4) & 1;
    if (newscoping != cs.scoping)
    {
        if (!newscoping || (ads_gun(cs.gunselect) && ads_classic_allowed(cs.gunselect)))
        {
            cs.scoping = newscoping;
            cs.scopemillis = gamemillis - ADSTIME(cs.perk2 == PERK_TIME) + min(gamemillis - cs.scopemillis, ADSTIME(cs.perk2 == PERK_TIME));
        }
        // else: clear the scope from the packet? other clients can ignore it?
    }
    // deal damage from movements
    if(!cs.protect(gamemillis, gamemode, mutators))
    {
        // medium transfer (falling damage)
        const bool newonfloor = (newf>>4)&1, newonladder = (newf>>5)&1, newunderwater = newo.z < smapstats.hdr.waterlevel;
        if((newonfloor || newonladder || newunderwater) && !cs.onfloor)
        {
            const float dz = cs.fallz - cs.o.z;
            if(newonfloor)
            { // air to solid
                bool hit = false;
                if(dz > 10)
                { // fall at least 2.5 meters to fall onto others
                    loopv(clients)
                    {
                        client &t = *clients[i];
                        clientstate &ts = t.state;
                        // basic checks
                        if (t.type == ST_EMPTY || ts.state != CS_ALIVE || i == cp.clientnum) continue;
                        // check from above
                        if(ts.o.distxy(cs.o) > 2.5f*PLAYERRADIUS) continue;
                        // check from side
                        const float dz2 = cs.o.z - ts.o.z;
                        if(dz2 > PLAYERABOVEEYE + 2 || -dz2 > PLAYERHEIGHT + 2) continue;
                        if(!isteam(&t, &cp) && !ts.protect(gamemillis, gamemode, mutators))
                            serverdied(t, cp, 0, OBIT_FALL, FRAG_NONE, cs.o);
                        hit = true;
                    }
                }
                if(!hit) // not cushioned by another player
                {
                    // 4 meters without damage + 2/0.5 HP/meter
                    // int damage = ((cs.fallz - newo.z) - 16) * HEALTHSCALE / (cs.perk1 == PERK1_LIGHT ? 8 : 2);
                    // 2 meters without damage, then square up to 10^2 = 100 for up to 20m (50m with lightweight)
                    int damage = 0;
                    if(dz > 8)
                        damage = powf(min<float>((dz - 8) / 4 / 2, 10), 2.f) * HEALTHSCALE; // 10 * 10 = 100
                    if(damage >= 1*HEALTHSCALE) // don't heal the player
                    {
                        // maximum damage is 99 for balance purposes
                        serverdamage(cp, cp, min(damage, (m_classic(gamemode, mutators) ? 30 : 99) * HEALTHSCALE), OBIT_FALL, FRAG_NONE, cs.o); // max 99, "30" (15) for classic
                    }
                }
            }
            else if(newunderwater && dz > 32) // air to liquid, more than 8 meters
                serverdamage(cp, cp, (m_classic(gamemode, mutators) ? 20 : 35) * HEALTHSCALE, OBIT_FALL_WATER, FRAG_NONE, cs.o); // fixed damage @ 35, "20" (10) for classic
            cs.onfloor = true;
        }
        else if(!newonfloor)
        { // airborne
            if(cs.onfloor || cs.fallz < cs.o.z) cs.fallz = cs.o.z;
            cs.onfloor = false;
        }
        // did we die?
        if(cs.state != CS_ALIVE) return false;
    }
    // out of map check
    vec checko(newo);
    checko.z += PLAYERHEIGHT / 2; // because the positions are now at the feet
    if (/*cp.type != ST_LOCAL &&*/ !m_edit(gamemode) && checkpos(checko, false))
    {
        if (cp.type == ST_AI) cp.suicide(NUMGUNS + 11);
        else
        {
            logline(ACLOG_INFO, "[%s] %s collides with the map (%d)", cp.gethostname(), cp.formatname(), ++cp.mapcollisions);
            defformatstring(msg)("%s \f2collides with the map \f5- \f3forcing death", cp.formatname());
            sendservmsg(msg);
            sendf(&cp, 1, "ri", SV_MAPIDENT);
            forcedeath(cp);
            cp.isonrightmap = false; // cannot spawn until you get the right map
        }
        return false; // no pickups for you!
    }
    // the rest can proceed without killing
    // item pickups
    if(!m_zombie(gamemode) || cp.team != TEAM_CLA)
        loopv(sents)
    {
        entity &e = sents[i];
        const bool cantake = (e.spawned && cs.canpickup(e.type, cp.type == ST_AI)), canheal = (e.type == I_HEALTH && cs.wounds.length());
        if(!cantake && !canheal) continue;
        const int ls = (1 << maplayout_factor) - 2, maplayoutid = getmaplayoutid(e.x, e.y);
        const bool getmapz = maplayout && e.x > 2 && e.y > 2 && e.x < ls && e.y < ls;
        const char &mapz = getmapz ? getblockfloor(maplayoutid, false) : 0;
        vec v(e.x, e.y, getmapz ? (mapz + e.attr1) : cs.o.z);
        float dist = cs.o.dist(v);
        if(dist > 3) continue;
        if(canheal)
        {
            // healing station
            addpt(cp, HEALWOUNDPT * cs.wounds.length(), PR_HEALWOUND);
            cs.wounds.shrink(0);
        }
        if(cantake)
        {
            // server side item pickup, acknowledge first client that moves to the entity
            e.spawned = false;
            int spawntime(int type);
            sendf(NULL, 1, "ri4", SV_ITEMACC, i, cp.clientnum, e.spawntime = spawntime(e.type));
            cs.pickup(sents[i].type);
        }
    }
    // flags
    if (m_flags(gamemode) && !m_secure(gamemode)) loopi(2)
    {
        void flagaction(int flag, int action, int actor);
        sflaginfo &f = sflaginfos[i];
        sflaginfo &of = sflaginfos[team_opposite(i)];
        bool forcez = false;
        vec v(-1, -1, cs.o.z);
        if (m_bomber(gamemode) && i == cp.team && f.state == CTFF_STOLEN && f.actor_cn == cp.clientnum)
        {
            v.x = of.x;
            v.y = of.y;
        }
        else switch (f.state)
        {
            case CTFF_STOLEN:
                if (!m_return(gamemode, mutators) || i != cp.team) break;
            case CTFF_INBASE:
                v.x = f.x; v.y = f.y;
                break;
            case CTFF_DROPPED:
                if (f.drop_cn == cp.clientnum && f.dropmillis + 2000 > servmillis) break;
                v.x = f.pos[0]; v.y = f.pos[1];
                forcez = true;
                break;
        }
        if (v.x < 0) continue;
        if (forcez)
            v.z = f.pos[2];
        else
            v.z = getsblock(getmaplayoutid((int)v.x, (int)v.y)).floor;
        float dist = cs.o.dist(v);
        if (dist > 2) continue;
        if (m_capture(gamemode))
        {
            if (i == cp.team) // it's our flag
            {
                if (f.state == CTFF_DROPPED)
                {
                    if (m_return(gamemode, mutators) /*&& (of.state != CTFF_STOLEN || of.actor_cn != sender)*/)
                        flagaction(i, FA_PICKUP, cp.clientnum);
                    else flagaction(i, FA_RETURN, cp.clientnum);
                }
                else if (f.state == CTFF_STOLEN && cp.clientnum == f.actor_cn)
                    flagaction(i, FA_RETURN, cp.clientnum);
                else if (f.state == CTFF_INBASE && of.state == CTFF_STOLEN && of.actor_cn == cp.clientnum && gamemillis >= of.stolentime + 1000)
                    flagaction(team_opposite(i), FA_SCORE, cp.clientnum);
            }
            else
            {
                /*if(m_return && of.state == CTFF_STOLEN && of.actor_cn == sender) flagaction(team_opposite(i), FA_RETURN, sender);*/
                flagaction(i, f.state == CTFF_INBASE ? FA_STEAL : FA_PICKUP, cp.clientnum);
            }
        }
        else if (m_hunt(gamemode) || m_bomber(gamemode))
        {
            // BTF only: score their flag by bombing their base!
            if (f.state == CTFF_STOLEN)
            {
                flagaction(i, FA_SCORE, cp.clientnum);
                // nuke message + points
                nuke(cp, !m_gsp1(gamemode, mutators), false); // no suicide for demolition, but suicide for bomber
                /*
                if(m_gsp1(gamemode, mutators))
                {
                    // force round win
                    loopv(clients) if(valid_client(i) && clients[i]->state.state == CS_ALIVE && !isteam(clients[i], &cp))
                    forcedeath(clients[i], true);
                }
                else explosion(cp, v, WEAP_GRENADE); // identical to self-nades, replace with something else?
                */
            }
            else if (i == cp.team)
            {
                if (m_hunt(gamemode)) f.drop_cn = -1; // force pickup
                flagaction(i, FA_PICKUP, cp.clientnum);
            }
            else if (f.state == CTFF_DROPPED && gamemillis >= of.stolentime + 500)
                flagaction(i, m_hunt(gamemode) ? FA_SCORE : FA_RETURN, cp.clientnum);
        }
        else if (m_keep(gamemode) && f.state == CTFF_INBASE)
            flagaction(i, FA_PICKUP, cp.clientnum);
        else if (m_ktf2(gamemode, mutators) && f.state != CTFF_STOLEN)
        {
            bool cantake = of.state != CTFF_STOLEN || of.actor_cn != cp.clientnum || !m_team(gamemode, mutators);
            if (!cantake)
            {
                cantake = true;
                loopv(clients)
                    if (i != cp.clientnum && valid_client(i) && clients[i]->type != ST_AI && clients[i]->team == cp.team)
                    {
                        cantake = false;
                        break;
                    }
            }
            if (cantake) flagaction(i, FA_PICKUP, cp.clientnum);
        }
    }
    // kill confirmed
    loopv(sconfirms) if (sconfirms[i].o.dist(cs.o) < 5.f)
    {
        if (cp.team == sconfirms[i].team)
        {
            addpt(cp, KCKILLPTS, PR_KC);
            //usesteamscore(sconfirms[i].team).points += sconfirms[i].points;
            // the following line doesn't have to set the valid flag twice
            //getsteamscore(sconfirms[i].team).frags += sconfirms[i].frag;
            //++usesteamscore(sconfirms[i].death).deaths;
        }
        else
        {
            addpt(cp, KCDENYPTS, cp.clientnum == sconfirms[i].target ? PR_KD_SELF : PR_KD);
            if (valid_client(sconfirms[i].actor) && clients[sconfirms[i].actor]->type != ST_AI)
                addptreason(*clients[sconfirms[i].actor], PR_KD_ENEMY);
        }

        sendf(NULL, 1, "ri2", SV_CONFIRMREMOVE, sconfirms[i].id);
        sconfirms.remove(i--);
    }
    // TODO throwing knife pickup
    return true;
}

int checktype(int type, client &cl)
{
    if (type < 0 || type >= SV_NUM) return -1; // out of range
    if (cl.type == ST_TCPIP)
    {
        // only allow edit messages in coop-edit mode
        if (!m_edit(smode) && type >= SV_EDITH && type <= SV_NEWMAP) return -1; // SV_EDITMODE is handled
        // overflow
        static const int exempt[] = { SV_POS, SV_POSC, SV_SPAWN, SV_SHOOT, SV_SHOOTC, SV_EXPLODE };
        // these types don't not contribute to overflow, just because the bots will have to send them too
        loopi(sizeof(exempt) / sizeof(int)) if (type == exempt[i]) return type;
        if(++cl.overflow >= 200) return -2;
    }
    return type;
}

// server side processing of updates: does very little and most state is tracked client only
// could be extended to move more gameplay to server (at expense of lag)

void process(ENetPacket *packet, int sender, int chan)
{
    ucharbuf p(packet->data, packet->dataLength);
    char text[MAXTRANS];
    client *cl = sender>=0 ? clients[sender] : NULL;
    int type;

    if(cl && !cl->isauthed)
    {
        if(chan==0) return;
        else if(chan!=1 || getint(p)!=SV_CONNECT) disconnect_client(*cl, DISC_TAGT);
        else
        {
            cl->acversion = getint(p);
            cl->acbuildtype = getint(p);
            cl->acthirdperson = getint(p);
            cl->acguid = getint(p) & (0x80 | 0x1F00 | 0x40 | 0x20 | 0x8 | 0x4);
            const int connectauthtoken = getint(p), connectauthuser = getint(p);
            getstring(text, p);
            filtername(text, text);
            if(!text[0]) copystring(text, "unarmed");
            copystring(cl->name, text, MAXNAMELEN + 1);
            getstring(text, p);
            copystring(cl->pwd, text);
            getstring(text, p);
            filterlang(cl->lang, text);
            cl->state.nextprimary = getint(p);
            cl->state.nextsecondary = getint(p);
            cl->state.nextperk1 = getint(p);
            cl->state.nextperk2 = getint(p);
            loopi(2) cl->skin[i] = getint(p);
            logversion(*cl);

            int disc = p.remaining() ? DISC_TAGT : allowconnect(*cl, connectauthtoken, connectauthuser);

            if (disc) disconnect_client(*cl, disc);
            else cl->isauthed = true;
        }
        if(!cl->isauthed) return;

        if(cl->type==ST_TCPIP)
        {
            loopv(clients) if(i != sender)
            {
                client &dup = *clients[i];
                if(dup.type==ST_TCPIP && dup.peer->address.host==cl->peer->address.host && dup.peer->address.port==cl->peer->address.port)
                    disconnect_client(dup, DISC_DUP);
            }

            // ask the master-server about this client
            extern void connectcheck(int cn, int guid, const char *hostname, int authreq, int authuser);
            connectcheck(sender, cl->acguid, cl->hostname, cl->authreq, cl->authuser);
        }

        sendwelcome(*cl);
        if(restorescore(*cl)) { sendresume(*cl, true); senddisconnectedscores(); }
        else if(cl->type==ST_TCPIP) senddisconnectedscores(cl);
        sendinitclient(*cl);
        findlimit(*cl, false);
        if (curvote)
        {
            sendcallvote(cl);
            curvote->evaluate();
        }

        checkai(); // connected
        // convertcheck();
        while(reassignai());
    }

    if(!cl) { logline(ACLOG_ERROR, "<NULL> client in process()"); return; }  // should never happen anyway

    if(packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;

    #define QUEUE_MSG { if(cl->type==ST_TCPIP) while(curmsg<p.length()) cl->messages.add(p.buf[curmsg++]); }
    #define QUEUE_BUF(body) \
    { \
        if(cl->type==ST_TCPIP) \
        { \
            curmsg = p.length(); \
            { body; } \
        } \
    }
    #define QUEUE_INT(n) QUEUE_BUF(putint(cl->messages, n))
    #define QUEUE_UINT(n) QUEUE_BUF(putuint(cl->messages, n))
    #define QUEUE_STR(text) QUEUE_BUF(sendstring(text, cl->messages))
    #define MSG_PACKET(packet) \
        packetbuf buf(16 + p.length() - curmsg, ENET_PACKET_FLAG_RELIABLE); \
        putint(buf, SV_CLIENT); \
        putint(buf, cl->clientnum); \
        /*putuint(buf, p.length() - curmsg);*/ \
        buf.put(&p.buf[curmsg], p.length() - curmsg); \
        ENetPacket *packet = buf.finalize();

    int curmsg;
    while((curmsg = p.length()) < p.maxlen)
    {
        type = checktype(getint(p), *cl);

        #ifdef _DEBUG
        if(type!=SV_POS && type!=SV_POSC && type!=SV_CLIENTPING && type!=SV_PINGPONG && type!=SV_CLIENT)
        {
            DEBUGVAR(cl->name);
            ASSERT(type>=0 && type<SV_NUM);
            DEBUGVAR(messagenames[type]);
            protocoldebug(true);
        }
        else protocoldebug(false);
        #endif

        switch(type)
        {
            case SV_TEXT:
            {
                int flags = getint(p) & 3;
                int voice = getint(p);
                getstring(text, p);
                filtertext(text, text);
                trimtrailingwhitespace(text);
                if(*text)
                    sendtext(text, *cl, flags, voice);
                break;
            }

            case SV_TEXTPRIVATE:
            {
                int targ = getint(p);
                getstring(text, p);
                filtertext(text, text);
                trimtrailingwhitespace(text);

                if(!valid_client(targ)) break;
                client *target = clients[targ];

                if(*text)
                {
                    const char *forbidden = forbiddenlist.forbidden(text);
                    if(forbidden)
                    {
                        sendservmsg("\f3Please do not spam; your message was not delivered.", cl);
                        logline(ACLOG_INFO, "[%s] %s says to %s: '%s', SPAM detected", cl->gethostname(), cl->formatname(), target->formatname(), text);
                    }
                    else if(spamdetect(*cl, text))
                    {
                        sendservmsg("\f3Watch your language! Your message was not delivered.", cl);
                        logline(ACLOG_INFO, "[%s] %s says to %s: '%s', forbidden (%s)", cl->gethostname(), cl->formatname(), target->formatname(), text, forbidden);
                    }
                    else
                    {
                        bool allowed = !(mastermode == MM_MATCH && cl->team != target->team) && cl->role >= roleconf('t');
                        logline(ACLOG_INFO, "[%s] %s says to %s: '%s'%s", cl->gethostname(), cl->formatname(), target->formatname(), text, allowed ? "" : ", disallowed");
                        if(allowed) sendf(target, 1, "riis", SV_TEXTPRIVATE, cl->clientnum, text);
                    }
                }
            }
            break;

            case SV_MAPIDENT:
            {
                int gzs = getint(p);
                int rev = getint(p);
                if(!isdedicated || (smapstats.cgzsize == gzs && smapstats.hdr.maprevision == rev))
                { // here any game really starts for a client: spawn, if it's a new game - don't spawn if the game was already running
                    cl->isonrightmap = true;
                    if (cl->loggedwrongmap) logline(ACLOG_INFO, "[%s] %s is now on the right map: revision %d/%d", cl->gethostname(), cl->formatname(), rev, gzs);
                    bool spawn = false;
                    if(team_isspect(cl->team))
                    {
                        if(numclients() < 2 && !m_demo(gamemode) && mastermode != MM_MATCH) // spawn on empty servers
                        {
                            spawn = updateclientteam(*cl, chooseteam(*cl), FTR_SILENT);
                        }
                    }
                    else
                    {
                        if((cl->freshgame || numclients() < 2) && !m_demo(gamemode)) spawn = true;
                    }
                    cl->freshgame = false;
                    if(spawn) sendspawn(*cl);

                }
                else
                {
                    forcedeath(*cl);
                    logline(ACLOG_INFO, "[%s] %s is on the wrong map: revision %d/%d", cl->gethostname(), cl->formatname(), rev, gzs);
                    cl->loggedwrongmap = true;
                }
                break;
            }

            case SV_WEAPCHANGE: // cn weap
            {
                int cn = getint(p), gunselect = getint(p);
                if (!cl->hasclient(cn)) break;
                client &cp = *clients[cn];
                if (gunselect < 0 || gunselect >= NUMGUNS) break;
#if (SERVER_BUILTIN_MOD & 8)
                if (gunselect != cp.state.primary)
                {
                    // stop bots from switching back
                    sendf(NULL, 1, "ri5", SV_RELOAD, cn, gunselect, cp.state.mag[gunselect] = 0, cp.state.ammo[gunselect] = 0);
                    // disallow switching
                    sendf(sender, 1, "ri3", SV_WEAPCHANGE, cn, cp.state.primary);
                }
#else
                cp.state.gunwait[cp.state.gunselect = gunselect] += SWITCHTIME(cp.state.perk1 == PERK_TIME);
                QUEUE_MSG;
#endif
                break;
            }

            case SV_QUICKSWITCH:
            {
                const int cn = getint(p);
                if (!cl->hasclient(cn)) break;
                clientstate &cs = clients[cn]->state;
                cs.gunwait[cs.gunselect = cs.primary] += SWITCHTIME(cs.perk1 == PERK_TIME) / 2;
                QUEUE_MSG;
                break;
            }

            case SV_THROWNADE:
            {
                const int cn = getint(p);
                vec from, vel;
                loopi(3) from[i] = getint(p)/DMF;
                loopi(3) vel[i] = getint(p)/DNF;
                int cooked = clamp(getint(p), 1, NADETTL);
                if (!cl->hasclient(cn)) break;
                clientstate &cps = clients[cn]->state;
                if (cps.grenades.throwable <= 0) break;
                --cps.grenades.throwable;
                checkpos(from);
                if (vel.magnitude() > NADEPOWER) vel.normalize().mul(NADEPOWER);
                sendf(NULL, 1, "ri9x", SV_THROWNADE, cn, (int)(from.x*DMF), (int)(from.y*DMF), (int)(from.z*DMF),
                    (int)(vel.x*DNF), (int)(vel.y*DNF), (int)(vel.z*DNF), cooked, sender);
                break;
            }

            case SV_LOADOUT:
            {
                int nextprimary = getint(p), nextsecondary = getint(p), perk1 = getint(p), perk2 = getint(p);
                clientstate &cs = cl->state;
                cs.nextperk1 = perk1;
                cs.nextperk2 = perk2;
                if (nextprimary >= 0 && nextprimary < NUMGUNS)
                    cs.nextprimary = nextprimary;
                if (nextsecondary >= 0 && nextsecondary < NUMGUNS)
                    cs.nextsecondary = nextsecondary;
                break;
            }

            case SV_SWITCHNAME:
            {
                QUEUE_MSG;
                getstring(text, p);
                filtername(text, text);
                if(!text[0]) copystring(text, "unarmed");
                QUEUE_STR(text);
                bool namechanged = strcmp(cl->name, text) != 0;
                if (namechanged) logline(ACLOG_INFO, "[%s] %s changed name to %s", cl->gethostname(), cl->formatname(), text);
                copystring(cl->name, text, MAXNAMELEN + 1);
                if(namechanged)
                {
                    // very simple spam detection (possible FIXME: centralize spam detection)
                    if(servmillis - cl->lastprofileupdate < 1000)
                    {
                        ++cl->fastprofileupdates;
                        if(cl->fastprofileupdates == 3) sendservmsg("\f3Please do not spam");
                        // TODO: ACR would delay/throttle name changes
                    }
                    else if(servmillis - cl->lastprofileupdate > 10000) cl->fastprofileupdates = 0;
                    cl->lastprofileupdate = servmillis;

                    switch (const int nwl = nickblacklist.checkwhitelist(*cl))
                    {
                        case NWL_PWDFAIL:
                        case NWL_IPFAIL:
                            logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong IP/PWD", cl->gethostname(), cl->name);
                            disconnect_client(*cl, nwl == NWL_IPFAIL ? DISC_NAME_IP : DISC_NAME_PWD);
                            break;

                        case NWL_UNLISTED:
                        {
                            int l = nickblacklist.checkblacklist(cl->name);
                            if(l >= 0)
                            {
                                logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", cl->gethostname(), cl->name, l);
                                disconnect_client(*cl, DISC_NAME);
                            }
                            break;
                        }
                    }
                }
                break;
            }

            case SV_SETTEAM:
            {
                int t = getint(p);
                if (cl->team == t || !team_isvalid(t)) break;

                if (cl->role < CR_ADMIN && t < TEAM_SPECT)
                {
                    if (mastermode == MM_OPEN && cl->state.forced && team_base(cl->team) != team_base(t))
                    {
                        // no free will changes for forced people
                        sendf(cl, 1, "ri2", SV_TEAMDENY, 0x10);
                        break;
                    }
                    else if (!autobalance_mode)
                    {
                        sendf(cl, 1, "ri2", SV_TEAMDENY, 0x11);
                        break;
                    }
                    else if (m_team(gamemode, mutators))
                    {
                        int *teamsizes = numteamclients(sender);
                        if (mastermode == MM_MATCH)
                        {
                            if (matchteamsize && t != TEAM_SPECT)
                            {
                                if (team_base(t) != team_base(cl->team))
                                {
                                    // no switching sides in match mode when teamsize is set
                                    sendf(cl, 1, "ri2", SV_TEAMDENY, 0x12);
                                    break;
                                }
                                else if (team_isactive(t) && teamsizes[t] >= matchteamsize)
                                {
                                    // ensure maximum team size
                                    sendf(cl, 1, "ri2", SV_TEAMDENY, t);
                                    break;
                                }
                            }
                        }
                        else if (team_isactive(t) && autoteam && teamsizes[t] > teamsizes[team_opposite(t)])
                        {
                            // don't switch to an already bigger team
                            sendf(cl, 1, "ri2", SV_TEAMDENY, t);
                            break;
                        }
                    }
                }
                updateclientteam(*cl, t, FTR_PLAYERWISH);
                checkai(); // user switch
                // convertcheck();
                break;
            }

            case SV_SWITCHSKIN:
            {
                loopi(2) cl->skin[i] = getint(p);
                QUEUE_MSG;

                if(servmillis - cl->lastprofileupdate < 1000)
                {
                    ++cl->fastprofileupdates;
                    if(cl->fastprofileupdates == 3) sendservmsg("\f3Please do not spam");
                    // TODO: throttle/delay skin switches
                }
                else if(servmillis - cl->lastprofileupdate > 10000) cl->fastprofileupdates = 0;
                cl->lastprofileupdate = servmillis;
                break;
            }

            case SV_THIRDPERSON:
                sendf(NULL, 1, "ri3x", SV_THIRDPERSON, sender, cl->acthirdperson = getint(p), sender);
                break;

            case SV_LEVEL:
                sendf(NULL, 1, "ri3x", SV_LEVEL, sender, cl->level = clamp(getint(p), 1, MAXLEVEL), sender);
                break;

            case SV_TRYSPAWN:
            {
                clientstate &cs = cl->state;
                if (cs.state == CS_WAITING) // dequeue spawn
                {
                    cs.state = CS_DEAD;
                    sendf(cl, 1, "ri2", SV_TRYSPAWN, 0);
                    break;
                }
                if (!cl->isonrightmap || !maplayout) // need the map for spawning
                {
                    //sendf(sender, 1, "ri", SV_MAPIDENT);
                    break;
                }
                if (cs.state != CS_DEAD || cs.lastspawn >= 0) break; // not dead or already enqueued
                if (team_isspect(cl->team))
                {
                    updateclientteam(*cl, chooseteam(*cl), FTR_PLAYERWISH);
                    checkai(); // spawn unspectate
                    // convertcheck();
                }
                // can the player be enqueued?
                if (!canspawn(*cl)) break;
                // enqueue for spawning
                cs.state = CS_WAITING;
                const int waitremain = SPAWNDELAY - gamemillis + cs.lastdeath;
                sendf(cl, 1, "ri2", SV_TRYSPAWN, waitremain >= 1 ? waitremain : 1);
                break;
            }

            case SV_SPAWN:
            {
                int cn = getint(p), ls = getint(p);
                vec o;
                loopi(3) o[i] = getint(p) / DMF;
                if(!cl->hasclient(cn)) break;
                client &cp = *clients[cn];
                clientstate &cs = cp.state;
                if((cs.state!=CS_ALIVE && cs.state!=CS_DEAD) || ls!=cs.lifesequence || cs.lastspawn<0) break;
                cs.lastspawn = gamemillis; // extend spawn protection time
                cs.state = CS_ALIVE;
                vec lasto = cs.o;
                cs.o = cp.spawnp = o;
                cp.spj = cp.ldt = 40;
                // send spawn packet, but not with QUEUE_BUF -- we need it sequenced
                sendf(NULL, 1, "rxi3i6vvi4", sender, SV_SPAWN, cn, ls,
                    cs.health, cs.armour, cs.perk1, cs.perk2, cs.primary, cs.secondary,
                    NUMGUNS, cs.ammo, NUMGUNS, cs.mag, (int)(o.x*DMF), (int)(o.y*DMF), (int)(o.z*DMF), cl->y);
                // bad spawn adjustment?
                if (!m_edit(gamemode) && (lasto.distxy(cs.o) >= 6 * PLAYERRADIUS)) // || fabs(lasto.z - cs.o.z) >= 2 * PLAYERHEIGHT))
                    serverdied(cp, cp, 0, OBIT_SPAWN, FRAG_NONE, cs.o);
                break;
            }

            case SV_SUICIDE:
            {
                const int cn = getint(p);
                if(!cl->hasclient(cn)) break;
                client *cp = clients[cn];
                if(cp->state.state != CS_DEAD)
                {
#if (SERVER_BUILTIN_MOD & 64)
                    if (cp->type != ST_AI && !cp->nuked)
                    {
                        cp->nuked = true;
                        nuke(*cp, true, true, true);
                    }
                    else
#endif
                    cp->suicide( cn == sender ? OBIT_DEATH : OBIT_BOT, cn == sender ? FRAG_GIB : FRAG_NONE);
                }
                break;
            }

            case SV_SHOOT: // TODO: cn id weap to.x to.y to.z heads.length heads.v
            case SV_SHOOTC: // TODO: cn id weap
            {
                const int cn = getint(p), id = getint(p), weap = getint(p);
                shotevent *ev = new shotevent(0, id, weap);
                if (!(ev->compact = (type == SV_SHOOTC)))
                {
                    loopi(3) ev->to[i] = getint(p) / DMF;
                    loopi(MAXCLIENTS)
                    {
                        posinfo info;
                        info.cn = getint(p);
                        if (info.cn < 0) break; // cannot hit self, so MAXCLIENTS should be the end
                        loopj(3) info.o[j] = getint(p) / DMF;
                        loopj(3) info.head[j] = getint(p) / DMF;
                        // reject duplicate positions
                        int k = 0;
                        for (k = 0; k < ev->pos.length(); k++)
                            if (ev->pos[k].cn == info.cn)
                                break;
                        // add if not found
                        if (k >= ev->pos.length())
                            ev->pos.add(info);
                    }
                }

                client *cp = cl->hasclient(cn) ? clients[cn] : NULL;
                if (cp)
                {
                    ev->millis = cp->getmillis(gamemillis, id);
                    cp->addevent(ev);
                }
                else delete ev;
                break;
            }

            case SV_EXPLODE: // cn id weap flags x y z
            {
                const int cn = getint(p), id = getint(p), weap = getint(p), flags = getint(p);
                vec o;
                loopi(3) o[i] = getint(p) / DMF;
                client *cp = cl->hasclient(cn) ? clients[cn] : NULL;
                if (!cp) break;
                cp->addevent(new destroyevent(cp->getmillis(gamemillis, id), id, weap, flags, o));
                break;
            }

            case SV_AKIMBO:
            {
                const int cn = getint(p), id = getint(p);
                if (!cl->hasclient(cn)) break;
                client *cp = clients[cn];
                cp->addevent(new akimboevent(cp->getmillis(gamemillis, id), id));
                break;
            }

            case SV_RELOAD: // cn id weap
            {
                int cn = getint(p), id = getint(p), weap = getint(p);
                if (!cl->hasclient(cn)) break;
                client *cp = clients[cn];
                cp->addevent(new reloadevent(cp->getmillis(gamemillis, id), id, weap));
                break;
            }

            case SV_PINGPONG:
                sendf(cl, 1, "ii", SV_PINGPONG, getint(p));
                break;

            case SV_CLIENTPING:
            {
                int ping = getint(p);
                if(!cl) break;
                ping = clamp(ping, 0, 9999);
                ping = cl->ping == 9999 ? ping : (cl->ping * 4 + ping) / 5;
                loopv(clients)
                    if(clients[i]->type != ST_EMPTY && (i == sender || clients[i]->ownernum == sender))
                        clients[i]->ping = ping;
                sendf(NULL, 1, "i3", SV_CLIENTPING, sender, ping);
                break;
            }

            case SV_POS:
            {
                int cn = getint(p);
                const bool broadcast = cl->hasclient(cn);
                vec newo, dvel;
                loopi(3) newo[i] = getuint(p)/DMF;
                int newy = getuint(p);
                int newp = getint(p);
                int newg = getuint(p);
                if ((newg >> 0) & 1) getint(p); // roll
                if ((newg >> 1) & 1) dvel.x = getint(p) / DVELF;
                if ((newg >> 2) & 1) dvel.y = getint(p) / DVELF;
                if ((newg >> 3) & 1) dvel.z = getint(p) / DVELF;
                int newf = getuint(p);
                if(!valid_client(cn)) break;
                client &cp = *clients[cn];
                clientstate &cs = cp.state;
                if(interm || !broadcast || (cs.state!=CS_ALIVE && cs.state!=CS_EDITING)) break;
                // relay if still alive
                if(!cp.isonrightmap || m_demo(gamemode) || !movechecks(cp, newo, newf, newg)) break;
                cs.o = newo;
                cs.vel.add(dvel);
                cp.y = newy;
                cp.p = newp;
                cp.g = newg;
                cp.f = newf;
                checkmove(cp);
                if(!isdedicated) break;
                cp.position.setsize(0);
                while(curmsg<p.length()) cp.position.add(p.buf[curmsg++]);
                break;
            }

            case SV_POSC:
            {
                bitbuf<ucharbuf> q(p);
                int cn = q.getbits(5);
                const bool broadcast = cl->hasclient(cn);
                int usefactor = q.getbits(2) + 7;
                int xt = q.getbits(usefactor + 4);
                int yt = q.getbits(usefactor + 4);
                const int newy = (q.getbits(9)*360)/512;
                const int newp = ((q.getbits(8)-128)*90)/127;
                if(!q.getbits(1)) q.getbits(6);
                vec dvel;
                if (!q.getbits(1))
                {
                    dvel.x = (q.getbits(4) - 8) / DVELF;
                    dvel.y = (q.getbits(4) - 8) / DVELF;
                    dvel.z = (q.getbits(4) - 8) / DVELF;
                }
                const int newf = q.getbits(8);
                int negz = q.getbits(1);
                int zfull = q.getbits(1);
                int s = q.rembits();
                if(s < 3) s += 8;
                if(zfull) s = 11;
                int zt = q.getbits(s);
                if(negz) zt = -zt;
                int g1 = q.getbits(1); // scoping
                int g2 = q.getbits(1); // shooting
                const int newg = (g1<<4) | (g2<<5);
                if(!broadcast) break;
                client &cp = *clients[cn];
                clientstate &cs = cp.state;
                if(!cp.isonrightmap || p.remaining() || p.overread()) { p.flags = 0; break; }
                if(((newf >> 6) & 1) != (cs.lifesequence & 1) || usefactor != (smapstats.hdr.sfactor < 7 ? 7 : smapstats.hdr.sfactor)) break;
                // relay if still alive
                vec newo(xt / DMF, yt / DMF, zt / DMF);
                if(m_demo(gamemode) || !movechecks(cp, newo, newf, newg)) break;
                cs.o = newo;
                cs.vel.add(dvel);
                cp.y = newy;
                cp.p = newp;
                cp.f = newf;
                cp.g = newg;
                checkmove(cp);
                if(!isdedicated) break;
                cp.position.setsize(0);
                while(curmsg<p.length()) cp.position.add(p.buf[curmsg++]);
                break;
            }

            case SV_SENDMAP:
            {
                getstring(text, p);
                filtertext(text, text);
                const char *sentmap = behindpath(text), *reject = NULL;
                int mapsize = getint(p);
                int cfgsize = getint(p);
                int cfgsizegz = getint(p);
                int revision = getint(p);
                if(p.remaining() < mapsize + cfgsizegz || MAXMAPSENDSIZE < mapsize + cfgsizegz)
                {
                    p.forceoverread();
                    break;
                }
                int mp = findmappath(sentmap);
                if(readonlymap(mp))
                {
                    reject = "map is ro";
                    defformatstring(msg)("\f3map upload rejected: map %s is readonly", sentmap);
                    sendservmsg(msg, cl);
                }
                else if( scl.incoming_limit && ( scl.incoming_limit << 20 ) < incoming_size + mapsize + cfgsizegz )
                {
                    reject = "server incoming reached its limits";
                    sendservmsg("\f3server does not support more incomings: limit reached", cl);
                }
                else if(mp == MAP_NOTFOUND && strchr(scl.mapperm, 'C') && cl->role < CR_ADMIN)
                {
                    reject = "no permission for initial upload";
                    sendservmsg("\f3initial map upload rejected: you need to be admin", cl);
                }
                else if(mp == MAP_TEMP && revision >= mapbuffer.revision && !strchr(scl.mapperm, 'u') && cl->role < CR_ADMIN) // default: only admins can update maps
                {
                    reject = "no permission to update";
                    sendservmsg("\f3map update rejected: you need to be admin", cl);
                }
                else if(mp == MAP_TEMP && revision < mapbuffer.revision && !strchr(scl.mapperm, 'r') && cl->role < CR_ADMIN) // default: only admins can revert maps to older revisions
                {
                    reject = "no permission to revert revision";
                    sendservmsg("\f3map revert to older revision rejected: you need to be admin to upload an older map", cl);
                }
                else
                {
                    if(mapbuffer.sendmap(sentmap, mapsize, cfgsize, cfgsizegz, &p.buf[p.len]))
                    {
                        incoming_size += mapsize + cfgsizegz;
                        logline(ACLOG_INFO,"[%s] %s sent map %s, rev %d, %d + %d(%d) bytes written",
                            clients[sender]->gethostname(), clients[sender]->formatname(), sentmap, revision, mapsize, cfgsize, cfgsizegz);
                        defformatstring(msg)("%s (%d) up%sed map %s, rev %d%s", clients[sender]->formatname(), sender, mp == MAP_NOTFOUND ? "load": "dat", sentmap, revision,
                            /*strcmp(sentmap, behindpath(smapname)) || smode == GMODE_COOPEDIT ? "" :*/ "\f3 (restart game to use new map version)");
                        sendservmsg(msg);
                    }
                    else
                    {
                        reject = "write failed (no 'incoming'?)";
                        sendservmsg("\f3map upload failed", cl);
                    }
                }
                if (reject)
                {
                    logline(ACLOG_INFO,"[%s] %s sent map %s rev %d, rejected: %s",
                        clients[sender]->gethostname(), clients[sender]->formatname(), sentmap, revision, reject);
                }
                p.len += mapsize + cfgsizegz;
                break;
            }

            case SV_RECVMAP:
            {
                if(mapbuffer.available())
                {
                    resetflag(cl->clientnum); // drop ctf flag
                    savedscore *sc = findscore(*cl, true); // save score
                    if(sc) sc->save(cl->state, cl->team);
                    mapbuffer.sendmap(cl, 2);
                    cl->mapchange(true);
                    sendwelcome(*cl, 2); // resend state properly
                }
                else sendservmsg("no map to get", cl);
                break;
            }

            case SV_REMOVEMAP:
            {
                getstring(text, p);
                filtertext(text, text);
                string filename;
                const char *rmmap = behindpath(text), *reject = NULL;
                int mp = findmappath(rmmap);
                int reqrole = strchr(scl.mapperm, 'D') ? CR_ADMIN : (strchr(scl.mapperm, 'd') ? CR_DEFAULT : CR_ADMIN + 100);
                if(cl->role < reqrole) reject = "no permission";
                else if(readonlymap(mp)) reject = "map is readonly";
                else if(mp == MAP_NOTFOUND) reject = "map not found";
                else
                {
                    formatstring(filename)(SERVERMAP_PATH_INCOMING "%s.cgz", rmmap);
                    remove(filename);
                    formatstring(filename)(SERVERMAP_PATH_INCOMING "%s.cfg", rmmap);
                    remove(filename);
                    defformatstring(msg)("map '%s' deleted", rmmap);
                    sendservmsg(msg, cl);
                    logline(ACLOG_INFO, "[%s] deleted map %s", clients[sender]->gethostname(), rmmap);
                }
                if (reject)
                {
                    logline(ACLOG_INFO, "[%s] deleting map %s failed: %s", clients[sender]->gethostname(), rmmap, reject);
                    defformatstring(msg)("\f3can't delete map '%s', %s", rmmap, reject);
                    sendservmsg(msg, cl);
                }
                break;
            }

            case SV_DROPFLAG:
            {
                int fl = clienthasflag(sender);
                flagaction(fl, FA_DROP, sender);
                /*
                while(fl >= 0)
                {
                    flagaction(fl, FA_DROP, sender);
                    fl = clienthasflag(sender);
                }
                */
                break;
            }

            case SV_CLAIMPRIV: // claim
            {
                getstring(text, p);
                pwddetail pd;
                pd.line = -1;
                if (cl->type == ST_LOCAL) setpriv(*cl, CR_MAX);
                else if (!passwords.check(cl->name, text, cl->salt, &pd))
                {
                    if (cl->authpriv >= CR_MASTER)
                    {
                        logline(ACLOG_INFO, "[%s] %s was already authed for %s", cl->gethostname(), cl->formatname(), privname(cl->authpriv));
                        setpriv(*cl, cl->authpriv);
                    }
                    else if (cl->role < CR_ADMIN && text[0])
                    {
                        disconnect_client(*cl, DISC_SOPLOGINFAIL); // avoid brute-force
                        return;
                    }
                }
                else if (!pd.priv)
                {
                    sendf(cl, 1, "ri4", SV_CLAIMPRIV, sender, 0, 3);
                    if (pd.line >= 0) logline(ACLOG_INFO, "[%s] %s used non-privileged password on line %d", cl->gethostname(), cl->formatname(), pd.line);
                }
                else
                {
                    setpriv(*cl, pd.priv);
                    if (pd.line >= 0) logline(ACLOG_INFO, "[%s] %s used %s password on line %d", cl->gethostname(), cl->formatname(), privname(pd.priv), pd.line);
                }
                break;
            }

            case SV_SETPRIV: // relinquish
                setpriv(*cl, CR_DEFAULT);
                break;

            case SV_AUTH_ACR_CHAL:
            {
                unsigned char hash[20];
                loopi(20) hash[i] = p.get();
                logline(ACLOG_INFO, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", 
                    hash[0], hash[1], hash[2], hash[3],
                    hash[4], hash[5], hash[6], hash[7],
                    hash[8], hash[9], hash[10], hash[11],
                    hash[12], hash[13], hash[14], hash[15],
                    hash[16], hash[17], hash[18], hash[19]);
                bool answered = answerchallenge(*cl, hash);
                if (cl->authreq && answered) cl->isauthed = true;
                else checkauthdisc(*cl);
                break;
            }

            case SV_CALLVOTE:
            {
                voteinfo *vi = new voteinfo;
                vi->type = getint(p);
                switch(vi->type)
                {
                    case SA_MAP:
                    {
                        int muts = getint(p), mode = getint(p);
                        getstring(text, p);
                        filtertext(text, text);
                        if (text[0] == '+' && text[1] == '1')
                        {
                            if (nextmapname[0])
                            {
                                copystring(text, nextmapname);
                                mode = nextgamemode;
                                muts = nextmutators;
                            }
                            else
                            {
#ifdef STANDALONE
                                int ccs = mode ? maprot.next(false, false) : maprot.get_next();
                                configset *c = maprot.get(ccs);
                                if (c)
                                {
                                    strcpy(text, c->mapname);
                                    mode = c->mode;
                                    muts = c->muts;
                                }
                                else fatal("unable to get next map in maprot");
#else
                                defformatstring(nextmapalias)("nextmap_%s", getclientmap());
                                strcpy(text, getalias(nextmapalias));
                                mode = getclientmode();
                                muts = mutators;
#endif
                            }
                        }
                        int qmode = (mode >= G_MAX ? mode - G_MAX : mode);
                        modecheck(qmode, muts);
                        if(m_demo(mode)) vi->action = new demoplayaction(newstring(text));
                        else
                        {
                            char *vmap = newstring(text[0] ? behindpath(text) : "");
                            vi->action = new mapaction(vmap, qmode, muts, sender, qmode!=mode);
                        }
                        break;
                    }
                    case SA_KICK:
                    {
                        int cn = getint(p);
                        getstring(text, p);
                        filtertext(text, text);
                        trimtrailingwhitespace(text);
                        vi->action = new kickaction(cn, text, cn == sender);
                        break;
                    }
                    case SA_BAN:
                    {
                        int m = getint(p), cn = getint(p);
                        getstring(text, p);
                        m = clamp(m, 1, 60);
                        if (cl->role < CR_ADMIN && m >= 10) m = 10;
                        filtertext(text, text);
                        trimtrailingwhitespace(text);
                        vi->action = new banaction(cn, m, text, cn == sender);
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
                    case SA_FORCETEAM:
                    {
                        int team = getint(p), cn = getint(p);
                        vi->action = new forceteamaction(cn, sender, team);
                        break;
                    }
                    case SA_GIVEADMIN:
                    {
                        int role = getint(p), cn = getint(p);
                        vi->action = new giveadminaction(cn, role, sender);
                        break;
                    }
                    // case SA_MAP:
                    case SA_RECORDDEMO:
                        vi->action = new recorddemoaction(getint(p) != 0);
                        break;
                    case SA_STOPDEMO:
                        // compatibility
                        break;
                    case SA_CLEARDEMOS:
                        vi->action = new cleardemosaction(getint(p));
                        break;
                    case SA_SERVERDESC:
                        getstring(text, p);
                        filtertext(text, text);
                        vi->action = new serverdescaction(newstring(text), sender);
                        break;
                    case SA_SHUFFLETEAMS:
                        vi->action = new shuffleteamaction();
                        break;
                    case SA_BOTBALANCE:
                        vi->action = new botbalanceaction(getint(p));
                        break;
                    case SA_SUBDUE:
                        vi->action = new subdueaction(getint(p));
                        break;
                    case SA_REVOKE:
                        vi->action = new revokeaction(getint(p));
                        break;
                    default:
                        vi->type = SA_KICK;
                        vi->action = new kickaction(-1, "<invalid type placeholder>", false);
                        break;
                }
                vi->owner = sender;
                vi->callmillis = servmillis;
                if(!scallvote(vi)) delete vi;
                break;
            }

            case SV_VOTE:
            {
                int vote = getint(p);
                if (!curvote || !curvote->action || vote < VOTE_YES || vote > VOTE_NO) break;
                if (cl->vote != VOTE_NEUTRAL)
                {
                    if (cl->vote == vote)
                    {
                        if (cl->role >= curvote->action->reqcall && cl->role >= curvote->action->reqveto)
                            curvote->evaluate(true, vote, sender);
                        else sendf(cl, 1, "ri2", SV_CALLVOTEERR, VOTEE_VETOPERM);
                        break;
                    }
                    else logline(ACLOG_INFO, "[%s] %s now votes %s", cl->gethostname(), cl->formatname(), vote == VOTE_NO ? "no" : "yes");
                }
                else logline(ACLOG_INFO, "[%s] %s voted %s", cl->gethostname(), cl->formatname(), vote == VOTE_NO ? "no" : "yes");
                cl->vote = vote;
                sendf(NULL, 1, "ri3", SV_VOTE, sender, vote);
                curvote->evaluate();
                break;
            }

            case SV_WHOIS:
            {
                const int cn = getint(p);
                if (!valid_client(cn) || clients[cn]->type != ST_TCPIP) break;
                sendf(NULL, 1, "ri4", SV_WHOIS, -1, sender, cn);
                uint ip = clients[cn]->peer->address.host;
                uchar mask = 0;
                if (cn == sender) mask = 32;
                else switch (clients[sender]->role)
                {
                    // admins and server owner: f.f.f.f/32 full ip
                    case CR_MAX: case CR_ADMIN: mask = 32; break;
                    // masters and users: f.f.h/12 full, full, half, empty
                    case CR_MASTER: case CR_DEFAULT: default: mask = 20; break;
                }
                if (mask < 32) ip &= (1 << mask) - 1;
                sendf(cl, 1, "ri5s", SV_WHOIS, cn, ip, mask, clients[cn]->peer->address.port, clients[cn]->authname);
                break;
            }

            case SV_LISTDEMOS:
                listdemos(cl);
                break;

            case SV_GETDEMO:
                senddemo(*cl, getint(p));
                break;

            case SV_SOUND:
            {
                int cn = getint(p), snd = getint(p);
                if (!cl->hasclient(cn)) break;
                switch(snd)
                {
                    case S_NOAMMO:
                        if(clients[cn]->state.mag) break;
                        // INTENTIONAL FALLTHROUGH
                    case S_JUMP:
#if (SERVER_BUILTIN_MOD & 1)
                        // native moonjump for humans
#if !(SERVER_BUILTIN_MOD & 2)
                        if (m_gib(gamemode, mutators))
#endif
                        if (snd == S_JUMP && cn == sender)
                        {
                            sendf(NULL, 1, "ri8i3", SV_DAMAGE, cn, cn, 200 * HEALTHSCALE, clients[cn]->state.armour, clients[cn]->state.health, GUN_KNIFE, FRAG_GIB, (int)(clients[cn]->state.o.x*DMF), (int)(clients[cn]->state.o.y*DMF), INT_MIN);
                            sendf(NULL, 1, "ri8i3", SV_DAMAGE, cn, cn, 0, clients[cn]->state.armour, clients[cn]->state.health, GUN_HEAL, FRAG_NONE, 0, 0, 0);
                        }
#endif
                    case S_SOFTLAND:
                    case S_HARDLAND:
                        QUEUE_MSG;
                        break;
                }
                break;
            }

            case SV_EXTENSION:
            {
                // AC server extensions
                //
                // rules:
                // 1. extensions MUST NOT modify gameplay or the behavior of the game in any way
                // 2. extensions may ONLY be used to extend or automate server administration tasks
                // 3. extensions may ONLY operate on the server and must not send any additional data to the connected clients
                // 4. extensions not adhering to these rules may cause the hosting server being banned from the masterserver
                //
                // also note that there is no guarantee that custom extensions will work in future AC versions


                getstring(text, p, 64);
                char *ext = text;   // extension specifier in the form of OWNER::EXTENSION, see sample below
                int n = getint(p);  // length of data after the specifier
                if(n < 0 || n > 50) return;

                // sample
                if(!strcmp(ext, "driAn::writelog"))
                {
                    // owner:       driAn - root@sprintf.org
                    // extension:   writelog - WriteLog v1.0
                    // description: writes a custom string to the server log
                    // access:      requires admin privileges
                    // usage:       /serverextension driAn::writelog "your log message here.."
                    // note:        There is a 49 character limit. The server will ignore messages with 50+ characters.

                    getstring(text, p, n);
                    if(valid_client(sender) && clients[sender]->role>=CR_ADMIN)
                    {
                        logline(ACLOG_INFO, "[%s] %s writes to log: %s", cl->gethostname(), cl->formatname(), text);
                        sendservmsg("your message has been logged", cl);
                    }
                }
                else if(!strcmp(ext, "set::teamsize"))
                {
                    // intermediate solution to set the teamsize (will be voteable)

                    getstring(text, p, n);
                    if(valid_client(sender) && clients[sender]->role>=CR_ADMIN && mastermode == MM_MATCH)
                    {
                        changematchteamsize(atoi(text));
                        defformatstring(msg)("match team size set to %d", matchteamsize);
                        sendservmsg(msg);
                    }
                }
                // else if()

                // add other extensions here

                else for(; n > 0; n--) getint(p); // ignore unknown extensions

                break;
            }

            // Edit messages
            case SV_EDITH:
            case SV_EDITT:
            case SV_EDITS:
            case SV_EDITD:
            case SV_EDITE:
            {
                int x = getint(p);
                int y = getint(p);
                int xs = getint(p);
                int ys = getint(p);
                int v = getint(p);
                switch (type)
                {
                    #define seditloop(body) \
                    { \
                        const int ssize = 1 << maplayout_factor; /* borrow the OUTBORD macro */ \
                        loop(xx, xs) loop(yy, ys) if (!OUTBORD(x + xx, y + yy)) \
                        { \
                            const int id = getmaplayoutid(x + xx, y + yy); \
                            body \
                        } \
                    }
                    case SV_EDITH:
                    {
                        int offset = getint(p);
                        seditloop({
                            if (!v) // ceil
                            {
                                getsblock(id).ceil += offset;
                                if (getsblock(id).ceil <= getsblock(id).floor)
                                    getsblock(id).ceil = getsblock(id).floor + 1;
                            }
                            else // floor
                            {
                                getsblock(id).floor += offset;
                                if (getsblock(id).floor >= getsblock(id).ceil)
                                    getsblock(id).floor = getsblock(id).ceil - 1;
                            }
                        });
                        break;
                    }
                    case SV_EDITS:
                    {
                        seditloop({ getsblock(id).type = v; });
                        break;
                    }
                    case SV_EDITD:
                    {
                        seditloop({
                            getsblock(id).vdelta += v;
                            if (getsblock(id).vdelta < 0)
                                getsblock(id).vdelta = 0;
                        });
                        break;
                    }
                    case SV_EDITE:
                    {
                        int low = 127, hi = -128;
                        seditloop({
                            if (getsblock(id).floor<low) low = getsblock(id).floor;
                            if (getsblock(id).ceil>hi) hi = getsblock(id).ceil;
                        });
                        seditloop({
                            if (!v) getsblock(id).ceil = hi; else getsblock(id).floor = low;
                            if (getsblock(id).floor >= getsblock(id).ceil) getsblock(id).floor = getsblock(id).ceil - 1;
                        });
                        break;
                    }
                    // ignore texture
                    case SV_EDITT: getint(p); break;
                }
                QUEUE_MSG;
                break;
            }

            case SV_EDITW:
                // set water level
                smapstats.hdr.waterlevel = getint(p);
                // water color alpha
                loopi(4) getint(p);
                QUEUE_MSG;
                break;

            case SV_EDITMODE:
            {
                const bool editing = getint(p) != 0;
                if (cl->state.state != (editing ? CS_ALIVE : CS_EDITING)) break;
                if (!m_edit(gamemode) && editing && cl->type == ST_TCPIP)
                {
                    // unacceptable!
                    cl->cheat("tried editmode");
                    break;
                }
                cl->state.state = editing ? CS_EDITING : CS_ALIVE;
                cl->state.onfloor = true; // prevent falling damage
                //cl->state.allowspeeding(gamemillis, 1000); // prevent speeding detection
                QUEUE_MSG;
                break;
            }

            case SV_EDITENT:
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
                if((e.spawned = e.fitsmode(smode, smuts)))
                    sendf(NULL, 1, "ri2", SV_ITEMSPAWN, id);
                e.spawntime = 0;
                QUEUE_MSG;
                break;
            }

            case SV_NEWMAP: // the server needs to create a new layout
            {
                const int size = getint(p);
                if(size < 0) maplayout_factor++;
                else maplayout_factor = size;
                DELETEA(maplayout)
                if(maplayout_factor >= 0) snewmap(maplayout_factor);
                QUEUE_MSG;
                break;
            }

            default:
            case -1:
                disconnect_client(*cl, DISC_TAGT);
                return;

            case -2:
                disconnect_client(*cl, DISC_OVERFLOW);
                return;
        }
    }

    if (p.overread() && sender >= 0) disconnect_client(*cl, DISC_EOP);

    #ifdef _DEBUG
    protocoldebug(false);
    #endif
}

void localclienttoserver(int chan, ENetPacket *packet)
{
    process(packet, 0, chan);
}

client &addclient()
{
    client *c = NULL;
    loopv(clients)
    {
        if(clients[i]->type==ST_EMPTY) { c = clients[i]; break; }
        else if(clients[i]->type==ST_AI) { deleteai(*clients[i]); c = clients[i]; break; }
    }
    if(!c)
    {
        c = new client;
        c->clientnum = clients.length();
        c->ownernum = -1;
        clients.add(c);
    }
    c->reset();
    return *c;
}

void checkintermission()
{
    if(minremain>0)
    {
        minremain = (gamemillis>=gamelimit || forceintermission) ? 0 : (gamelimit - gamemillis + 60000 - 1)/60000;
        sendf(NULL, 1, "ri3", SV_TIMEUP, (gamemillis>=gamelimit || forceintermission) ? gamelimit : gamemillis, gamelimit);
    }
    if(!interm && minremain<=0) interm = gamemillis+10000;
    forceintermission = false;
}

void resetserverifempty()
{
    loopv(clients) if(clients[i]->type!=ST_EMPTY) return;
    resetserver("", G_DM, G_M_NONE, 10);
    matchteamsize = 0;
#ifdef STANDALONE
    botbalance = -1;
#else
    botbalance = 0;
#endif
    autoteam = true;
    changemastermode(MM_OPEN);
    nextmapname[0] = '\0';
    savedlimits.shrink(0);
}

void sendworldstate()
{
    static enet_uint32 lastsend = 0;
    if(clients.empty()) return;
    enet_uint32 curtime = enet_time_get()-lastsend;
    if(curtime<40) return;
    bool flush = buildworldstate();
    lastsend += curtime - (curtime%40);
    if(flush) enet_host_flush(serverhost);
    if(demorecord) recordpackets = true; // enable after 'old' worldstate is sent
}

void rereadcfgs(void)
{
    maprot.read();
    ipblacklist.read();
    nickblacklist.read();
    forbiddenlist.read();
    passwords.read();
}

void loggamestatus(const char *reason)
{
    int fragscore[2] = {0, 0}, flagscore[2] = {0, 0}, pnum[2] = {0, 0};
    string text;
    formatstring(text)("%d minutes remaining", minremain);
    logline(ACLOG_INFO, "");
    logline(ACLOG_INFO, "Game status: %s on %s, %s, %s, %d clients%c %s",
                      modestr(gamemode, mutators), smapname, reason ? reason : text, mmfullname(mastermode), totalclients, custom_servdesc ? ',' : '\0', servdesc_current);
    if(!scl.loggamestatus) return;
    logline(ACLOG_INFO, "cn  name             %s%s score frag death %sping role    host", m_team(gamemode, mutators) ? "team " : "", m_flags(gamemode) ? "flag " : "", m_team(gamemode, mutators) ? "tk " : "");
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type == ST_EMPTY || !c.name[0]) continue;
        formatstring(text)("%2d%c %-16s ", c.clientnum, c.type == ST_AI ? '*' : ' ', c.name);         // cn * name
        if(m_team(gamemode, mutators)) concatformatstring(text, "%-4s ", team_string(c.team, true));  // teamname (abbreviated)
        if(m_flags(gamemode)) concatformatstring(text, "%4d ", c.state.flagscore);                    // flag
        concatformatstring(text, "%6d ", c.state.points);                                             // score
        concatformatstring(text, "%4d %5d", c.state.frags, c.state.deaths);                           // frag death
        if(m_team(gamemode, mutators)) concatformatstring(text, " %2d", c.state.teamkills);           // tk
        logline(ACLOG_INFO, "%s%5d %-7s %s", text, c.ping, privname(c.role), c.gethostname());
        if(c.team != TEAM_SPECT)
        {
            int t = team_base(c.team);
            flagscore[t] += c.state.flagscore;
            fragscore[t] += c.state.frags;
            pnum[t] += 1;
        }
    }
    if(mastermode == MM_MATCH)
    {
        loopv(savedscores)
        {
            savedscore &sc = savedscores[i];
            if(sc.valid)
            {
                formatstring(text)(m_team(gamemode, mutators) ? "%-4s " : "", team_string(sc.team, true));
                if(m_flags(gamemode)) concatformatstring(text, "%4d ", sc.flagscore);
                logline(ACLOG_INFO, "   %-16s %s%4d %5d%s    - disconnected", sc.name, text, sc.frags, sc.deaths, m_team(gamemode, mutators) ? "  -" : "");
                if(sc.team != TEAM_SPECT)
                {
                    int t = team_base(sc.team);
                    flagscore[t] += sc.flagscore;
                    fragscore[t] += sc.frags;
                    pnum[t] += 1;
                }
            }
        }
    }
    if(m_team(gamemode, mutators))
    {
        loopi(2) logline(ACLOG_INFO, "Team %4s:%3d players,%5d frags%c%5d flags", team_string(i), pnum[i], fragscore[i], m_flags(gamemode) ? ',' : '\0', flagscore[i]);
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

void serverslice(uint timeout)   // main server update, called from cube main loop in sp, or dedicated server loop
{
    static int msend = 0, mrec = 0, csend = 0, crec = 0, mnum = 0, cnum = 0;
#ifdef STANDALONE
    int nextmillis = (int)enet_time_get();
    if(svcctrl) svcctrl->keepalive();
#else
    int nextmillis = isdedicated ? (int)enet_time_get() : lastmillis;
#endif
    int diff = nextmillis - servmillis;
    gamemillis += diff;
    servmillis = nextmillis;
    servertime = ((diff + 3 * servertime)>>2);
    if (servertime > 40) serverlagged = servmillis;

#ifndef STANDALONE
    if(m_demo(gamemode))
    {
        readdemo();
        extern void silenttimeupdate(int milliscur, int millismax);
        silenttimeupdate(gamemillis, gametimemaximum);
    }
#endif

    if(minremain>0)
    {
        processevents();
        checkitemspawns(diff);
        bool ktfflagingame = false;
        if (m_flags(gamemode))
        {
            if (m_secure(gamemode))
            {
                loopv(ssecures)
                {
                    // service around every 40 milliseconds, for the best (25 fps)
                    // 10000ms / 255 units = ~39.2 ms / unit
                    int sec_diff = (gamemillis - ssecures[i].last_service) / 39;
                    if (!sec_diff) continue;
                    ssecures[i].last_service += sec_diff * 39;
                    if (!m_gsp1(gamemode, mutators)) sec_diff *= 2; // secure faster if non-direct
                    int teams_inside[2] = { 0 };
                    loopvj(clients)
                        if (valid_client(j) && (clients[j]->team >= 0 && clients[j]->team < 2) && clients[j]->state.state == CS_ALIVE && clients[j]->state.o.dist(ssecures[i].o) <= 8.f + PLAYERRADIUS)
                            ++teams_inside[clients[j]->team];
                    const int returnbonus = ssecures[i].team == TEAM_SPECT ? 1 : m_gsp1(gamemode, mutators) ? 2 : 0; // how fast flags can return to its original owner, but 0 counts as 1 if there is a defender
                    int defending = 0, opposing = 0;
                    loopj(2)
                    {
                        if (j == ssecures[i].enemy || ssecures[i].enemy == TEAM_SPECT) opposing += teams_inside[j];
                        else defending += teams_inside[j];
                    }
                    if (opposing > defending)
                    {
                        // starting to secure/overthrow?
                        if (ssecures[i].enemy == TEAM_SPECT)
                        {
                            int team_max = 0, max_team = TEAM_SPECT;
                            bool teams_matched = true;
                            loopj(2) // prepared for more teams
                            {
                                if (teams_inside[j] > team_max)
                                {
                                    team_max = teams_inside[j];
                                    max_team = j;
                                    teams_matched = false;
                                }
                                else if (teams_inside[j] == team_max) teams_matched = true;
                            }
                            // first frame: start to capture, but we don't know how many units to give
                            if (!teams_matched && max_team != ssecures[i].team) ssecures[i].enemy = max_team;
                        }
                        else
                        {
                            // securing/overthrowing
                            ssecures[i].overthrown += sec_diff * (opposing - defending);
                            if (ssecures[i].overthrown >= 255)
                            {
                                const bool is_secure = ssecures[i].team == TEAM_SPECT || m_gsp1(gamemode, mutators);
                                loopvj(clients)
                                    if (valid_client(j) && clients[j]->team == ssecures[i].enemy && clients[j]->state.state == CS_ALIVE && clients[j]->state.o.dist(ssecures[i].o) <= 8.f + PLAYERRADIUS)
                                    {
                                        addpt(*clients[j], SECUREPT, is_secure ? PR_SECURE_SECURE : PR_SECURE_OVERTHROW);
                                        clients[j]->state.invalidate().flagscore += m_gsp1(gamemode, mutators) ? ssecures[i].team == TEAM_SPECT ? 2 : 3 : 1;
                                    }
                                ssecures[i].team = is_secure ? ssecures[i].enemy : TEAM_SPECT;
                                if (is_secure)
                                {
                                    ssecures[i].enemy = TEAM_SPECT;
                                    ssecures[i].overthrown = 0;
                                }
                                else ssecures[i].overthrown = max(1, ssecures[i].overthrown - 255);
                            }
                            sendsecureflaginfo(&ssecures[i]);
                        }
                    }
                    else if ((defending > opposing || (!opposing && returnbonus)) && ssecures[i].overthrown)
                    {
                        // going back to the original owner
                        ssecures[i].overthrown -= sec_diff * (max(1, returnbonus) + defending - opposing);
                        if (ssecures[i].overthrown <= 0)
                        {
                            ssecures[i].enemy = TEAM_SPECT;
                            ssecures[i].overthrown = 0;
                        }
                        sendsecureflaginfo(&ssecures[i]);
                    }
                    // else: we are at an impasse
                }
                static int lastsecurereward = 0;
                if (servmillis > lastsecurereward + 5000)
                {
                    // reward points for having some bases secured
                    lastsecurereward = servmillis;
                    int bonuses[2] = { 0 };
                    loopv(ssecures)
                        if (ssecures[i].team >= 0 && ssecures[i].team < 2)
                        {
                            ++bonuses[ssecures[i].team];
                            //++usesteamscore(ssecures[i].team).flagscore;
                        }
                    loopv(clients)
                        if (valid_client(i) && (clients[i]->team >= 0 && clients[i]->team < 2) && bonuses[clients[i]->team])
                            addpt(*clients[i], SECUREDPT * bonuses[clients[i]->team], PR_SECURE_SECURED);
                }
            }
            else
            {
                loopi(2)
                {
                    sflaginfo &f = sflaginfos[i];
                    if (f.state == CTFF_DROPPED && gamemillis - f.lastupdate > (m_capture(gamemode) ? 30000 : 10000)) flagaction(i, FA_RESET, -1);
                    if (m_hunt(gamemode) && f.state == CTFF_INBASE && gamemillis - f.lastupdate > (smapstats.flags[0] && smapstats.flags[1] ? 10000 : 1000))
                        htf_forceflag(i);
                    if (m_keep(gamemode) && f.state == CTFF_STOLEN && gamemillis - f.lastupdate > 15000)
                        flagaction(i, FA_SCORE, -1);
                    if (f.state == CTFF_INBASE || f.state == CTFF_STOLEN) ktfflagingame = true;
                }
            }
        }
        if(m_keep(gamemode) && !ktfflagingame) flagaction(rnd(2), FA_RESET, -1); // ktf flag watchdog
        arenacheck();
        convertcheck();
        if ( scl.afk_limit && mastermode == MM_OPEN && next_afk_check < servmillis && gamemillis > 20 * 1000 ) check_afk();
    }

    if(curvote)
    {
        if(!curvote->isalive()) curvote->evaluate(true);
        if(curvote->result!=VOTE_NEUTRAL) DELETEP(curvote);
    }

    int nonlocalclients = numnonlocalclients();

    if(forceintermission || ((smode>1 || (gamemode==0 && nonlocalclients)) && gamemillis-diff>0 && gamemillis/60000!=(gamemillis-diff)/60000))
        checkintermission();
    if(m_demo(gamemode) && !demoplayback) maprot.restart();
    else if(interm && ( (scl.demo_interm && sending_demo) ? gamemillis>(interm<<1) : gamemillis>interm ) )
    {
        sending_demo = false;
        loggamestatus("game finished");
        if(demorecord) enddemorecord();
        interm = nextsendscore = 0;

        //start next game
        if(nextmapname[0]) startgame(nextmapname, nextgamemode, nextmutators);
        else maprot.next();
        nextmapname[0] = '\0';
    }

    resetserverifempty();

    if(!isdedicated) return;     // below is network only

    serverms(smode, smuts, numplayers(false), minremain, smapname, servmillis, serverhost->address, &mnum, &msend, &mrec, &cnum, &csend, &crec, SERVER_PROTOCOL_VERSION);

    if (autobalance && m_team(gamemode, mutators) && !m_zombie(gamemode) && !m_duke(gamemode, mutators) && !interm && servmillis - lastfillup > 5000 && refillteams()) lastfillup = servmillis;

    loopv(clients)
    {
        client &cl = *clients[i];
        if (cl.type == ST_TCPIP && (!cl.isauthed || cl.connectauth) && cl.connectmillis + 10000 <= servmillis)
            disconnect_client(cl, DISC_TIMEOUT);
    }

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
            logline(ACLOG_INFO, "Status at %s: %d remote clients, %.1f send, %.1f rec (K/sec);"
                                         " Ping: #%d|%d|%d; CSL: #%d|%d|%d (bytes)",
                                          timestring(true, "%d-%m-%Y %H:%M:%S"), nonlocalclients, serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024,
                                          mnum, msend, mrec, cnum, csend, crec);
            mnum = msend = mrec = cnum = csend = crec = 0;
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
                c.state.state = CS_DEAD;
                c.salt = rnd(0x1000000)*((servmillis%1000)+1);
                char hn[1024];
                copystring(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");
                logline(ACLOG_INFO, "[%s] client connected", c.gethostname());
                sendservinfo(c);
                totalclients++;
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
                disconnect_client(*clients[cn]);
                break;
            }

            default:
                break;
        }
    }
    sendworldstate();
}

void cleanupserver()
{
    if(serverhost) { enet_host_destroy(serverhost); serverhost = NULL; }
    if(svcctrl)
    {
        svcctrl->stop();
        DELETEP(svcctrl);
    }
    exitlogging();
}

int getpongflags(enet_uint32 ip)
{
    int flags = mastermode << PONGFLAG_MASTERMODE;
    flags |= scl.serverpassword[0] ? 1 << PONGFLAG_PASSWORD : 0;
    loopv(bans) if(bans[i].address.host == ip) { flags |= 1 << PONGFLAG_BANNED; break; }
    if (ipblacklist.check(ip))
        flags |= 1 << PONGFLAG_BLACKLIST;
    /*
    if (ipmutelist.check(ip))
        flags |= 1 << PONGFLAG_MUTE;
    */
    if (scl.bypassglobalbans)
        flags |= 1 << PONGFLAG_BYPASSBANS;
    if (scl.bypassglobalpriv)
        flags |= 1 << PONGFLAG_BYPASSPRIV;
    return flags;
}

void extping_namelist(ucharbuf &p)
{
    loopv(clients)
    {
        if(clients[i]->type == ST_TCPIP && clients[i]->isauthed) sendstring(clients[i]->name, p);
    }
    sendstring("", p);
}

void extping_serverinfo(ucharbuf &pi, ucharbuf &po)
{
    char lang[3];
    lang[0] = tolower(getint(pi)); lang[1] = tolower(getint(pi)); lang[2] = '\0';
    const char *reslang = lang, *buf = infofiles.getinfo(lang); // try client language
    if(!buf) buf = infofiles.getinfo(reslang = "en");     // try english
    sendstring(buf ? reslang : "", po);
    if(buf)
    {
        for(const char *c = buf; *c && po.remaining() > MAXINFOLINELEN + 10; c += strlen(c) + 1) sendstring(c, po);
        sendstring("", po);
    }
}

void extping_maprot(ucharbuf &po)
{
    putint(po, CONFIG_MAXPAR);
    string text;
    bool abort = false;
    loopv(maprot.configsets)
    {
        if(po.remaining() < 100) abort = true;
        configset &c = maprot.configsets[i];
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
    if(scl.maxclients)
        po.put(chokelog, scl.maxclients); // send logs for every used slot
}

void extinfo_cnbuf(ucharbuf &p, int cn)
{
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

void extinfo_statsbuf(ucharbuf &p, int pid, int bpos, ENetSocket &pongsock, ENetAddress &addr, ENetBuffer &buf, int len, int *csend)
{
    loopv(clients)
    {
        if(clients[i]->type != ST_TCPIP) continue;
        if(pid>-1 && clients[i]->clientnum!=pid) continue;

        bool ismatch = mastermode == MM_MATCH;
        putint(p,EXT_PLAYERSTATS_RESP_STATS);  // send player stats following
        putint(p,clients[i]->clientnum);  //add player id
        putint(p,clients[i]->ping);             //Ping
        sendstring(clients[i]->name,p);         //Name
        sendstring(team_string(clients[i]->team),p); //Team
        // "team_string(clients[i]->team)" sometimes return NULL according to RK, causing the server to crash. WTF ?
        putint(p,clients[i]->state.frags);      //Frags
        putint(p,clients[i]->state.flagscore);  //Flagscore
        putint(p,clients[i]->state.deaths);     //Death
        putint(p,clients[i]->state.teamkills);  //Teamkills
        putint(p,ismatch ? 0 : clients[i]->state.damage*100/max(clients[i]->state.shotdamage,1)); //Accuracy
        putint(p,ismatch ? 0 : clients[i]->state.health);     //Health
        putint(p,ismatch ? 0 : clients[i]->state.armour);     //Armour
        putint(p,ismatch ? 0 : clients[i]->state.gunselect);  //Gun selected
        putint(p,clients[i]->role);             //Role
        putint(p,clients[i]->state.state);      //State (Alive,Dead,Spawning,Lagged,Editing)
        uint ip = clients[i]->peer->address.host; // only 3 byte of the ip address (privacy protected)
        p.put((uchar*)&ip,3);

        buf.dataLength = len + p.length();
        enet_socket_send(pongsock, &addr, &buf, 1);
        *csend += (int)buf.dataLength;

        if(pid>-1) break;
        p.len=bpos;
    }
}

void extinfo_teamscorebuf(ucharbuf &p)
{
    putint(p, m_team(gamemode, mutators) ? EXT_ERROR_NONE : EXT_ERROR);
    putint(p, gamemode);
    putint(p, minremain); // possible TODO: use gamemillis, gamelimit here too?
    if(!m_team(gamemode, mutators)) return;

    int teamsizes[TEAM_NUM] = { 0 }, fragscores[TEAM_NUM] = { 0 }, flagscores[TEAM_NUM] = { 0 };
    loopv(clients) if(clients[i]->type!=ST_EMPTY && team_isvalid(clients[i]->team))
    {
        teamsizes[clients[i]->team] += 1;
        fragscores[clients[i]->team] += clients[i]->state.frags;
        flagscores[clients[i]->team] += clients[i]->state.flagscore;
    }

    loopi(TEAM_NUM) if(teamsizes[i])
    {
        sendstring(team_string(i), p); // team name
        putint(p, fragscores[i]); // add fragscore per team
        putint(p, m_flags(gamemode) ? flagscores[i] : -1); // add flagscore per team
        putint(p, -1); // ?
    }
}


#ifndef STANDALONE
void localdisconnect()
{
    loopv(clients) if(clients[i]->type==ST_LOCAL) clients[i]->zap();
}

void localconnect()
{
    servstate.reset();
    client &c = addclient();
    c.type = ST_LOCAL;
    c.role = CR_ADMIN;
    copystring(c.hostname, "local");
    sendservinfo(c);
}
#endif

string server_name = "unarmed server";

void quitproc(int param)
{
    // this triggers any "atexit"-calls:
    exit(param == 2 ? EXIT_SUCCESS : EXIT_FAILURE); // 3 is the only reply on Win32 apparently, SIGINT == 2 == Ctrl-C
}

void initserver(bool dedicated, int argc, char **argv)
{
    const char *service = NULL;

    for(int i = 1; i<argc; i++)
    {
        if(!scl.checkarg(argv[i]))
        {
            char *a = &argv[i][2];
            if(!scl.checkarg(argv[i]) && argv[i][0]=='-') switch(argv[i][1])
            {
                case '-': break;
                case 'S': service = a; break;
                default: break; /*printf("WARNING: unknown commandline option\n");*/ // less warnings - 2011feb05:ft: who disabled this - I think this should be on - more warnings == more clarity
            }
            else if (strncmp(argv[i], "assaultcube://", 13)) printf("WARNING: unknown commandline argument\n");
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

    smapname[0] = '\0';

    string identity;
    if(scl.logident[0]) filtertext(identity, scl.logident, 0);
    else formatstring(identity)("%s#%d", scl.ip[0] ? scl.ip : "local", scl.serverport);
    int conthres = scl.verbose > 1 ? ACLOG_DEBUG : (scl.verbose ? ACLOG_VERBOSE : ACLOG_INFO);
    if(dedicated && !initlogging(identity, scl.syslogfacility, conthres, scl.filethres, scl.syslogthres, scl.logtimestamp))
        printf("WARNING: logging not started!\n");
    logline(ACLOG_INFO, "logging local ACR server (version %d, protocol %d/%d) now..", AC_VERSION, SERVER_PROTOCOL_VERSION, EXT_VERSION);

    copystring(servdesc_current, scl.servdesc_full);
    servermsinit(scl.master ? scl.master : AC_MASTER_URI, scl.ip, CUBE_SERVINFO_PORT(scl.serverport), dedicated);

    if((isdedicated = dedicated))
    {
        ENetAddress address = { ENET_HOST_ANY, (enet_uint16)scl.serverport };
        if(scl.ip[0] && enet_address_set_host(&address, scl.ip)<0) logline(ACLOG_WARNING, "server ip not resolved!");
        serverhost = enet_host_create(&address, scl.maxclients+1, 3, 0, scl.uprate);
        if(!serverhost) fatal("could not create server host");
        loopi(scl.maxclients) serverhost->peers[i].data = (void *)-1;

        maprot.init(scl.maprot);
        maprot.next(false, true); // ensure minimum maprot length of '1'
        passwords.init(scl.pwdfile, scl.adminpasswd);
        ipblacklist.init(scl.blfile);
        nickblacklist.init(scl.nbfile);
        forbiddenlist.init(scl.forbidden);
        infofiles.init(scl.infopath, scl.motdpath);
        infofiles.getinfo("en"); // cache 'en' serverinfo
        logline(ACLOG_VERBOSE, "holding up to %d recorded demos in memory", scl.maxdemos);
        if(scl.demopath[0]) logline(ACLOG_VERBOSE,"all recorded demos will be written to: \"%s\"", scl.demopath);
        if(scl.voteperm[0]) logline(ACLOG_VERBOSE,"vote permission string: \"%s\"", scl.voteperm);
        if(scl.mapperm[0]) logline(ACLOG_VERBOSE,"map permission string: \"%s\"", scl.mapperm);
        logline(ACLOG_VERBOSE,"server description: \"%s\"", scl.servdesc_full);
        if(scl.servdesc_pre[0] || scl.servdesc_suf[0]) logline(ACLOG_VERBOSE,"custom server description: \"%sCUSTOMPART%s\"", scl.servdesc_pre, scl.servdesc_suf);
        logline(ACLOG_VERBOSE,"maxclients: %d, lag trust: %d", scl.maxclients, scl.lagtrust);
        if(scl.master) logline(ACLOG_VERBOSE,"master server URL: \"%s\"", scl.master);
        if(scl.serverpassword[0]) logline(ACLOG_VERBOSE,"server password: \"%s\"", hiddenpwd(scl.serverpassword));
    }

    resetserverifempty();

    if(isdedicated)       // do not return, this becomes main loop
    {
        #ifdef WIN32
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        #endif
        // kill -2 / Ctrl-C - see http://msdn.microsoft.com/en-us/library/xdkz3x12%28v=VS.100%29.aspx (or VS-2008?) for caveat (seems not to pertain to AC - 2011feb05:ft)
        if (signal(SIGINT, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGINT!");
        // kill -15 / probably process-manager on Win32 *shrug*
        if (signal(SIGTERM, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGTERM!");
        #ifndef WIN32
        // kill -1
        if (signal(SIGHUP, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGHUP!");
        // kill -9 is uncatchable - http://en.wikipedia.org/wiki/SIGKILL
        //if (signal(SIGKILL, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGKILL!");
        #endif
        logline(ACLOG_INFO, "dedicated server started, waiting for clients...");
        logline(ACLOG_INFO, "Ctrl-C to exit"); // this will now actually call the atexit-hooks below - thanks to SIGINT hooked above - noticed and signal-code-docs found by SKB:2011feb05:ft:
        atexit(enet_deinitialize);
        atexit(cleanupserver);
        enet_time_set(0);
        for(;;) serverslice(5);
    }
}

#ifdef STANDALONE

void localservertoclient(int chan, uchar *buf, int len, bool demo) {}
void fatal(const char *s, ...)
{
    defvformatstring(msg,s,s);
    defformatstring(out)("ACR fatal error: %s", msg);
    if (logline(ACLOG_ERROR, "%s", out));
    else puts(out);
    cleanupserver();
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    #ifdef WIN32
    //atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    #ifndef _DEBUG
    #ifndef __GNUC__
    __try {
    #endif
    #endif
    #endif

    for(int i = 1; i<argc; i++)
    {
        if (!strncmp(argv[i],"--wizard",8)) return wizardmain(argc, argv);
    }

    if(enet_initialize()<0) fatal("Unable to initialise network module");
    initserver(true, argc, argv);
    return EXIT_SUCCESS;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}
#endif

