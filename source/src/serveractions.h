// available server actions

enum { EE_LOCAL_SERV = 1 << 0, EE_DED_SERV = 1 << 1, EE_ALL = (1 << 2) - 1 }; // execution environment

int privconf(int key)
{
	if(strchr(scl.voteperm, tolower(key))) return PRIV_NONE;
	if(strchr(scl.voteperm, toupper(key))) return PRIV_ADMIN;
	return islower(key) ? PRIV_NONE : PRIV_ADMIN;
}

struct serveraction
{
	uchar reqpriv, reqveto; // required client privilege to call and veto
	int length;
	float passratio;
	int area; // only on ded servers
	string desc;

	virtual void perform() = 0;
	virtual bool isvalid() { return true; }
	virtual bool isdisabled() { return false; }
	virtual bool isremovingaplayer() const { return false; }
	serveraction() : reqpriv(PRIV_NONE), length(40000), area(EE_ALL), passratio(0.5f), reqveto(PRIV_ADMIN) { *desc = 0; }
	virtual ~serveraction() { }
};

struct mapaction : serveraction
{
	char *map;
	int mode, muts;
	bool mapok;
	void perform()
	{
		if(*map == '+')
		{
			nextgamemode = mode;
			nextgamemuts = muts;
			copystring(nextmapname, map + 1);
		}
		else if(isdedicated && numclients() > 2 && smode >= 0 && !m_edit(gamemode) && gamemillis > gamelimit/5)
		{
			forceintermission = true;
			nextgamemode = mode;
			nextgamemuts = muts;
			copystring(nextmapname, map);
		}
		else
		{
			resetmap(map, mode, muts);
		}
	}
	bool isvalid() { return serveraction::isvalid() && *map && m_valid(mode) && mode != G_DEMO && mapok; }
	bool isdisabled() { return configsets.inrange(curcfgset) && !configsets[curcfgset].vote; }
	mapaction(char *map, int mode, int muts, int caller) : map(map), mode(mode), muts(muts)
	{
		bool is_next = *map == '+';
		if(is_next) ++map; // skip over the +
		int maploc = MAP_NOTFOUND;
		mapstats *ms = (map && *map) ? getservermapstats(map, false, &maploc) : NULL;
		mapok = true;
		if(m_edit(mode))
		{ // admin needed for coopedit
			reqpriv = privconf('E');
			if(reqpriv) sendservmsg("\f3INFO: coopedit is restricted", caller);
		}
		else if(!ms || maploc == MAP_NOTFOUND)
		{
			sendservmsg(maploc == MAP_NOTFOUND ? "\f3the server does not have this map (sendmap first)" : "\f3the server could not read the map", caller);
			mapok = false;
		}
		else
		{
			const bool spawns = ((!m_team(mode, muts) || m_keep(mode)|| m_zombie(mode)) ? ms->spawns[2] : false) || (ms->spawns[0] && ms->spawns[1]);
			const bool flags = m_secure(mode) || m_hunt(mode) || !m_affinity(mode) || (ms->flags[0] && ms->flags[1]);
			const bool secures = !m_secure(mode) || ms->flags[2];
				
			if(!spawns || !flags || !secures)
			{
				reqpriv = privconf('P');
				if(reqpriv && !strchr(scl.voteperm, 'P')) mapok = false;
				defformatstring(msg)("\f3map \"%s\" does not support \"%s\": missing ", behindpath(map), modestr(mode, muts, false));
				if(!spawns) concatstring(msg, "player spawns, ");
				if(!flags) concatstring(msg, "flag bases, ");
				if(!secures) concatstring(msg, "secure flags, ");
				// trim off the last 2
				msg[strlen(msg)-2] = '\0';
				sendservmsg(msg, caller);
			}

			if(is_next && nextgamemode == mode && nextgamemuts == muts && !strcmp(nextmapname, map)){
				mapok = false;
				sendservmsg("This is already the next map/mode/mutators!", caller);
			}
		}
		loopv(scl.adminonlymaps) // admin needed for these maps
			if(!strcmp(behindpath(map), scl.adminonlymaps[i]))
				reqpriv = PRIV_ADMIN;
		passratio = 0.59f; // you need 60% to vote a map
		formatstring(desc)("%s map '%s' in mode '%s'", is_next ? "set next" : "load", map, modestr(mode, muts));
	}
	~mapaction() { DELETEA(map); }
};

struct demoplayaction : serveraction
{
	char *map;
	void perform() { resetmap(map, G_DEMO, G_M_NONE); }
	demoplayaction(char *map) : map(map)
	{
		area = EE_LOCAL_SERV; // only local
	}

	~demoplayaction() { DELETEA(map); }
};

struct playeraction : serveraction
{
	int cn;
	virtual bool isvalid() { return valid_client(cn); }
	playeraction(int cn) : cn(cn) { };
};

struct forceteamaction : playeraction
{
	void perform() { updateclientteam(cn, team_opposite(clients[cn]->team), FTR_AUTOTEAM); checkai(); }
	virtual bool isvalid() { return playeraction::isvalid() && m_team(gamemode, mutators); }
	forceteamaction(int cn, int caller) : playeraction(cn)
	{
		if(cn != caller){ reqpriv = privconf('f'); passratio = 0.65f;}
		else passratio = 0.55f;
		if(valid_client(cn)) formatstring(desc)("force player %s to the enemy team", clients[cn]->name);
		else copystring(desc, "invalid forceteam");
	}
};

struct revokeaction : playeraction
{
	void perform(){ setpriv(cn, PRIV_NONE); }
	virtual bool isvalid() { return playeraction::isvalid() && clients[cn]->priv;}
	revokeaction(int cn) : playeraction(cn){
		area = EE_DED_SERV; // dedicated only
		reqpriv = max<int>(PRIV_ADMIN, valid_client(cn) ? clients[cn]->priv : 0);
		passratio = 0.1f;
		if(valid_client(cn)) formatstring(desc)("revoke %s's %s", clients[cn]->name, privname(clients[cn]->priv));
		else formatstring(desc)("invalid revoke to %d", cn);
	}
};

struct spectaction : playeraction
{
	void perform(){ if(isvalid()){ if(clients[cn]->team == TEAM_SPECT) updateclientteam(cn, chooseteam(*clients[cn]), FTR_AUTOTEAM); else updateclientteam(cn, TEAM_SPECT, FTR_AUTOTEAM); checkai(); } }
	spectaction(int cn, int caller) : playeraction(cn){
		if(cn != caller){ reqpriv = privconf('f'); passratio = 0.65f;}
		else passratio = 0.55f;
		if(valid_client(cn)) formatstring(desc)("toggle spectator for %s", clients[cn]->name);
		else copystring(desc, "invalid spect");
	}
};

struct giveadminaction : playeraction
{
	int give, from;
	void perform() {
		if(valid_client(from) && clients[from]->priv < PRIV_ADMIN) setpriv(from, PRIV_NONE);
		setpriv(cn, give);
	}
	// virtual bool isvalid() { return valid_client(cn); } // give to anyone
	giveadminaction(int cn, int wants, int caller) : from(caller), playeraction(cn){
		give = min(wants, clients[from]->priv);
		reqpriv = max(give, 1);
		reqveto = PRIV_MASTER; // giverole
		passratio = 0.1f;
		if(valid_client(cn)) formatstring(desc)("give %s to %s", privname(give), clients[cn]->name);
		else formatstring(desc)("invalid give-%s to %d", privname(give), cn);
	}
};

inline uchar protectAdminPriv(const char conf, int cn){
	return max(privconf(conf), valid_client(cn) && clients[cn]->priv >= PRIV_ADMIN ? clients[cn]->priv : PRIV_NONE);
}

struct subdueaction : playeraction
{
	void perform() { forcedeath(clients[cn], true); }
	virtual bool isvalid() { return playeraction::isvalid() && !m_edit(gamemode) && clients[cn]->team != TEAM_SPECT; }
	subdueaction(int cn) : playeraction(cn)
	{
		passratio = 0.8f;
		reqpriv = protectAdminPriv('Q', cn);
		length = 25000; // 25s
		if(valid_client(cn)) formatstring(desc)("subdue player %s", clients[cn]->name);
		else copystring(desc, "invalid subdue");
	}
};

struct removeplayeraction : playeraction
{
	removeplayeraction(int cn) : playeraction(cn) { }
	bool isremovingaplayer() const { return true; }
	bool weak(bool kicking) {
		if(!valid_client(cn)) return false;
		// lagging?
		if(kicking && (clients[cn]->ping > 500 || clients[cn]->state.spj > 50 || clients[cn]->state.ldt > 80)) return false;
		// 3+ KDr, 6+ kills
		if(clients[cn]->state.frags >= min(2, clients[cn]->state.deaths) * 3) return false;
		// no teamkills
		// check spam?
		return true; // why kick?
	}
};

struct kickaction : removeplayeraction
{
	string reason;
	void perform() { disconnect_client(cn, DISC_KICK); }
	virtual bool isvalid() { return playeraction::isvalid() && valid_client(cn, true) && strlen(reason) >= 4; }
	kickaction(int cn, const char *r, bool self_vote) : removeplayeraction(cn)
	{
		area = EE_DED_SERV; // dedicated only
		const bool is_weak = self_vote || weak(true);
		copystring(reason, r);
		passratio = is_weak ? .85f : .68f; // 68%-85%
		reqpriv = protectAdminPriv('k', cn);
		reqveto = PRIV_MASTER; // kick
		length = is_weak ? 10000 : 35000; // 35s (10s if weak)
		if(valid_client(cn)) formatstring(desc)("kick player %s for %s%s", clients[cn]->name, reason, is_weak ? " (weak)" : "");
		else formatstring(desc)("invalid kick for %s", reason);
	}
};

struct banaction : removeplayeraction
{
	string reason;
	int bantime;
	void perform(){
		banclient(clients[cn], bantime);
		disconnect_client(cn, DISC_BAN);
	}
	virtual bool isvalid() { return playeraction::isvalid() && valid_client(cn, true); }
	banaction(int cn, int minutes, const char *r, bool self_vote) : removeplayeraction(cn)
	{
		area = EE_DED_SERV; // dedicated only
		const bool is_weak = self_vote || weak(minutes <= 1);
		copystring(reason, r);
		passratio = is_weak ? .89f : .75f; // 75% - 89%
		reqpriv = protectAdminPriv('b', cn);
		reqveto = PRIV_MASTER; // ban
		length = is_weak ? 8000 : 30000; // 30s (8s if weak)
		if(isvalid()) formatstring(desc)("ban player %s for %d minutes for %s%s", clients[cn]->name, (bantime = minutes), reason, is_weak ? " (weak)" : "");
		else copystring(desc, "invalid ban");
	}
};

struct removebansaction : serveraction
{
	void perform() { bans.shrink(0); }
	removebansaction()
	{
		area = EE_DED_SERV; // dedicated only
		passratio = 0.7f;
		reqpriv = privconf('b');
		copystring(desc, "remove all bans");
	}
};

struct mastermodeaction : serveraction
{
	int mode;
	void perform() { mastermode = mode; }
	bool isvalid() { return mode >= 0 && mode < MM_NUM; }
	mastermodeaction(int mode) : mode(mode)
	{
		area = EE_DED_SERV; // dedicated only
		reqpriv = privconf('M');
		if(isvalid()) formatstring(desc)("change mastermode to '%s'", mmfullname(mode));
	}
};

struct enableaction : serveraction
{
	bool enable;
	enableaction(bool enable) : enable(enable) {}
};

struct autoteamaction : enableaction
{
	void perform(){
		autoteam = enable;
		if(m_team(gamemode, mutators) && enable) refillteams(true);
	}
	autoteamaction(bool enable) : enableaction(enable){
		reqpriv = privconf('a');
		if(isvalid()) formatstring(desc)("%s autoteam", enable ? "enable" : "disable");
	}
};

struct shuffleteamaction : serveraction
{
	void perform() { shuffleteams(); }
	bool isvalid() { return serveraction::isvalid() && m_team(gamemode, mutators); }
	shuffleteamaction()
	{
		reqpriv = privconf('s');
		if(isvalid()) copystring(desc, "shuffle teams");
	}
};

struct nextroundaction : serveraction
{
	void perform() { forceintermission = true; }
	bool isvalid() { return serveraction::isvalid(); }
	nextroundaction()
	{
		reqpriv = privconf('n');
		if(isvalid()) copystring(desc, "start next map");
	}
};

struct recorddemoaction : enableaction
{
	void perform() { demonextmatch = enable; }
	recorddemoaction(bool enable) : enableaction(enable)
	{
		area = EE_DED_SERV; // dedicated only
		reqpriv = privconf('R');
		if(isvalid()) formatstring(desc)("%s demorecord", enable ? "enable" : "disable");
	}
};

struct stopdemoaction : serveraction
{
	void perform()
	{
		if(m_demo(gamemode)) enddemoplayback();
		else enddemorecord();
	}
	stopdemoaction()
	{
		reqpriv = PRIV_ADMIN;
		copystring(desc, "stop demo");
	}
};

struct cleardemosaction : serveraction
{
	int demo;
	void perform() { cleardemos(demo); }
	cleardemosaction(int demo) : demo(demo)
	{
		area = EE_DED_SERV; // dedicated only
		reqpriv = privconf('C');
		if(isvalid()) formatstring(desc)("clear demo %d", demo);
	}
};

struct botbalanceaction : serveraction
{
	int bb;
	void perform() { botbalance = bb; checkai(); }
	botbalanceaction(int b) : bb(b)
	{
		reqpriv = privconf('a');
		reqveto = PRIV_MASTER; // botbalance
		if(isvalid()){
			formatstring(desc)(b<0?"automatically balance bots":b==0?"disable all bots":b==1?"bots balance teams only":"balance to %d players", b);
		}
	}
};

struct serverdescaction : serveraction
{
	char *sdesc;
	int cn;
	ENetAddress address;
	void perform() { updatesdesc(sdesc, &address); }
	bool isvalid() { return serveraction::isvalid() && updatedescallowed() && valid_client(cn); }
	serverdescaction(char *sdesc, int cn) : sdesc(sdesc), cn(cn)
	{
		area = EE_DED_SERV; // dedicated only
		reqpriv = privconf('D');
		formatstring(desc)("set server description to '%s'", sdesc);
		if(isvalid()) address = clients[cn]->peer->address;
	}
	~serverdescaction() { DELETEA(sdesc); }
};

struct voteinfo
{
	int owner, callmillis, result, type;
	serveraction *action;

	voteinfo() : owner(0), callmillis(0), result(VOTE_NEUTRAL), action(NULL), type(SA_NUM) {}

	void end(int result, int veto)
	{
		if(!action || !action->isvalid()) result = VOTE_NO; // don't perform() invalid votes
		if(valid_client(veto)) logline(ACLOG_INFO, "[%s] vote %s, forced by %s (%d)", gethostname(owner), result == VOTE_YES ? "passed" : "failed", clients[veto]->name, veto);
		else logline(ACLOG_INFO, "[%s] vote %s (%s)", gethostname(owner), result == VOTE_YES ? "passed" : "failed", veto == -2 ? "enough votes" : veto == -3 ? "expiry" : "unknown");
		sendf(-1, 1, "ri3", N_VOTERESULT, result, veto);
		this->result = result;
		if(result == VOTE_YES)
		{
			if(valid_client(owner)){
				if(action->isremovingaplayer()) clients[owner]->lastkickcall = 0;
				else clients[owner]->lastvotecall = 0;
			}
			if(action) action->perform();
		}
		loopv(clients) clients[i]->vote = VOTE_NEUTRAL;
	}

	bool isvalid() { return valid_client(owner) && action != NULL && action->isvalid(); }
	bool isalive() { return servmillis - callmillis < action->length; }

	void evaluate(bool forceend = false, int veto = VOTE_NEUTRAL, int vetoowner = -1)
	{
		if(result!=VOTE_NEUTRAL) return; // block double action
		if(!action || !action->isvalid()) end(VOTE_NO, -1);
		int stats[VOTE_NUM+1] = {0};
		loopv(clients) if(valid_client(i, true)){
			++stats[clients[i]->vote%VOTE_NUM];
			++stats[VOTE_NUM];
		}
		const int expireresult = stats[VOTE_YES]/(float)(stats[VOTE_NO]+stats[VOTE_YES]) > action->passratio ? VOTE_YES : VOTE_NO;
		sendf(-1, 1, "ri4", N_VOTEREMAIN, expireresult,
			(int)floor(stats[VOTE_NUM] * action->passratio) + 1 - stats[VOTE_YES],
			(int)floor(stats[VOTE_NUM] * action->passratio) + 1 - stats[VOTE_NO]); 
		// can it end?
		if(forceend){
			if(veto == VOTE_NEUTRAL) end(expireresult, -3);
			else end(veto, vetoowner);
		}
		else if(stats[VOTE_YES]/(float)stats[VOTE_NUM] > action->passratio || (!isdedicated && clients[owner]->type==ST_LOCAL))
			end(VOTE_YES, -2);
		else if(stats[VOTE_NO]/(float)stats[VOTE_NUM] > action->passratio)
			end(VOTE_NO, -2);
	}
};