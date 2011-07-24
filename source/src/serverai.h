// ai (bots)
int findaiclient(int exclude = -1){ // person with least bots
	int cn = -1, bots = MAXBOTS;
	loopv(clients){
		client *c = clients[i];
		if(i == exclude || !valid_client(i) || c->clientnum < 0 || c->state.ownernum >= 0 || !*c->name || !c->isauthed) break;
		int n = 0;
		loopvj(clients) if(clients[j]->state.ownernum == i) n++;
		if(n < bots){
			bots = cn;
			cn = i;
		}
	}
	return cn;
}

bool addai(){
	int aiowner = findaiclient(), cn = -1, numbots = 0;
	if(!valid_client(aiowner)) return false;
	loopv(clients){
		if(numbots > MAXBOTS) return false;
		if(clients[i]->state.ownernum >= 0) numbots++;
		else if(clients[i]->type == ST_EMPTY){
			cn = i;
			break;
		}
	}
	if(cn < 0){
		client *c = new client;
		c->clientnum = cn = clients.length();
		clients.add(c);
	}
	clients[cn]->reset();
	clients[cn]->state.ownernum = aiowner;
	clients[cn]->isauthed = true;
	clients[cn]->team = freeteam();
	sendf(-1, 1, "ri4", N_INITAI, cn, aiowner, clients[cn]->team);
	return true;
}

void deleteai(client &c){
    if(c.state.ownernum < 0) return;
    const int cn = c.clientnum;
	sdropflag(cn);
	if(c.priv) setpriv(cn, PRIV_NONE, 0, true);
    sendf(-1, 1, "ri2", N_DELBOT, cn);
	c.state.ownernum = -1;
	c.zap();
}

bool delai(){
	loopvrev(clients) if(clients[i]->state.ownernum >= 0){
		deleteai(*clients[i]);
		return true;
	}
	return false;
}

void clearai(){
	loopv(clients) if(clients[i]->state.ownernum >= 0) deleteai(*clients[i]);
}

void checkai(){
	int balance = 0;
	const int people = numclients(true);
	switch(botbalance){
		case -1: balance = max(people, m_duel ? 2 : 4); break;
		case  0: balance = 0; break; // no bots
		default: balance = max(people, m_duel ? 2 : botbalance); break;
	}
	if(balance > 0){
		int plrs[2] = {0}, highest = -1;
		loopv(clients) if(clients[i]->state.ownernum < 0 && clients[i]->team < 2){
			plrs[clients[i]->team]++;
			if(highest < 0 || plrs[clients[i]->team] > plrs[highest]) highest = clients[i]->team;
		}
		if(highest >= 0){
			int bots = balance-people;
			loopi(2) if(i != highest && plrs[i] < plrs[highest]) loopj(plrs[highest]-plrs[i]){
				if(bots > 0) bots--;
				else balance++;
			}
		}
	}
	if(balance > 0){
		while(numclients(true) < balance) if(!addai()) break;
		while(numclients(true) > balance) if(!delai()) break;
	}
	else clearai();
}