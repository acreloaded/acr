// creation of scoreboard pseudo-menu

#include "cube.h"
#define SCORERATIO(F,D) (float)(F >= 0 ? F : 0) / (float)(D > 0 ? D : 1)

gmenu *scoremenu = NULL;
bool needscoresreorder = true;

void showscores(bool on)
{
    if(on) showmenu("score", false);
    else closemenu("score");
}

COMMANDF(showscores, "i", (int *on) { showscores(*on != 0); });

VARFP(sc_flags,      0,  0, 100, needscoresreorder = true);
VARFP(sc_frags,      0, 10, 100, needscoresreorder = true);
VARFP(sc_deaths,    -1, 20, 100, needscoresreorder = true);
VARFP(sc_assists,    0, 30, 100, needscoresreorder = true);
VARFP(sc_ratio,     -1, 40, 100, needscoresreorder = true);
VARFP(sc_score,     -1, 50, 100, needscoresreorder = true);
VARFP(sc_rank,      -1, 55, 100, needscoresreorder = true);
VARFP(sc_lag,       -1, 60, 100, needscoresreorder = true);
VARFP(sc_clientnum,  0, 70, 100, needscoresreorder = true);
VARFP(sc_name,       0, 80, 100, needscoresreorder = true);

struct coldata
{
    int priority;
    char *val;

    coldata() : priority(-1), val(NULL) {}
    ~coldata()
    {
        DELETEA(val);
    }
};

// FIXME ? if two columns share teh same priority
// they will be sorted by the order they were added with addcol
int sortcolumns(coldata *col_a, coldata *col_b)
{
    if(col_a->priority > col_b->priority) return 1;
    else if(col_a->priority < col_b->priority) return -1;
    return 0;
}

struct sline
{
    string s;
    const char *altfont;
    color *bgcolor;
    char textcolor;
    vector<coldata> cols;

    sline() : altfont(NULL), bgcolor(NULL), textcolor(0) { copystring(s, ""); }

    void addcol(int priority, const char *format = NULL, ...)
    {
        if(priority < 0) return;
        coldata &col = cols.add();
        col.priority = priority;
        if(format && *format)
        {
            defvformatstring(sf, format, format);
            col.val = newstring(sf);
        }
    }

    char *getcols()
    {
        if(s[0] == '\0')
        {
            if(textcolor) formatstring(s)("\f%c", textcolor);
            cols.sort(sortcolumns);
            loopv(cols)
            {
               if(i > 0) concatstring(s, "\t");
               if(cols[i].val) concatstring(s, cols[i].val);
            }
        }
        return s;
    }
};

static vector<sline> scorelines;
vector<discscore> discscores;

teamscore teamscores[2] = { teamscore(TEAM_CLA), teamscore(TEAM_RVSF) };

struct teamsum
{
    int team, lvl, ping, pj;
    vector<playerent *> teammembers;
    teamsum(int t) : team(t), lvl(0), ping(0), pj(0) { }

    void addplayer(playerent *d)
    {
        if (!d) return;
        teammembers.add(d);
        extern int level;
        lvl += d == player1 ? level : d->level;
        ping += d->ping;
        pj += d->plag;
    }
};

static int teamscorecmp(const teamsum *a, const teamsum *b)
{
    teamscore *x = &teamscores[a->team], *y = &teamscores[b->team];
    if(x->flagscore > y->flagscore) return -1;
    if(x->flagscore < y->flagscore) return 1;
    if(x->frags > y->frags) return -1;
    if(x->frags < y->frags) return 1;
    if(x->points > y->points) return -1;
    if(x->points < y->points) return 1;
    if(x->deaths < y->deaths) return -1;
    return x->team - y->team;
}

static int scorecmp(playerent **x, playerent **y)
{
    if((*x)->flagscore > (*y)->flagscore) return -1;
    if((*x)->flagscore < (*y)->flagscore) return 1;
    if((*x)->frags > (*y)->frags) return -1;
    if((*x)->frags < (*y)->frags) return 1;
    if((*x)->points > (*y)->points) return -1;
    if((*x)->points < (*y)->points) return 1;
    if((*x)->deaths > (*y)->deaths) return 1;
    if((*x)->deaths < (*y)->deaths) return -1;
    if((*x)->lifesequence > (*y)->lifesequence) return 1;
    if((*x)->lifesequence < (*y)->lifesequence) return -1;
    return 0;
}

static int discscorecmp(const discscore *x, const discscore *y)
{
    if(x->team < y->team) return -1;
    if(x->team > y->team) return 1;
    if(m_flags(gamemode) && x->flags > y->flags) return -1;
    if(m_flags(gamemode) && x->flags < y->flags) return 1;
    if(x->frags > y->frags) return -1;
    if(x->frags < y->frags) return 1;
    if(x->deaths > y->deaths) return 1;
    if(x->deaths < y->deaths) return -1;
    return strcmp(x->name, y->name);
}

// const char *scoreratio(int frags, int deaths, int precis = 0)
// {
//     static string res;
//     float ratio = SCORERATIO(frags, deaths);
//     int precision = precis;
//     if(!precision)
//     {
//         if(ratio<10.0f) precision = 2;
//         else if(ratio<100.0f) precision = 1;
//     }
//     formatstring(res)("%.*f", precision, ratio);
//     return res;
// }

void renderdiscscores(int team)
{
    loopv(discscores) if(team == team_group(discscores[i].team))
    {
        discscore &d = discscores[i];
        sline &line = scorelines.add();
        if(team_isspect(d.team)) line.textcolor = '4';
        const char *clag = team_isspect(d.team) ? "SPECT" : "";

        if(m_flags(gamemode)) line.addcol(sc_flags, "%d", d.flags);
        line.addcol(sc_frags, "%d", d.frags);
        line.addcol(sc_assists, "%d", d.assists);
        line.addcol(sc_deaths, "%d", d.deaths);
        line.addcol(sc_ratio, "%.2f", SCORERATIO(d.frags, d.deaths));
        line.addcol(sc_score, "%d", max(d.points, 0));
        line.addcol(sc_lag, clag);
        line.addcol(sc_clientnum, "DISC");
        line.addcol(sc_rank); //line.addcol(sc_rank, "%d", d.rank);
        line.addcol(sc_name, d.name);
    }
}

VARP(cncolumncolor, 0, 5, 9);

void renderscore(playerent *d)
{
    string lagping, name;
    static color localplayerc(0.2f, 0.2f, 0.2f, 0.2f),
        damagedplayerc(0.4f, 0.1f, 0.1f, 0.3f),
        damagingplayerc(0.1f, 0.1f, 0.4f, 0.3f),
        regenplayerc(0.1f, 0.4f, 0.1f, 0.3f);

    if (team_isspect(d->team)) copystring(lagping, colorping(d->ping));
    else if (d->state == CS_WAITING || (d->ping > 999 && d->plag > 99)) formatstring(lagping)("LAG/%s", colorpj(d->plag), colorping(d->ping));
    else formatstring(lagping)("%s/%s", colorpj(d->plag), colorping(d->ping));

    copystring(name, colorname(d));

    extern votedisplayinfo *curvote;
    if (curvote && curvote->millis >= totalmillis && d->ownernum < 0)
        concatstring(name,
            d->vote == VOTE_YES ? " \f5[\f0Y\f5]" :
            d->vote == VOTE_NO ? " \f5[\f3N\f5]" :
            " \f5[\f2?\f5]");

    if (!team_isspect(d->team))
    {
        defformatstring(stat)("%d%.*f",
            (d->state == CS_DEAD || d->health <= 0) ? 4 :
            d->health > 50 * HEALTHSCALE ? 0 :
            d->health > 25 * HEALTHSCALE ? 2 : 3,
            HEALTHPRECISION,
            d->health / (float)HEALTHSCALE);
        if (d->armour)
            concatformatstring(stat, "\f5-\f4%d", d->armour);
        concatformatstring(name, " \f5[\f%s\f5]", stat);
    }

    const int buildinfo = d->build, third = (d == player1) ? thirdperson : d->thirdperson;
    if (d->ownernum >= 0); // bot icon? in the future?
    else if (buildinfo & 0x40) concatstring(name, "\a4  "); // Windows
    else if (buildinfo & 0x20) concatstring(name, "\a3  "); // Mac
    else if (buildinfo & 0x04) concatstring(name, "\a2  "); // Linux
    if (buildinfo & 0x08) concatstring(name, "\a1  "); // Debug
    if (third) concatstring(name, "\a0  "); // Third-Person
    if (buildinfo & 0x02) concatstring(name, "\a5  "); // Authed

    const char *ign = d->ignored ? " (ignored)" : (d->muted ? " (muted)" : "");
    sline &line = scorelines.add();
    if(team_isspect(d->team)) line.textcolor = '4';
    line.bgcolor = d->lastpain + 500 > lastmillis ? &damagedplayerc :
        d->lasthit + 500 > lastmillis ? &damagingplayerc :
        d->lastregen + 500 > lastmillis ? &regenplayerc :
        d == player1 ? &localplayerc :
        NULL;

    if(m_flags(gamemode)) line.addcol(sc_flags, "%d", d->flagscore);
    line.addcol(sc_frags, "%d", d->frags);
    line.addcol(sc_assists, "%d", d->assists);
    line.addcol(sc_deaths, "%d", d->deaths);
    line.addcol(sc_ratio, "%.2f", SCORERATIO(d->frags, d->deaths));
    line.addcol(sc_score, "%d", max(d->points, 0));
    line.addcol(sc_lag, lagping);
    line.addcol(sc_clientnum, "\fs\f%d%d\fr", cncolumncolor, d->clientnum);
    const int rmod10 = d->rank % 10;
    line.addcol(sc_rank, "%d%s", d->rank, (d->rank / 10 == 1) ? "th" : rmod10 == 1 ? "st" : rmod10 == 2 ? "nd" : rmod10 == 3 ? "rd" : "th");
    line.addcol(sc_name, "\fs\f%c%s\fr%s", privcolor(d->clientrole, d->state == CS_DEAD), name, ign);
    line.altfont = "build";
}

int totalplayers = 0;

int renderteamscore(teamsum &t)
{
    if(!scorelines.empty()) // space between teams
    {
        sline &space = scorelines.add();
        space.s[0] = 0;
    }
    sline &line = scorelines.add();
    int n = t.teammembers.length();
    defformatstring(plrs)("(%d %s)", n, t.team == TEAM_SPECT ? "spectating" :
        m_zombie(gamemode) && t.team == TEAM_CLA ? "zombies" : n == 1 ? "player" : "players");

    if (team_isactive(t.team))
    {
        const teamscore &ts = teamscores[t.team];
        if (m_flags(gamemode)) line.addcol(sc_flags, "%d", ts.flagscore);
        line.addcol(sc_frags, "%d", ts.frags);
        line.addcol(sc_assists, "%d", ts.assists);
        line.addcol(sc_deaths, "%d", ts.deaths);
        line.addcol(sc_ratio, "%.2f", SCORERATIO(ts.frags, ts.deaths));
        line.addcol(sc_score, "%d", max(ts.points, 0));
    }
    else
    {
        if (m_flags(gamemode)) line.addcol(sc_flags);
        line.addcol(sc_frags);
        line.addcol(sc_assists);
        line.addcol(sc_deaths);
        line.addcol(sc_ratio);
        line.addcol(sc_score);
    }
    if (t.team == TEAM_SPECT)
        line.addcol(sc_lag, "%s", colorping(t.ping / max(t.teammembers.length(), 1)));
    else
        line.addcol(sc_lag, "%s/%s", colorpj(t.pj / max(t.teammembers.length(), 1)), colorping(t.ping / max(t.teammembers.length(), 1)));
    line.addcol(sc_clientnum, m_team(gamemode, mutators) || t.team == TEAM_SPECT ? team_string(t.team, true) : "FFA");
    line.addcol(sc_rank);
    line.addcol(sc_name, "%s", plrs);

    static color teamcolors[4] = { color(1.0f, 0, 0, 0.2f), color(0, 0, 1.0f, 0.2f), color(.4f, .4f, .4f, .3f), color(.8f, .8f, .8f, .4f) };
    line.bgcolor = &teamcolors[t.team == TEAM_SPECT ? 2 : m_team(gamemode, mutators) ? team_base(t.team) : 3];
    loopv(t.teammembers)
    {
        // Hide dead AI zombies
        if (m_zombie(gamemode) && t.teammembers[i]->team == TEAM_CLA && t.teammembers[i]->ownernum >= 0 && t.teammembers[i]->state == CS_DEAD)
            continue;
        renderscore(t.teammembers[i]);
    }
    return n;
}

extern bool watchingdemo;

void reorderscorecolumns()
{
    needscoresreorder = false;
    extern gmenu *scoremenu;
    sline sscore;

    if(m_flags(gamemode)) sscore.addcol(sc_flags, "flags");
    sscore.addcol(sc_frags, "frags");
    sscore.addcol(sc_assists, "assists");
    sscore.addcol(sc_deaths, "deaths");
    sscore.addcol(sc_ratio, "ratio");
    sscore.addcol(sc_score, "score");
    sscore.addcol(sc_lag, "pj/ping");
    sscore.addcol(sc_clientnum, "cn");
    sscore.addcol(sc_rank, "rank");
    sscore.addcol(sc_name, "name");
    menutitle(scoremenu, newstring(sscore.getcols()));
}

void renderscores(gmenu *menu, bool init)
{
    if(needscoresreorder) reorderscorecolumns();
    static string modeline, serverline;

    modeline[0] = '\0';
    serverline[0] = '\0';
    scorelines.shrink(0);

    vector<playerent *> scores;
    if(!watchingdemo) scores.add(player1);
    totalplayers = 1;
    loopv(players) if(players[i]) { scores.add(players[i]); totalplayers++; }
    scores.sort(scorecmp);
    discscores.sort(discscorecmp);
    // rank players
    int n = 1;
    loopv(scores)
    {
        if (i && scores[i - 1]->points != scores[i]->points)
            ++n;
        scores[i]->rank = n;
    }

    int spectators = 0;
    loopv(scores) if(scores[i]->team == TEAM_SPECT) spectators++;
    loopv(discscores) if(discscores[i].team == TEAM_SPECT) spectators++;

    int winner = -1;
    if(m_team(gamemode, mutators))
    {
        teamsum teamsums[2] = { teamsum(TEAM_CLA), teamsum(TEAM_RVSF) };

        loopv(scores) if(scores[i]->team != TEAM_SPECT) teamsums[team_base(scores[i]->team)].addplayer(scores[i]);
        //loopv(discscores) if (discscores[i].team != TEAM_SPECT) teamsums[team_base(discscores[i].team)].addscore(discscores[i]);

        int sort = teamscorecmp(&teamsums[TEAM_CLA], &teamsums[TEAM_RVSF]) < 0 ? 0 : 1;
        loopi(2)
        {
            renderteamscore(teamsums[sort ^ i]);
            renderdiscscores(sort ^ i);
        }
        winner = m_flags(gamemode) ?
            (teamscores[sort].flagscore > teamscores[team_opposite(sort)].flagscore ? sort : -1) :
            (teamscores[sort].frags > teamscores[team_opposite(sort)].frags ? sort : -1);

    }
    else
    { // ffa mode
        teamsum ffateamsum = teamsum(0);
        loopv(scores) if (scores[i]->team != TEAM_SPECT) ffateamsum.addplayer(scores[i]);
        renderteamscore(ffateamsum);
        loopi(2) renderdiscscores(i);
        if(scores.length() > 0)
        {
            winner = scores[0]->clientnum;
            if(scores.length() > 1
                && ((m_flags(gamemode) && scores[0]->flagscore == scores[1]->flagscore)
                     || (!m_flags(gamemode) && scores[0]->frags == scores[1]->frags)))
                winner = -1;
        }
    }
    if(spectators)
    {
        if(!scorelines.empty()) // space between teams and spectators
        {
            sline &space = scorelines.add();
            space.s[0] = 0;
        }
        renderdiscscores(TEAM_SPECT);
        teamsum spectteamsum = teamsum(TEAM_SPECT);
        loopv(scores) if (scores[i]->team == TEAM_SPECT) spectteamsum.addplayer(scores[i]);
        renderteamscore(spectteamsum);
    }

    if(getclientmap()[0])
    {
        bool fldrprefix = !strncmp(getclientmap(), "maps/", strlen("maps/"));
        formatstring(modeline)("\"%s\" on map %s", modestr(gamemode, mutators, modeacronyms > 0), fldrprefix ? getclientmap()+strlen("maps/") : getclientmap());
    }

    extern int minutesremaining;
    if((gamemode>1 || (gamemode==0 && (multiplayer(false) || watchingdemo))) && minutesremaining >= 0)
    {
        if(!minutesremaining)
        {
            concatstring(modeline, ", intermission");

            if (m_team(gamemode, mutators)) // Add in the winning team
            {
                switch(winner)
                {
                    case TEAM_CLA: concatstring(modeline, ", \f3CLA wins!"); break;
                    case TEAM_RVSF: concatstring(modeline, ", \f1RVSF wins!"); break;
                    case -1:
                    default:
                        concatstring(modeline, ", \f2it's a tie!");
                    break;
                }
            }
            else // Add the winning player
            {
                if (winner < 0) concatstring(modeline, ", \f2it's a tie!");
                else concatformatstring(modeline, ", \f1%s wins!", scores[0]->name);
            }
        }
        else concatformatstring(modeline, ", %d %s remaining", minutesremaining, minutesremaining==1 ? "minute" : "minutes");
    }

    if(multiplayer(false))
    {
        serverinfo *s = getconnectedserverinfo();
        if(s)
        {
            if(servstate.mastermode > MM_OPEN) concatformatstring(serverline, servstate.mastermode == MM_MATCH ? "M%d " : "P ", servstate.matchteamsize);
            // ft: 2010jun12: this can write over the menu boundary
            //concatformatstring(serverline, "%s:%d %s", s->name, s->port, s->sdesc);
            // for now we'll just cut it off, same as the serverbrowser
            // but we might want to consider wrapping the bottom-line to accomodate longer descriptions - to a limit.
            string text;
            filterservdesc(text, s->sdesc);
            //for(char *p = text; (p = strchr(p, '\"')); *p++ = ' ');
            //text[30] = '\0'; // serverbrowser has less room - +8 chars here - 2010AUG03 - seems it was too much, falling back to 30 (for now): TODO get real width of menu as reference-width. FIXME: cutoff
            if(s->port == CUBE_DEFAULT_SERVER_PORT)
                concatformatstring(serverline, "%s %s", s->name, text);
            else
                concatformatstring(serverline, "%s:%d %s", s->name, s->port, text);
            //printf("SERVERLINE: %s\n", serverline);
        }
    }

    menureset(menu);
    loopv(scorelines) menuimagemanual(menu, NULL, scorelines[i].altfont, scorelines[i].getcols(), NULL, scorelines[i].bgcolor);
    menuheader(menu, modeline, serverline);

    // update server stats
    static int lastrefresh = 0;
    if(!lastrefresh || lastrefresh+5000<lastmillis)
    {
        refreshservers(NULL, init);
        lastrefresh = lastmillis;
    }
}

#define MAXJPGCOM 65533  // maximum JPEG comment length

void addstr(char *dest, const char *src) { if(strlen(dest) + strlen(src) < MAXJPGCOM) strcat(dest, src); }

const char *asciiscores(bool destjpg)
{
    static char *buf = NULL;
    static string team, flags, text;
    playerent *d;
    vector<playerent *> scores;

    if(!buf) buf = (char *) malloc(MAXJPGCOM +1);
    if(!buf) return "";

    if(!watchingdemo) scores.add(player1);
    loopv(players) if(players[i]) scores.add(players[i]);
    scores.sort(scorecmp);

    buf[0] = '\0';
    if(destjpg)
    {
        formatstring(text)("AssaultCube Reloaded Screenshot (%s)\n", asctime());
        addstr(buf, text);
    }
    if(getclientmap()[0])
    {
        formatstring(text)("\n\"%s\" on map %s", modestr(gamemode, mutators, 0), getclientmap(), asctime());
        addstr(buf, text);
    }
    if(multiplayer(false))
    {
        serverinfo *s = getconnectedserverinfo();
        if(s)
        {
            string sdesc;
            filtertext(sdesc, s->sdesc, 1);
            formatstring(text)(", %s:%d %s", s->name, s->port, sdesc);
            addstr(buf, text);
        }
    }
    if(destjpg)
        addstr(buf, "\n");
    else
    {
        formatstring(text)("\n%sfrags deaths cn%s name\n", m_flags(gamemode) ? "flags " : "", m_team(gamemode, mutators) ? " team" : "");
        addstr(buf, text);
    }
    loopv(scores)
    {
        d = scores[i];
//         const char *sr = scoreratio(d->frags, d->deaths);
        formatstring(team)(destjpg ? ", %s" : " %-4s", team_string(d->team, true));
        formatstring(flags)(destjpg ? "%d/" : " %4d ", d->flagscore);
        if(destjpg)
            formatstring(text)("%s%s (%s%d/%d)\n", d->name, m_team(gamemode, mutators) ? team : "", m_flags(gamemode) ? flags : "", d->frags, d->deaths);
        else
            formatstring(text)("%s %4d   %4d %2d%s %s%s\n", m_flags(gamemode) ? flags : "", d->frags, d->deaths, d->clientnum,
                            m_team(gamemode, mutators) ? team : "", d->name, d->clientrole ? " (op)" : d==player1 ? " (you)" : "");
        addstr(buf, text);
    }
    discscores.sort(discscorecmp);
    loopv(discscores)
    {
        discscore &d = discscores[i];
//         const char *sr = scoreratio(d.frags, d.deaths);
        formatstring(team)(destjpg ? ", %s" : " %-4s", team_string(d.team, true));
        formatstring(flags)(destjpg ? "%d/" : " %4d ", d.flags);
        if(destjpg)
            formatstring(text)("%s(disconnected)%s (%s%d/%d)\n", d.name, m_team(gamemode, mutators) ? team : "", m_flags(gamemode) ? flags : "", d.frags, d.deaths);
        else
            formatstring(text)("%s %4d   %4d --%s %s(disconnected)\n", m_flags(gamemode) ? flags : "", d.frags, d.deaths, m_team(gamemode, mutators) ? team : "", d.name);
        addstr(buf, text);
    }
    if(destjpg)
    {
        extern int minutesremaining;
        formatstring(text)("(%sfrags/deaths), %d minute%s remaining\n", m_flags(gamemode) ? "flags/" : "", minutesremaining, minutesremaining == 1 ? "" : "s");
        addstr(buf, text);
    }
    return buf;
}

void consolescores()
{
    printf("%s\n", asciiscores());
}

void winners()
{
    string winners = "";
    vector<playerent *> scores;
    if(!watchingdemo) scores.add(player1);
    loopv(players) if(players[i]) { scores.add(players[i]); }
    scores.sort(scorecmp);
    discscores.sort(discscorecmp);

    if(m_team(gamemode, mutators))
    {
        teamsum teamsums[2] = { teamsum(TEAM_CLA), teamsum(TEAM_RVSF) };

        loopv(scores) if (scores[i]->team != TEAM_SPECT) teamsums[team_base(scores[i]->team)].addplayer(scores[i]);
        /*
        loopv(discscores) if(discscores[i].team != TEAM_SPECT)
            teamsums[team_base(discscores[i].team)].addscore(discscores[i]);
        */

        int sort = teamscorecmp(&teamsums[TEAM_CLA], &teamsums[TEAM_RVSF]);
        if(!sort) copystring(winners, "0 1");
        else itoa(winners, sort < 0 ? 0 : 1);
    }
    else
    {
        loopv(scores)
        {
            if(!i || !scorecmp(&scores[i], &scores[i-1])) concatformatstring(winners, "%s%d", i ? " " : "", scores[i]->clientnum);
            else break;
        }
    }

    result(winners);
}

COMMAND(winners, "");
