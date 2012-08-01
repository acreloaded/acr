// m2s2c
client *findauth(uint id){
	loopv(clients) if(clients[i]->authreq == id) return clients[i];
	return NULL;
}

extern vector<authrequest> authrequests;

bool reqauth(int cn, char *name, int authtoken){
	if(!valid_client(cn)) return false;
	client &cl = *clients[cn];
	if(!isdedicated || !canreachauthserv){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return false;} // not dedicated/connected
	if(cl.authreq){ sendf(cn, 1, "ri2", N_AUTHCHAL, 1);	return false;	} // already pending
	if(cl.authmillis + 2000 > servmillis){ sendf(cn, 1, "ri3", N_AUTHCHAL, 6, cl.authmillis + 2000 - servmillis); return false; } // flood check
	cl.authmillis = servmillis;
	cl.authtoken = authtoken;
	authrequest &r = authrequests.add();
	r.id = cl.authreq = nextauthreq++;
	r.answer = false;
	r.usr = newstring(name, MAXNAMELEN);
	logline(ACLOG_INFO, "[%s] requests auth #%d as %s", gethostname(cn), r.id, r.usr);
	sendf(cn, 1, "ri2", N_AUTHCHAL, 0);
	return true;
}

int allowconnect(client &ci, const char *pwd = NULL, int authreq = 0, char *authname = NULL){
	if(ci.type == ST_LOCAL) return DISC_NONE;
	//if(!m_valid(smode)) return DISC_PRIVATE;
	if(ci.priv >= PRIV_ADMIN) return DISC_NONE;
	if(authreq && reqauth(ci.clientnum, authname, authreq)){
		logline(ACLOG_INFO, "[%s] %s logged in, requesting auth", gethostname(ci.clientnum), formatname(ci));
		ci.connectauth = true;
		return DISC_NONE;
	}
	// nickname list
	int bl = 0, wl = nbl.checknickwhitelist(ci);
	const char *wlp = wl == NWL_PASS ? ", nickname whitelist match" : "";
	if(wl == NWL_UNLISTED) bl = nbl.checknickblacklist(ci.name);
	if(wl == NWL_IPFAIL || wl == NWL_PWDFAIL)
	{ // nickname matches whitelist, but IP is not in the required range or PWD doesn't match
		logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong %s", gethostname(ci.clientnum), formatname(ci), wl == NWL_IPFAIL ? "IP" : "PWD");
		return wl == NWL_IPFAIL ? DISC_NAME_IP : DISC_NAME_PWD;
	}
	else if(bl > 0){ // nickname matches blacklist
		logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", gethostname(ci.clientnum), formatname(ci), bl);
		return DISC_NAME;
	}
	const bool banned = isbanned(ci.clientnum);
	const bool srvfull = numnonlocalclients() > scl.maxclients;
	const bool srvprivate = mastermode >= MM_PRIVATE;
	pwddetail pd;
	if(pwd && checkadmin(ci.name, pwd, ci.salt, &pd) && (pd.priv >= PRIV_ADMIN || (banned && !srvfull && !srvprivate))){ // admin (or master or deban) password match
		bool banremoved = false;
		if(pd.priv) setpriv(ci.clientnum, pd.priv);
		if(banned) loopv(bans) if(bans[i].host == ci.peer->address.host) { banremoved = true; bans.remove(i); break; } // remove admin bans
		logline(ACLOG_INFO, "[%s] %s logged in using the password in line %d%s%s", gethostname(ci.clientnum), formatname(ci), pd.line, wlp, banremoved ? ", (ban removed)" : "");
		return DISC_NONE;
	}
	if(srvprivate) return DISC_PRIVATE;
	if(srvfull) return DISC_FULL;
	if(banned) return DISC_REFUSE;
	// does the master server want a disconnection?
	if(ci.authpriv < PRIV_NONE && ci.masterverdict) return ci.masterverdict;
	if(pwd && *scl.serverpassword){ // server password required
		if(!strcmp(genpwdhash(ci.name, scl.serverpassword, ci.salt), pwd)){
			logline(ACLOG_INFO, "[%s] %s logged in using the server password%s", gethostname(ci.clientnum), formatname(ci), wlp);
		}
		else return DISC_PASSWORD;
	}
	logline(ACLOG_INFO, "[%s] %s logged in%s", gethostname(ci.clientnum), formatname(ci), wlp);
	return DISC_NONE;
}

void checkauthdisc(client &ci, bool force = false){
	if(ci.connectauth || force){
		ci.connectauth = false;
		const int disc = allowconnect(ci);
		if(disc) disconnect_client(ci.clientnum, disc);
	}
}

void authsuceeded(uint id, char priv, char *name){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "[%s] auth #%d suceeded for %s as '%s'", gethostname(c->clientnum), id, privname(priv), name);
	bool banremoved = false;
	loopv(bans) if(bans[i].host == c->peer->address.host){ bans.remove(i--); banremoved = true; } // deban
	// broadcast "identified" if privileged or a ban was removed
	sendf(priv || banremoved ? -1 : c->clientnum, 1, "ri3s", N_AUTHCHAL, 5, c->clientnum, name);
	if(priv){
		c->authpriv = clamp<char>(priv, PRIV_MASTER, PRIV_MAX);
		/*if(c->connectauth)*/ setpriv(c->clientnum, c->authpriv);
		// unmute if auth has privilege
		c->muted = false;
	}
	else c->authpriv = PRIV_NONE; // bypass master bans
	checkauthdisc(*c); // can bypass passwords
}

void authfail(uint id, bool disconnect){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "[%s] auth #%d %s!", gethostname(c->clientnum), id, disconnect ? "failed" : "mismatch");
	if(disconnect) disconnect_client(c->clientnum, DISC_LOGINFAIL);
	else{
		sendf(c->clientnum, 1, "ri2", N_AUTHCHAL, 3);
		checkauthdisc(*c);
	}
}

void authchallenged(uint id, int nonce){
	client *c = findauth(id);
	if(c) sendf(c->clientnum, 1, "ri3", N_AUTHREQ, nonce, c->authtoken);
}

bool answerchallenge(int cn, int *hash){
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
	r.hash = new int[5];
	memcpy(r.hash, hash, sizeof(int) * 5);
	logline(ACLOG_INFO, "[%s] answers auth #%d", gethostname(cn), r.id);
	sendf(cn, 1, "ri2", N_AUTHCHAL, 4);
	return true;
}

void mastermute(int cn){
	if(!valid_client(cn)) return;
	client &ci = *clients[cn];
	ci.muted = true;
}

void masterverdict(int cn, int result){
	if(!valid_client(cn)) return;
	client &ci = *clients[cn];
	ci.masterverdict = result;
	if(!ci.connectauth && result) checkauthdisc(ci, true);
}

void logversion(client &ci, int ver, int defs, int guid){
	string cdefs;
	*cdefs = 0;
	if(defs & 0x40) concatstring(cdefs, "W");
	if(defs & 0x20) concatstring(cdefs, "M");
	if(defs & 0x8) concatstring(cdefs, "D");
	if(defs & 0x4) concatstring(cdefs, "L");
	logline(ACLOG_INFO, "[%s] %s runs %d [%s] [GUID-%08X]", gethostname(ci.clientnum), formatname(ci), ver, cdefs, ci.guid = guid);
}
