// ai (bots)
int findaiclient(int exclude = -1){ // person with least bots
	//return -1; // disable adding bots
	int cn = -1, bots = MAXBOTS;
	loopv(clients){
		client *c = clients[i];
		if(i == exclude || !valid_client(i) || c->clientnum < 0 || c->state.ownernum >= 0 /*|| !*c->name || !c->connected*/) break;
		int n = 0;
		loopvj(clients) if(clients[j]->state.ownernum == i) n++;
		if(n < bots){
			bots = n;
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
	client &b = *clients[cn];
	formatstring(b.name)("bot%d", cn);
	b.type = ST_AI;
	b.reset();
	b.connected = true;
	sendf(-1, 1, "ri5", N_INITAI, cn, (b.team = freeteam()), (b.skin = rand()), (b.state.ownernum = aiowner));
	return true;
}

void deleteai(client &c){
    if(c.type != ST_AI || c.state.ownernum < 0) return;
    const int cn = c.clientnum;
	sdropflag(cn);
	if(c.priv) setpriv(cn, PRIV_NONE, 0, true);
    sendf(-1, 1, "ri2", N_DELAI, cn);
	c.state.ownernum = -1;
	c.zap();
}

bool delai(){
	loopvrev(clients) if(clients[i]->type == ST_AI && clients[i]->state.ownernum >= 0){
		deleteai(*clients[i]);
		return true;
	}
	return false;
}

void clearai(){
	loopv(clients) if(clients[i]->type == ST_AI && clients[i]->state.ownernum >= 0) deleteai(*clients[i]);
}

void checkai(){
	int balance = 0;
	const int people = numclients();
	if(people) switch(botbalance){
		case -1: balance = max(people, m_duel ? maplayout_factor - 4 : maplayout_factor * maplayout_factor / 6); break; // auto
			// map sizes 6/7/8/9/10/11 duel: 2, 3, 4, 5, 6, 7 normal: 6, 8, 11, 14, 17, 20
		case  0: balance = 0; break; // force no bots
		default: balance = max(people, botbalance); break; // force bot count
	}
	if(balance > 0 && m_team){
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