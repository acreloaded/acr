inline bool is_lagging(client *cl)
{
    return ( cl->spj > 50 || cl->ping > 500 || cl->ldt > 80 ); // do not change this except if you really know what are you doing
}

inline bool outside_border(vec &po)
{
    return (po.x < 0 || po.y < 0 || po.x >= maplayoutssize || po.y >= maplayoutssize);
}

extern inline void addban(client *cl, int reason, int type = BAN_AUTO);

#define MINELINE 50

//FIXME
/* There are smarter ways to implement this function, but most probably they will be very complex */
int getmaxarea(int inversed_x, int inversed_y, int transposed, int ml_factor, char *ml)
{
    int ls = (1 << ml_factor);
    int xi = 0, oxi = 0, xf = 0, oxf = 0, fx = 0, fy = 0;
    int area = 0, maxarea = 0;
    bool sav_x = false, sav_y = false;

    if (transposed) fx = ml_factor;
    else fy = ml_factor;

    // walk on x for each y
    for ( int y = (inversed_y ? ls-1 : 0); (inversed_y ? y >= 0 : y < ls); (inversed_y ? y-- : y++) ) {

    /* Analyzing each cube of the line */
        for ( int x = (inversed_x ? ls-1 : 0); (inversed_x ? x >= 0 : x < ls); (inversed_x ? x-- : x++) ) {
            if ( ml[ ( x << fx ) + ( y << fy ) ] != 127 ) {      // if it is not solid
                if ( sav_x ) {                                          // if the last cube was saved
                    xf = x;                                             // new end for this line
                }
                else {
                    xi = x;                                             // new begin of the line
                    sav_x = true;                                       // accumulating cubes from now
                }
            } else {                                    // solid
                if ( xf - xi > MINELINE ) break;                        // if the empty line is greater than a minimum, get out
                sav_x = false;                                          // stop the accumulation of cubes
            }
        }

    /* Analyzing this line with the previous one */
        if ( xf - xi > MINELINE ) {                                     // if the line has the minimun threshold of emptiness
            if ( sav_y ) {                                              // if the last line was saved
                if ( 2*oxi + MINELINE < 2*xf &&
                     2*xi + MINELINE < 2*oxf ) {                        // if the last line intersect this one
                    area += xf - xi;
                } else {
                    oxi = xi;                                           // new area vertices
                    oxf = xf;
                }
            }
            else {
                oxi = xi;
                oxf = xf;
                sav_y = true;                                           // accumulating lines from now
            }
        } else {
            sav_y = false;                                              // stop the accumulation of lines
            if (area > maxarea) maxarea = area;                         // new max area
            area=0;
        }

        sav_x = false;                                                  // reset x
        xi = xf = 0;
    }
    return maxarea;
}

int getmaxarea(int inversed_x, int inversed_y, int transposed, int ml_factor, ssqr *ml)
{
    int ls = (1 << ml_factor);
    int xi = 0, oxi = 0, xf = 0, oxf = 0, fx = 0, fy = 0;
    int area = 0, maxarea = 0;
    bool sav_x = false, sav_y = false;

    if (transposed) fx = ml_factor;
    else fy = ml_factor;

    // walk on x for each y
    for ( int y = (inversed_y ? ls-1 : 0); (inversed_y ? y >= 0 : y < ls); (inversed_y ? y-- : y++) ) {

    /* Analyzing each cube of the line */
        for ( int x = (inversed_x ? ls-1 : 0); (inversed_x ? x >= 0 : x < ls); (inversed_x ? x-- : x++) ) {
            if ( ml[ ( x << fx ) + ( y << fy ) ].floor != 127 ) {      // if it is not solid
                if ( sav_x ) {                                          // if the last cube was saved
                    xf = x;                                             // new end for this line
                }
                else {
                    xi = x;                                             // new begin of the line
                    sav_x = true;                                       // accumulating cubes from now
                }
            } else {                                    // solid
                if ( xf - xi > MINELINE ) break;                        // if the empty line is greater than a minimum, get out
                sav_x = false;                                          // stop the accumulation of cubes
            }
        }

    /* Analyzing this line with the previous one */
        if ( xf - xi > MINELINE ) {                                     // if the line has the minimun threshold of emptiness
            if ( sav_y ) {                                              // if the last line was saved
                if ( 2*oxi + MINELINE < 2*xf &&
                     2*xi + MINELINE < 2*oxf ) {                        // if the last line intersect this one
                    area += xf - xi;
                } else {
                    oxi = xi;                                           // new area vertices
                    oxf = xf;
                }
            }
            else {
                oxi = xi;
                oxf = xf;
                sav_y = true;                                           // accumulating lines from now
            }
        } else {
            sav_y = false;                                              // stop the accumulation of lines
            if (area > maxarea) maxarea = area;                         // new max area
            area=0;
        }

        sav_x = false;                                                  // reset x
        xi = xf = 0;
    }
    return maxarea;
}

int checkarea(int maplayout_factor, char *maplayout)
{
    int area = 0, maxarea = 0;
    for (int i=0; i < 8; i++) {
        area = getmaxarea((i & 1),(i & 2),(i & 4), maplayout_factor, maplayout);
        if ( area > maxarea ) maxarea = area;
    }
    return maxarea;
}

int checkarea(int maplayout_factor, ssqr *maplayout)
{
    int area = 0, maxarea = 0;
    for (int i=0; i < 8; i++) {
        area = getmaxarea((i & 1),(i & 2),(i & 4), maplayout_factor, maplayout);
        if ( area > maxarea ) maxarea = area;
    }
    return maxarea;
}

inline void addptreason(client *c, int reason, int amt = 0)
{
    if (c->type != ST_AI) sendf(c->clientnum, 1, "ri3", SV_POINTS, reason, amt);
}

void addpt(client *c, int points, int reason)
{
    if (!c || !points) return;
    if (c->state.perk1 == PERK1_SCORE) points *= points > 0 ? 1.35f : 1.1f;
    c->state.invalidate().points += points;
    if (reason >= 0) addptreason(c, reason, points);
}

/** cnumber is the number of players in the game, at a max value of 12 */
#define CTFPICKPT     cnumber                      // player picked the flag (ctf)
#define CTFDROPPT    -cnumber                      // player dropped the flag to other player (probably)
#define HTFLOSTPT  -2*cnumber                      // penalty
#define CTFLOSTPT     cnumber*distance/100         // bonus: 1/4 of the flag score bonus
#define CTFRETURNPT   cnumber                      // flag return
#define CTFSCOREPT   (cnumber*distance/25+10)      // flag score
#define HTFSCOREPT   (cnumber*4+10)
#define KTFSCOREPT   (cnumber*2+10)
#define SECUREPT      10                           // Secure/Overthrow
#define SECUREDPT     1                            // Secured bonus
/*
#define COMBOPT       5                            // player frags with combo
#define REPLYPT       2                            // reply success
#define TWDONEPT      5                            // team work done
#define CTFLDEFPT     cnumber                      // player defended the flag in the base (ctf)
#define CTFLCOVPT     cnumber*2                    // player covered the flag stealer (ctf)
#define HTFLDEFPT     cnumber                      // player defended a droped flag (htf)
#define HTFLCOVPT     cnumber*3                    // player covered the flag keeper (htf)
#define COVERPT       cnumber*2                    // player covered teammate
*/
#define DEATHPT      -4                            // player died
#define BONUSPT       target->state.points/400     // bonus (for killing high level enemies :: beware with exponential behavior!)
#define FLBONUSPT     target->state.points/300     // bonus if flag team mode
#define TMBONUSPT     target->state.points/200     // bonus if team mode (to give some extra reward for playing tdm modes)
#define HTFFRAGPT     cnumber/2                    // player frags while carrying the flag
#define CTFFRAGPT   2*cnumber                      // player frags the flag stealer
#define FRAGPT        10                           // player frags (normal)
#define HEADSHOTPT    15                           // player gibs with head shot
#define MELEEPT       20                           // player gibs with the knife or sword
#define NADEPT        11                           // player gibs with a grenade
#define SHOTGPT       12                           // player gibs with the shotgun
#define GIBPT         11                           // player gibs otherwise

#define FIRSTKILLPT   25                           // player makes the first kill
#define REVENGEKILLPT  5                           // player gets a payback
#define TKPT         -20                           // player tks
#define FLAGTKPT     -2*(10+cnumber)               // player tks the flag keeper/stealer

#define ASSISTMUL 0.225f                           // multiply reward by this for assisters
#define ASSISTRETMUL 0.125f                        // multiply assisters' rewards and return to original damager
#define HEALTEAMPT     8                           // player heals his teammate else with the heal gun
#define HEALSELFPT     2                           // player heals himself with the heal gun
#define HEALENEMYPT   -1                           // player heals his enemy with the heal gun
//#define HEALWOUNDPT    4                           // player heals his wound or wounds, times the number of his wounds

#define ARENAWINPT  20                             // player survives the arena round
#define ARENAWINDPT 15                             // player's team won the arena round
#define ARENALOSEPT 1                              // player lost the arena round

#define KCKILLPTS   3                              // player confirms a kill for himself or his teammate
#define KCDENYPTS   2                              // player prevents the enemy from scoring KC points

int killpoints(const client *target, client *actor, int gun, int style, bool assist = false)
{
    if (target == actor) return 0;
    int cnumber = totalclients, gain = 0;
    int reason = -1;
    if (isteam(actor, target))
    {
        if (clienthasflag(target->clientnum) >= 0) gain += FLAGTKPT;
        else gain += TKPT;
    }
    else
    {
        if (m_team(gamemode, mutators))
        {
            if (!m_flags(gamemode)) gain += TMBONUSPT;
            else gain += FLBONUSPT;
            if (m_hunt(gamemode) && clienthasflag(actor->clientnum) >= 0) gain += HTFFRAGPT;
            if (m_capture(gamemode) && clienthasflag(target->clientnum) >= 0) gain += CTFFRAGPT;
        }
        else gain += BONUSPT;
        if (style & FRAG_GIB)
        {
            if (melee_weap(gun)) gain += MELEEPT;
            else if (gun == GUN_GRENADE) gain += NADEPT;
            else if (gun == GUN_SHOTGUN)
            {
                gain += SHOTGPT;
                reason = PR_SPLAT;
            }
            else if (isheadshot(gun, style))
            {
                gain += HEADSHOTPT;
                reason = PR_HS;
            }
            else gain += GIBPT;
        }
        else gain += FRAGPT;
    }
    if (style & FRAG_FIRST) gain += FIRSTKILLPT;
    if (style & FRAG_REVENGE) gain += REVENGEKILLPT;
    gain *= clamp(actor->state.combo, 1, 5);
    if (assist) gain *= ASSISTMUL;
    else loopv(target->state.damagelog)
    {
        if (!valid_client(target->state.damagelog[i])) continue;
        gain += max(0, killpoints(target, clients[target->state.damagelog[i]], gun, style, true)) * ASSISTRETMUL;
    }
    if (gain) addpt(actor, gain, assist ? PR_ASSIST : reason);
    return gain;
}

int flagpoints(client *c, int message)
{
    int total = 0;
    const int cnumber = totalclients < 13 ? totalclients : 12;
    switch (message)
    {
        case FA_PICKUP:
            c->state.flagpickupo = c->state.o;
            if (m_capture(gamemode)) total += CTFPICKPT;
            break;
        case FA_DROP:
            if (m_capture(gamemode)) total += CTFDROPPT;
            break;
        case FA_LOST:
            if (m_hunt(gamemode)) total += HTFLOSTPT;
            else if (m_capture(gamemode)) {
                float distance = c->state.flagpickupo.dist(c->state.o);
                if (distance > 200) distance = 200;                   // ~200 is the distance between the flags in ac_depot
                total += CTFLOSTPT;
            }
            break;
        case FA_RETURN:
            total += CTFRETURNPT;
            break;
        case FA_SCORE:
            if (m_capture(gamemode)) {
                float distance = c->state.o.dist(c->state.flagpickupo);
                if (distance > 200) distance = 200;
                total += CTFSCOREPT;
            } else total += HTFSCOREPT;
            break;
        case FA_KTFSCORE:
            total += KTFSCOREPT;
            break;
        default:
            break;
    }
    addpt(c, total);
    return total;
}

int next_afk_check = 200;

/* this function is managed to the PUBS, id est, many people playing in an open server */
void check_afk()
{
    next_afk_check = servmillis + 7 * 1000;
    /* if we have few people (like 2x2), or it is not a teammode with the server not full: do nothing! */
    if ( totalclients < 5 || ( totalclients < scl.maxclients && !m_team(gamemode, mutators))  ) return;
    loopv(clients)
    {
        client &c = *clients[i];
        if ( c.type != ST_TCPIP || c.connectmillis + 60 * 1000 > servmillis ||
             c.inputmillis + scl.afk_limit > servmillis || clienthasflag(c.clientnum) != -1 ) continue;
        if ( ( c.state.state == CS_DEAD && !m_duke(gamemode, mutators) && c.state.lastdeath + 45 * 1000 < gamemillis) ||
             ( c.state.state == CS_ALIVE ) /*||
             ( c.state.state == CS_SPECTATE && totalclients >= scl.maxclients )  // only kick spectator if server is full - 2011oct16:flowtron: mmh, that seems reasonable enough .. still, kicking spectators for inactivity seems harsh! disabled ATM, kick them manually if you must.
             */
            )
        {
            defformatstring(msg)("%s is afk, switching to spectator", c.formatname());
            sendservmsg(msg);
            logline(ACLOG_INFO, "[%s] %s", c.gethostname(), msg);
            updateclientteam(i, TEAM_SPECT, FTR_SILENT);
            checkai(); // AFK check
            convertcheck();
        }
    }
}

/**
If you read README.txt you must know that AC does not have cheat protection implemented.
However this file is the sketch to a very special kind of cheat detection tools in server side.

This is not based in program tricks, i.e., encryption, secret bytes, nor monitoring/scanning tools.

The idea behind these cheat detections is to check (or reproduce) the client data, and verify if
this data is expected or possible. Also, there is no need to check all clients all time, and
one coding this kind of check must pay a special attention to the lag effect and how it can
affect the data observed. This is not a trivial task, and probably it is the main reason why
such tools were never implemented.

This part is here for compatibility purposes.
If you know nothing about these detections, please, just ignore it.
*/

inline void checkmove(client *cl)
{
    cl->ldt = gamemillis - cl->lmillis;
    cl->lmillis = gamemillis;
    if ( cl->ldt < 40 ) cl->ldt = 40;
    cl->t += cl->ldt;
    cl->spj = (( 7 * cl->spj + cl->ldt ) >> 3);

    if ( cl->input != cl->f )
    {
        cl->input = cl->f;
        cl->inputmillis = servmillis;
    }

    // TODO: detect speedhack
}
