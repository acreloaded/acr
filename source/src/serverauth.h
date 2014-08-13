// ACR's simplified auth

uint nextauthreq = 0; // move down?

client *findauth(uint id)
{
    loopv(clients) if(clients[i].authreq == id) return &clients[i];
    return NULL;
}

extern vector<authrequest> authrequests;

inline bool canreqauth(client &cl, int authtoken, int authuser)
{
    extern bool canreachauthserv;
    const int cn = cl.clientnum;
    if (!isdedicated || !canreachauthserv){ sendf(cn, 1, "ri2", SV_AUTH_ACR_CHAL, 2); return false; } // not dedicated/connected
    // if (ci.authreq){ sendf(cn, 1, "ri2", SV_AUTH_ACR_CHAL, 1);	return false;	} // already pending
    // if (ci.authmillis + 2000 > servmillis){ sendf(cn, 1, "ri3", SV_AUTH_ACR_CHAL, 6, cl.authmillis + 2000 - servmillis); return false; } // flood check
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

    int bantype = getbantype(cl.clientnum);
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
    else if (passwords.check(cl.name, cl.pwd, cl.salt, &pd, (cl.type == ST_TCPIP ? cl.peer->address.host : 0)) && (pd.priv >= CR_ADMIN || (banned && !srvfull && !srvprivate)) && bantype != BAN_MASTER) // pass admins always through
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
    else if (!scl.bypassglobalbans && cl.authpriv <= -1 && cl.masterdisc) return cl.masterdisc;
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
        if (disc) disconnect_client(cl.clientnum, disc);
    }
}

void authfailed(uint id, bool fail)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
    logline(ACLOG_INFO, "[%s] auth #%d %s!", cl->gethostname(), id, fail ? "failed" : "had an error");
    sendf(cl->clientnum, 1, "ri2", SV_AUTH_ACR_CHAL, 3);
    checkauthdisc(*cl);
}

void authsucceeded(uint id, int priv, const char *name)
{
    client *cl = findauth(id);
    if (!cl) return;
    cl->authreq = 0;
    filtertext(cl->authname, name);
    if (scl.bypassglobalpriv && priv)
    {
        priv = CR_DEFAULT;
        logline(ACLOG_INFO, "[%s] auth #%d succeeded as '%s' (%s ignored)", cl->gethostname(), id, cl->authname, privname(priv));
    }
    else
        logline(ACLOG_INFO, "[%s] auth #%d succeeded for %s as '%s'", cl->gethostname(), id, privname(priv), cl->authname);
    //bool banremoved = false;
    loopv(bans) if (bans[i].address.host == cl->peer->address.host){ bans.remove(i--); /*banremoved = true;*/ } // remove temporary bans
    // broadcast "identified" if privileged or a ban was removed
    sendf(-1, 1, "ri3s", SV_AUTH_ACR_CHAL, 5, cl->clientnum, cl->authname);
    if (priv)
    {
        cl->authpriv = clamp(priv, (int)CR_MASTER, (int)CR_MAX);
        setpriv(*cl, cl->authpriv);
        // unmute if auth has privilege
        // cl->muted = false;
    }
    else cl->authpriv = CR_DEFAULT; // bypass master bans
    checkauthdisc(*cl); // can bypass passwords
}

void authchallenged(uint id, int nonce)
{
    client *cl = findauth(id);
    if(!cl) return;
    sendf(cl->clientnum, 1, "ri3", SV_AUTH_ACR_REQ, nonce, cl->authtoken);
    logline(ACLOG_INFO, "[%s] auth #%d challenged by master", cl->gethostname(), id);
}

bool answerchallenge(client &cl, unsigned char hash[20])
{
    if (!isdedicated){ sendf(cl.clientnum, 1, "ri2", SV_AUTH_ACR_CHAL, 2); return false; }
    if (!cl.authreq) return false;
    loopv(authrequests)
    {
        if (authrequests[i].id == cl.authreq)
        {
            sendf(cl.clientnum, 1, "ri2", SV_AUTH_ACR_CHAL, 1);
            return false;
        }
    }
    authrequest &r = authrequests.add();
    r.id = cl.authreq;
    memcpy(r.hash, hash, sizeof(unsigned char) * 20);
    logline(ACLOG_INFO, "[%s] answers auth #%d", cl.gethostname(), r.id);
    sendf(cl.clientnum, 1, "ri2", SV_AUTH_ACR_CHAL, 4);
    return true;
}

void masterdisc(int cn, int result)
{
    if (!valid_client(cn)) return;
    client &cl = clients[cn];
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
