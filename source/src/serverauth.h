// ACR's simplified auth

uint nextauthreq = 0; // move down?

client *findauth(uint id)
{
    loopv(clients) if(clients[i]->authreq == id) return clients[i];
    return NULL;
}

void authfailed(uint id, bool fail)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
    logline(ACLOG_INFO, "[%s] auth #%d %s!", cl->gethostname(), id, fail ? "failed" : "had an error");
    sendf(cl->clientnum, 1, "ri2", SV_AUTH_ACR_CHAL, 3);
    //checkauthdisc(*c);
}

void authsucceeded(uint id, char priv, const char *name)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
    filtertext(cl->authname, name);
    logline(ACLOG_INFO, "[%s] auth #%d suceeded for %s as '%s'", cl->gethostname(), id, privname(priv), cl->authname);
    // TODO
}

void authchallenged(uint id, int nonce)
{
    client *cl = findauth(id);
    if(!cl) return;
    //sendf(cl->clientnum, 1, "ri3", SV_AUTH_ACR_REQ, nonce, cl->authtoken);
    logline(ACLOG_INFO, "[%s] auth #%d challenged by master", cl->gethostname(), id);
}

void answerchallenge(client *cl, int hash[5])
{
    if (!cl->authreq) return;
    // TODO
}

void masterdisc(int cn, int result)
{
    if (!valid_client(cn)) return;
    client &ci = *clients[cn];
    // ci.masterdisc = result;
    // if (!ci.connectauth && result) checkauthdisc(ci, true);
}
