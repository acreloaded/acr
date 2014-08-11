// for AUTH: WIP

client *findauth(uint id)
{
    loopv(clients) if(clients[i]->authreq == id) return clients[i];
    return NULL;
}

void authfailed(uint id)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
}

void authsucceeded(uint id)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
    logline(ACLOG_INFO, "player authenticated: %s", cl->formatname());
    defformatstring(auth4u)("player authenticated: %s", cl->formatname());
    sendf(-1, 1, "ris", SV_SERVMSG, auth4u);
    //setmaster(cl, true, "", ci->authname);//TODO? compare to sauerbraten
}

void authchallenged(uint id, const char *val)
{
    client *cl = findauth(id);
    if(!cl) return;
    //sendf(cl->clientnum, 1, "risis", SV_AUTHCHAL, "", id, val);
}

uint nextauthreq = 0;

void tryauth(client *cl, const char *user)
{
    extern bool requestmasterf(const char *fmt, ...);
    if(!nextauthreq) nextauthreq = 1;
    cl->authreq = nextauthreq++;
    filtertext(cl->authname, user, false, 100);
    if(!requestmasterf("reqauth %u %s\n", cl->authreq, cl->authname))
    {
        cl->authreq = 0;
        sendf(cl->clientnum, 1, "ris", SV_SERVMSG, "not connected to authentication server");
    }
}

void answerchallenge(client *cl, uint id, char *val)
{
    if(cl->authreq != id) return;
    extern bool requestmasterf(const char *fmt, ...);
    for(char *s = val; *s; s++)
    {
        if(!isxdigit(*s)) { *s = '\0'; break; }
    }
    if(!requestmasterf("confauth %u %s\n", id, val))
    {
        cl->authreq = 0;
        sendf(cl->clientnum, 1, "ris", SV_SERVMSG, "not connected to authentication server");
    }
}

// :for AUTH
