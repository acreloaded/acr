
enum { GUN_KNIFE = 0, GUN_PISTOL, GUN_SHOTGUN, GUN_SUBGUN, GUN_SNIPER, GUN_BOLT, GUN_ASSAULT, GUN_GRENADE, GUN_AKIMBO, NUMGUNS };
#define reloadable_gun(g) (g != GUN_KNIFE && g != GUN_GRENADE)
#define ads_gun(g) (g != GUN_KNIFE && g != GUN_GRENADE && g != GUN_AKIMBO)

enum { FRAG_NONE = 0, FRAG_GIB = 1 << 0, FRAG_REVENGE = 1 << 1, FRAG_CRITICAL = 1 << 2, FRAG_FIRST = 1 << 3, FRAG_OVER = 1 << 4,
		FRAG_VALID = (1 << ((4) + 1)) - 1 };

struct playerent;
struct bounceent;

struct weapon
{
	const static int weaponchangetime = 400;
	const static int scopetime = 275;
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
	virtual void attackhit(const vec &o);
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
	void renderstats();
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
	void attackshell(const vec &to);
	virtual void attackfx(const vec &from, const vec &to, int millis);
	int modelanim();
	virtual void checkautoreload();
};


struct subgun : gun
{
	subgun(playerent *owner);
	bool selectable();
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
	void checkautoreload();
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

	void drawstats();
	void attackfx(const vec &from, const vec &to, int millis);
	void attackhit(const vec &o);
	void renderstats();
	void renderaimhelp(int teamtype);
	void onownerdies();

	int flashtime() const;
};
