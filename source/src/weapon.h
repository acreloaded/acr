enum
{
    GUN_KNIFE = 0,
    GUN_PISTOL,
    GUN_CARBINE,
    GUN_SHOTGUN,
    GUN_SUBGUN,
    GUN_SNIPER,
    GUN_ASSAULT,
    GUN_CPISTOL,
    GUN_GRENADE,
    GUN_AKIMBO,
    NUMGUNS
};

#define melee_weap(g) (g == GUN_KNIFE) // || g == GUN_SWORD)
#define explosive_weap(g) (g == GUN_GRENADE || g == GUN_RPG)
#define suppressed_weap(g) (melee_weap(g) || g == GUN_GRENADE || g == GUN_HEAL)
#define sniper_weap(g) (g == GUN_SNIPER || g == GUN_BOLT || g == GUN_SNIPER2)
#define burst_weap(g) (g == GUN_ASSAULT || g == GUN_ASSAULT2 || g == GUN_SUBGUN)
#define ads_gun(g) (!melee_weap(g) && g != GUN_GRENADE && g != GUN_AKIMBO)
#define ads_classic_allowed(g) (!m_classic(gamemode, mutators) || sniper_weap(g) || g == GUN_HEAL)

enum
{
    FRAG_NONE = 0,
    FRAG_GIB = 1 << 0,
    FRAG_SCOPE_NONE = 1 << 1,
    FRAG_SCOPE_FULL = 1 << 2,
    FRAG_REVENGE = 1 << 3,
    FRAG_CRIT = 1 << 4,
    FRAG_FLAG = 1 << 5,
    FRAG_COMEBACK = 1 << 6,
    FRAG_FIRST = 1 << 7,
    FRAG_VALID = (1 << ((7) + 1)) - 1
}; // up to 1 << 6 is optimal

enum
{
    STREAK_AIRSTRIKE = 0,
    STREAK_RADAR,
    STREAK_NUKE,
    STREAK_DROPNADE,
    STREAK_REVENGE,
    STREAK_JUG,
    STREAK_NUM
};

class playerent;
class bounceent;

struct weapon
{
    const static int weaponchangetime;
    const static float weaponbeloweye;
    static void equipplayer(playerent *pl);

    weapon(class playerent *owner, int type);
    virtual ~weapon() {}

    int type;
    playerent *owner;
    const struct guninfo &info;
    int &ammo, &mag, &gunwait, shots;
    virtual int dynspread();
    virtual float dynrecoil();
    int reloading, lastaction;

    virtual bool attack(vec &targ) = 0;
    virtual void attackfx(const vec &from, const vec &to, int millis) = 0;
    virtual void attackphysics(vec &from, vec &to);
    virtual void attacksound();
    virtual bool reload(bool autoreloaded);
    virtual void reset() {}
    virtual bool busy() { return false; }

    virtual int modelanim() = 0;
    virtual void updatetimers(int millis);
    virtual bool selectable();
    virtual bool deselectable();
    virtual void renderstats();
    virtual void renderhudmodel();
    virtual void renderaimhelp(bool teamwarning);

    virtual void onselecting();
    virtual void ondeselecting() {}
    virtual void onammopicked() {}
    virtual void onownerdies() {}
    virtual void removebounceent(bounceent *b) {}

    void sendshoot(vec &from, vec &to, int millis);
    bool modelattacking();
    void renderhudmodel(int lastaction, int index = 0);

    static bool valid(int id);

    virtual int flashtime() const;
};

class grenadeent;

enum { GST_NONE = 0, GST_INHAND, GST_THROWING };

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
    virtual void attackfx(const vec &from, const vec &to, int millis);
    int modelanim();
    void checkautoreload();
};


struct subgun : gun
{
    subgun(playerent *owner);
    int dynspread();
    bool selectable();
};


struct sniperrifle : gun
{
    bool scoped;
    int scoped_since;

    sniperrifle(playerent *owner);
    void attackfx(const vec &from, const vec &to, int millis);
    bool reload(bool autoreloaded);

    int dynspread();
    float dynrecoil();
    bool selectable();
    void onselecting();
    void ondeselecting();
    void onownerdies();
    void renderhudmodel();
    void renderaimhelp(bool teamwarning);
    void setscope(bool enable);
};


struct carbine : gun
{
    carbine(playerent *owner);
    bool selectable();
};

struct shotgun : gun
{
    shotgun(playerent *owner);
    void attackphysics(vec &from, vec &to);
    bool attack(vec &targ);
    void attackfx(const vec &from, const vec &to, int millis);
    bool selectable();
};


struct assaultrifle : gun
{
    assaultrifle(playerent *owner);
    int dynspread();
    float dynrecoil();
    bool selectable();
};

struct cpistol : gun
{
    bool bursting;
    cpistol(playerent *owner);
    bool reload(bool autoreloaded);
    bool selectable();
    void onselecting();
    void ondeselecting();
    bool attack(vec &targ);
    void setburst(bool enable);
};

struct pistol : gun
{
    pistol(playerent *owner);
    bool selectable();
};


struct akimbo : gun
{
    akimbo(playerent *owner);

    int akimboside;
    int akimbomillis;
    int akimbolastaction[2];

    void attackfx(const vec &from, const vec &to, int millis);
    void onammopicked();
    void onselecting();
    bool selectable();
    void updatetimers(int millis);
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

    int flashtime() const;
};

