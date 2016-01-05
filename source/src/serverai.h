// server-side ai (bot) manager
int findaiclient(int exclude = -1) // person with least bots, if possible
{
    int cn = -1, bots = MAXCLIENTS;
    loopv(clients)
    {
        client *c = clients[i];
        if(i == exclude || c->type == ST_EMPTY || c->type == ST_AI /*|| !*c->name || !c->connected*/) continue;
        int n = 0;
        loopvj(clients)
            if(clients[j]->type == ST_AI && clients[j]->ownernum == i)
                ++n;
        if(n < bots || cn < 0)
        {
            bots = n;
            cn = i;
        }
    }
    return cn;
}

bool addai()
{
    int aiowner = findaiclient(), cn = -1, numbots = 0;
    if(!valid_client(aiowner)) return false;
    loopv(clients)
    {
        if(clients[i]->type == ST_AI && ++numbots >= scl.maxbots)
            return false;
        else if(clients[i]->type == ST_EMPTY)
        {
            cn = i;
            break;
        }
    }
    if(cn == -1)
    {
        if(clients.length() >= MAXCLIENTS)
            return false;
        client *c = new client;
        c->clientnum = cn = clients.length();
        clients.add(c);
    }
    client &b = *clients[cn];
    b.reset();
    b.type = ST_AI;
    b.isauthed = b.isonrightmap = true;
    b.team = chooseteam(b);
    b.skin[0] = randomMT(); // random skins
    b.skin[1] = randomMT(); // random skins
    b.level = 40 + rnd(61); // how smart/stupid the bot is can be set here (currently random from 40 to 100)
    b.ownernum = aiowner;
    copystring(b.name, "a bot");
    copystring(b.hostname, "<bot>");
    sendf(NULL, 1, "ri8", SV_INITAI, cn, b.ownernum, b.bot_seed = randomMT(), b.skin[0], b.skin[1], b.team, b.level);
    forcedeath(b);
    if(canspawn(b))
        sendspawn(b);
    return true;
}

void deleteai(client &c)
{
    if(c.type != ST_AI || c.ownernum < 0)
        return;
    const int cn = c.clientnum;
    c.ownernum = -1;
    clientdisconnect(c);
    sendf(NULL, 1, "ri2", SV_DELAI, cn);
}

bool delai()
{
    loopvrev(clients) if(clients[i]->type == ST_AI)
    {
        deleteai(*clients[i]);
        return true;
    }
    return false;
}

bool shiftai(client &c, int ncn = -1, int exclude = -1)
{
    if(!valid_client(ncn) || clients[ncn]->type == ST_AI)
    {
        ncn = findaiclient(exclude);
        if(!valid_client(ncn) || clients[ncn]->type == ST_AI) return false;
    }
    c.ownernum = ncn;
    forcedeath(c); // prevent spawn state bugs
    sendf(NULL, 1, "ri3", SV_REASSIGNAI, c.clientnum, c.ownernum);
    return true;
}

void clearai(){ loopv(clients) if(clients[i]->type == ST_AI) deleteai(*clients[i]); }

bool reassignai(int exclude = -1)
{
    int hi = -1, lo = -1, hicount = -1, locount = -1;
    loopv(clients)
    {
        client *ci = clients[i];
        if(ci->type == ST_EMPTY || ci->type == ST_AI || !ci->name[0] || !ci->isauthed || ci->clientnum == exclude) continue;
        int thiscount = 0;
        loopvj(clients) if(clients[j]->ownernum == ci->clientnum) ++thiscount;
        if(hi < 0 || thiscount > hicount){ hi = i; hicount = thiscount; }
        if(lo < 0 || thiscount < locount){ lo = i; locount = thiscount; }
    }
    if(hi >= 0 && lo >= 0 && hicount > locount + 1)
    {
        client *ci = clients[hi];
        loopv(clients) if(clients[i]->type == ST_AI && clients[i]->ownernum == ci->clientnum) return shiftai(*clients[i], lo);
    }
    return false;
}

void checkai()
{
    // check if bots are disallowed
    if(!m_ai(gamemode)) return clearai();
    // check balance
    if(m_progressive(gamemode, mutators))
    {
        if(progressiveround > MAXZOMBIEROUND) return clearai();
        const int zombies = clamp(progressiveround, 1, 20),
            zombies_suicide = max((int)floor(progressiveround / 2.f), progressiveround - 10);
        int zombies_suicide_given = 0;
        while (countclients(ST_AI) < zombies) if (!addai()) break;
        while (countclients(ST_AI) > zombies) if (!delai()) break;
#if !(SERVER_BUILTIN_MOD & 128)
        // force suicide bomber count
        loopv(clients) if(clients[i]->type == ST_AI)
        {
            bool has_bomber = (++zombies_suicide_given <= zombies_suicide);
            clients[i]->state.deathstreak = has_bomber ? progressiveround == MAXZOMBIEROUND ? 8 : 5 : 0;
            clients[i]->state.streakondeath = has_bomber ? progressiveround == MAXZOMBIEROUND ? STREAK_REVENGE : STREAK_DROPNADE : -1;
        }
#endif
    }
    else
    {
        int balance = 0;
        const int humans = numplayers(false);
        if(humans)
        {
            switch(botbalance)
            {
                case 0: // force no bots, except for zombies
                    if(!m_zombie(gamemode))
                    {
                        balance = 0;
                        break;
                    }
                    // fallthrough for zombies
                case -1: // auto
                    if(m_zombie(gamemode)) balance = min(zombiebalance + humans, 30); // effectively zombiebalance, but capped at 30
                    else if(m_duke(gamemode, mutators)) balance = max(humans, maplayout_factor - 3); // 3 - 5 - 8 (6 - 8 - 11 layout factor)
                    else if(m_team(gamemode, mutators)) balance = clamp((smapstats.spawns[0] + smapstats.spawns[1]) / 3, max(6, humans), 14);
                    else balance = clamp(smapstats.spawns[2] / 3, max(4, humans), 10);
                    break; // auto
                default:
                    if(botbalance > 0)
                        balance = max(humans, botbalance); // force bot count
                    else
                        balance = (botbalance / -100) + (-botbalance % 100); // team balance
                    break;
            }
        }
        if(balance > 0)
        {
            if(m_team(gamemode, mutators) && !m_zombie(gamemode))
            {
                if (botbalance < -1) balance = (botbalance / -100) + (-botbalance % 100);
                else
                {
                    int plrs[2] = { 0 }, highest = -1;
                    loopv(clients) if (valid_client(i) && clients[i]->type != ST_AI && clients[i]->team < 2)
                    {
                        ++plrs[clients[i]->team];
                        if (highest < 0 || plrs[clients[i]->team] > plrs[highest]) highest = clients[i]->team;
                    }
                    if (highest >= 0)
                    {
                        int bots = balance - humans;
                        loopi(2) if (i != highest && plrs[i] < plrs[highest]) loopj(plrs[highest] - plrs[i])
                        {
                            if (bots > 0) --bots;
                            else ++balance;
                        }
                    }
                    // fix if odd
                    if (botbalance == -1 && (balance & 1)) ++balance;
                }
            }
            while(numplayers() < balance) if(!addai()) break;
            while(numplayers() > balance) if(!delai()) break;
            if(m_team(gamemode, mutators) && !m_convert(gamemode, mutators))
                loopvrev(clients)
            {
                client &ci = *clients[i];
                if(ci.type != ST_AI) continue;
                int teamb = chooseteam(ci, ci.team);
                if (teamb == TEAM_SPECT) continue;
                if(ci.team != teamb) updateclientteam(ci, teamb, FTR_SILENT);
            }
        }
        else clearai();
    }
}
