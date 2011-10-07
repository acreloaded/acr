// cnumber is the number of players in the game, at a max value of 12
// distance = distance between flags
// tpts = target's points
#define CTFPICKPT     cnumber                      // player picked the flag (ctf)
#define CTFDROPPT    -cnumber                      // player dropped the flag to other player (probably)
#define HTFLOSTPT  -2*cnumber                      // penalty
#define CTFLOSTPT     cnumber*distance/100         // bonus: 1/4 of the flag score bonus
#define CTFRETURNPT   cnumber                      // flag return
#define CTFSCOREPT   (cnumber*distance/25+10)      // CTF score
#define HTFSCOREPT   (cnumber*4+10)                // HTF score
#define KTFSCOREPT   (cnumber*2+10)                // KTF score
/*
#define CTFLDEFPT     cnumber                      // player defended the flag in the base (ctf)
#define CTFLCOVPT     cnumber*2                    // player covered the flag stealer (ctf)
#define HTFLDEFPT     cnumber                      // player defended a droped flag (htf)
#define HTFLCOVPT     cnumber*3                    // player covered the flag keeper (htf)
#define COVERPT       cnumber*2                    // player covered teammate
*/
#define DEATHPT       -4                           // player died
#define BONUSPT       tpts/400                     // bonus (for killing high level enemies :: beware with exponential behavior!)
#define FLBONUSPT     tpts/300                     // bonus if flag team mode
#define TMBONUSPT     tpts/200                     // bonus if team mode (to give some extra reward for playing tdm modes)
#define HTFFRAGPT     cnumber/2                    // player frags while carrying the flag
#define CTFFRAGPT     2*cnumber                    // player frags the flag stealer
#define FRAGPT        10                           // player frags (normal)
#define HEADSHOTPT    15                           // player gibs with head shot
#define KNIFENADEPT   20                           // player gibs with the knife or nades
#define SHOTGPT       12                           // player gibs with the shotgun
#define FIRSTKILLPT   25                           // player makes the first kill
#define REVENGEKILLPT 10                           // player gets a payback
#define TKPT         -20                           // player tks
#define FLAGTKPT     -2*(10+cnumber)               // player tks the flag keeper/stealer
#define ASSISTMUL 0.225f                           // multiply reward by this for assisters
#define ASSISTRETMUL 0.125f                        // multiply assisters' rewards and return to original damager

// server point tools

inline void addpt(client *c, int points){
	if(c->state.perk == PERK_BRIBE) points *= points > 0 ? 1.35f : 1.1f;
	sendf(-1, 1, "ri3", N_POINTS, c->clientnum, (c->state.points += points));
}

int killpoints(client *target, client *actor, int gun, int style, bool assist = false){
	int cnumber = numauthedclients(), tpts = target->state.points, gain = 0;
	bool suic = target == actor;
	addpt(target, DEATHPT);
	if(!suic){
		if(isteam(actor, target)){ // friendly fire assists only
			if(m_team){
				if(!m_flags) gain += TMBONUSPT;
				else gain += FLBONUSPT;
				if (m_htf && clienthasflag(actor->clientnum) >= 0) gain += HTFFRAGPT;
				if (m_ctf && clienthasflag(target->clientnum) >= 0) gain += CTFFRAGPT;
			} else gain += BONUSPT;
			if (style & FRAG_GIB) {
				if (gun == WEAP_KNIFE || gun != WEAP_GRENADE) gain += KNIFENADEPT;
				else if (gun == WEAP_SHOTGUN) gain += SHOTGPT;
				else gain += HEADSHOTPT;
			}
			else gain += FRAGPT;
		} else {
			if (clienthasflag(target->clientnum) >= 0) gain += FLAGTKPT;
			else gain += TKPT;
		}
		if(style & FRAG_FIRST) gain += FIRSTKILLPT;
		if(style & FRAG_REVENGE) gain += REVENGEKILLPT;
		gain *= clamp(actor->state.combo, 1, 5);
		if(assist) gain *= ASSISTMUL;
		else loopv(target->state.damagelog){
			if(valid_client(target->state.damagelog[i])) continue;
			gain += killpoints(target, clients[target->state.damagelog[i]], gun, style, true) * ASSISTRETMUL;
		}
		addpt(actor, gain);
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
