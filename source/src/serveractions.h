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
	serveraction() : reqpriv(PRIV_NONE), length(40000), area(EE_ALL), passratio(0.5f), reqveto(PRIV_ADMIN) { *desc = 0; }
	virtual ~serveraction() { }
};

struct mapaction : serveraction
{
	char *map;
	int mode;
	void perform()
	{
		if(isdedicated && numclients() > 2 && smode >= 0 && !m_edit(gamemode) && gamemillis > gamelimit/5)
		{
			forceintermission = true;
			nextgamemode = mode;
			copystring(nextmapname, map);
		}
		else
		{
			resetmap(map, mode);
		}
	}
	bool isvalid() { return serveraction::isvalid() && map[0] && m_valid(mode); }
	bool isdisabled() { return configsets.inrange(curcfgset) && !configsets[curcfgset].vote; }
	mapaction(char *map, int mode, int caller) : map(map), mode(mode)
	{
		if(isdedicated)
		{
			const bool notify = valid_client(caller) && clients[caller]->priv < PRIV_ADMIN;
			mapstats *ms = getservermapstats(map);
			if(privconf('x') && !ms){ // admin needed for unknown maps
				reqpriv = PRIV_ADMIN;
				if(notify) sendmsg(12, caller);
			}
			if(m_edit(mode) && notify){ // admin needed for coopedit
				sendmsg(10, caller);
				reqpriv = PRIV_ADMIN;
			}
			if(ms && privconf('P')) // admin needed for mismatched modes
			{
				int smode = mode;  // 'borrow' the mode macros by replacing a global by a local var
				bool spawns = (m_team(gamemode, mutators) && !m_keep(gamemode) && !m_zombie(gamemode)) ? ms->hasteamspawns : ms->hasffaspawns;
				bool flags = m_affinity(gamemode) && !m_hunt(gamemode) ? ms->hasflags : true;
				if(!spawns || !flags)
				{
					reqpriv = PRIV_ADMIN;
					string mapfail;
					copystring(mapfail + 1, behindpath(map));
					*mapfail = mode;
					if(!spawns) *mapfail |= 0x80;
					if(!flags) *mapfail |= 0x40;
					sendmsgs(15, mapfail, caller);
				}
			}
			loopv(scl.adminonlymaps) // admin needed for these maps
				if(!strcmp(behindpath(map), scl.adminonlymaps[i])) reqpriv = PRIV_ADMIN;
			if(notify) passratio = 0.6f; // you need 60% to vote a map without admin
		}
		formatstring(desc)("load map '%s' in mode '%s'", map, modestr(mode));
	}
	~mapaction() { DELETEA(map); }
};

struct demoplayaction : serveraction
{
	char *map;
	void perform() { resetmap(map, G_DEMO); }
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
	void perform() { updateclientteam(cn, team_opposite(clients[cn]->team), FTR_AUTOTEAM); }
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
	void perform(){ if(isvalid()){ if(clients[cn]->team == TEAM_SPECT) updateclientteam(cn, freeteam(cn), FTR_AUTOTEAM); else updateclientteam(cn, TEAM_SPECT, FTR_AUTOTEAM); } }
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

struct kickaction : playeraction
{
	string reason;
	void perform() { disconnect_client(cn, DISC_KICK); }
	virtual bool isvalid() { return playeraction::isvalid() && valid_client(cn, true) && strlen(reason) >= 4; }
	kickaction(int cn, const char *r) : playeraction(cn)
	{
		area = EE_DED_SERV; // dedicated only
		copystring(reason, r);
		passratio = 0.7f;
		reqpriv = protectAdminPriv('k', cn);
		reqveto = PRIV_MASTER; // kick
		length = 35000; // 35s
		if(valid_client(cn)) formatstring(desc)("kick player %s for %s", clients[cn]->name, reason);
		else formatstring(desc)("invalid kick for %s", reason);
	}
};

struct banaction : playeraction
{
	int bantime;
	void perform(){
		banclient(clients[cn], bantime);
		disconnect_client(cn, DISC_BAN);
	}
	virtual bool isvalid() { return playeraction::isvalid() && valid_client(cn, true); }
	banaction(int cn, int minutes) : playeraction(cn)
	{
		area = EE_DED_SERV; // dedicated only
		passratio = 0.75f;
		reqpriv = protectAdminPriv('b', cn);
		reqveto = PRIV_MASTER; // ban
		length = 30000; // 30s
		if(isvalid()) formatstring(desc)("ban player %s for %d minutes", clients[cn]->name, (bantime = minutes));
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
			formatstring(desc)(b<0?"automatically balance bots":b==0?"disable all bots":"balance to %d bots", b);
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

	void end(int result, int veto = -1)
	{
		if(!action || !action->isvalid()) result = VOTE_NO; // don't perform() invalid votes
		logline(ACLOG_INFO, valid_client(veto) ? "[%s] vote %s, forced by %s (%d)" : "[%s] vote %s", gethostname(owner), result == VOTE_YES ? "passed" : "failed", valid_client(veto) ? clients[veto]->name : "winning votes", veto);
		sendf(-1, 1, "ri3", N_VOTERESULT, result, veto);
		this->result = result;
		if(result == VOTE_YES)
		{
			if(valid_client(owner)) clients[owner]->lastvotecall = 0;
			if(action) action->perform();
		}
		loopv(clients) clients[i]->vote = VOTE_NEUTRAL;
	}

	bool isvalid() { return valid_client(owner) && action != NULL && action->isvalid(); }
	bool isalive() { return servmillis - callmillis < action->length; }

	void evaluate(bool forceend = false, int veto = VOTE_NEUTRAL, int vetoowner = -1)
	{
		if(result!=VOTE_NEUTRAL) return; // block double action
		if(!action || !action->isvalid()) end(VOTE_NO);
		int stats[VOTE_NUM+1] = {0};
		loopv(clients) if(valid_client(i, true)){
			++stats[clients[i]->vote%VOTE_NUM];
			++stats[VOTE_NUM];
		}
		if(forceend){
			if(veto == VOTE_NEUTRAL) end(stats[VOTE_YES]/(float)(stats[VOTE_NO]+stats[VOTE_YES]) > action->passratio ? VOTE_YES : VOTE_NO);
			else end(veto, vetoowner);
		}

		if(stats[VOTE_YES]/(float)stats[VOTE_NUM] > action->passratio || (!isdedicated && clients[owner]->type==ST_LOCAL))
			end(VOTE_YES);
		else if(stats[VOTE_NO]/(float)stats[VOTE_NUM] > action->passratio)
			end(VOTE_NO);
		else return;
	}
};