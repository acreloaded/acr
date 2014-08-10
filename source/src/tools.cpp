// implementation of generic tools

#include "cube.h"

unsigned int &genguid(int b, uint a, int c, const char *z)
{
    static unsigned int value = 0;
    value = 0;
    unsigned int temp = 0;
    extern void *basicgen();
    char *inpStr = (char *)basicgen();
    if(inpStr)
    {
        char *start = inpStr;
        while(*inpStr)
        {
            temp = *inpStr++;
            temp += value;
            value = temp << 10;
            temp += value;
            value = temp >> 6;
            value ^= temp;
        }
        delete[] start;
    }
    temp = value << 3;
    temp += value;
    unsigned int temp2 = temp >> 11;
    temp = temp2 ^ temp;
    temp2 = temp << 15;
    value = temp2 + temp;
    if(value < 2) value += 2;
    return value;
}

void *basicgen()
{
    // WARNING: the following code is designed to give you a headache, but it probably won't
#if defined(WIN32)// && !defined(__GNUC__)
#if defined(__GNUC__)
#define KEY_WOW64_64KEY 0x0100
#endif
    extern char *getregszvalue(HKEY root, const char *keystr, const char *query, REGSAM extraaccess = 0);
    const char * const *temp = (char **) (char ***) (char *********) 20;
    --temp = (char **) (char ****) 2000;
    temp = (char **) (char ****) 21241;
    int temp2 = (short) (unsigned) (size_t) 87938749U;
    temp2 >>= (int) (size_t) 20;
    temp2 <<= (int) (size_t) (long) 1;
    char *temp3 = getregszvalue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid", KEY_WOW64_64KEY); // will fail on windows 2000
    if(temp3) return temp3;
    return (void *)getregszvalue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid"); // will fail on 64-bit
    temp += temp2;
    temp2 -= (int)temp;
    return (void *)temp;
    /*
    void ****pguid = 2 + ( (void ****) (void **) (void ***) new GUID );
    CoCreateGuid((GUID *)(--pguid - 1)); // #include <Objbase.h>
    //pguid -= 0xF0F0;
    void *pt = new string;
    memset(pt, 0, sizeof((char *)pt)/sizeof(*(char *)pt));
    memcpy(pt, (void *)--pguid, sizeof(GUID));
    formatstring(pt)("%lu%hu%hu%d", ((GUID *)pguid)->Data1, ((GUID *)pguid)->Data3, ((GUID *)pguid)->Data2, *((GUID *)pguid)->Data4);
    delete (GUID *)pguid;
    conoutf("%s", pt);
    return pt;
    */
    /*
    UUID u; // #pragma comment(lib, "Rpcrt4.lib")
            //#include <Rpc.h>
    switch(UuidCreateSequential (&u)){
        default: return nullptr;
        case RPC_S_OK:
        case RPC_S_UUID_LOCAL_ONLY: // can we trust it?
            break;
    }
    char *pt = new string;
    formatstring(pt)("%lu%hu%hu%d", u.Data1, u.Data2, u.Data3, u.Data4);
    return pt;
    */
#elif defined(__GNUC__) || defined(linux) || defined(__linux) || defined(__linux__) || defined(__APPLE__)
    char *pt = new string;
    formatstring(pt)("%lu", gethostid());
    return pt;
#else
    // OS not supported :(
    const char * const * temp = (char **)(char ***)20;
    --temp;
    temp = (char **)2000;
    return (void *)(char *)(temp = nullptr);
#endif
}

const char *timestring(bool local, const char *fmt)
{
    static string asciitime;
    time_t t = time(nullptr);
    struct tm * timeinfo;
    timeinfo = local ? localtime(&t) : gmtime (&t);
    strftime(asciitime, sizeof(string) - 1, fmt ? fmt : "%Y%m%d_%H.%M.%S", timeinfo); // sortable time for filenames
    return asciitime;
}

const char *asctime()
{
    return timestring(true, "%c");
}

const char *numtime()
{
    static string numt;
    formatstring(numt)("%ld", (long long) time(nullptr));
    return numt;
}

int mapdims[8];     // min/max X/Y and delta X/Y and min/max Z

extern ssqr *maplayout, *testlayout;
extern persistent_entity *mapents;
extern int maplayout_factor, testlayout_factor, Mvolume, Marea, Mopen, SHhits;
extern float Mheight;
extern int checkarea(int, ssqr *);

mapstats *loadmapstats(const char *filename, bool getlayout)
{
    static mapstats s;
    static uchar *enttypes = nullptr;
    static short *entposs = nullptr;

    DELETEA(enttypes);
    loopi(MAXENTTYPES) s.entcnt[i] = 0;
    loopi(3) s.spawns[i] = 0;
    loopi(2) s.flags[i] = 0;

    stream *f = opengzfile(filename, "rb");
    if(!f) return nullptr;
    memset(&s.hdr, 0, sizeof(header));
    if(f->read(&s.hdr, sizeof(header)-sizeof(int)*16)!=sizeof(header)-sizeof(int)*16 || (strncmp(s.hdr.head, "CUBE", 4) && strncmp(s.hdr.head, "ACMP",4) && strncmp(s.hdr.head, "ACRM",4))) { delete f; return nullptr; }
    lilswap(&s.hdr.version, 4);
    if(s.hdr.version>MAPVERSION || s.hdr.numents > MAXENTITIES || (s.hdr.version>=4 && f->read(&s.hdr.waterlevel, sizeof(int)*16)!=sizeof(int)*16)) { delete f; return nullptr; }
    if((s.hdr.version==7 || s.hdr.version==8) && !f->seek(sizeof(char)*128, SEEK_CUR)) { delete f; return nullptr; }
    if(s.hdr.version>=4)
    {
        lilswap(&s.hdr.waterlevel, 1);
        lilswap(&s.hdr.maprevision, 2);
    }
    else s.hdr.waterlevel = -100000;
    if(getlayout)
    {
        DELETEA(mapents);
        mapents = new persistent_entity[s.hdr.numents];
    }
    persistent_entity e;
    enttypes = new uchar[s.hdr.numents];
    entposs = new short[s.hdr.numents * 3];
    loopi(s.hdr.numents)
    {
        f->read(&e, sizeof(persistent_entity));
        lilswap((short *)&e, 4);
        TRANSFORMOLDENTITIES(s.hdr)
        if(e.type == PLAYERSTART && (e.attr2 == 0 || e.attr2 == 1 || e.attr2 == 100)) s.spawns[e.attr2 == 100 ? 2 : e.attr2]++;
        if(e.type == CTF_FLAG) { s.flags[min(e.attr2, (uchar)2)]++; if(e.attr2 == 0 || e.attr2 == 1) s.flagents[e.attr2] = i; }
        s.entcnt[e.type]++;
        enttypes[i] = e.type;
        entposs[i * 3] = e.x; entposs[i * 3 + 1] = e.y; entposs[i * 3 + 2] = e.z + e.attr1;
        if(getlayout) mapents[i] = e;
    }
    DELETEA(testlayout);
    int minfloor = 0;
    int maxceil = 0;
    if(s.hdr.sfactor <= LARGEST_FACTOR && s.hdr.sfactor >= SMALLEST_FACTOR)
    {
        testlayout_factor = s.hdr.sfactor;
        int layoutsize = 1 << (testlayout_factor * 2);
        bool fail = false;
        testlayout = new ssqr[layoutsize + 256];
        memset(testlayout, 0, layoutsize * sizeof(ssqr));
        ssqr *t = nullptr;
        int diff = 0;
        Mvolume = Marea = SHhits = 0;
        loopk(layoutsize)
        {
            ssqr &sq = testlayout[k];
            sq.type = f->getchar();
            int n = 1;
            switch (sq.type)
            {
                case 255:
                {
                    if(!t || (n = f->getchar()) < 0) { fail = true; break; }
                    loopi(n) memcpy(&sq + i, t, sizeof(ssqr));
                    k += n - 1;
                    break;
                }
                case 254: // only in MAPVERSION<=2
                    if(!t) { fail = true; break; }
                    memcpy(&sq, t, sizeof(ssqr));
                    f->getchar(); f->getchar();
                    break;
                default:
                    if(sq.type<0 || sq.type>=MAXTYPE)  { fail = true; break; }
                    sq.floor = f->getchar();
                    sq.ceil = f->getchar();
                    if(sq.floor >= sq.ceil && sq.ceil > -128) sq.floor = sq.ceil - 1;  // for pre 12_13
                    diff = sq.ceil - sq.floor;
                    if(sq.type != FHF && sq.floor<minfloor) minfloor = sq.floor;
                    if(sq.ceil>maxceil) maxceil = sq.ceil;
                    sq.wtex = f->getchar();
                    f->getchar(); // ftex
                    sq.ctex = f->getchar();
                    if(s.hdr.version<=2) { f->getchar(); f->getchar(); }
                    sq.vdelta = f->getchar();
                    if(s.hdr.version>=2) f->getchar(); // utex
                    if(s.hdr.version>=5) f->getchar(); // tag
                    break;
                case SOLID:
                    sq.floor = 127;
                    sq.ceil = 16;
                    sq.wtex = f->getchar();
                    sq.vdelta = f->getchar();
                    if(s.hdr.version<=2) { f->getchar(); f->getchar(); }
                    break;
            }
            if (sq.type != SOLID && diff > 6)
            {
                // Lucas (10mar2013): Removed "pow2" because it was too strict
                if (diff > MAXMHEIGHT) SHhits += /*pow2*/(diff-MAXMHEIGHT)*n;
                Marea += n;
                Mvolume += diff * n;
            }
            if(fail) break;
            t = &sq;
        }
        if(fail) { DELETEA(testlayout); }
        else
        {
            Mheight = Marea ? (float)Mvolume/Marea : 0;
            Mopen = checkarea(testlayout_factor, testlayout);
        }
    }
    if(getlayout)
    {
        DELETEA(maplayout);
        if (testlayout)
        {
            maplayout_factor = testlayout_factor;
            extern int maplayoutssize;
            maplayoutssize = 1 << testlayout_factor;
            int layoutsize = 1 << (testlayout_factor * 2);
            maplayout = new ssqr[layoutsize + 256];
            memcpy(maplayout, testlayout, layoutsize * sizeof(ssqr));

            loopk(8) mapdims[k] = k < 2 ? maplayoutssize : 0;
            loopk(layoutsize) if (testlayout[k].floor != 127)
            {
                int cwx = k%maplayoutssize,
                cwy = k/maplayoutssize;
                if(cwx < mapdims[0]) mapdims[0] = cwx;
                if(cwy < mapdims[1]) mapdims[1] = cwy;
                if(cwx > mapdims[2]) mapdims[2] = cwx;
                if(cwy > mapdims[3]) mapdims[3] = cwy;
            }
            loopk(2) mapdims[k+4] = mapdims[k+2] - mapdims[k];
            mapdims[6] = minfloor;
            mapdims[7] = maxceil;
        }
    }
    delete f;
    s.enttypes = enttypes;
    s.entposs = entposs;
    s.cgzsize = getfilesize(filename);
    return &s;
}

///////////////////////// debugging ///////////////////////

#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    string out, t;
    formatstring(out)("Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, nullptr, ::SymFunctionTableAccess, ::SymGetModuleBase, nullptr))
    {
        struct { IMAGEHLP_SYMBOL sym; string n; } si = { { sizeof( IMAGEHLP_SYMBOL ), 0, 0, 0, sizeof(string) } };
        IMAGEHLP_LINE li = { sizeof( IMAGEHLP_LINE ) };
        DWORD off;
        if(SymGetSymFromAddr(GetCurrentProcess(), (DWORD)sf.AddrPC.Offset, &off, &si.sym) && SymGetLineFromAddr(GetCurrentProcess(), (DWORD)sf.AddrPC.Offset, &off, &li))
        {
            char *del = strrchr(li.FileName, '\\');
            formatstring(t)("%s - %s [%d]\n", si.sym.Name, del ? del + 1 : li.FileName, li.LineNumber);
            concatstring(out, t);
        }
    }
    fatal("%s", out);
}
#elif defined(linux) || defined(__linux) || defined(__linux__)

#include <execinfo.h>

// stack dumping on linux, inspired by Sachin Agrawal's sample code

struct signalbinder
{
    static void stackdumper(int sig)
    {
        printf("stacktrace:\n");

        const int BTSIZE = 25;
        void *array[BTSIZE];
        int n = backtrace(array, BTSIZE);
        char **symbols = backtrace_symbols(array, n);
        for(int i = 0; i < n; i++)
        {
            printf("%s\n", symbols[i]);
        }
        free(symbols);

        fatal("ACR error (%d)", sig);

    }

    signalbinder()
    {
        // register signals to dump the stack if they are raised,
        // use constructor for early registering
        signal(SIGSEGV, stackdumper);
        signal(SIGFPE, stackdumper);
        signal(SIGILL, stackdumper);
        signal(SIGBUS, stackdumper);
        signal(SIGSYS, stackdumper);
        signal(SIGABRT, stackdumper);
    }
};

signalbinder sigbinder;

#endif


///////////////////////// misc tools ///////////////////////

bool cmpb(void *b, int n, enet_uint32 c)
{
    ENetBuffer buf;
    buf.data = b;
    buf.dataLength = n;
    return enet_crc32(&buf, 1)==c;
}

bool cmpf(char *fn, enet_uint32 c)
{
    int n = 0;
    char *b = loadfile(fn, &n);
    bool r = cmpb(b, n, c);
    delete[] b;
    return r;
}

enet_uint32 adler(unsigned char *data, size_t len)
{
    enet_uint32 a = 1, b = 0;
    while (len--)
    {
        a += *data++;
        b += a;
    }
    return b;
}

bool isbigendian()
{
    return !*(const uchar *)&islittleendian;
}

void strtoupper(char *t, const char *s)
{
    if(!s) s = t;
    while(*s)
    {
        *t = toupper(*s);
        t++; s++;
    }
    *t = '\0';
}

const char *atoip(const char *s, enet_uint32 *ip)
{
    unsigned int d[4];
    int n;
    if(!s || sscanf(s, "%u.%u.%u.%u%n", d, d + 1, d + 2, d + 3, &n) != 4) return nullptr;
    *ip = 0;
    loopi(4)
    {
        if(d[i] > 255) return nullptr;
        *ip = (*ip << 8) + d[i];
    }
    return s + n;
}

const char *atoipr(const char *s, iprange *ir)
{
    if((s = atoip(s, &ir->lr)) == nullptr) return nullptr;
    ir->ur = ir->lr;
    s += strspn(s, " \t");
    if(*s == '-')
    {
        if(!(s = atoip(s + 1, &ir->ur)) || ir->lr > ir->ur) return nullptr;
    }
    else if(*s == '/')
    {
        int m, n;
        if(sscanf(s + 1, "%d%n", &m, &n) != 1 || m < 0 || m > 32) return nullptr;
        unsigned long bm = (1 << (32 - m)) - 1;
        ir->lr &= ~bm;
        ir->ur |= bm;
        s += 1 + n;
    }
    return s;
}

const char *iptoa(const enet_uint32 ip)
{
    static string s[2];
    static int buf = 0;
    buf = (buf + 1) % 2;
    formatstring(s[buf])("%d.%d.%d.%d", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return s[buf];
}

const char *iprtoa(const struct iprange &ipr)
{
    static string s[2];
    static int buf = 0;
    buf = (buf + 1) % 2;
    if(ipr.lr == ipr.ur) copystring(s[buf], iptoa(ipr.lr));
    else formatstring(s[buf])("%s-%s", iptoa(ipr.lr), iptoa(ipr.ur));
    return s[buf];
}

int cmpiprange(const struct iprange *a, const struct iprange *b)
{
    if(a->lr < b->lr) return -1;
    if(a->lr > b->lr) return 1;
    return 0;
}

int cmpipmatch(const struct iprange *a, const struct iprange *b)
{
    return - (a->lr < b->lr) + (a->lr > b->ur);
}

char *concatformatstring(char *d, const char *s, ...)
{
    static defvformatstring(temp, s, s);
    return concatstring(d, temp);
}

const char *hiddenpwd(const char *pwd, int showchars)
{
    static int sc = 3;
    static string text;
    copystring(text, pwd);
    if(showchars > 0) sc = showchars;
    for(int i = (int)strlen(text) - 1; i >= sc; i--) text[i] = '*';
    return text;
}
//////////////// geometry utils ////////////////

static inline float det2x2(float a, float b, float c, float d) { return a*d - b*c; }
static inline float det3x3(float a1, float a2, float a3,
                           float b1, float b2, float b3,
                           float c1, float c2, float c3)
{
    return a1 * det2x2(b2, b3, c2, c3)
         - b1 * det2x2(a2, a3, c2, c3)
         + c1 * det2x2(a2, a3, b2, b3);
}

float glmatrixf::determinant() const
{
    float a1 = v[0], a2 = v[1], a3 = v[2], a4 = v[3],
          b1 = v[4], b2 = v[5], b3 = v[6], b4 = v[7],
          c1 = v[8], c2 = v[9], c3 = v[10], c4 = v[11],
          d1 = v[12], d2 = v[13], d3 = v[14], d4 = v[15];

    return a1 * det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4)
         - b1 * det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4)
         + c1 * det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4)
         - d1 * det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4);
}

void glmatrixf::adjoint(const glmatrixf &m)
{
    float a1 = m.v[0], a2 = m.v[1], a3 = m.v[2], a4 = m.v[3],
          b1 = m.v[4], b2 = m.v[5], b3 = m.v[6], b4 = m.v[7],
          c1 = m.v[8], c2 = m.v[9], c3 = m.v[10], c4 = m.v[11],
          d1 = m.v[12], d2 = m.v[13], d3 = m.v[14], d4 = m.v[15];

    v[0]  =  det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4);
    v[1]  = -det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4);
    v[2]  =  det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4);
    v[3]  = -det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4);

    v[4]  = -det3x3(b1, b3, b4, c1, c3, c4, d1, d3, d4);
    v[5]  =  det3x3(a1, a3, a4, c1, c3, c4, d1, d3, d4);
    v[6]  = -det3x3(a1, a3, a4, b1, b3, b4, d1, d3, d4);
    v[7]  =  det3x3(a1, a3, a4, b1, b3, b4, c1, c3, c4);

    v[8]  =  det3x3(b1, b2, b4, c1, c2, c4, d1, d2, d4);
    v[9]  = -det3x3(a1, a2, a4, c1, c2, c4, d1, d2, d4);
    v[10] =  det3x3(a1, a2, a4, b1, b2, b4, d1, d2, d4);
    v[11] = -det3x3(a1, a2, a4, b1, b2, b4, c1, c2, c4);

    v[12] = -det3x3(b1, b2, b3, c1, c2, c3, d1, d2, d3);
    v[13] =  det3x3(a1, a2, a3, c1, c2, c3, d1, d2, d3);
    v[14] = -det3x3(a1, a2, a3, b1, b2, b3, d1, d2, d3);
    v[15] =  det3x3(a1, a2, a3, b1, b2, b3, c1, c2, c3);
}

bool glmatrixf::invert(const glmatrixf &m, float mindet)
{
    float a1 = m.v[0], b1 = m.v[4], c1 = m.v[8], d1 = m.v[12];
    adjoint(m);
    float det = a1*v[0] + b1*v[1] + c1*v[2] + d1*v[3]; // float det = m.determinant();
    if(fabs(det) < mindet) return false;
    float invdet = 1/det;
    loopi(16) v[i] *= invdet;
    return true;
}

