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

enum // game modes
{
	G_DEMO = -1, G_EDIT, G_DM, G_CTF, G_HTF, G_KTF, G_BOMBER, G_ZOMBIE, G_MAX,
	G_FIRST = G_DEMO,
};

enum // game mutators
{
	G_M_NONE = 0,
	G_M_TEAM = 1 << 0, G_M_SURVIVOR = 1 << 1, G_M_CONVERT = 1 << 2, // alters gameplay
	G_M_REAL = 1 << 3, G_M_EXPERT = 1 << 4, // alters damage
	G_M_PISTOL = 1 << 5, G_M_GIB = 1 << 6, G_M_KNIFE = 1 << 7, // alters weapons
	G_M_GSP1 = 1 << 8,

	G_M_GAMEPLAY = G_M_TEAM|G_M_SURVIVOR|G_M_CONVERT,
	G_M_DAMAGE = G_M_REAL|G_M_EXPERT,
	G_M_WEAPON = G_M_PISTOL|G_M_GIB|G_M_KNIFE,
	G_M_GSP = G_M_GSP1,

	G_M_MOST = G_M_GAMEPLAY|G_M_DAMAGE|G_M_WEAPON,
	G_M_ALL = G_M_MOST|G_M_GSP,

	G_M_GSN = 1, G_M_NUM = 9,
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

extern gametypes gametype[G_MAX-G_FIRST];
extern mutstypes mutstype[G_M_NUM];

#define m_lms		(gamemode == GMODE_SURVIVOR || gamemode == GMODE_TEAMSURVIVOR)
#define m_return	(gamemode == GMODE_RCTF)
#define m_ctf		(gamemode == GMODE_CTF || gamemode == GMODE_RCTF)
#define m_pistol	(gamemode == GMODE_PISTOLFRENZY)
#define m_lss		(gamemode == GMODE_LASTSWISSSTANDING || gamemode == GMODE_HANDHELD || gamemode == GMODE_KNIFE)
#define m_osok		(gamemode == GMODE_ONESHOTONEKILL || gamemode == GMODE_TEAMONESHOTONEKILL)
#define m_htf		(gamemode == GMODE_HTF)
#define m_btf		(gamemode == GMODE_BOMBER)
#define m_ktf2		(gamemode == GMODE_TKTF2 || gamemode == GMODE_KTF2)
#define m_ktf		(gamemode == GMODE_TKTF || gamemode == GMODE_KTF || m_ktf2)
#define m_edit		(gamemode == GMODE_COOPEDIT)
#define	m_expert	(gamemode == GMODE_EXPERTTDM || gamemode == GMODE_EXPERTDM)
#define m_real		(gamemode == GMODE_REALTDM || gamemode == GMODE_REALDM)
#define m_classic	(gamemode == GMODE_CDM || gamemode == GMODE_CTDM)

#define m_noitems		(m_lms || m_osok || gamemode == GMODE_KNIFE)
#define m_noitemsnade	(m_lss && gamemode != GMODE_KNIFE)
#define m_nopistol		(m_osok || m_lss)
#define m_noprimary		(m_pistol || m_lss)
#define m_noradar		(m_classic)
#define m_nonuke		(m_zombies_rounds)
#define m_duel			(m_lms || gamemode == GMODE_LASTSWISSSTANDING || m_osok || m_zombies_rounds)
#define m_flags			(m_ctf || m_htf || m_ktf || m_btf)
#define m_team			(gamemode==GMODE_TEAMDEATHMATCH || gamemode==GMODE_TEAMONESHOTONEKILL || \
							gamemode==GMODE_TEAMSURVIVOR || m_ctf || m_htf || m_btf || gamemode==GMODE_TKTF || gamemode==GMODE_TKTF2 || \
							gamemode==GMODE_REALTDM || gamemode == GMODE_EXPERTTDM || gamemode == GMODE_CTDM || m_zombies)
#define m_valid(mode)	((mode)>=0 && (mode)<GMODE_NUM)
#define m_demo			(gamemode == GMODE_DEMO)
#define m_ai			(m_valid(gamemode) && !m_edit) // bots not available in coopedit
#define m_zombies		(m_zombies_rounds || m_onslaught) // extra bots!
#define m_zombies_rounds (gamemode==GMODE_ZOMBIES)
#define m_onslaught		(gamemode==GMODE_ONSLAUGHT)