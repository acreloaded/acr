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
	int role; // required client role
	int length;
	float passratio;
	int area; // only on ded servers
	string desc;

	virtual void perform() = 0;
	virtual bool isvalid() { return true; }
	virtual bool isdisabled() { return false; }
	serveraction() : role(PRIV_NONE), length(40000), area(EE_DED_SERV), passratio(0.5f) { desc[0] = '\0'; }
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
	bool isvalid() { return serveraction::isvalid() && mode != GMODE_DEMO && map[0] && !(isdedicated && !m_mp(mode)); }
	bool isdisabled() { return configsets.inrange(curcfgset) && !configsets[curcfgset].vote; }
	mapaction(char *map, int mode, int caller) : map(map), mode(mode)
	{
		if(isdedicated)
		{
			bool notify = valid_client(caller) && clients[caller]->priv < PRIV_ADMIN;
			mapstats *ms = getservermapstats(map);
			if(strchr(scl.voteperm, 'x') && !ms){ // admin needed for unknown maps
				role = PRIV_ADMIN;
				if(notify) sendservmsg("the server does not have this map", caller);
			}
			if(mode == GMODE_COOPEDIT && notify){ // admin needed for coopedit
				sendservmsg("\f3coopedit is restricted to admins!", caller);
				role = PRIV_ADMIN;
			}
			if(ms && !strchr(scl.voteperm, 'P')) // admin needed for mismatched modes
			{
				int smode = mode;  // 'borrow' the mode macros by replacing a global by a local var
				bool spawns = (m_teammode && !m_ktf) ? ms->hasteamspawns : ms->hasffaspawns;
				bool flags = m_flags && !m_htf ? ms->hasflags : true;
				if(!spawns || !flags)
				{
					role = PRIV_ADMIN;
					s_sprintfd(msg)("\f3map \"%s\" does not support \"%s\": ", behindpath(map), modestr(mode, false));
					if(!spawns) s_strcat(msg, "player spawns");
					if(!spawns && !flags) s_strcat(msg, " and ");
					if(!flags) s_strcat(msg, "flag bases");
					s_strcat(msg, " missing");
					if(notify) sendservmsg(msg, caller);
					logline(ACLOG_INFO, "%s", msg);
				}
			}
			loopv(scl.adminonlymaps)
			{
				if(!strcmp(behindpath(map), scl.adminonlymaps[i])) role = PRIV_ADMIN;
			}
			if(notify) passratio = 0.6f; // you need 60% to vote a map without admin
		}
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
	virtual bool isvalid() { return valid_client(cn) && clients[cn]->priv < PRIV_ADMIN; } // kick & ban can't be done on admins
	playeraction(int cn) : cn(cn) { };
};

struct forceteamaction : playeraction
{
	void perform() { updateclientteam(cn, team_opposite(clients[cn]->team), FTR_AUTOTEAM); }
	virtual bool isvalid() { return m_teammode && valid_client(cn); }
	forceteamaction(int cn, int caller) : playeraction(cn)
	{
		if(cn != caller){ role = roleconf('f'); passratio = 0.55f;}
		else passratio = 0.7f;
		if(valid_client(cn)) s_sprintf(desc)("force player %s to the enemy team", clients[cn]->name);
		else s_strcpy(desc, "invalid forceteam");
	}
};

struct giveadminaction : playeraction
{
	int give, from;
	void perform() {
		if(valid_client(from) && clients[from]->priv < PRIV_ADMIN) changeclientrole(from, PRIV_NONE, NULL, true);
		changeclientrole(cn, give, NULL, true);
	}
	virtual bool isvalid() { return valid_client(cn); } // give to anyone
	giveadminaction(int cn, int wants, int caller) : from(caller), playeraction(cn){
		give = min(wants, clients[from]->priv);
		role = max(give, 1);
		passratio = 0.1f;
		if(valid_client(cn)) s_sprintf(desc)("give %s to %s", privname(give), clients[cn]->name);
		else s_sprintf(desc)("invalid give-%s to %d", privname(give), cn);
	}
};

struct subdueaction : playeraction
{
	void perform() { forcedeath(clients[cn], true); }
	virtual bool isvalid() { return !m_edit && valid_client(cn); }
	subdueaction(int cn) : playeraction(cn)
	{
		passratio = 0.8f;
		role = roleconf('Q');
		length = 25000; // 25s
		if(valid_client(cn)) s_sprintf(desc)("subdue player %s", clients[cn]->name);
		else s_strcpy(desc, "invalid subdue");
		area |= EE_LOCAL_SERV;
	}
};

struct kickaction : playeraction
{
	void perform() { disconnect_client(cn, DISC_KICK); }
	kickaction(int cn) : playeraction(cn)
	{
		passratio = 0.7f;
		role = roleconf('k');
		length = 35000; // 35s
		if(valid_client(cn)) s_sprintf(desc)("kick player %s", clients[cn]->name);
		else s_strcpy(desc, "invalid kick");
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
		role = roleconf('b');
		length = 30000; // 30s
		if(isvalid()) s_sprintf(desc)("ban player %s for %d minutes", clients[cn]->name, (bantime = minutes));
		else s_strcpy(desc, "invalid ban");
	}
};

struct removebansaction : serveraction
{
	void perform() { bans.setsize(0); }
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
	void perform()
	{
		if(m_teammode && enable) refillteams(true);
	}
	autoteamaction(bool enable) : enableaction(enable)
	{
		role = roleconf('a');
		if(isvalid()) s_sprintf(desc)("%s autoteam", enable ? "enable" : "disable");
	}
};

struct shuffleteamaction : serveraction
{
	void perform() { shuffleteams(); }
	bool isvalid() { return serveraction::isvalid() && m_teammode; }
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
			stats[clients[i]->vote] += clients[i]->priv ? 2 : 1;
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
