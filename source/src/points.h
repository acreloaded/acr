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
// #define TKPT         -20                        // player tks
// #define FLAGTKPT     -2*(10+cnumber)            // player tks the flag keeper/stealer
#define ASSISTMUL 0.225f                             // multiply reward by this for assisters