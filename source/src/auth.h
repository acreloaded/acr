client *findauth(uint id){
	loopv(clients) if(clients[i]->authreq == id) return clients[i];
	return NULL;
}

void authchallenged(uint id, int nonce){
	client *c = findauth(id);
	if(!c) return;
	sendf(c->clientnum, 1, "ri2", N_AUTHREQ, nonce);
	logline(ACLOG_INFO, "masterserver challenged auth #%d", id);
}

void authsuceeded(uint id, char priv, char *name){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	if(!priv) return;
	priv = clamp(priv, (char)PRIV_MASTER, (char)PRIV_MAX);
	changeclientrole(c->clientnum, priv, NULL, true);
	logline(ACLOG_INFO, "masterserver passed auth #%d as %s", id, name);
	sendf(-1, 1, "ri3s", N_AUTHCHAL, 5, c->clientnum, name);
}

void authfail(uint id, bool disconnect){
	client *c = findauth(id);
	if(!c) return;
	c->authreq = 0;
	logline(ACLOG_INFO, "masterserver failed auth #%d%s", id, disconnect ? " (login fail)" : "");
	if(disconnect) disconnect_client(c->clientnum, DISC_LOGINFAIL);
	else sendf(c->clientnum, 1, "ri2", N_AUTHCHAL, 3);
}
