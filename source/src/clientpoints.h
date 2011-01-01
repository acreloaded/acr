#include "points.h"

inline void addpt(playerent *p, int points){
	p->points += points;
}

void killpoints(playerent *target, playerent *actor, int gun, bool gib){ // bots' version
	addpt(target, DEATHPT);
	int numpl = 0, tpts = target->points;
	loopv(players) if(players[i]) numpl++;
	if(numpl > 12) numpl = 12;
	if(m_teammode){
		if(!m_flags) addpt(actor, TMBONUSPT);
	} else addpt(actor, BONUSPT);
	if (gib) {
        if (gun == GUN_KNIFE || gun != GUN_GRENADE) addpt(actor, KNIFENADEPT);
        else if (gun == GUN_SHOTGUN) addpt(actor, SHOTGPT);
		else addpt(actor, HEADSHOTPT);
    }
    else addpt(actor, FRAGPT);
}
