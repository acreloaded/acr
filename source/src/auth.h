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

void authsuceeded(uint id, char priv, char *name){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	if(!priv) return;
	priv = clamp(priv, (char)PRIV_MASTER, (char)PRIV_MAX);
	changeclientrole(c->clientnum, priv, NULL, true);
	sendf(-1, 1, "ri3s", N_AUTHCHAL, 5, c->clientnum, name);
}

void authfail(uint id, bool disconnect){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "[%s] auth #%d failed!", c->hostname, id);
	if(disconnect) disconnect_client(c->clientnum, DISC_LOGINFAIL);
	else sendf(c->clientnum, 1, "ri2", N_AUTHCHAL, 3);
}

// c2s2m
void reqauth(int cn, int authtoken){
	if(!valid_client(cn)) return;
	client &cl = *clients[cn];
	if(!isdedicated){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return;} // not dedicated/connected
	if(cl.authreq){ sendf(cn, 1, "ri2", N_AUTHCHAL, 1);	return;	} // already pending
	const int authlimit = 10000; // 10 seconds
	if(cl.authmillis + authlimit > servmillis){ sendf(cn, 1, "ri3", N_AUTHCHAL, 6, cl.authmillis + authlimit - servmillis); return; } // flood check
	cl.authmillis = servmillis;
	cl.authtoken = authtoken;
	authrequest &r = authrequests.add();
	r.id = cl.authreq = nextauthreq++;
	r.answer = false;
	sendf(cn, 1, "ri2", N_AUTHCHAL, 0);
}

void answerchallenge(int cn, int hash[5]){
	if(!valid_client(cn)) return;
	client &cl = *clients[cn];
	if(!isdedicated){ sendf(cn, 1, "ri2", N_AUTHCHAL, 2); return;}
	if(!cl.authreq) return;
	loopv(authrequests){
		if(authrequests[i].id == cl.authreq){
			sendf(cn, 1, "ri2", N_AUTHCHAL, 1);
			break;
		}
	}
	authrequest &r = authrequests.add();
	r.id = cl.authreq;
	r.answer = true;
	s_sprintf(r.chal)("%08x%08x%08x%08x%08x", hash[0], hash[1], hash[2], hash[3], hash[4]);
	sendf(cn, 1, "ri2", N_AUTHCHAL, 4);
}
