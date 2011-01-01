#include "points.h"

inline void addpt(client *c, int points){
	sendf(-1, 1, "ri3", SV_POINTS, c->clientnum, (c->state.points += points));
}

void killpoints(client *& target, client *& actor, int gun, bool gib){
	int cnumber = numauthedclients(), tpts = target->state.points;
	bool suic = target == actor;
	addpt(target, DEATHPT);
	if(!suic){
		//if(tk){ // NO MORE TKs
		int tpts = target->state.points;
		if(m_teammode){
			if(!m_flags) addpt(actor, TMBONUSPT);
			else addpt(actor, FLBONUSPT);
			if (m_htf && clienthasflag(actor->clientnum) >= 0) addpt(actor, HTFFRAGPT);
            if (m_ctf && clienthasflag(target->clientnum) >= 0) addpt(actor, CTFFRAGPT);
		} else addpt(actor, BONUSPT);
		if (gib) {
            if (gun == GUN_KNIFE || gun != GUN_GRENADE) addpt(actor, KNIFENADEPT);
            else if (gun == GUN_SHOTGUN) addpt(actor, SHOTGPT);
			else addpt(actor, HEADSHOTPT);
        }
        else addpt(actor, FRAGPT);
		/*}else{
			if ( targethasflag >= 0 ) addpt(actor, FLAGTKPT);
			else addpt(actor, TKPT);
		}*/
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
