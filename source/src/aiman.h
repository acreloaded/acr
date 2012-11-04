// server-side ai (bot) manager
int findaiclient(int exclude = -1){ // person with least bots, if possible
	int cn = -1, bots = MAXCLIENTS;
	loopv(clients){
		client *c = clients[i];
		if(i == exclude || !valid_client(i, true) || c->clientnum < 0 /*|| !*c->name || !c->connected*/) continue;
		int n = 0;
		loopvj(clients) if(clients[j]->type == ST_AI && clients[j]->state.ownernum == i) ++n;
		if(n < bots || cn < 0){
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
		if(numbots > (m_zombie(gamemode) ? MAXCLIENTS : MAXBOTS)) return false;
		if(clients[i]->type == ST_AI) ++numbots;
		else if(clients[i]->type == ST_EMPTY){
			cn = i;
			break;
		}
	}
	if(cn < 0){
		if(clients.length() >= MAXCLIENTS) return false;
		client *c = new client;
		c->clientnum = cn = clients.length();
		clients.add(c);
	}
	client &b = *clients[cn];
	b.reset();
	b.type = ST_AI;
	b.connected = true;
	b.team = chooseteam(b); // choose its team
	b.state.skin = rand();// random skin
	b.state.level = 45 + rnd(51); // how smart/stupid the bot is can be set here (currently random from 45 to 95)
	b.state.ownernum = aiowner;
	mkbotname(b);
	sendf(-1, 1, "ri5si", N_INITAI, cn, b.team, b.state.skin, b.state.level, b.name, b.state.ownernum);
	forcedeath(&b);
	if(canspawn(&b, true)) sendspawn(&b);
	return true;
}

void deleteai(client &c){
    if(c.type != ST_AI || c.state.ownernum < 0) return;
	const int cn = c.clientnum;
	c.state.ownernum = -1;
	clientdisconnect(cn);
	sendf(-1, 1, "ri2", N_DELAI, cn);
}

bool delai(){
	loopvrev(clients) if(clients[i]->type == ST_AI){
		deleteai(*clients[i]);
		return true;
	}
	return false;
}

bool shiftai(client &c, int ncn = -1, int exclude = -1){
	if(!valid_client(ncn, true)){
		ncn = findaiclient(exclude);
		if(!valid_client(ncn, true)) return false;
	}
	c.state.ownernum = ncn;
	forcedeath(&c); // prevent spawn state bugs
	sendf(-1, 1, "ri3s", N_REASSIGNAI, c.clientnum, c.state.ownernum, c.name);
	return true;
}

void clearai(){ loopv(clients) if(clients[i]->type == ST_AI) deleteai(*clients[i]); }

bool reassignai(int exclude = -1){
	int hi = -1, lo = -1, hicount = -1, locount = -1;
	loopv(clients)
	{
		client *ci = clients[i];
		if(ci->type == ST_EMPTY || ci->type == ST_AI || !ci->name[0] || !ci->connected || ci->clientnum == exclude) continue;
		int thiscount = 0;
		loopvj(clients) if(clients[j]->state.ownernum == ci->clientnum) ++thiscount;
		if(hi < 0 || thiscount > hicount){ hi = i; hicount = thiscount; }
		if(lo < 0 || thiscount < locount){ lo = i; locount = thiscount; }
	}
	if(hi >= 0 && lo >= 0 && hicount > locount + 1)
	{
		client *ci = clients[hi];
		loopv(clients) if(clients[i]->type == ST_AI && clients[i]->state.ownernum == ci->clientnum) return shiftai(*clients[i], lo);
	}
	return false;
}

void checkai(){
	// check if bots are disallowed
	if(!m_ai(gamemode) || !botbalance) return clearai();
	// check balance
	if(m_progressive(gamemode, mutators)){
		if(progressiveround > MAXZOMBIEROUND) return clearai();
		const int zombies = clamp(progressiveround, 1, 20),
			zombies_suicide = max((int)floor(progressiveround / 2.f), progressiveround - 10);
		int zombies_suicide_given = 0;
		while(countbots() < zombies) if(!addai()) break;
		while(countbots() > zombies) if(!delai()) break;
		// force suicide bombers
		loopv(clients) if(clients[i]->type == ST_AI){
			bool has_bomber = (++zombies_suicide_given <= zombies_suicide);
			clients[i]->state.deathstreak = has_bomber ? 10 : 0;
			clients[i]->state.streakondeath = has_bomber ? STREAK_REVENGE : -1;
		}
	}
	else{
		int balance = 0;
		const int humans = countplayers(false);
		if(humans) switch(botbalance){
			case -1: // auto
				if(m_zombie(gamemode)) balance = min(15 + 2 * humans, 30); // effectively 15 + n
				else if(m_duke(gamemode, mutators)) balance = max(humans, maplayout_factor - 3); // 3 - 5 - 8 (6 - 8 - 11 layout factor)
				else if(m_team(gamemode, mutators)) balance = clamp((smapstats.spawns[0] + smapstats.spawns[1]) / 3, max(6, humans), 14);
				else balance = clamp(smapstats.spawns[2] / 3, max(4, humans), 10);
				break; // auto
			// case  0: balance = 0; break; // force no bots
			default: balance = max(humans, botbalance); break; // force bot count
		}
		if(balance > 0){
			if(m_team(gamemode, mutators) && !m_zombie(gamemode)){
				int plrs[2] = {0}, highest = -1;
				loopv(clients) if(valid_client(i, true) && clients[i]->team < 2){
					++plrs[clients[i]->team];
					if(highest < 0 || plrs[clients[i]->team] > plrs[highest]) highest = clients[i]->team;
				}
				if(highest >= 0){
					int bots = balance-humans;
					loopi(2) if(i != highest && plrs[i] < plrs[highest]) loopj(plrs[highest]-plrs[i]){
						if(bots > 0) --bots;
						else ++balance;
					}
				}
				// fix if odd
				if(botbalance < 0 && (balance & 1)) ++balance;
			}
			while(countplayers() < balance) if(!addai()) break;
			while(countplayers() > balance) if(!delai()) break;
			if(m_team(gamemode, mutators) && !m_convert(gamemode, mutators)) loopvrev(clients){
				client &ci = *clients[i];
				if(ci.type != ST_AI) continue;
				int teamb = chooseteam(ci, ci.team);
				if(ci.team != teamb) updateclientteam(i, teamb, FTR_SILENT);
			}
		}
		else clearai();
	}
}