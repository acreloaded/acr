// available server actions

enum { EE_LOCAL_SERV = 1<<0, EE_DED_SERV = 1<<1, EE_ALL = (1<<2) - 1 }; // execution environment

int roleconf(int key)
{
    if(strchr(scl.voteperm, tolower(key))) return CR_DEFAULT;
    if(strchr(scl.voteperm, toupper(key))) return CR_ADMIN;
    return islower(key) ? CR_DEFAULT : CR_ADMIN;
}

struct serveraction
{
    int reqcall, reqveto; // required client role to call and veto
    int length;
    float passratio;
    int area; // only on ded servers
    string desc;

    virtual void perform() = 0;
    virtual bool isvalid() { return true; }
    virtual bool isdisabled() const { return false; }
    serveraction() : reqcall(CR_DEFAULT), reqveto(CR_ADMIN), length(40000), passratio(.5f), area(EE_ALL) { desc[0] = '\0'; }
    virtual ~serveraction() { }
};

struct mapaction : serveraction
{
    char *map;
    int mode, muts;
    bool mapok, queue;
    void perform()
    {
        if(queue)
        {
            nextgamemode = mode;
            nextmutators = muts;
            copystring(nextmapname, map);
        }
        else if(isdedicated && numclients() > 2 && !m_demo(smode) && !m_edit(smode) && ( gamemillis > gamelimit/4 || scl.demo_interm ))
        {
            forceintermission = true;
            nextgamemode = mode;
            nextmutators = muts;
            copystring(nextmapname, map);
        }
        else
        {
            startgame(map, mode, muts);
        }
    }
    bool isvalid() { return serveraction::isvalid() && m_valid(mode) && !m_demo(mode) && map[0] && mapok; }
    bool isdisabled() const { return maprot.current() && !maprot.current()->vote; }
    mapaction(char *map, int mode, int muts, int caller, bool q) : map(map), mode(mode), muts(muts), queue(q)
    {
        if(isdedicated)
        {
            bool notify = valid_client(caller);
            int maploc = MAP_NOTFOUND;
            mapstats *ms = map[0] ? getservermapstats(map, false, &maploc) : NULL;
            bool validname = validmapname(map);
            mapok = (ms != NULL) && validname && ( m_edit(mode) ? !readonlymap(maploc) : mapisok(ms) );
            if(!mapok)
            {
                if(notify)
                {
                    if(!validname)
                        sendservmsg("invalid map name", clients[caller]);
                    else
                    {
                        sendservmsg(ms ?
                            ( m_edit(mode) ? "this map cannot be coopedited in this server" : "sorry, but this map does not satisfy some quality requisites to be played in MultiPlayer Mode" ) :
                            "the server does not have this map",
                            clients[caller]);
                    }
                }
            }
            else if (m_edit(mode))
            {
                reqcall = roleconf('E');
                if (notify && reqcall) sendservmsg("\f3INFO: coopedit is restricted", clients[caller]);
            }
            else
            { // check, if map supports mode
                const bool spawns = ((!m_team(mode, muts) || m_keep(mode) || m_zombie(mode)) && ms->spawns[2]) || (ms->spawns[0] && ms->spawns[1]);
                const bool flags = m_secure(mode) || m_hunt(mode) || !m_flags(mode) || (ms->flags[0] && ms->flags[1]);
                const bool secures = !m_secure(mode) || ms->flags[2];
                if(!spawns || !flags || !secures)
                { // unsupported mode
                    reqcall = roleconf('P');
                    defformatstring(msg)("\f3map \"%s\" does not support \"%s\": missing", behindpath(map), modestr(mode, muts, false));
                    if (!spawns) concatstring(msg, "player spawns, ");
                    if (!flags) concatstring(msg, "flag bases, ");
                    if (!secures) concatstring(msg, "secure flags, ");
                    // trim off the last 2
                    msg[strlen(msg) - 2] = '\0';
                    if (notify) sendservmsg(msg, clients[caller]);
                    logline(ACLOG_INFO, "%s", msg);
                }
            }
            loopv(scl.adminonlymaps)
            {
                const char *s = scl.adminonlymaps[i], *h = strchr(s, '#'), *m = behindpath(map);
                size_t sl = strlen(s);
                if(h)
                {
                    if(h != s)
                    {
                        sl = h - s;
                        if(mode != atoi(h + 1)) continue;
                    }
                    else
                    {
                        if(mode == atoi(h+1))
                        {
                            reqcall = CR_ADMIN;
                            break;
                        }
                    }
                }
                if(sl == strlen(m) && !strncmp(m, scl.adminonlymaps[i], sl)) reqcall = CR_ADMIN;
            }
        }
        else mapok = true;
        passratio = 0.59999999f; // 60% to vote a map
        formatstring(desc)("%s map '%s' in mode '%s'", q ? "set next" : "load", map, modestr(mode, muts));
    }
    ~mapaction() { DELETEA(map); }
};

struct demoplayaction : serveraction
{
    char *demofilename;
    void perform() { startdemoplayback(demofilename); }
    demoplayaction(char *demofilename) : demofilename(demofilename)
    {
        area = EE_LOCAL_SERV; // only local
    }

    ~demoplayaction() { DELETEA(demofilename); }
};

struct playeraction : serveraction
{
    int cn;
    virtual bool isvalid() { return valid_client(cn); }
    playeraction(int cn) : cn(cn) { }
};

struct forceteamaction : playeraction
{
    int team;
    void perform() { updateclientteam(*clients[cn], team, FTR_AUTO); checkai(); /* forceteam */ }
    virtual bool isvalid() { return playeraction::isvalid() && m_team(gamemode, mutators) && team_isvalid(team) && team != clients[cn]->team; }
    forceteamaction(int cn, int caller, int team) : playeraction(cn), team(team)
    {
        if (cn != caller)
        {
            reqcall = roleconf('f');
            passratio = 0.65f; // 65% to force others
        }
        else passratio = 0.55f; // 55% to force self
        if (valid_client(cn) && team_isvalid(team))
            formatstring(desc)("force player %s to team %s", clients[cn]->formatname(), teamnames[team]);
    }
};

struct spectaction : playeraction
{
    void perform()
    {
        if (clients[cn]->team == TEAM_SPECT)
            updateclientteam(*clients[cn], chooseteam(*clients[cn]), FTR_AUTO);
        else updateclientteam(*clients[cn], TEAM_SPECT, FTR_AUTO);
        checkai();
    }
    spectaction(int cn, int caller) : playeraction(cn){
        if (cn != caller)
        {
            reqcall = roleconf('f');
            passratio = 0.65f;
        }
        else passratio = 0.55f;
        if (valid_client(cn))
            formatstring(desc)("toggle spectator for %s", clients[cn]->formatname());
    }
};

struct giveadminaction : playeraction
{
    int from, give;
    void perform()
    {
        if (valid_client(from) && clients[from]->role < CR_ADMIN)
            setpriv(*clients[from], CR_DEFAULT); // transfer master instead of give
        setpriv(*clients[cn], give);
    }
    virtual bool isvalid() { return playeraction::isvalid() && valid_client(from); }
    giveadminaction(int cn, int role, int caller) : playeraction(cn)
    {
        reqcall = give = clamp(role, 1, clients[from = caller]->role);
        reqveto = CR_MASTER; // giveadmin
        passratio = 0.1f;
        if (valid_client(cn))
            formatstring(desc)("give %s to %s", privname(give), clients[cn]->formatname());
    }
};

struct revokeaction : playeraction
{
    void perform() { setpriv(*clients[cn], CR_DEFAULT); }
    virtual bool isvalid() { return playeraction::isvalid() && clients[cn]->role; }
    revokeaction(int cn) : playeraction(cn)
    {
        area = EE_DED_SERV; // dedicated only
        reqcall = max((int)(CR_ADMIN), valid_client(cn) ? clients[cn]->role : 0);
        passratio = 0.1f;
        if (valid_client(cn))
            formatstring(desc)("revoke %s from %s", privname(clients[cn]->role), clients[cn]->formatname());
    }
};

inline int protectAdminPriv(const char conf, int cn)
{
    return max(roleconf(conf), valid_client(cn) && clients[cn]->role >= CR_ADMIN ? clients[cn]->role : 0);
}

struct subdueaction : playeraction
{
    void perform() { forcedeath(*clients[cn]); }
    virtual bool isvalid() { return playeraction::isvalid() && !m_edit(gamemode) && clients[cn]->team != TEAM_SPECT; }
    subdueaction(int cn) : playeraction(cn)
    {
        passratio = 0.8f;
        reqcall = protectAdminPriv('q', cn);
        length = 25000; // 25s
        if (valid_client(cn))
            formatstring(desc)("subdue player %s", clients[cn]->formatname());
    }
};

struct removeplayeraction : playeraction
{
    removeplayeraction(int cn) : playeraction(cn) { }
    virtual bool isvalid() { return playeraction::isvalid() && clients[cn]->type == ST_TCPIP; }
    bool weak(bool kicking)
    {
        if (!valid_client(cn)) return false;
        // lagging? (does not apply to bans)
        if (kicking && is_lagging(*clients[cn])) return false;
        // 3+ K/D ratio & 6+ kills
        if (clients[cn]->state.frags >= max(6, clients[cn]->state.deaths * 3)) return false;
        // check spam?
        return true; // why kick or ban?
    }
};

struct kickaction : removeplayeraction
{
    string reason;
    void perform()  { disconnect_client(*clients[cn], DISC_MKICK); }
    virtual bool isvalid() { return removeplayeraction::isvalid() && strlen(reason) >= 4; }
    kickaction(int cn, const char *r, bool self_vote) : removeplayeraction(cn)
    {
        area = EE_DED_SERV; // dedicated only
        const bool is_weak = self_vote || weak(true);
        copystring(reason, r);
        passratio = is_weak ? .85f : .68f; // 68%-85%
        reqcall = protectAdminPriv('k', cn);
        reqveto = CR_MASTER; // kick
        length = is_weak ? 10000 : 35000; // 35s (10s if weak)
        if (valid_client(cn))
            formatstring(desc)("kick player %s for %s%s", clients[cn]->formatname(), reason, is_weak ? " (weak)" : "");
    }
};

struct banaction : removeplayeraction
{
    string reason;
    int minutes;
    void perform()
    {
        // TODO use ban time
        addban(*clients[cn], DISC_MBAN, BAN_VOTE);
    }
    virtual bool isvalid() { return removeplayeraction::isvalid() && strlen(reason) >= 4; }
    banaction(int cn, int mins, char *r, bool self_vote) : removeplayeraction(cn), minutes(mins)
    {
        area = EE_DED_SERV; // dedicated only
        const bool is_weak = self_vote || weak(minutes <= 1);
        copystring(reason, r);
        passratio = is_weak ? .89f : .75f; // 75% - 89%
        reqcall = protectAdminPriv('b', cn);
        reqveto = CR_MASTER; // ban
        length = is_weak ? 8000 : 30000; // 30s (8s if weak)
        if (isvalid())
            formatstring(desc)("ban player %s for %d minutes for %s%s", clients[cn]->formatname(), minutes, reason, is_weak ? " (weak)" : "");
    }
};

struct removebansaction : serveraction
{
    void perform() { bans.shrink(0); }
    removebansaction()
    {
        area = EE_DED_SERV; // dedicated only
        passratio = 0.7f;
        reqcall = roleconf('b');
        reqveto = CR_MASTER; // removebans
        copystring(desc, "remove all bans");
    }
};

struct botbalanceaction : serveraction
{
    int balance;
    void perform() { botbalance = balance; checkai(); /* botbalance changed */ }
    bool isvalid() { return true /*balance >= -9999 && balance <= MAXCLIENTS*/; }
    botbalanceaction(int balance) : balance(clamp(balance, -9999, MAXCLIENTS))
    {
        reqcall = roleconf('a');
        reqveto = CR_MASTER; // botbalance
        if (isvalid())
        {
            if (balance == 1) copystring(desc, "bots balance teams only");
            else if (!balance) copystring(desc, "disable all bots");
            else if (balance == -1) copystring(desc, "automatically balance bots");
            else if (balance < -1) formatstring(desc)("balance to %d RED, %d BLUE", (balance / -100), -balance % 100);
            else formatstring(desc)("balance to %d players", balance);
        }
    }
};

struct mastermodeaction : serveraction
{
    int mode;
    void perform() { changemastermode(mode); }
    bool isvalid() { return mode >= 0 && mode < MM_NUM; }
    mastermodeaction(int mode) : mode(mode)
    {
        area = EE_DED_SERV; // dedicated only
        reqcall = roleconf('M');
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
    void perform()
    {
        autoteam = enable;
        sendservermode();
        if(m_team(gamemode, mutators) && enable) refillteams(true);
    }
    autoteamaction(bool enable) : enableaction(enable)
    {
        reqcall = roleconf('a');
        if(isvalid()) formatstring(desc)("%s autoteam", enable ? "enable" : "disable");
    }
};

struct shuffleteamaction : serveraction
{
    void perform()
    {
        // shuffle sound?
        shuffleteams();
    }
    bool isvalid() { return serveraction::isvalid() && m_team(gamemode, mutators); }
    shuffleteamaction()
    {
        reqcall = roleconf('s');
        if(isvalid()) copystring(desc, "shuffle teams");
    }
};

struct recorddemoaction : enableaction            // TODO: remove completely
{
    void perform() { }
    bool isvalid() { return serveraction::isvalid(); }
    recorddemoaction(bool enable) : enableaction(enable)
    {
        area = EE_DED_SERV; // dedicated only
        reqcall = roleconf('R');
        if(isvalid()) formatstring(desc)("%s demorecord", enable ? "enable" : "disable");
    }
};

struct cleardemosaction : serveraction
{
    int demo;
    void perform() { cleardemos(demo); }
    cleardemosaction(int demo) : demo(demo)
    {
        area = EE_DED_SERV; // dedicated only
        reqcall = roleconf('C');
        if(isvalid()) formatstring(desc)("clear demo %d", demo);
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
        reqcall = roleconf('D');
        formatstring(desc)("set server description to '%s'", sdesc);
        if(isvalid()) address = clients[cn]->peer->address;
    }
    ~serverdescaction() { DELETEA(sdesc); }
};
