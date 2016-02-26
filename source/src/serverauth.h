// ACR's simplified auth

uint nextauthreq = 0; // move down?

client *findauth(uint id)
{
    loopv(clients) if(clients[i]->authreq == id) return clients[i];
    return NULL;
}

extern vector<authrequest> authrequests;

inline bool canreqauth(client &cl, int authtoken, int authuser)
{
    extern bool canreachauthserv;
    if (!isdedicated || !canreachauthserv){ sendf(&cl, 1, "ri2", SV_AUTH_ACR_CHAL, 2); return false; } // not dedicated/connected
    // if (ci.authreq){ sendf(&cl, 1, "ri2", SV_AUTH_ACR_CHAL, 1); return false; } // already pending
    // if (ci.authmillis + 2000 > servmillis){ sendf(&cl, 1, "ri3", SV_AUTH_ACR_CHAL, 6, cl.authmillis + 2000 - servmillis); return false; } // flood check
    return true;
}

int allowconnect(client &cl, int authreq = 0, int authuser = 0)
{
    if (cl.type == ST_LOCAL) return DISC_NONE;
    //if (!m_valid(gamemode)) return DISC_PRIVATE;
    if (cl.role >= CR_ADMIN) return DISC_NONE;

    if (authreq && authuser && canreqauth(cl, authreq, authuser))
    {
        cl.authtoken = authreq;
        cl.authuser = authuser;
        if (!nextauthreq)
            nextauthreq = 1;
        cl.authreq = nextauthreq++;
        cl.connectauth = true;
        logline(ACLOG_INFO, "[%s] %s logged in, requesting auth #%d as %d", cl.gethostname(), cl.formatname(), cl.authreq, authuser);
        return DISC_NONE;
    }

    int bantype = getbantype(cl);
    bool banned = bantype > BAN_NONE;
    bool srvfull = numnonlocalclients() > scl.maxclients;
    bool srvprivate = mastermode == MM_PRIVATE || mastermode == MM_MATCH;
    bool matchreconnect = mastermode == MM_MATCH && findscore(cl, false);
    int bl = 0, wl = nickblacklist.checkwhitelist(cl);
    const char *wlp = wl == NWL_PASS ? ", nickname whitelist match" : "";
    pwddetail pd;
    if (wl == NWL_UNLISTED) bl = nickblacklist.checkblacklist(cl.name);
    if (matchreconnect && !banned)
    {
        // former player reconnecting to a server in match mode
        logline(ACLOG_INFO, "[%s] %s logged in (reconnect to match)%s", cl.gethostname(), cl.formatname(), wlp);
        return DISC_NONE;
    }
    else if (wl == NWL_IPFAIL || wl == NWL_PWDFAIL)
    {
        // nickname matches whitelist, but IP is not in the required range or PWD doesn't match
        logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong %s%s", cl.gethostname(), cl.formatname(), wl == NWL_IPFAIL ? "IP" : "PWD", wlp);
        return NWL_IPFAIL ? DISC_NAME_IP : DISC_NAME_PWD;
    }
    else if (bl > 0)
    {
        // nickname matches blacklist
        logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d%s", cl.gethostname(), cl.formatname(), bl, wlp);
        return DISC_NAME;
    }
    else if (passwords.check(cl.name, cl.pwd, cl.salt, &pd, (cl.type == ST_TCPIP ? cl.peer->address.host : 0)) && (pd.priv >= CR_ADMIN || (banned && !srvfull && !srvprivate))) // pass admins always through
    {
        // admin (or deban) password match
        bool banremoved = false;
        if (pd.priv) setpriv(cl, pd.priv);
        if (bantype == BAN_VOTE)
        {
            loopv(bans) if (bans[i].address.host == cl.peer->address.host) { bans.remove(i); banremoved = true; break; } // remove admin bans
        }
        logline(ACLOG_INFO, "[%s] %s logged in using the admin password in line %d%s%s", cl.gethostname(), cl.formatname(), pd.line, wlp, banremoved ? ", ban removed" : "");
        return DISC_NONE;
    }
    else if (scl.serverpassword[0] && !(srvprivate || srvfull || banned))
    {
        // server password required
        if (!strcmp(genpwdhash(cl.name, scl.serverpassword, cl.salt), cl.pwd))
        {
            logline(ACLOG_INFO, "[%s] %s client logged in (using serverpassword)%s", cl.gethostname(), cl.formatname(), wlp);
            return DISC_NONE;
        }
        else return DISC_WRONGPW;
    }
    else if (srvprivate) return DISC_MASTERMODE;
    else if (srvfull) return DISC_MAXCLIENTS;
    else if (banned) return DISC_BANREFUSE;
    // does the master server want a disconnection?
    else if (!scl.bypassglobalbans && cl.authpriv == -1 && cl.masterdisc) return cl.masterdisc;
    else
    {
        logline(ACLOG_INFO, "[%s] %s logged in (default)%s", cl.gethostname(), cl.formatname(), wlp);
        return DISC_NONE;
    }
}

void checkauthdisc(client &cl, bool force = false)
{
    if (cl.connectauth || force)
    {
        cl.connectauth = false;
        const int disc = allowconnect(cl);
        if (disc) disconnect_client(cl, disc);
    }
}

void authfailed(uint id, bool fail)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
    logline(ACLOG_INFO, "[%s] auth #%d %s!", cl->gethostname(), id, fail ? "failed" : "had an error");
    sendf(cl, 1, "ri2", SV_AUTH_ACR_CHAL, 3);
    checkauthdisc(*cl);
}

void authsucceeded(uint id, int priv, const char *name)
{
    client *cl = findauth(id);
    if (!cl) return;
    cl->authreq = 0;
    filtertext(cl->authname, name);
    if (scl.bypassglobalpriv && priv != -1)
    {
        priv = CR_DEFAULT;
        logline(ACLOG_INFO, "[%s] auth #%d succeeded as '%s' (%s ignored)", cl->gethostname(), id, cl->authname, privname(priv));
    }
    else
        logline(ACLOG_INFO, "[%s] auth #%d succeeded for %s as '%s'", cl->gethostname(), id, privname(priv, true), cl->authname);
    sendf(NULL, 1, "ri4s", SV_AUTH_ACR_CHAL, 5, cl->clientnum, priv, cl->authname);
    if (priv < 0)
    {
        // name only
        cl->authpriv = -2;
    }
    else
    {
        // even CR_DEFAULT can bypass master bans
        // remove temporary bans
        loopv(bans)
            if (bans[i].type == BAN_VOTE && bans[i].address.host == cl->peer->address.host)
                bans.remove(i--);
        cl->authpriv = clamp(priv, (int)CR_DEFAULT, (int)CR_MAX);
        // setpriv(*cl, cl->authpriv);
        // unmute if auth has privilege
        // cl->muted = false;
    }
    checkauthdisc(*cl); // can bypass passwords
}

void authchallenged(uint id, const char *chal)
{
    client *cl = findauth(id);
    if(!cl) return;
    uchar buf[128] = { 0 };
    loopi(128)
    {
        #define hex2char(c) (c > '9' ? c-'a'+10 : c-'0')
        if(!chal[i << 1])
            break;
        buf[i] = hex2char(chal[i << 1]) << 4;
        if(!chal[(i << 1) | 1])
            break;
        buf[i] |= hex2char(chal[(i << 1) | 1]);
    }
    sendf(cl, 1, "ri2m", SV_AUTH_ACR_REQ, cl->authtoken, 128, buf);
    logline(ACLOG_INFO, "[%s] auth #%d challenged by master", cl->gethostname(), id);
    logline(ACLOG_DEBUG, "%s", chal);
}

bool answerchallenge(client &cl, uchar crandom[48], uchar canswer[32])
{
    if (!isdedicated){ sendf(&cl, 1, "ri2", SV_AUTH_ACR_CHAL, 2); return false; }
    if (!cl.authreq) return false;
    loopv(authrequests)
    {
        if (authrequests[i].id == cl.authreq)
        {
            sendf(&cl, 1, "ri2", SV_AUTH_ACR_CHAL, 1);
            return false;
        }
    }
    authrequest &r = authrequests.add();
    r.id = cl.authreq;
    memcpy(r.crandom, crandom, sizeof(uchar) * 48);
    memcpy(r.canswer, canswer, sizeof(uchar) * 32);
    logline(ACLOG_INFO, "[%s] answers auth #%d", cl.gethostname(), r.id);
    sendf(&cl, 1, "ri2", SV_AUTH_ACR_CHAL, 4);
    return true;
}

void masterdisc(int cn, int result)
{
    if (!valid_client(cn)) return;
    client &cl = *clients[cn];
    cl.masterdisc = result;
    if (!cl.connectauth && result) checkauthdisc(cl, true);
}

void logversion(client &cl)
{
    string cdefs;
    if (cl.acbuildtype & 0x40) cdefs[0] = 'W';
    else if (cl.acbuildtype & 0x20) cdefs[0] = 'M';
    else if (cl.acbuildtype & 0x04) cdefs[0] = 'L';
    if (cl.acbuildtype & 0x08)
    {
        cdefs[1] = 'D';
        cdefs[2] = '\0';
    }
    cdefs[1] = '\0';
    logline(ACLOG_INFO, "[%s] %s runs %d [%x-%s] [GUID-%08X]", cl.gethostname(), cl.formatname(), cl.acversion, cl.acbuildtype, cdefs, cl.acguid);
}
