// misc useful functions used by the server

#include "pch.h"
#include "cube.h"
#include "crypto.h"

#ifdef _DEBUG
bool protocoldbg = false;
void protocoldebug(bool enable) { protocoldbg = enable; }
#define DEBUGCOND (protocoldbg)
#endif

// all network traffic is in 32bit ints, which are then compressed using the following simple scheme (assumes that most values are small).

void putint(ucharbuf &p, int n)
{
	DEBUGVAR(n);
	if(n<128 && n>-127) p.put(n);
	else if(n<0x8000 && n>=-0x8000) { p.put(0x80); p.put(n); p.put(n>>8); }
	else { p.put(0x81); p.put(n); p.put(n>>8); p.put(n>>16); p.put(n>>24); }
}

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
void putuint(ucharbuf &p, int n)
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

void putfloat(ucharbuf &p, float f)
{
	lilswap(&f, 1);
	p.put((uchar *)&f, sizeof(float));
}

float getfloat(ucharbuf &p)
{
	float f;
	p.get((uchar *)&f, sizeof(float));
	return lilswap(f);
}

void sendstring(const char *text, ucharbuf &p)
{
	const char *t = text;
	while(*t) putint(p, *t++);
	putint(p, 0);
	DEBUGVAR(text);
}

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

void filtertext(char *dst, const char *src, int whitespace, int len)
{ // whitespace: no whitespace at all (0), blanks only (1), blanks & newline (2)
	for(int c = *src; c; c = *++src)
	{
		c &= 0x7F; // 7-bit ascii
		switch(c)
		{
			case '\f': ++src; continue;
		}
		if(isspace(c) ? whitespace && (whitespace>1 || c == ' ') : isprint(c))
		{
			*dst++ = c;
			if(!--len) break;
		}
	}
	*dst = '\0';
}

void filtername(char *dst, const char *src){
	int len = MAXNAMELEN;
	for(char c = *src & 0x7F; c; c = *++src)
	{
		if(c == '\f'){ ++src; continue; }
		if(c == ' ' || isprint(c)){
			//if(c == ' ' && ((&c - 1 < dst) || (*(&c - 1))==' ' || len < 2 || (*(&c + 1))==' ')) continue;
			if(c == ' ' && (len == MAXNAMELEN || dst[-1] == ' ')) continue;
			*dst++ = c;
			if(!--len) break;
		}
	}
	if(len != MAXNAMELEN && dst[-1] == ' ') dst[-1] = 0;
	else *dst = 0;
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

const char *modefullnames[GMODE_NUM-GMODE_DEMO] =
{
	"demo playback",
	"team deathmatch", "coopedit", "deathmatch", "survivor",
	"team survivor", "ctf", "pistol frenzy", "last swiss standing",
	"one shot, one kill", "team one shot, one kill", "hunt the flag", "team keep the flag", "keep the flag",
	"real-team deathmatch", "expert-team deathmatch", "real deathmatch", "expert deathmatch",
	"knife-only", "handheld-only", "return-capture the flag",
	"classic deathmatch", "classic-team deathmatch",
	"double keep the flag", "team-double keep the flag",
	"zombies", "onslaught",
	"bomber",
};

const char *modeacronymnames[GMODE_NUM-GMODE_DEMO] =
{
	"demo",
	"TDM", "edit", "DM", "SURV",
	"TSURV", "CTF", "PF", "LSS",
	"OSOK", "TOSOK", "HTF", "TKTF", "KTF",
	"rTDM", "eTDM", "rDM", "eDM",
	"knife", "handheld", "rCTF",
	"cDM", "cTDM",
	"DKTF2", "DTKTF2",
	"ZOM", "ONZ",
	"BOMB",
};

const char *voteerrors[VOTEE_NUM] = { "voting is currently disabled", "there is already a vote pending", "no permission to veto", "can't vote that often", "this vote is not allowed in the current environment (singleplayer/multiplayer)", "no permission", "invalid vote" };
const char *mmfullnames[MM_NUM] = { "open", "locked", "private" };

inline const char *fullmodestr(int n) { return (n>=GMODE_DEMO && n < GMODE_NUM) ? modefullnames[n - GMODE_DEMO] : "unknown"; }
inline const char *acronymmodestr(int n) { return (n>=GMODE_DEMO && n < GMODE_NUM) ? modeacronymnames[n - GMODE_DEMO] : "n/a"; }
const char *modestr(int n, bool acronyms) { return acronyms ? acronymmodestr (n) : fullmodestr(n); }
const char *voteerrorstr(int n) { return (n>=0 && n < VOTEE_NUM) ? voteerrors[n] : "unknown"; }
const char *mmfullname(int n) { return (n>=0 && n < MM_NUM) ? mmfullnames[n] : "unknown"; }

// cryptographic tools

const char *genpwdhash(const char *name, const char *pwd, int salt)
{
	static string temp;
	formatstring(temp)("%s %d %s %s %d", pwd, salt, name, pwd, abs(PROTOCOL_VERSION));
	tiger::hashval hash;
	tiger::hash((uchar *)temp, (int)strlen(temp), hash);
	formatstring(temp)("%llx %llx %llx", hash.chunks[0], hash.chunks[1], hash.chunks[2]);
	return temp;
}

bool gensha1(const char *s, unsigned int *dst){
	sha1 hasher = sha1();
	hasher << s;
	return hasher.Result(dst);
}
