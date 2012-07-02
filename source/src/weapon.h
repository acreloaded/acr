enum {
	WEAP_KNIFE = 0,
	WEAP_PISTOL,
	WEAP_SHOTGUN,
	WEAP_SUBGUN,
	WEAP_SNIPER,
	WEAP_BOLT,
	WEAP_ASSAULT,
	WEAP_GRENADE,
	WEAP_AKIMBO,
	WEAP_HEAL,
	WEAP_SWORD,
	WEAP_RPG,
	WEAP_MAX,
	// extra obits
	OBIT_START = WEAP_MAX,
	OBIT_DEATH = OBIT_START,
	OBIT_BOT,
	OBIT_IMPACT,
	OBIT_RPG_STUCK,
	OBIT_KNIFE_BLEED,
	OBIT_KNIFE_IMPACT,
	OBIT_FF,
	OBIT_DROWN,
	OBIT_FALL,
	OBIT_CHEAT,
	OBIT_AIRSTRIKE,
	OBIT_NUKE,
	OBIT_SPECT,
	OBIT_SPECIAL,
	OBIT_REVIVE = OBIT_SPECIAL,
	OBIT_TEAM,
	OBIT_STYLE,
	OBIT_HEADSHOT = OBIT_STYLE,
	OBIT_CRIT,
	OBIT_FIRST,
	OBIT_REVENGE,
	OBIT_STEALTH,
	OBIT_PENETRATE,
	OBIT_RICOCHET,
	OBIT_NUM
};

#define melee_weap(g) (g == WEAP_KNIFE || g == WEAP_SWORD)
#define explosive_weap(g) (g == WEAP_GRENADE || g == WEAP_RPG)
#define reloadable_weap(g) (!melee_weap(g) && g != WEAP_GRENADE)
#define suppressed_weap(g) (melee_weap(g) || g == WEAP_GRENADE)
#define ads_gun(g) (!melee_weap(g) && g != WEAP_GRENADE && g != WEAP_AKIMBO)
#define ads_classic_allowed(g) (!m_classic(gamemode, mutators) || g == WEAP_SNIPER || g == WEAP_BOLT || g == WEAP_HEAL)

enum { FRAG_NONE = 0, FRAG_GIB = 1 << 0, FRAG_REVENGE = 1 << 1, FRAG_CRIT = 1 << 2, FRAG_FLAG = 1 << 3, FRAG_FIRST = 1 << 4, FRAG_STEALTH = 1 << 5, FRAG_RICOCHET = 1 << 6, FRAG_PENETRATE = 1 << 7,
		FRAG_VALID = (1 << ((7) + 1)) - 1 }; // up to 1 << 6 is optimal

enum { STREAK_AIRSTRIKE = 0, STREAK_RADAR, STREAK_NUKE, STREAK_DROPNADE, STREAK_REVENGE, STREAK_NUM };

struct playerent;
struct bounceent;

struct weapon
{
	const static int weaponchangetime = 400;
	const static int scopetime;
	static void equipplayer(playerent *pl);

	weapon(struct playerent *owner, int type);
	virtual ~weapon() {}

	int type;
	playerent *owner;
	const struct guninfo &info;
	int &ammo, &mag, &gunwait, shots;
	virtual int dynspread();
	virtual float dynrecoil();
	int reloading, timebalance;

	virtual bool attack(vec &targ) = 0;
	virtual void attackfx(const vec &from, const vec &to, int millis) = 0;
	virtual void attackphysics(const vec &from, const vec &to);
	virtual void attackhit(const vec &o);
	virtual void attacksound();
	virtual bool reload();
	virtual void reset() { timebalance = 0; }
	virtual bool busy() { return false; }

	virtual int modelanim() = 0;
	virtual void updatetimers();
	virtual bool selectable();
	virtual bool deselectable();
	virtual void renderstats();
	virtual void renderhudmodel();
	virtual void renderaimhelp(int teamtype);

	virtual void onselecting();
	// virtual void ondeselecting() {}
	virtual void onammopicked() {}
	virtual void onownerdies() {}
	virtual void removebounceent(bounceent *b) {}

	void sendshoot(const vec &to);
	bool modelattacking();
	void renderhudmodel(int lastaction, bool flip = false);

	static bool valid(int id);

	virtual int flashtime() const;
};

struct grenadeent;

enum { GST_NONE, GST_INHAND, GST_THROWING };

struct grenades : weapon
{
	grenadeent *inhandnade;
	const int throwwait;
	int state;

	grenades(playerent *owner);
	bool attack(vec &targ);
	void attackfx(const vec &from, const vec &to, int millis);
	void attackhit(const vec &o);
	int modelanim();
	void activatenade();
	void thrownade();
	void thrownade(const vec &vel);
	void dropnade();
	bool selectable();
	void reset();
	bool busy();
	void onselecting();
	void onownerdies();
	void removebounceent(bounceent *b);
	int flashtime() const;

	void renderaimhelp(int teamtype){}
};


struct gun : weapon
{
	const static int adsscope = 550;
	gun(playerent *owner, int type);
	virtual bool attack(vec &targ);
	virtual void attackshell(const vec &to);
	virtual void attackfx(const vec &from, const vec &to, int millis);
	int modelanim();
	virtual bool checkautoreload();
};


struct subgun : gun
{
	subgun(playerent *owner);
	bool selectable();
};

struct heal : gun
{
	heal(playerent *owner);

	bool selectable();

	void attackfx(const vec &from, const vec &to, int millis);

	int flashtime() const;
};

struct sword : weapon
{
    sword(playerent *owner);

	bool selectable();

    bool attack(vec &targ);
    int modelanim();

    void attackfx(const vec &from, const vec &to, int millis);
	void renderstats(){}
	void renderaimhelp(int teamtype){}

    int flashtime() const;
};

struct crossbow : gun
{
	crossbow(playerent *owner);
	bool selectable();
	int modelanim();

	virtual void attackfx(const vec &from, const vec &to, int millis);
	void attackhit(const vec &o);
	void attackshell(const vec &to){};
};

struct scopedprimary : gun
{
	scopedprimary(playerent *owner, int type);
	void attackfx(const vec &from, const vec &to, int millis);

	float dynrecoil();
	bool selectable();
	void renderhudmodel();
	void renderaimhelp(int teamtype);
};

struct sniperrifle : scopedprimary
{
	sniperrifle(playerent *owner);
};

struct boltrifle : scopedprimary
{
	boltrifle(playerent *owner);
};

struct shotgun : gun
{
	shotgun(playerent *owner);
	void attackfx(const vec &from, const vec &to, int millis);
	bool selectable();
	void renderaimhelp(int teamtype);
	bool autoreloading;
	bool reload();
	bool checkautoreload();
};


struct assaultrifle : gun
{
	assaultrifle(playerent *owner);
	float dynrecoil();
	bool selectable();
};


struct pistol : gun
{
	pistol(playerent *owner);
	bool selectable();
};


struct akimbo : gun
{
	akimbo(playerent *owner);

	bool akimboside;
	int akimbolastaction[2];

	bool attack(vec &targ);
	void onammopicked();
	void onselecting();
	bool selectable();
	void updatetimers();
	void reset();
	void renderhudmodel();
};

struct knifeent;

struct knife : weapon
{
	knife(playerent *owner);
	knifeent *inhandknife;
	int state;

	bool attack(vec &targ);
	void reset();
	bool selectable();
	int modelanim();

	void activateknife();
	void throwknife(bool weak = false);
	void throwknife(const vec &vel);

	void attackfx(const vec &from, const vec &to, int millis);
	void renderstats() {}
	void renderaimhelp(int teamtype){}
	void onownerdies();
	void removebounceent(bounceent *b);

	int flashtime() const;
};
