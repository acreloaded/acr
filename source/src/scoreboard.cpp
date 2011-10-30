// creation of scoreboard pseudo-menu

#include "pch.h"
#include "cube.h"

void *scoremenu = NULL, *teammenu = NULL, *ctfmenu = NULL;

void showscores(int on){
	if(on) showmenu(m_flags ? "ctf score" : (m_team ? "team score" : "score"), false);
	else if (!intermission){
		closemenu("score");
		closemenu("team score");
		closemenu("ctf score");
	}
}

void showscores_(char *on){
	if(on[0]) showscores(ATOI(on));
	else showscores(addreleaseaction("showscores")!=NULL);
}

COMMANDN(showscores, showscores_, ARG_1STR);

struct sline{
	string s;
	color *bgcolor;
	sline() : bgcolor(NULL) {}
};

static vector<sline> scorelines;

struct teamscore{
	int team, points, frags, assists, deaths, flagscore, lvl, ping, pj;
	vector<playerent *> teammembers;
	teamscore(int t) : team(t), frags(0), assists(0), deaths(0), points(0), flagscore(0), lvl(0), ping(0), pj(0) {}

	virtual void addscore(playerent *d){
		if(!d) return;
		teammembers.add(d);
		frags += d->frags;
		assists += d->assists;
		deaths += d->deaths;
		points += d->points;
		extern int level;
		lvl += d == player1 ? level : d->level;
		if(m_flags) flagscore += d->flagscore;
		ping += d->ping;
		pj += d->plag;
	}
};

struct spectscore : teamscore{
	spectscore() : teamscore(TEAM_SPECT) {}

	void addscore(playerent *d){
		if(d) teammembers.add(d);
	}
};

static int teamscorecmp(const teamscore *x, const teamscore *y){
	if(x->flagscore > y->flagscore) return -1;
	if(x->flagscore < y->flagscore) return 1;
	if(x->points > y->points) return -1;
	if(x->points < y->points) return 1;
	if(x->frags > y->frags) return -1;
	if(x->frags < y->frags) return 1;
	if(x->assists > y->assists) return -1;
	if(x->assists < y->assists) return 1;
	if(x->deaths < y->deaths) return -1;
	if(x->deaths > y->deaths) return 1;
	return x->team > y->team;
}

static int scorecmp(const playerent **x, const playerent **y){
	if((*x)->flagscore > (*y)->flagscore) return -1;
	if((*x)->flagscore < (*y)->flagscore) return 1;
	if((*x)->points > (*y)->points) return -1;
	if((*x)->points < (*y)->points) return 1;
	if((*x)->frags > (*y)->frags) return -1;
	if((*x)->frags < (*y)->frags) return 1;
	if((*x)->assists > (*y)->assists) return -1;
	if((*x)->assists < (*y)->assists) return 1;
	if((*x)->deaths > (*y)->deaths) return 1;
	if((*x)->deaths < (*y)->deaths) return -1;
	if((*x)->lifesequence > (*y)->lifesequence) return 1;
	if((*x)->lifesequence < (*y)->lifesequence) return -1;
	return strcmp((*x)->name, (*y)->name);
}

struct scoreratio{
	float ratio;
	int precision;

	void calc(int frags, int deaths){
		// ratio
		if(frags>=0 && deaths>0) ratio = (float)frags/(float)deaths;
		else if(frags>=0 && deaths==0) ratio = frags * 2.f;
		else ratio = 0.0f;

		// precision
		if(ratio<10.0f) precision = 2;
		else if(ratio>=10.0f && ratio<100.0f) precision = 1;
		else precision = 0;
	}
};

void renderscore(void *menu, playerent *d){
	static color localplayerc(0.2f, 0.2f, 0.2f, 0.2f), damagedplayerc(0.4f, 0.1f, 0.1f, 0.3f);
	const char *clag = d->state==CS_WAITING ? "LAG" : colorpj(d->plag), *cping = colorping(d->ping);
	sline &line = scorelines.add();
	line.bgcolor = d->lastpain + 500 > lastmillis ? &damagedplayerc : d==player1 ? &localplayerc : NULL;
	string &s = line.s;
	scoreratio sr;
	sr.calc(d->frags, d->deaths);
	extern int level;
	if(m_flags) formatstring(s)("%d\t%d\t%d\t%d\t%d\t%.*f\t%s\t%s\t%d\t%d\t\f%d%s", d->points, d->flagscore, d->frags, d->assists, d->deaths, sr.precision, sr.ratio, clag, cping, d->clientnum, d == player1 ? level : d->level, privcolor(d->priv, d->state == CS_DEAD), colorname(d, true));
	else formatstring(s)("%d\t%d\t%d\t%d\t%.*f\t%s\t%s\t%d\t%d\t\f%d%s", d->points, d->frags, d->assists, d->deaths, sr.precision, sr.ratio, clag, cping, d->clientnum, d == player1 ? level : d->level, privcolor(d->priv, d->state == CS_DEAD), colorname(d, true));
}

void renderteamscore(void *menu, teamscore &t){
	if(!scorelines.empty()){ // space between teams
		sline &space = scorelines.add();
		space.s[0] = 0;
	}
	sline &line = scorelines.add();
	defformatstring(plrs)("(%d %s)", t.teammembers.length(), t.team == TEAM_SPECT ? _("sb_spectating") :
															m_zombies && t.team == TEAM_RED ? _("sb_zombies") :
															t.teammembers.length() == 1 ? _("sb_player") : _("sb_players"));
	scoreratio sr;
	sr.calc(t.frags, t.deaths);
	const char *tlag = colorpj(t.pj/max(t.teammembers.length(),1)), *tping = colorping(t.ping/max(t.teammembers.length(), 1));
	const char *teamname = m_team || t.team == TEAM_SPECT ? team_string(t.team) : "FFA Total";
	if(m_flags) formatstring(line.s)("%d\t%d\t%d\t%d\t%d\t%.*f\t%s\t%s\t\t%d\t%s\t\t%s", t.points, t.flagscore, t.frags, t.assists, t.deaths, sr.precision, sr.ratio, tlag, tping, t.lvl, teamname, plrs);
	else formatstring(line.s)("%d\t%d\t%d\t%d\t%.*f\t%s\t%s\t\t%d\t%s\t\t%s", t.points, t.frags, t.assists, t.deaths, sr.precision, sr.ratio, tlag, tping, t.lvl, teamname, plrs);
	static color teamcolors[TEAM_NUM+1] = { color(1.0f, 0, 0, 0.2f), color(0, 0, 1.0f, 0.2f), color(.4f, .4f, .4f, .3f), color(.8f, .8f, .8f, .4f) };
	line.bgcolor = &teamcolors[!m_team && t.team != TEAM_SPECT ? TEAM_NUM : t.team];
	loopv(t.teammembers){
		if(m_zombies && t.teammembers[i]->team == TEAM_RED && t.teammembers[i]->state == CS_DEAD) continue;
		renderscore(menu, t.teammembers[i]);
	}
}

extern bool watchingdemo;

void renderscores(void *menu, bool init){
	static string modeline, serverline;

	modeline[0] = '\0';
	serverline[0] = '\0';
	scorelines.shrink(0);

	vector<playerent *> scores;
	if(!watchingdemo) scores.add(player1);
	loopv(players) if(players[i]) scores.add(players[i]);
	scores.sort(scorecmp);

	if(init){
		int sel = scores.find(player1);
		if(sel>=0) menuselect(menu, sel);
	}

	if(getclientmap()[0]){
		bool fldrprefix = !strncmp(getclientmap(), "maps/", strlen("maps/"));
		formatstring(modeline)("\"%s\" on map %s", modestr(gamemode, modeacronyms > 0), fldrprefix ? getclientmap()+strlen("maps/") : getclientmap());
	}

	extern int minutesremaining, gametimecurrent, lastgametimeupdate, gametimemaximum;
	if(!minutesremaining) concatstring(modeline, ", intermission");
	else if(minutesremaining > 0){
		const int cssec = (gametimemaximum-gametimecurrent-(lastmillis-lastgametimeupdate))/1000;
		defformatstring(timestr)(", %d:%02d remaining", (int)floor(cssec/60.f), cssec%60);
		concatstring(modeline, timestr);
	}
	else{
		const int cssec = (gametimecurrent+(lastmillis-lastgametimeupdate))/1000;
		defformatstring(timestr)(", %d:%02d elasped", (int)floor(cssec/60.f), cssec%60);
		concatstring(modeline, timestr);
	}

	if(multiplayer(false)){
		serverinfo *s = getconnectedserverinfo();
		if(s) formatstring(serverline)("%s:%d %s", s->name, s->port, s->sdesc);
	}

	//if(m_team){
		teamscore teamscores[TEAM_NUM] = { teamscore(TEAM_RED), teamscore(TEAM_BLUE), spectscore() };

		#define fixteam(pl) (pl->team == TEAM_BLUE && !m_team ? TEAM_RED : pl->team)
		loopv(players){
			if(!players[i]) continue;
			teamscores[fixteam(players[i])].addscore(players[i]);
		}
		teamscores[fixteam(player1)].addscore(player1);
		loopi(TEAM_NUM) teamscores[i].teammembers.sort(scorecmp);

		int sort = teamscorecmp(&teamscores[TEAM_RED], &teamscores[TEAM_BLUE]);
		if(!m_team) renderteamscore(menu, teamscores[TEAM_RED]);
		else loopi(2) renderteamscore(menu, teamscores[sort < 0 ? i : i^1]);
		if(teamscores[TEAM_SPECT].teammembers.length()) renderteamscore(menu, teamscores[TEAM_SPECT]);
	//}
	//else loopv(scores) renderscore(menu, scores[i]);

	menureset(menu);
	loopv(scorelines) menumanual(menu, scorelines[i].s, NULL, scorelines[i].bgcolor);
	menuheader(menu, modeline, serverline);

	// update server stats
	static int lastrefresh = 0;
	if(!lastrefresh || lastrefresh+5000<lastmillis){
		refreshservers(NULL, init);
		lastrefresh = lastmillis;
	}
}

void consolescores(){
	static string team, flags;
	playerent *d;
	scoreratio sr;
	vector<playerent *> scores;

	if(!watchingdemo) scores.add(player1);
	loopv(players) if(players[i]) scores.add(players[i]);
	scores.sort(scorecmp);

	if(getclientmap()[0]) printf("\n\"%s\" on map %s", modestr(gamemode, 0), getclientmap());
	if(multiplayer(false)){
		serverinfo *s = getconnectedserverinfo();
		string text;
		if(s){
			filtertext(text, s->sdesc, 1);
			printf(", %s:%d %s", s->name, s->port, text);
		}
	}
	printf("\npoints %sfrags assists deaths ratio cn%s name\n", m_flags ? "flags " : "", m_team ? " team" : "");
	loopv(scores){
		d = scores[i];
		sr.calc(d->frags, d->deaths);
		formatstring(team)(" %-4s", team_string(d->team));
		formatstring(flags)(" %4d ", d->flagscore);
		printf("%6d %s %4d %7d   %4d %5.2f %2d%s %s%s\n", d->points, m_flags ? flags : "", d->frags, d->assists, d->deaths, sr.ratio, d->clientnum,
					m_team ? team : "", d->name,
						d->priv == PRIV_MAX ? " (highest)" :
						d->priv == PRIV_ADMIN ? " (admin)" :
						d->priv == PRIV_MASTER ? " (master)" :
						d == player1 ? " (you)" :
					"");
	}
	printf("\n");
}
