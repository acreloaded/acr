// old game modes
/*
enum
{
	GMODE_DEMO = -1, // treat as GMODE_START
	GMODE_TEAMDEATHMATCH = 0,
	GMODE_COOPEDIT,
	GMODE_DEATHMATCH,
	GMODE_SURVIVOR,
	GMODE_TEAMSURVIVOR,
	GMODE_CTF, // capture the flag
	GMODE_PISTOLFRENZY,
	GMODE_LASTSWISSSTANDING,
	GMODE_ONESHOTONEKILL,
	GMODE_TEAMONESHOTONEKILL,
	GMODE_HTF, // hunt the flag/VIP
	GMODE_TKTF, // team keep the flag
	GMODE_KTF, // keep the flag
	GMODE_REALTDM, GMODE_EXPERTTDM,
	GMODE_REALDM, GMODE_EXPERTDM,
	GMODE_KNIFE, GMODE_HANDHELD,
	GMODE_RCTF, // return-CTF
	GMODE_CDM, GMODE_CTDM, // classic DM/TDM
	GMODE_KTF2, GMODE_TKTF2, // double KTF/TKTF
	GMODE_ZOMBIES, GMODE_ONSLAUGHT, // awesome!
	GMODE_BOMBER,
	GMODE_NUM
};
*/

// new game modes
enum // game modes
{
	G_DEMO = 0, G_EDIT, G_DM, G_CTF, G_HTF, G_KTF, G_BOMBER, G_ZOMBIE, G_MAX,
};

enum // game mutators
{
	G_M_NONE = 0,
	G_M_TEAM = 1 << 0, G_M_CLASSIC = 1 << 1, G_M_CONVERT = 1 << 2, // alters gameplay mostly
	G_M_REAL = 1 << 3, G_M_EXPERT = 1 << 4, // alters damage mostly
	G_M_SNIPER = 1 << 5, G_M_PISTOL = 1 << 6, G_M_GIB = 1 << 7, G_M_KNIFE = 1 << 8, // alters weapons mostly
	G_M_GSP1 = 1 << 9, // game-specific

	G_M_GAMEPLAY = G_M_TEAM|G_M_CLASSIC|G_M_CONVERT,
	G_M_DAMAGE = G_M_REAL|G_M_EXPERT,
	G_M_WEAPON = G_M_SNIPER|G_M_PISTOL|G_M_GIB|G_M_KNIFE,

	G_M_MOST = G_M_GAMEPLAY|G_M_DAMAGE|G_M_WEAPON,
	G_M_ALL = G_M_MOST|G_M_GSP1,

	G_M_GSN = 1, G_M_GSP = 9, G_M_NUM = 10,
};

struct gametypes
{
    int type, implied, mutators[G_M_GSN+1];
    const char *name, *gsp[G_M_GSN];
};
struct mutstypes
{
    int type, implied, mutators;
    const char *name;
};

extern gametypes gametype[G_MAX];
extern mutstypes mutstype[G_M_NUM];

#define m_valid(g) ((g)>=0 && (g)<G_MAX)

#define m_demo(g)           (g == G_DEMO)
#define m_edit(g)           (g == G_EDIT)
#define m_dm(g)             (g == G_DM)
#define m_capture(g)        (g == G_CTF)
#define m_hunt(g)           (g == G_HTF)
#define m_keep(g)           (g == G_KTF)
#define m_bomber(g)         (g == G_BOMBER)
#define m_zombie(g)         (g == G_ZOMBIE)

#define m_affinity(g)       (m_capture(g) || m_hunt(g) || m_keep(g) || m_bomber(g))
#define m_ai(g)             (m_valid(g) && !m_demo(g) && !m_edit(g)) // extra bots not available in demo/edit

// can add gsp implieds below
#define m_implied(a,b)      (gametype[a].implied)
#define m_doimply(a,b,c)    (gametype[a].implied|mutstype[c].implied)
#define m_mimplied(a,b)     ((muts & (G_M_GSP1)) && (mode == G_DM || mode == G_ZOMBIE))

#define m_team(a,b)         ((b & G_M_TEAM) || (m_implied(a,b) & G_M_TEAM))
#define m_sniper(a,b)        ((b & G_M_SNIPER) || (m_implied(a,b) & G_M_SNIPER))
#define m_classic(a,b)      ((b & G_M_CLASSIC) || (m_implied(a,b) & G_M_CLASSIC))
#define m_convert(a,b)      ((b & G_M_CONVERT) || (m_implied(a,b) & G_M_CONVERT))
#define m_real(a,b)         ((b & G_M_REAL) || (m_implied(a,b) & G_M_REAL))
#define m_expert(a,b)       ((b & G_M_EXPERT) || (m_implied(a,b) & G_M_EXPERT))
#define m_pistol(a,b)       ((b & G_M_PISTOL) || (m_implied(a,b) & G_M_PISTOL))
#define m_gib(a,b)          ((b & G_M_GIB) || (m_implied(a,b) & G_M_GIB))
#define m_knife(a,b)        ((b & G_M_KNIFE) || (m_implied(a,b) & G_M_KNIFE))

#define m_gsp1(a,b)         ((b & G_M_GSP1) || (m_implied(a,b) & G_M_GSP1))
//#define m_gsp(a,b)          (m_gsp1(a,b))

#define m_noitems(a,b)      (m_sniper(a, b) || m_knife(a, b))
#define m_noitemsnade(a,b)  (m_gib(a,b))
#define m_nopistol(a,b)     (m_sniper(a,b) || m_gib(a,b))
#define m_noprimary(a,b)    (m_pistol(a,b) || m_gib(a,b))

#define m_duke(a,b)         (m_survivor(a,b))
#define m_regen(a,b)        (!m_duke(a,b) && !m_sniper(a,b))
/*
#define m_scores(a)         (a >= G_EDITMODE && a <= G_DEATHMATCH)
#define m_sweaps(a,b)       (m_medieval(a, b) || m_ballistic(a, b) || m_arena(a, b) || m_league(a ,b))

#define m_weapon(a,b)       (m_loadout(a,b) ? (m_arena(a, b) ? -WEAP_ITEM : -WEAP_MAX) : (m_medieval(a,b) ? WEAP_SWORD : (m_ballistic(a,b) ? WEAP_ROCKET : (m_sniper(a,b) ? GAME(instaweapon) : (m_trial(a) ? GAME(trialweapon) : GAME(spawnweapon))))))
#define m_delay(a,b)        (m_play(a) && !m_duke(a,b) ? (m_trial(a) ? GAME(trialdelay) : (m_bomber(a) ? GAME(bomberdelay) : (m_sniper(a, b) ? GAME(instadelay) : GAME(spawndelay)))) : 0)
#define m_protect(a,b)      (m_duke(a,b) ? GAME(duelprotect) : (m_sniper(a, b) ? GAME(instaprotect) : GAME(spawnprotect)))
#define m_noitems(gamemode, mutators)(a,b)      (m_trial(a) || GAME(itemsallowed) < (m_limited(a,b) ? 2 : 1))
#define m_spawnhp(a,b)      (m_sniper(a,b) ? 1 : GAME(spawnhealth))
#define m_health(a,b,c)     (int(ceilf(m_spawnhp(a,b)*(!m_sniper(a, b) && m_league(a, b) && isweap(c) ? WEAP(c, leaguehealth) : 1.f))))

#define w_reload(w1,w2)     (w1 != WEAP_MELEE && ((isweap(w2) ? w1 == w2 : w1 < -w2) || (isweap(w1) && WEAP(w1, reloads))))
#define w_carry(w1,w2)      (w1 > WEAP_MELEE && (isweap(w2) ? w1 != w2 : w1 >= -w2) && (isweap(w1) && WEAP(w1, carried)))
#define w_attr(a,w1,w2)     (m_edit(gamemode)(a) || (w1 >= WEAP_OFFSET && w1 != w2) ? w1 : (w2 == WEAP_GRENADE ? WEAP_ROCKET : WEAP_GRENADE))
#define w_spawn(weap)       int(ceilf(GAME(itemspawntime)*WEAP(weap, frequency)))
#define w_lmax(a,b)         (m_league(a, b) ? WEAP_MAX : WEAP_ITEM)
*/

#define m_lss(a,b)          (m_survivor(a,b) && m_gib(a,b))
#define m_return(a,b)       (m_capture(a) && m_gsp1(a,b))
#define m_ktf2(a,b)         (m_keep(a) && m_gsp1(a,b))
#define m_zombies_rounds(a,b) (m_zombie(a) && !m_gsp1(a,b))
#define m_onslaught(a,b)    (m_zombie(a) && m_gsp1(a,b))
#define m_survivor(a,b)     ((m_dm(a) && m_gsp1(a,b)) || (m_zombie(a) && !m_gsp1(a,b)))

#define m_noradar(a,b)      (m_classic(a,b))
#define m_nonuke(a,b)       (m_zombie(a) && !m_gsp1(a,b))