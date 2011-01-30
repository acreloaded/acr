
enum { GUN_KNIFE = 0, GUN_PISTOL, GUN_SHOTGUN, GUN_SUBGUN, GUN_SNIPER, GUN_SLUG, GUN_ASSAULT, GUN_GRENADE, GUN_AKIMBO, NUMGUNS };
#define reloadable_gun(g) (g != GUN_KNIFE && g != GUN_GRENADE)
#define ads_gun(g) (g != GUN_GRENADE && g != GUN_AKIMBO)

enum { FRAG_NONE = 0, FRAG_GIB = 1 << 0, FRAG_OVERKILL = 1 << 1, FRAG_REVENGE = 1 << 2, FRAG_CRITICAL = 1 << 3, FRAG_FIRST = 1 << 4 };

struct playerent;
struct bounceent;

struct weapon
{
	const static int weaponchangetime = 400;
	const static int scopetime = 275;
	const static float weaponbeloweye /*= 0.2f*/;
	static void equipplayer(playerent *pl);

	weapon(struct playerent *owner, int type);
	virtual ~weapon() {}

	int type;
	playerent *owner;
	const struct guninfo &info;
	int &ammo, &mag, &gunwait, shots;
	virtual int dynspread();
	virtual float dynrecoil();
	int reloading;

	virtual bool attack(vec &targ) = 0;
	virtual void attackfx(const vec &from, const vec &to, int millis) = 0;
	virtual void attackphysics(vec &from, vec &to);
	virtual void attacksound();
	virtual bool reload();
	virtual void reset() { }
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

	void sendshoot(vec &from, vec &to);
	bool modelattacking();
	void renderhudmodel(int lastaction, int index = 0);

	static bool valid(int id);

	virtual int flashtime() const;
};

struct grenadeent;

enum { GST_NONE, GST_INHAND, GST_THROWING };

struct grenades : weapon
{
	grenadeent *inhandnade;
	const int throwwait;
	int throwmillis;
	int state;

	grenades(playerent *owner);
	bool attack(vec &targ);
	void attackfx(const vec &from, const vec &to, int millis);
	int modelanim();
	void activatenade(const vec &to);
	void thrownade();
	void thrownade(const vec &vel);
	void dropnade();
	void renderstats();
	bool selectable();
	void reset();
	bool busy();
	void onselecting();
	void onownerdies();
	void removebounceent(bounceent *b);
	int flashtime() const;
};


struct gun : weapon
{
	gun(playerent *owner, int type);
	virtual bool attack(vec &targ);
	void attackshell(const vec &to);
	virtual void attackfx(const vec &from, const vec &to, int millis);
	int modelanim();
	void checkautoreload();
};


struct subgun : gun
{
	subgun(playerent *owner);
	bool selectable();
};


struct sniperrifle : gun
{
	const static int adsscope = 550;
	sniperrifle(playerent *owner);
	void attackfx(const vec &from, const vec &to, int millis);

	float dynrecoil();
	bool selectable();
	void renderhudmodel();
	void renderaimhelp(int teamtype);
};

struct sluggun : gun
{
	sluggun(playerent *owner);
	bool selectable();
};

struct shotgun : gun
{
	shotgun(playerent *owner);
	bool attack(vec &targ);
	void attackfx(const vec &from, const vec &to, int millis);
	bool selectable();
	void renderaimhelp(int teamtype);
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
	int akimbomillis;
	int akimbolastaction[2];

	bool attack(vec &targ);
	void onammopicked();
	void onselecting();
	bool selectable();
	void updatetimers();
	void reset();
	void renderhudmodel();
	bool timerout();
};


struct knife : weapon
{
	knife(playerent *owner);

	bool attack(vec &targ);
	int modelanim();

	void drawstats();
	void attackfx(const vec &from, const vec &to, int millis);
	void renderstats();
	void renderaimhelp(int teamtype){}

	int flashtime() const;
};
