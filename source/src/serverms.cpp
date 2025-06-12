// all server side masterserver and pinging functionality

#include "cube.h"

#ifdef STANDALONE
bool resolverwait(const char *name, ENetAddress *address)
{
    return enet_address_set_host(address, name) >= 0;
}

int connectwithtimeout(ENetSocket sock, const char *hostname, ENetAddress &remoteaddress)
{
    int result = enet_socket_connect(sock, &remoteaddress);
    if(result<0) enet_socket_destroy(sock);
    return result;
}
#endif

bool canreachauthserv = false;

ENetSocket httpgetsend(ENetAddress &remoteaddress, const char *hostname, const char *req, const char *agent, ENetAddress *localaddress = NULL)
{
    if (remoteaddress.host == ENET_HOST_ANY)
    {
        remoteaddress.port = masterport;
#if defined AC_MASTER_DOMAIN && defined AC_MASTER_IPS
        if (!strcmp(hostname, AC_MASTER_DOMAIN))
        {
            logline(ACLOG_INFO, "[%s] using %s...", AC_MASTER_IPS, AC_MASTER_DOMAIN);
            if (!resolverwait(AC_MASTER_IPS, &remoteaddress)) return ENET_SOCKET_NULL;
        }
        else
#endif
        {
            logline(ACLOG_INFO, "looking up %s...", hostname);
            if (!resolverwait(hostname, &remoteaddress)) return ENET_SOCKET_NULL;
            char hn[1024];
            logline(ACLOG_INFO, "[%s] resolved %s", (!enet_address_get_host_ip(&remoteaddress, hn, sizeof(hn))) ? hn : "unknown", hostname);
        }
    }
    ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
    if (sock != ENET_SOCKET_NULL && localaddress && enet_socket_bind(sock, localaddress) < 0)
    {
        enet_socket_destroy(sock);
        sock = ENET_SOCKET_NULL;
    }
    if (sock == ENET_SOCKET_NULL || connectwithtimeout(sock, hostname, remoteaddress)<0)
    {
        logline(ACLOG_WARNING, sock == ENET_SOCKET_NULL ? "could not open socket" : "could not connect");
        return ENET_SOCKET_NULL;
    }
    ENetBuffer buf;
    defformatstring(httpget)("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", req, hostname, agent);
    buf.data = httpget;
    buf.dataLength = strlen((char *)buf.data);
    //logline(ACLOG_INFO, "sending request to %s...", hostname);
    logline(ACLOG_VERBOSE, "sending request to %s: GET %s", hostname, req);
    enet_socket_send(sock, NULL, &buf, 1);
    canreachauthserv = true;
    return sock;
}

bool httpgetreceive(ENetSocket sock, ENetBuffer &buf, int timeout = 0)
{
    if (sock == ENET_SOCKET_NULL) return false;
    enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
    if (enet_socket_wait(sock, &events, timeout) >= 0 && events)
    {
        int len = enet_socket_receive(sock, NULL, &buf, 1);
        if (len <= 0)
        {
            enet_socket_destroy(sock);
            return false;
        }
        buf.data = ((char *)buf.data) + len;
        ((char*)buf.data)[0] = 0;
        buf.dataLength -= len;
    }
    return true;
}

uchar *stripheader(uchar *b)
{
    char *s = strstr((char *)b, "\n\r\n");
    if (!s) s = strstr((char *)b, "\n\n");
    return s ? (uchar *)s : b;
}

ENetSocket mastersock = ENET_SOCKET_NULL;
ENetAddress masteraddress = { ENET_HOST_ANY, 80 };
ENetAddress serveraddress = { ENET_HOST_ANY, ENET_PORT_ANY };
string masterbase, masterpath;
int masterport = AC_MASTER_PORT;
unsigned lastupdatemaster = 10000, lastresolvemaster = 10000, lastauthreqprocessed = 0;
#define MAXMASTERTRANS MAXTRANS // enlarge if response is big...
uchar masterrep[MAXMASTERTRANS];
ENetBuffer masterb;
// FIXME: a linked list makes more sense for these:
vector<authrequest> authrequests;
vector<connectrequest> connectrequests;
uint authid = 0;
int authcn = -1;

void freeconnectcheck(int cn)
{
    if (authcn == cn)
        authcn = -1;
    loopv(connectrequests)
        if (connectrequests[i].cn == cn)
            connectrequests.remove(i--);
}

void connectcheck(int cn, int guid, const char *hostname, uint authreq, uint authuser)
{
    freeconnectcheck(cn);
    extern bool isdedicated;
    if (!isdedicated) return;
    connectrequest &creq = connectrequests.add();
    creq.cn = cn;
    creq.guid = guid;
    creq.hostname = newstring(hostname);
    creq.id = authreq;
    creq.user = authuser;
}

// send alive signal to masterserver every 40 minutes of uptime
#define MSKEEPALIVE (40*60*1000)
// re-resolve the master-server domain every 4 hours
#define MSRERESOLVE (4*60*60*1000)
static inline void updatemasterserver(int millis, uint port)
{
    if (mastersock != ENET_SOCKET_NULL) return;
    string path;
    path[0] = '\0';

    if (millis - lastupdatemaster > MSKEEPALIVE)
    {
        logline(ACLOG_INFO, "sending registration request to master server");
        formatstring(path)("%s/r?v=" STR(PROTOCOL_VERSION) "&g=" STR(AC_VERSION) "&p=%u&guid32=%lu", masterpath, port, *&genguid(546545656, 23413376U, 3453455, "h6ji54ehjwo345gjio34s5jig"));
        lastupdatemaster = millis + 1;
    }
    else if (millis - lastauthreqprocessed > 2500 && authrequests.length())
    {
        authrequest r = authrequests.remove(0);
        authid = r.id;

        char cbuf[2*48+1], abuf[2*32+1];
        cbuf[2*48] = '\0';
        abuf[2*32] = '\0';
        loopi(48)
        {
            cbuf[i*2] = "0123456789abcdef"[r.crandom[i] >> 4];
            cbuf[i*2+1] = "0123456789abcdef"[r.crandom[i] & 0xF];
        }
        loopi(32)
        {
            abuf[i*2] = "0123456789abcdef"[r.canswer[i] >> 4];
            abuf[i*2+1] = "0123456789abcdef"[r.canswer[i] & 0xF];
        }

        formatstring(path)("%s/v?p=%u&i=%u&a=%s&c=%s", masterpath, port, r.id, abuf, cbuf);
        lastauthreqprocessed = millis;
    }
    else if (connectrequests.length())
    {
        if (!canreachauthserv) connectrequests.shrink(0);
        else
        {
            connectrequest c = connectrequests.remove(0);
            authid = c.id;
            authcn = c.cn;

            // FIXME: this assumes we have IPv4 hostnames
            if (authid)
                formatstring(path)("%s/a?p=%u&a=::ffff:%s&guid32=%lu&i=%u&u=%u", masterpath, port, c.hostname, c.guid, c.id, c.user);
            else
                formatstring(path)("%s/a?p=%u&a=::ffff:%s&guid32=%lu", masterpath, port, c.hostname, c.guid);

            delete[] c.hostname;
        }
    }
    if (!path[0]) return; // no request
    if (millis - lastresolvemaster > MSRERESOLVE)
    {
        masteraddress.host = ENET_HOST_ANY;
        lastresolvemaster = millis + 1;
    }
    const char *agent = "ACR-Server/" STR(AC_VERSION);
    mastersock = httpgetsend(masteraddress, masterbase, path, agent, &serveraddress);
    masterrep[0] = 0;
    masterb.data = masterrep;
    masterb.dataLength = MAXMASTERTRANS - 1;
    // mastersock could be ENET_SOCKET_NULL
    // but authid and authcn would be ignored
}

bool processmastercmd(const char *p)
{
    switch (*p)
    {
        // verdict: allow/ban connect
        case 'b':
        {
            int disc;
            disc = DISC_NONE;
            switch (*++p)
            {
                // GOOD reasons
                case 'm': // muted and not allowed to speak
                    // extern void mastermute(int cn);
                    // mastermute(currentmsrequest->c->cn);
                    break;
                case 'w': // IP whitelisted, not actually a banned verdict
                    break;
                // BAD reasons
                case 'i': // IP banned
                    disc = DISC_MBAN;
                    break;
                default: // unknown reason
                    disc = DISC_NUM;
                    break;
            }
            // fallthrough
        case 'a':
            if (*p == 'a')
                disc = DISC_NONE;

            if (authcn == -1) break;

            extern void masterdisc(int cn, int result);
            masterdisc(authcn, disc);

            return true;
        }

        // auth
        case 'd': // fail to claim
        case 'f': // failure
            if (!authid) break;
            extern void authfailed(uint id, bool fail);
            authfailed(authid, *p == 'd');
            return true;
        case 's': // succeed
        {
            char privk = *++p;
            if (!authid || !privk) break;
            string name;
            filtertext(name, ++p, 1, MAXNAMELEN);
            if (!*name) copystring(name, "<unnamed>");
            extern void authsucceeded(uint id, int priv, const char *name);
            authsucceeded(authid, privk >= '0' && privk <= '3' ? privk - '0' : -1, name);
            return true;
        }
        case 'c': // challenge
        {
            if (!authid || !*++p) break;
            extern void authchallenged(uint id, const char *chal);
            authchallenged(authid, p);
            return true;
        }
    }
    return false;
}

void checkmasterreply()
{
    if (mastersock == ENET_SOCKET_NULL || httpgetreceive(mastersock, masterb)) return;
    mastersock = ENET_SOCKET_NULL;
    char replytext[MAXMASTERTRANS];
    char *text = replytext;
    filtertext(text, (const char *)stripheader(masterrep), 2, MAXMASTERTRANS - 1);
    while (isspace(*text)) text++;
    char *replytoken = strtok(text, "\n");
    while (replytoken)
    {
        // process commands
        if (*replytoken == '*')
        {
            replytoken++;
            if (!processmastercmd(replytoken))
                logline(ACLOG_INFO, "masterserver sent an unknown command: %s", replytoken);
        }
        else
        {
            while (isspace(*replytoken)) replytoken++;
            if (*replytoken) logline(ACLOG_INFO, "masterserver reply: %s", replytoken);
        }
        replytoken = strtok(NULL, "\n");
    }
    authid = 0;
    authcn = -1;
}

ENetSocket pongsock = ENET_SOCKET_NULL, lansock = ENET_SOCKET_NULL;
extern int getpongflags(enet_uint32 ip);

void serverms(int mode, int muts, int numplayers, int minremain, char *smapname, int millis, const ENetAddress &localaddr, int *mnum, int *msend, int *mrec, int *cnum, int *csend, int *crec, int protocol_version)
{
    checkmasterreply();
    updatemasterserver(millis, localaddr.port);

    static ENetSocketSet sockset;
    ENET_SOCKETSET_EMPTY(sockset);
    ENetSocket maxsock = pongsock;
    ENET_SOCKETSET_ADD(sockset, pongsock);
    if(mastersock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, mastersock);
        ENET_SOCKETSET_ADD(sockset, mastersock);
    }
    if(lansock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, lansock);
        ENET_SOCKETSET_ADD(sockset, lansock);
    }
    if(enet_socketset_select(maxsock, &sockset, NULL, 0) <= 0) return;

    // reply all server info requests
    static uchar data[MAXTRANS];
    ENetBuffer buf;
    ENetAddress addr;
    buf.data = data;
    int len;

    loopi(2)
    {
        ENetSocket sock = i ? lansock : pongsock;
        if(sock == ENET_SOCKET_NULL || !ENET_SOCKETSET_CHECK(sockset, sock)) continue;

        buf.dataLength = sizeof(data);
        len = enet_socket_receive(sock, &addr, &buf, 1);
        if(len < 0) continue;

        // ping & pong buf
        ucharbuf pi(data, len), po(&data[len], sizeof(data)-len);
        bool std = false;
        if(getint(pi) != 0) // std pong
        {
            extern struct servercommandline scl;
            extern string servdesc_current;
            (*mnum)++; *mrec += len; std = true;
            putint(po, protocol_version);
            putint(po, mode);
            putint(po, muts);
            putint(po, numplayers);
            putint(po, minremain);
            sendstring(smapname, po);
            sendstring(servdesc_current, po);
            putint(po, scl.maxclients);
            putint(po, getpongflags(addr.host));
            if(pi.remaining())
            {
                int query = getint(pi);
                switch(query)
                {
                    case EXTPING_NAMELIST:
                    {
                        extern void extping_namelist(ucharbuf &p);
                        putint(po, query);
                        extping_namelist(po);
                        break;
                    }
                    case EXTPING_SERVERINFO:
                    {
                        extern void extping_serverinfo(ucharbuf &pi, ucharbuf &po);
                        putint(po, query);
                        extping_serverinfo(pi, po);
                        break;
                    }
                    case EXTPING_MAPROT:
                    {
                        extern void extping_maprot(ucharbuf &po);
                        putint(po, query);
                        extping_maprot(po);
                        break;
                    }
                    case EXTPING_UPLINKSTATS:
                    {
                        extern void extping_uplinkstats(ucharbuf &po);
                        putint(po, query);
                        extping_uplinkstats(po);
                        break;
                    }
                    case EXTPING_NOP:
                    default:
                        putint(po, EXTPING_NOP);
                        break;
                }
            }
        }
        else // ext pong - additional server infos
        {
            (*cnum)++; *crec += len;
            int extcmd = getint(pi);
            putint(po, EXT_ACK);
            putint(po, EXT_VERSION);

            switch(extcmd)
            {
                case EXT_UPTIME:        // uptime in seconds
                {
                    putint(po, uint(millis)/1000);
                    break;
                }

                case EXT_PLAYERSTATS:   // playerstats
                {
                    int cn = getint(pi);     // get requested player, -1 for all
                    if(!valid_client(cn) && cn != -1)
                    {
                        putint(po, EXT_ERROR);
                        break;
                    }
                    putint(po, EXT_ERROR_NONE);              // add no error flag

                    int bpos = po.length();                  // remember buffer position
                    putint(po, EXT_PLAYERSTATS_RESP_IDS);    // send player ids following
                    extinfo_cnbuf(po, cn);
                    *csend += int(buf.dataLength = len + po.length());
                    enet_socket_send(pongsock, &addr, &buf, 1); // send all available player ids
                    po.len = bpos;

                    extinfo_statsbuf(po, cn, bpos, pongsock, addr, buf, len, csend);
                    return;
                }

                case EXT_TEAMSCORE:
                    extinfo_teamscorebuf(po);
                    break;

                default:
                    putint(po,EXT_ERROR);
                    break;
            }
        }

        buf.dataLength = len + po.length();
        enet_socket_send(pongsock, &addr, &buf, 1);
        if(std) *msend += (int)buf.dataLength;
        else *csend += (int)buf.dataLength;
    }
}

// this function should be made better, because it is used just ONCE (no need of so much parameters)
void servermsinit(const char *master, const char *ip, int infoport, bool listen)
{
    const char *mid = strstr(master, "/");
    if (mid)
    {
        copystring(masterbase, master, mid - master + 1);
        copystring(masterpath, mid + 1);
    }
    else
    {
        copystring(masterbase, master);
        copystring(masterpath, "");
    }

    if(listen)
    {
        ENetAddress address = { ENET_HOST_ANY, (enet_uint16)infoport };
        if(*ip)
        {
            if(enet_address_set_host(&address, ip)<0) logline(ACLOG_WARNING, "server ip not resolved");
            else serveraddress.host = address.host;
        }
        pongsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if(pongsock != ENET_SOCKET_NULL && enet_socket_bind(pongsock, &address) < 0)
        {
            enet_socket_destroy(pongsock);
            pongsock = ENET_SOCKET_NULL;
        }
        if(pongsock == ENET_SOCKET_NULL) fatal("could not create server info socket");
        else enet_socket_set_option(pongsock, ENET_SOCKOPT_NONBLOCK, 1);
        address.port = CUBE_SERVINFO_PORT_LAN;
        lansock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if(lansock != ENET_SOCKET_NULL && (enet_socket_set_option(lansock, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(lansock, &address) < 0))
        {
            enet_socket_destroy(lansock);
            lansock = ENET_SOCKET_NULL;
        }
        if(lansock == ENET_SOCKET_NULL) logline(ACLOG_WARNING, "could not create LAN server info socket");
        else enet_socket_set_option(lansock, ENET_SOCKOPT_NONBLOCK, 1);
    }
}
