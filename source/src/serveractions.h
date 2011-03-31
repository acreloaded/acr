// available server actions

enum { EE_LOCAL_SERV = 1, EE_DED_SERV = 1<<1 }; // execution environment

int roleconf(int key)
{
	if(strchr(scl.voteperm, tolower(key))) return PRIV_NONE;
	if(strchr(scl.voteperm, toupper(key))) return PRIV_ADMIN;
	return (key) == tolower(key) ? PRIV_NONE : PRIV_ADMIN;
}

struct serveraction
{
	uchar role, vetorole; // required client role to call and veto
	int length;
	float passratio;
	int area; // only on ded servers
	string desc;

	virtual void perform() = 0;
	virtual bool isvalid() { return true; }
	virtual bool isdisabled() { return false; }
	serveraction() : role(PRIV_NONE), length(40000), area(EE_DED_SERV), passratio(0.5f), vetorole(PRIV_MASTER) { desc[0] = '\0'; }
	virtual ~serveraction() { }
};

struct mapaction : serveraction
{
	char *map;
	int mode;
	void perform()
	{
		if(isdedicated && numclients() > 2 && smode >= 0 && smode != 1 && gamemillis > gamelimit/4)
		{
			forceintermission = true;
			nextgamemode = mode;
			s_strcpy(nextmapname, map);
		}
		else
		{
			resetmap(map, mode);
		}
	}
	bool isvalid() { return serveraction::isvalid() && mode != GMODE_DEMO && map[0] && !(isdedicated && !m_fight(mode)); }
	bool isdisabled() { return configsets.inrange(curcfgset) && !configsets[curcfgset].vote; }
	mapaction(char *map, int mode, int caller) : map(map), mode(mode)
	{
		if(isdedicated)
		{
			bool notify = valid_client(caller) && clients[caller]->priv < PRIV_ADMIN;
			mapstats *ms = getservermapstats(map);
			if(strchr(scl.voteperm, 'x') && !ms){ // admin needed for unknown maps
				role = PRIV_ADMIN;
				if(notify) sendmsg(12, caller);
			}
			if(mode == GMODE_COOPEDIT && notify){ // admin needed for coopedit
				sendmsg(10, caller);
				role = PRIV_ADMIN;
			}
			if(ms && !strchr(scl.voteperm, 'P')) // admin needed for mismatched modes
			{
				int smode = mode;  // 'borrow' the mode macros by replacing a global by a local var
				bool spawns = (m_team && !m_ktf) ? ms->hasteamspawns : ms->hasffaspawns;
				bool flags = m_flags && !m_htf ? ms->hasflags : true;
				if(!spawns || !flags)
				{
					role = PRIV_ADMIN;
					string mapfail;
					s_strcpy(mapfail + 1, behindpath(map));
					*mapfail = mode;
					if(!spawns) *mapfail |= 0x80;
					if(!flags) *mapfail |= 0x40;
					sendmsgs(15, mapfail, caller);
				}
			}
			loopv(scl.adminonlymaps)
			{
				if(!strcmp(behindpath(map), scl.adminonlymaps[i])) role = PRIV_ADMIN;
			}
			if(notify) passratio = 0.6f; // you need 60% to vote a map without admin
		}
		vetorole = PRIV_ADMIN; // don't let masters abuse maps
		area |= EE_LOCAL_SERV; // local too
		s_sprintf(desc)("load map '%s' in mode '%s'", map, modestr(mode));
	}
	~mapaction() { DELETEA(map); }
};

struct demoplayaction : serveraction
{
	char *map;
	void perform() { resetmap(map, GMODE_DEMO); }
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
	virtual bool isvalid() { return m_team && valid_client(cn); }
	forceteamaction(int cn, int caller) : playeraction(cn)
	{
		area |= EE_LOCAL_SERV;
		if(cn != caller){ role = roleconf('f'); passratio = 0.65f;}
		else passratio = 0.55f;
		if(valid_client(cn)) s_sprintf(desc)("force player %s to the enemy team", clients[cn]->name);
		else s_strcpy(desc, "invalid forceteam");
	}
};

struct revokeaction : playeraction
{
	void perform(){ changeclientrole(cn, PRIV_NONE); }
	virtual bool isvalid() { return playeraction::isvalid() && clients[cn]->priv;}
	revokeaction(int cn) : playeraction(cn){
		role = max<int>(PRIV_ADMIN, valid_client(cn) ? clients[cn]->priv : 0);
		passratio = 0.1f;
		if(valid_client(cn)) s_sprintf(desc)("revoke %s's %s", clients[cn]->name, privname(clients[cn]->priv));
		else s_sprintf(desc)("invalid revoke to %d", cn);
	}
};

struct giveadminaction : playeraction
{
	int give, from;
	void perform() {
		if(valid_client(from) && clients[from]->priv < PRIV_ADMIN) changeclientrole(from, PRIV_NONE, NULL, true);
		changeclientrole(cn, give, NULL, true);
	}
	// virtual bool isvalid() { return valid_client(cn); } // give to anyone
	giveadminaction(int cn, int wants, int caller) : from(caller), playeraction(cn){
		give = min(wants, clients[from]->priv);
		role = max(give, 1);
		passratio = 0.1f;
		if(valid_client(cn)) s_sprintf(desc)("give %s to %s", privname(give), clients[cn]->name);
		else s_sprintf(desc)("invalid give-%s to %d", privname(give), cn);
	}
};

inline uchar protectAdminRole(const char conf, int cn){
	return max(roleconf(conf), valid_client(cn) && clients[cn]->priv >= PRIV_ADMIN ? clients[cn]->priv : PRIV_NONE);
}

struct subdueaction : playeraction
{
	void perform() { forcedeath(clients[cn], true); }
	virtual bool isvalid() { return !m_edit && valid_client(cn); }
	subdueaction(int cn) : playeraction(cn)
	{
		passratio = 0.8f;
		role = protectAdminRole('Q', cn);
		vetorole = PRIV_ADMIN; // don't let admins abuse this either!
		length = 25000; // 25s
		if(valid_client(cn)) s_sprintf(desc)("subdue player %s", clients[cn]->name);
		else s_strcpy(desc, "invalid subdue");
		area |= EE_LOCAL_SERV;
	}
};

struct kickaction : playeraction
{
	string reason;
	void perform() { disconnect_client(cn, DISC_KICK); }
	kickaction(int cn, char *r) : playeraction(cn)
	{
		s_strcpy(reason, r);
		passratio = 0.7f;
		role = protectAdminRole('k', cn);
		length = 35000; // 35s
		if(valid_client(cn)) s_sprintf(desc)("kick player %s for %s", clients[cn]->name, reason);
		else s_sprintf(desc)("invalid kick for %s", reason);
	}
};

struct banaction : playeraction
{
	int bantime;
	void perform(){
		banclient(clients[cn], bantime);
		disconnect_client(cn, DISC_BAN);
	}
	banaction(int cn, int minutes) : playeraction(cn)
	{
		passratio = 0.75f;
		role = protectAdminRole('b', cn);
		length = 30000; // 30s
		if(isvalid()) s_sprintf(desc)("ban player %s for %d minutes", clients[cn]->name, (bantime = minutes));
		else s_strcpy(desc, "invalid ban");
	}
};

struct removebansaction : serveraction
{
	void perform() { bans.shrink(0); }
	removebansaction()
	{
		passratio = 0.7f;
		role = roleconf('b');
		s_strcpy(desc, "remove all bans");
	}
};

struct mastermodeaction : serveraction
{
	int mode;
	void perform() { mastermode = mode; }
	bool isvalid() { return mode >= 0 && mode < MM_NUM; }
	mastermodeaction(int mode) : mode(mode)
	{
		role = roleconf('M');
		if(isvalid()) s_sprintf(desc)("change mastermode to '%s'", mmfullname(mode));
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
		if(m_team && enable) refillteams(true);
	}
	autoteamaction(bool enable) : enableaction(enable){
		role = roleconf('a');
		if(isvalid()) s_sprintf(desc)("%s autoteam", enable ? "enable" : "disable");
	}
};

struct shuffleteamaction : serveraction
{
	void perform() { shuffleteams(); }
	bool isvalid() { return serveraction::isvalid() && m_team; }
	shuffleteamaction()
	{
		role = roleconf('s');
		if(isvalid()) s_strcpy(desc, "shuffle teams");
	}
};

struct recorddemoaction : enableaction
{
	void perform() { demonextmatch = enable; }
	recorddemoaction(bool enable) : enableaction(enable)
	{
		role = roleconf('R');
		if(isvalid()) s_sprintf(desc)("%s demorecord", enable ? "enable" : "disable");
	}
};

struct stopdemoaction : serveraction
{
	void perform()
	{
		if(m_demo) enddemoplayback();
		else enddemorecord();
	}
	stopdemoaction()
	{
		role = PRIV_ADMIN;
		area |= EE_LOCAL_SERV;
		s_strcpy(desc, "stop demo");
	}
};

struct cleardemosaction : serveraction
{
	int demo;
	void perform() { cleardemos(demo); }
	cleardemosaction(int demo) : demo(demo)
	{
		role = roleconf('C');
		if(isvalid()) s_sprintf(desc)("clear demo %d", demo);
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
		role = roleconf('D');
		s_sprintf(desc)("set server description to '%s'", sdesc);
		if(isvalid()) address = clients[cn]->peer->address;
	}
	~serverdescaction() { DELETEA(sdesc); }
};

struct voteinfo
{
	int owner, callmillis, result, type;
	serveraction *action;

	voteinfo() : owner(0), callmillis(0), result(VOTE_NEUTRAL), action(NULL), type(SA_NUM) {}

	void end(int result, bool veto = false)
	{
		if(!action || !action->isvalid()) result = VOTE_NO; // don't perform() invalid votes
		sendf(-1, 1, "ri2", N_VOTERESULT, result | (veto ? 0x80 : 0));
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

	void evaluate(bool forceend = false, int veto = VOTE_NEUTRAL)
	{
		if(result!=VOTE_NEUTRAL) return; // block double action
		if(!action || !action->isvalid()) end(VOTE_NO);
		int stats[VOTE_NUM+1] = {0};
		loopv(clients) if(clients[i]->type != ST_EMPTY){
			stats[clients[i]->vote] += clients[i]->priv >= PRIV_ADMIN ? 2 : 1;
			stats[VOTE_NUM]++;
		}
		if(forceend){
			if(veto == VOTE_NEUTRAL) end(stats[VOTE_YES]/(float)(stats[VOTE_NO]+stats[VOTE_YES]) > action->passratio ? VOTE_YES : VOTE_NO);
			else end(veto, true);
		}

		if(stats[VOTE_YES]/stats[VOTE_NUM] > action->passratio || (!isdedicated && clients[owner]->type==ST_LOCAL))
			end(VOTE_YES);
		else if(stats[VOTE_NO]/stats[VOTE_NUM] >= action->passratio)
			end(VOTE_NO);
		else return;
	}
};
