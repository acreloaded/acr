// misc useful functions used by the server

#include "cube.h"

#ifdef _DEBUG
bool protocoldbg = false;
void protocoldebug(bool enable) { protocoldbg = enable; }
#define DEBUGCOND (protocoldbg)
#endif

// all network traffic is in 32bit ints, which are then compressed using the following simple scheme (assumes that most values are small).

template<class T>
static inline void putint_(T &p, int n)
{
    DEBUGVAR(n);
    if(n<128 && n>-127) p.put(n);
    else if(n<0x8000 && n>=-0x8000) { p.put(0x80); p.put(n); p.put(n>>8); }
    else { p.put(0x81); p.put(n); p.put(n>>8); p.put(n>>16); p.put(n>>24); }
}
void putint(ucharbuf &p, int n) { putint_(p, n); }
void putint(packetbuf &p, int n) { putint_(p, n); }
void putint(vect<uchar> &p, int n) { putint_(p, n); }

int getint(ucharbuf &p)
{
    int c = (char)p.get();
    if(c==-128) { int n = p.get(); n |= char(p.get())<<8; DEBUGVAR(n); return n; }
    else if(c==-127) { int n = p.get(); n |= p.get()<<8; n |= p.get()<<16; n |= (p.get()<<24); DEBUGVAR(n); return n; }
    else
    {
        DEBUGVAR(c);
        return c;
    }
}

// much smaller encoding for unsigned integers up to 28 bits, but can handle signed
template<class T>
static inline void putuint_(T &p, int n)
{
    DEBUGVAR(n);
    if(n < 0 || n >= (1<<21))
    {
        p.put(0x80 | (n & 0x7F));
        p.put(0x80 | ((n >> 7) & 0x7F));
        p.put(0x80 | ((n >> 14) & 0x7F));
        p.put(n >> 21);
    }
    else if(n < (1<<7)) p.put(n);
    else if(n < (1<<14))
    {
        p.put(0x80 | (n & 0x7F));
        p.put(n >> 7);
    }
    else
    {
        p.put(0x80 | (n & 0x7F));
        p.put(0x80 | ((n >> 7) & 0x7F));
        p.put(n >> 14);
    }
}
void putuint(ucharbuf &p, int n) { putuint_(p, n); }
void putuint(packetbuf &p, int n) { putuint_(p, n); }
void putuint(vect<uchar> &p, int n) { putuint_(p, n); }

int getuint(ucharbuf &p)
{
    int n = p.get();
    if(n & 0x80)
    {
        n += (p.get() << 7) - 0x80;
        if(n & (1<<14)) n += (p.get() << 14) - (1<<14);
        if(n & (1<<21)) n += (p.get() << 21) - (1<<21);
        if(n & (1<<28)) n |= 0xF0000000;
    }
    DEBUGVAR(n);
    return n;
}

template<class T>
static inline void putfloat_(T &p, float f)
{
    lilswap(&f, 1);
    p.put((uchar *)&f, sizeof(float));
}
void putfloat(ucharbuf &p, float f) { putfloat_(p, f); }
void putfloat(packetbuf &p, float f) { putfloat_(p, f); }
void putfloat(vect<uchar> &p, float f) { putfloat_(p, f); }

float getfloat(ucharbuf &p)
{
    float f;
    p.get((uchar *)&f, sizeof(float));
    return lilswap(f);
}

template<class T>
static inline void sendstring_(const char *text, T &p)
{
    const char *t = text;
    if(t) { while(*t) putint(p, *t++); }
    putint(p, 0);
    DEBUGVAR(text);
}
void sendstring(const char *t, ucharbuf &p) { sendstring_(t, p); }
void sendstring(const char *t, packetbuf &p) { sendstring_(t, p); }
void sendstring(const char *t, vect<uchar> &p) { sendstring_(t, p); }

void getstring(char *text, ucharbuf &p, int len)
{
    char *t = text;
    do
    {
        if(t>=&text[len]) { text[len-1] = 0; return; }
        if(!p.remaining()) { *t = 0; return; }
        *t = getint(p);
    }
    while(*t++);
    DEBUGVAR(text);
}

#define follower(x) (((x) & 0xc0) == 0x80)

int getutf8char(const uchar *&s)
{
    int res = *s++;
    if(res < 0x80) return res;
    if((res & 0xe0) == 0xc0)
    { // 2-Byte
        if(follower(s[0]))
        {
            res &= 0x1f;
            res <<= 6;
            res |= *s++ & 0x3f;
            return res;
        }
        return -1;
    }
    if((res & 0xf0) == 0xe0)
    { // 3-Byte
        if(follower(s[0]) && follower(s[1]))
        {
            res &= 0x0f;
            loopi(2)
            {
                res <<= 6;
                res |= *s++ & 0x3f;
            }
            return res;
        }
        return -1;
    }
    if((res & 0xf8) == 0xf0)
    { // 4-Byte
        if(follower(s[0]) && follower(s[1]) && follower(s[2]))
        {
            res &= 0x07;
            loopi(3)
            {
                res <<= 6;
                res |= *s++ & 0x3f;
            }
            return res;
        }
        return -1;
    }
    return -1;
}

int pututf8char(uchar *&d, int s)
{
    if(s < 0 || s > 0x1fffff) return 0;
    if(s < 0x80)
    {
        *d++ = s;
        return 1;
    }
    if(s < 0x800)
    {
        *d++ = ((s >> 6) & 0x1f) | 0xc0;
        *d++ = (s & 0x3f) | 0x80;
        return 2;
    }
    if(s < 0x10000)
    {
        *d++ = ((s >> 12) & 0x0f) | 0xe0;
        *d++ = ((s >> 6) & 0x3f) | 0x80;
        *d++ = (s & 0x3f) | 0x80;
        return 3;
    }
    *d++ = ((s >> 18) & 0x07) | 0xf0;
    *d++ = ((s >> 12) & 0x3f) | 0x80;
    *d++ = ((s >> 6) & 0x3f) | 0x80;
    *d++ = (s & 0x3f) | 0x80;
    return 4;
}

void filtertext(char *dst, const char *src, int whitespace, int len)
{ // whitespace: no whitespace at all (0), blanks only (1), blanks & newline (2), spaces for underlines (-1)
    for(int c = *src; c; c = *++src)
    {
        c &= 0x7F; // 7-bit ascii
        if(c == '\f')
        {
            if(!*++src) break;
            continue;
        }
        if(isspace(c) ? whitespace && (whitespace>1 || c == ' '): isprint(c))
        {
            if ( whitespace < 0 && c == ' ' ) c = '_';
            *dst++ = c;
            if(!--len) break;
        }
    }
    *dst = '\0';
}

void filterrichtext(char *dst, const char *src, int len)
{
    int b, c;
    unsigned long ul;
    for(c = *src; c; c = *++src)
    {
        c &= 0x7F; // 7-bit ascii
        if(c == '\\')
        {
            b = 0;
            c = *++src;
            switch(c)
            {
                case '\0': --src; continue;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'x':
                    b = 16;
                    c = *++src;
                default:
                    if(isspace(c)) continue;
                    if(b == 0 && !isdigit(c)) break;
                    ul = strtoul(src, (char **) &src, b);
                    --src;
                    c = (int) ul;
                    if(!c) continue; // number conversion failed
                    break;
            }
        }
        *dst++ = c;
        if(!--len || !*src) break;
    }
    *dst = '\0';
}

void filterservdesc(char *dst, const char *src, int len)
{ // only colors and spaces allowed
    for(int c = *src; c; c = *++src)
    {
        c &= 0x7F; // 7-bit ascii
        if((!isspace(c) && isprint(c)) || c == ' ' || c == '\f')
        {
            *dst++ = c;
            if(!--len) break;
        }
    }
    *dst = '\0';
}

void filterlang(char *d, const char *s)
{
    if(strlen(s) == 2)
    {
        loopi(2) d[i] = tolower(s[i]);
        d[2] = '\0';
        if(islower(d[0]) && islower(d[1])) return;
    }
    *d = '\0';
}

void trimtrailingwhitespace(char *s)
{
    for(int n = (int)strlen(s) - 1; n >= 0 && isspace(s[n]); n--)
        s[n] = '\0';
}

void cutcolorstring(char *text, int len)
{ // limit string length, ignore color codes
    while(*text)
    {
        if(*text == '\f' && text[1]) text++;
        else len--;
        if(len < 0) { *text = '\0'; break; }
        text++;
    }
}

inline const char *gamename(int mode, int muts, int compact = 0)
{
    if(!m_valid(mode))
        return compact ? "n/a" : "unknown";
    modecheck(mode, muts);
    gametypes &gt = gametype[mode];
    static string gname;
    gname[0] = 0;
    if(gt.mutators[0] && muts) loopi(G_M_NUM)
    {
        int implied = m_implied(mode, muts);
        if(/*(gt.mutators[0]&mutstype[i].type) && */(muts&mutstype[i].type) && (!implied || !(implied&mutstype[i].type)))
        {
            const char *mut = i < G_M_GSP ? mutstype[i].name : gt.gsp[i-G_M_GSP];
            if(mut && *mut)
            {
                string name;
                switch(compact)
                {
                    case 2: formatstring(name)("%s%c", *gname ? gname : "", mut[0]); break;
                    case 1: formatstring(name)("%s%s%c", *gname ? gname : "", *gname ? "-" : "", mut[0]); break;
                    case 0: default: formatstring(name)("%s%s%s", *gname ? gname : "", *gname ? "-" : "", mut); break;
                }
                copystring(gname, name);
            }
        }
    }
    if(!m_mimplied(mode, muts))
    {
        defformatstring(mname)("%s%s%s", *gname ? gname : "", *gname ? " " : "", gt.name);
        copystring(gname, mname);
    }
    return gname;
}

const char *voteerrors[VOTEE_NUM] = { "voting is currently disabled", "there is already a vote pending", "no permission to veto", "can't vote that often", "this vote is not allowed in the current environment (singleplayer/multiplayer)", "no permission", "invalid vote" };
const char *mmfullnames[MM_NUM] = { "open", "private", "match" };

const char *modestr(int gamemode, int mutators, bool acronyms) { return gamename(gamemode, mutators, acronyms ? 1 : 0); }
const char *voteerrorstr(int n) { return (n>=0 && (size_t)n < sizeof(voteerrors)/sizeof(voteerrors[0])) ? voteerrors[n] : "unknown"; }
const char *mmfullname(int n) { return (n>=0 && n < MM_NUM) ? mmfullnames[n] : "unknown"; }

int defaultgamelimit(int gamemode, int mutators) { return m_edit(gamemode) ? 1440 : m_progressive(gamemode, mutators) ? 45 : m_team(gamemode, mutators) ? 15 : 10; }

void modecheck(int &mode, int &muts, int trying)
{
    if(!m_valid(mode))
    {
        mode = G_DM;
        muts = m_implied(mode, G_M_NONE);
    }
    //#define modecheckreset(a) { if(muts && ++count < G_M_NUM*(G_M_GSN+5)) { i = -1; a; } else { muts = 0; i = G_M_NUM; break; } }
    if(!gametype[mode].mutators[0])
        muts = G_M_NONE;
    else
    {
        int gsps = 0;
        int implied = m_implied(mode, muts);
        int allowed = G_M_ALL;

        loopi(G_M_NUM)
        {
            if(!(muts & (1 << i))) continue;
            if(i >= G_M_GSP)
            {
                gsps |= 1 << (i - G_M_GSP);
                allowed &= gametype[mode].mutators[i-G_M_GSP+1];
            }

            allowed &= mutstype[i].mutators;
        }

        if(!gsps)
            allowed &= gametype[mode].mutators[0];

        loopi(G_M_NUM)
        {
            if(!((muts & allowed) & (1 << i))) continue;
            implied |= mutstype[i].implied;
        }

        muts = (muts & allowed) | implied;

        /*
        int count = 0, implied = m_implied(mode, muts);
        if(implied) muts |= implied;
        loopi(G_M_GSN)
        {
            int m = 1<<(i+G_M_GSP);
            if(!(gametype[mode].mutators[0]&m))
            {
                muts &= ~m;
                trying &= ~m;
            }
        }
        if(muts) loopi(G_M_NUM)
        {
            if(trying && !(gametype[mode].mutators[0]&mutstype[i].type) && (trying&mutstype[i].type))
                trying &= ~mutstype[i].type;
            if(!(gametype[mode].mutators[0]&mutstype[i].type) && (muts&mutstype[i].type))
            {
                muts &= ~mutstype[i].type;
                trying &= ~mutstype[i].type;
                modecheckreset(continue);
            }
            loopj(G_M_GSN)
            {
                if(!gametype[mode].mutators[j+1]) continue;
                int m = 1<<(j+G_M_GSP);
                if(!(muts&m)) continue;
                loopk(G_M_GSN)
                {
                    if(!gametype[mode].mutators[k+1]) continue;
                    int n = 1<<(k+G_M_GSP);
                    if(!(muts&n)) continue;
                    if(trying && (trying&m) && !(gametype[mode].mutators[k+1]&m))
                    {
                        muts &= ~n;
                        trying &= ~n;
                        modecheckreset(break);
                    }
                }
                if(i < 0) break;
            }
            if(i < 0) continue;
            if(muts&mutstype[i].type) loopj(G_M_NUM)
            {
                if(mutstype[i].mutators && !(mutstype[i].mutators&mutstype[j].type) && (muts&mutstype[j].type))
                {
                    implied = m_implied(mode, muts);
                    if(trying && (trying&mutstype[j].type) && !(implied&mutstype[i].type))
                    {
                        muts &= ~mutstype[i].type;
                        trying &= ~mutstype[i].type;
                    }
                    else
                    {
                        muts &= ~mutstype[j].type;
                        trying &= ~mutstype[j].type;
                    }
                    modecheckreset(break);
                }
                int implying = m_doimply(mode, muts, i);
                if(implying && (implying&mutstype[j].type) && !(muts&mutstype[j].type))
                {
                    muts |= mutstype[j].type;
                    modecheckreset(break);
                }
            }
        }
        */
    }
}

// gamemode definitions
gametypes gametype[G_MAX] = {
    /*
    {
        type, implied,
        {
            mutators
        },
        name, { gsp },
    },
    */
    {
        G_DEMO, G_M_NONE,
        {
            G_M_NONE,
            G_M_NONE,
        },
        "demo", { "" },
    },
    {
        G_EDIT, G_M_NONE,
        {
            G_M_MOST & ~G_M_GAMEPLAY, // probably superfluous for editmode anyways
            G_M_NONE,
        },
        "coopedit", { "" },
    },
    {
        G_DM, G_M_NONE,
        {
            G_M_ALL,
            G_M_ALL & ~(G_M_CONVERT),
        },
        "deathmatch", { "survivor" },
    },
    {
        G_CTF, G_M_TEAM,
        {
            G_M_ALL,
            G_M_ALL,
        },
        "capture the flag", { "direct" },
    },
    {
        G_STF, G_M_TEAM,
        {
            G_M_ALL,
            G_M_ALL,
        },
        "secure the flag", { "direct" },
    },
    {
        G_HTF, G_M_TEAM,
        {
            G_M_ALL,
            G_M_ALL,
        },
        "hunt the flag", { "strict" },
    },
    {
        G_KTF, G_M_NONE,
        {
            G_M_ALL,
            G_M_ALL,
        },
        "keep the flag", { "double" },
    },
    {
        G_BOMBER, G_M_TEAM,
        {
            G_M_ALL,
            G_M_ALL,
        },
        "bomber", { "demolition" },
    },
    {
        G_ZOMBIE, G_M_TEAM,
        {
            G_M_ALL,
            G_M_ALL & ~(G_M_CONVERT),
        },
        "zombies", { "progressive" },
    }
};
// mutator definitions
mutstypes mutstype[G_M_NUM] = {
    /*
    {
        type, implied,
        mutators,
        name,
    },
    */
    {
        G_M_TEAM, G_M_TEAM,
        G_M_ALL,
        "team",
    },
    {
        G_M_CLASSIC, G_M_CLASSIC,
        G_M_ALL,
        "classic",
    },
    {
        G_M_CONFIRM, G_M_CONFIRM|G_M_TEAM, // confirm forces team
        G_M_ALL,
        "confirm",
    },
    {
        G_M_VAMPIRE, G_M_VAMPIRE,
        G_M_ALL,
        "vampire",
    },
    {
        G_M_CONVERT, G_M_CONVERT|G_M_TEAM, // convert forces team
        G_M_ALL,
        "convert",
    },
    {
        G_M_PSYCHIC, G_M_PSYCHIC,
        G_M_ALL,
        "psychic",
    },
    // damage
    {
        G_M_JUGGERNAUT, G_M_JUGGERNAUT,
        G_M_ALL, // does not interfere
        "juggernaut",
    },
    {
        G_M_REAL, G_M_REAL,
        G_M_ALL & ~(G_M_EXPERT), // hardcore conflicts with expert
        "hardcore",
    },
    {
        G_M_EXPERT, G_M_EXPERT,
        G_M_ALL & ~(G_M_REAL), // expert conflicts with hardcore
        "expert",
    },
    // weapons are mutually exclusive
    {
        G_M_INSTA, G_M_INSTA,
        (G_M_ALL & ~G_M_WEAPON) | G_M_INSTA,
        "insta",
    },
    {
        G_M_SNIPING, G_M_SNIPING,
        (G_M_ALL & ~G_M_WEAPON) | G_M_SNIPING,
        "sniping",
    },
    {
        G_M_PISTOL, G_M_PISTOL,
        (G_M_ALL & ~G_M_WEAPON) | G_M_PISTOL,
        "pistol",
    },
    {
        G_M_GIB, G_M_GIB,
        (G_M_ALL & ~G_M_WEAPON) | G_M_GIB,
        "gibbing",
    },
    {
        G_M_EXPLOSIVE, G_M_EXPLOSIVE,
        (G_M_ALL & ~G_M_WEAPON) | G_M_EXPLOSIVE,
        "explosive",
    },
    // game specific ones
    {
        G_M_GSP1, G_M_GSP1,
        G_M_ALL,
        "gsp1",
    },
};

