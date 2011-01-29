#include "points.h"

inline void addpt(client *c, int points){
	sendf(-1, 1, "ri3", N_POINTS, c->clientnum, (c->state.points += points));
}

void killpoints(client *target, client *actor, int gun, int style){
	int cnumber = numauthedclients(), tpts = target->state.points, gain = 0;
	bool suic = target == actor;
	addpt(target, DEATHPT);
	if(!suic){
		//if(tk){ // NO MORE TKs
		if(m_teammode){
			if(!m_flags) gain += TMBONUSPT;
			else gain += FLBONUSPT;
			if (m_htf && clienthasflag(actor->clientnum) >= 0) gain += HTFFRAGPT;
            if (m_ctf && clienthasflag(target->clientnum) >= 0) gain += CTFFRAGPT;
		} else gain += BONUSPT;
		if (style & FRAG_GIB) {
            if (gun == GUN_KNIFE || gun != GUN_GRENADE) gain += KNIFENADEPT;
            else if (gun == GUN_SHOTGUN) gain += SHOTGPT;
			else gain += HEADSHOTPT;
        }
        else gain += FRAGPT;
		/*}else{
			if ( targethasflag >= 0 ) addpt(actor, FLAGTKPT);
			else addpt(actor, TKPT);
		}*/
		if(style & FRAG_FIRST) gain += FIRSTKILLPT;
		addpt(actor, gain);
		gain *= ASSISTMUL;
		loopv(target->state.damagelog){
			if(!valid_client(target->state.damagelog[i])) continue;
			client *c = clients[target->state.damagelog[i]];
			if(!c || isteam(c, actor)) continue;
			addpt(c, gain);
		}
	}
}

void flagpoints (client *c, int message)
{
    float distance = 0;
    int cnumber = numauthedclients() < 12 ? numauthedclients() : 12;
    switch (message){
        case FA_PICKUP:
			c->state.flagpickupo = c->state.o;
            if (m_ctf) addpt(c, CTFPICKPT);
            break;
        case FA_DROP:
            if (m_ctf) addpt(c, CTFDROPPT);
            break;
        case FA_LOST:
            if (m_htf) addpt(c, HTFLOSTPT);
            else if (m_ctf) {
                distance = c->state.flagpickupo.dist(c->state.o);
                if (distance > 200) distance = 200;      // ~200 is the distance between the flags in ac_depot
                addpt(c, CTFLOSTPT);
            }
            break;
        case FA_RETURN:
            addpt(c, CTFRETURNPT);
            break;
        case FA_SCORE:
			if (m_ctf){
				distance = c->state.o.dist(c->state.flagpickupo);
				if (distance > 200) distance = 200;
				addpt(c, CTFSCOREPT);
			} else addpt(c, HTFSCOREPT);
			break;
		case FA_KTFSCORE:
            addpt(c, KTFSCOREPT);
            break;
        default:
            break;
    }
}
