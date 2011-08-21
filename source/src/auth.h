// m2s2c
client *findauth(uint id){
	loopv(clients) if(clients[i]->authreq == id) return clients[i];
	return NULL;
}

void authchallenged(uint id, int nonce){
	client *c = findauth(id);
	if(!c) return;
	sendf(c->clientnum, 1, "ri3", N_AUTHREQ, nonce, c->authtoken);
}

int allowconnect(client &ci, const char *pwd = "", bool log = false){
	if(ci.type == ST_LOCAL) return DISC_NONE;
	//if(!m_valid(smode)) return DISC_PRIVATE;
	if(ci.priv >= PRIV_ADMIN) return DISC_NONE;
	// nickname list
	int bl = 0, wl = nbl.checknickwhitelist(ci);
	const char *wlp = wl == NWL_PASS ? ", nickname whitelist match" : "";
	if(wl == NWL_UNLISTED) bl = nbl.checknickblacklist(ci.name);
	if(wl == NWL_IPFAIL || wl == NWL_PWDFAIL)
	{ // nickname matches whitelist, but IP is not in the required range or PWD doesn't match
		if(log) logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong %s", ci.hostname, ci.name, wl == NWL_IPFAIL ? "IP" : "PWD");
		return DISC_PASSWORD;
	}
	else if(bl > 0){ // nickname matches blacklist
		if(log) logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", ci.hostname, ci.name, bl);
		return DISC_NAME;
	}
	const bool banned = isbanned(ci.clientnum);
	const bool srvfull = numnonlocalclients() > scl.maxclients;
	const bool srvprivate = mastermode >= MM_PRIVATE;
	pwddetail pd;
	if(checkadmin(ci.name, pwd, ci.salt, &pd) && (pd.priv >= PRIV_ADMIN || (banned && !srvfull && !srvprivate))){ // admin (or master or deban) password match
		bool banremoved = false;
		if(pd.priv) setpriv(ci.clientnum, pd.priv, NULL, true);
		if(banned) loopv(bans) if(bans[i].host == ci.peer->address.host) { banremoved = true; bans.remove(i); break; } // remove admin bans
		if(log) logline(ACLOG_INFO, "[%s] %s logged in using the password in line %d%s%s", ci.hostname, ci.name, pd.line, wlp, banremoved ? ", (ban removed)" : "");
		return DISC_NONE;
	}
	if(scl.serverpassword[0] && !(srvprivate || srvfull || banned)){ // server password required
		if(!strcmp(genpwdhash(ci.name, scl.serverpassword, ci.salt), pwd)){
			if(log) logline(ACLOG_INFO, "[%s] %s logged in using the server password%s", ci.hostname, ci.name, wlp);
		}
		else return DISC_PASSWORD;
	}
	if(srvprivate) return DISC_PRIVATE;
	if(srvfull) return DISC_FULL;
	if(banned) return DISC_REFUSE;
	if(log) logline(ACLOG_INFO, "[%s] %s logged in (default)%s", ci.hostname, ci.name, wlp);
	return DISC_NONE;
}

void checkauthdisc(client &ci){
	if(ci.connectauth){
		ci.connectauth = false;
		const int disc = allowconnect(ci);
		if(disc) disconnect_client(ci.clientnum, disc);
	}
}

void authsuceeded(uint id, char priv, char *name){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "[%s] auth #%d suceeded for %s as '%s'", c->hostname, id, privname(priv), name);
	sendf(-1, 1, "ri3s", N_AUTHCHAL, 5, c->clientnum, name);
	if(priv){
		priv = clamp(priv, (char)PRIV_MASTER, (char)PRIV_MAX);
		setpriv(c->clientnum, priv, NULL, true);
	}
	loopv(bans) if(bans[i].host == c->peer->address.host) bans.remove(i); // deban
	checkauthdisc(*c);
}

void authfail(uint id, bool disconnect){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "[%s] auth #%d failed!", c->hostname, id);
	if(disconnect) disconnect_client(c->clientnum, DISC_LOGINFAIL);
	else{
		sendf(c->clientnum, 1, "ri2", N_AUTHCHAL, 3);
		checkauthdisc(*c);
	}
}

// c2s2m
bool reqauth(int cn, int authtoken){
	if(!valid_client(cn)) return false;
	client &cl = *clients[cn];
	if(!isdedicated){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return false;} // not dedicated/connected
	if(cl.authreq){ sendf(cn, 1, "ri2", N_AUTHCHAL, 1);	return false;	} // already pending
	if(cl.authmillis + 2000 > servmillis){ sendf(cn, 1, "ri3", N_AUTHCHAL, 6, cl.authmillis + 2000 - servmillis); return false; } // flood check
	cl.authmillis = servmillis;
	cl.authtoken = authtoken;
	authrequest &r = authrequests.add();
	r.id = cl.authreq = nextauthreq++;
	r.answer = false;
	logline(ACLOG_INFO, "[%s] requests auth #%d", cl.hostname, r.id);
	sendf(cn, 1, "ri2", N_AUTHCHAL, 0);
	return true;
}

bool answerchallenge(int cn, int hash[5]){
	if(!valid_client(cn)) return false;
	client &cl = *clients[cn];
	if(!isdedicated){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return false;}
	if(!cl.authreq) return false;
	loopv(authrequests){
		if(authrequests[i].id == cl.authreq){
			sendf(cn, 1, "ri2", N_AUTHCHAL, 1);
			return false;
		}
	}
	authrequest &r = authrequests.add();
	r.id = cl.authreq;
	r.answer = true;
	formatstring(r.chal)("%08x%08x%08x%08x%08x", hash[0], hash[1], hash[2], hash[3], hash[4]);
	sendf(cn, 1, "ri2", N_AUTHCHAL, 4);
	return true;
}

void logversion(client &ci, int clientversion, int defs){
	string cdefs;
	*cdefs = 0;
	if(defs & 0x40) concatstring(cdefs, "W");
	if(defs & 0x20) concatstring(cdefs, "M");
	if(defs & 0x8) concatstring(cdefs, "D");
	if(defs & 0x4) concatstring(cdefs, "L");
	logline(ACLOG_INFO, "[%s] %s runs %d [%s]", ci.hostname, ci.name, clientversion, cdefs);
}
