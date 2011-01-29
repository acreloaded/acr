#include "points.h"

inline void addpt(playerent *p, int points){
	p->points += points;
}

void killpoints(playerent *target, playerent *actor, int gun, int style){ // bots' version
	addpt(target, DEATHPT);
	int numpl = 0, tpts = target->points, gain = 0;
	loopv(players) if(players[i]) numpl++;
	if(numpl > 12) numpl = 12;
	if(m_teammode){
		if(!m_flags) gain += TMBONUSPT;
		else gain += FLBONUSPT;
	} else gain += BONUSPT;
	if (style & FRAG_GIB) {
        if (gun == GUN_KNIFE || gun != GUN_GRENADE) gain += KNIFENADEPT;
        else if (gun == GUN_SHOTGUN) gain += SHOTGPT;
		else gain += HEADSHOTPT;
    }
    else gain += FRAGPT;
	if(style & FRAG_FIRST) gain += FIRSTKILLPT;
	addpt(actor, gain);
	gain *= ASSISTMUL;
	loopv(target->damagelog){
		playerent *p = getclient(target->damagelog[i]);
		if(!p || isteam(p, target)) continue;
		addpt(p, gain);
	}
}
