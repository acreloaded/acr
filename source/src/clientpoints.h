#include "points.h"

inline void addpt(playerent *p, int points){
	p->points += points;
}

void killpoints(playerent *target, playerent *actor, int gun, bool gib){ // bots' version
	addpt(target, DEATHPT);
	int numpl = 0, tpts = target->points, gain = 0;
	loopv(players) if(players[i]) numpl++;
	if(numpl > 12) numpl = 12;
	if(m_teammode){
		if(!m_flags) gain += TMBONUSPT;
		else gain += FLBONUSPT;
	} else gain += BONUSPT;
	if (gib) {
        if (gun == GUN_KNIFE || gun != GUN_GRENADE) gain += KNIFENADEPT;
        else if (gun == GUN_SHOTGUN) gain += SHOTGPT;
		else gain += HEADSHOTPT;
    }
    else gain += FRAGPT;
	addpt(actor, gain);
	gain *= ASSISTMUL;
	loopv(actor->damagelog) if(getclient(actor->damagelog[i])) addpt(getclient(actor->damagelog[i]), gain);
}
