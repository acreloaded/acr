// console.cpp: the console buffer, its display, and command line control

#include "pch.h"
#include "cube.h"

#define CONSPAD (FONTH/3)

VARP(altconsize, 0, 0, 100);
VARP(fullconsize, 0, 40, 100);
VARP(consize, 0, 6, 100);
VARP(confade, 0, 20, 60);

struct console : consolebuffer<cline>
{
	int conskip;
	void setconskip(int n)
	{
		conskip = clamp(conskip + n, 0, conlines.length());
	}

	static const int WORDWRAP = 80;

	int fullconsole;
	void toggleconsole()
	{
		if(!fullconsole) fullconsole = altconsize ? 1 : 2;
		else fullconsole = ++fullconsole % 3;
	}

	void render()
	{
		int conwidth = (fullconsole ? VIRTW : int(floor(getradarpos().x)))*2 - 2*CONSPAD - 2*FONTH/3;
		int h = VIRTH*2 - 2*CONSPAD - 2*FONTH/3;
		int conheight = min(fullconsole ? (h*(fullconsole==1 ? altconsize : fullconsize))/100 : FONTH*consize, h);

		if(fullconsole) blendbox(CONSPAD, CONSPAD, conwidth+CONSPAD+2*FONTH/3, conheight+CONSPAD+2*FONTH/3, true);

		int numl = conlines.length(), offset = min(conskip, numl);

		if(!fullconsole && confade)
		{
			if(!conskip)
			{
				numl = 0;
				loopvrev(conlines) if(totalmillis-conlines[i].millis < confade*1000) { numl = i+1; break; }
			}
			else offset--;
		}

		int y = 0;
		loopi(numl) //determine visible height
		{
			// shuffle backwards to fill if necessary
			int idx = offset+i < numl ? offset+i : --offset;
			char *line = conlines[idx].line;
			int width, height;
			text_bounds(line, width, height, conwidth);
			y += height;
			if(y > conheight) { numl = i; if(offset == idx) ++offset; break; }
		}
		y = CONSPAD+FONTH/3;
		loopi(numl)
		{
			int idx = offset + numl-i-1;
			cline &l = conlines[idx];
			char *line = l.line;

			int fade = 255;
            if(l.millis+confade*1000-totalmillis<1000 && !fullconsole){ // fading out
				fade = (l.millis+confade*1000-totalmillis)*255/1000;
				y -= FONTH * (totalmillis + 1000 - l.millis - confade*1000) / 1000;
			} else if(i+1 == numl && lastmillis - l.millis < 500){ // fading in
				fade = (lastmillis - l.millis)*255/500;
				y += FONTH * (l.millis + 500 - lastmillis) / 500;
			}

			draw_text(line, CONSPAD+FONTH/3, y, 0xFF, 0xFF, 0xFF, fade, -1, conwidth);
			int width, height;
			text_bounds(line, width, height, conwidth);
			y += height;
		}
	}

	console() : consolebuffer<cline>(200), fullconsole(false) { maxlines = 8; }
};

console con;

VARP(chatfade, 0, 15, 30);
struct chatlist : consolebuffer<cline>{
    void render(){
        const int conwidth = 2 * VIRTW * 3 / 10;
		int linei = conlines.length(), y = 2 * VIRTH * 52 / 100;
		loopi(conlines.length()){
			char *l = conlines[i].line;
			int width, height;
			text_bounds(l, width, height, conwidth);
			linei -= -1 + floor(float(height/FONTH));
		}
        loopi(linei){
			cline &l = conlines[i];
			if(totalmillis-l.millis < chatfade*1000 || con.fullconsole){
				int fade = 255;
				if(l.millis + chatfade*1000 - totalmillis < 1000 && !con.fullconsole){ // fading out
					fade = (l.millis + chatfade*1000 - totalmillis) * 255/1000;
					y -= FONTH * (totalmillis + 1000 - l.millis - chatfade*1000) / 1000;
				}
				else if(i == 0 && lastmillis-l.millis < 500){ // fading in
					fade = (lastmillis - l.millis)*255/500;
					y += FONTH * (l.millis + 500 - lastmillis) / 500;
				}
				int width, height;
				text_bounds(l.line, width, height, conwidth);
				y -= height;
				draw_text(l.line, CONSPAD+FONTH/3 + VIRTW / 100, y, 0xFF, 0xFF, 0xFF, fade, -1, conwidth);
			}
        }
    }
    chatlist() : consolebuffer<cline>(6) {}
};
chatlist chat;

Texture **obittex(){
	static Texture *tex[OBIT_NUM];
	if(!*tex){
		const char *texname[OBIT_NUM-OBIT_START] = { "death", "bot", "bow_impact", "bow_stuck", "knife_bleed", "knife_impact", "headshot", "gib", "crit", "first", "ff", "drown", "fall", "cheat", "airstrike", "nuke", "spect", "revive", "team", };
		loopi(OBIT_NUM){
			defformatstring(tname)("packages/misc/obit/%s.png", i == WEAP_AKIMBO ? "akimbo" : i < OBIT_START ? guns[i].modelname : texname[i - OBIT_START]);
			tex[i] = textureload(tname);
		}
	}
	return tex;
}

VARP(obitfade, 0, 10, 60);
struct oline { char *actor; char *target; int weap, millis, style; bool headshot; };
struct obitlist
{
	int maxlines;
	vector<oline> olines;

	obitlist() : maxlines(12) {}

	oline &addline(playerent *actor, int weap, int style, bool headshot, playerent *target, int millis)	// add a line to the obit buffer
	{
		oline cl;
		cl.actor = olines.length()>maxlines ? olines.pop().actor : newstringbuf("");   // constrain the buffer size
		cl.target = olines.length()>maxlines ? olines.pop().target : newstringbuf("");   // constrain the buffer size
		cl.millis = millis;						// for how long to keep line on screen
		cl.weap = weap;
		const int colorset[2][3] = {{0, 1, 3}, {8, 9, 7}};
		formatstring(cl.actor)("\f%d%s", colorset[0][actor == gamefocus ? 0 : isteam(actor, gamefocus) ? 1 : 2], actor == target ? "" : actor ? colorname(actor) : "unknown");
		formatstring(cl.target)("\f%d%s", colorset[weap >= OBIT_SPECIAL ? 0 : 1][target == gamefocus ? 0 : isteam(target, gamefocus) ? 1 : 2], target ? colorname(target) : "unknown");
		cl.style = style;
		cl.headshot = headshot;
		return olines.insert(0, cl);
	}

	void setmaxlines(int numlines)
	{
		maxlines = numlines;
		while(olines.length() > maxlines){
			oline &o = olines.pop();
			delete[] o.actor;
			delete[] o.target;
		}
	}
		
	virtual ~obitlist() { setmaxlines(0); }

	int drawobit(int style, int left, int top, uchar fade){
		int aspect = 1;
		switch(style){
			case WEAP_SHOTGUN:
			case WEAP_SNIPER:
			case OBIT_AIRSTRIKE:
				aspect = 4; break;
			case WEAP_BOLT:
			case WEAP_ASSAULT:
				aspect = 3; break;
			case WEAP_KNIFE:
			case WEAP_SUBGUN:
				aspect = 2; break;
			default: break; // many are square
		}

		Texture **guntexs = obittex();
		const int sz = FONTH;

		glColor4f(1, 1, 1, fade * (style == OBIT_HEADSHOT ? fabs(sinf(totalmillis / 2000.f * 2 * PI)) : 1.f) / 255);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBindTexture(GL_TEXTURE_2D, guntexs[style]->id);

		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(left, top);
		glTexCoord2f(1, 0); glVertex2f(left + sz * aspect, top);
		glTexCoord2f(1, 1); glVertex2f(left + sz * aspect, top + sz);
		glTexCoord2f(0, 1); glVertex2f(left, top + sz);
		glEnd();

		return aspect * sz;
	}

	void render(){
		const float ts = 1.6f; // factor that will alter the text size
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0, VIRTW*ts, VIRTH*ts, 0, -1, 1);
		const int left = VIRTW * .8f * ts;
		const int conwidth = VIRTW * ts - left; // draw all the way to the right
		int linei = olines.length(), y = ts * VIRTH * .5f;
		loopv(olines){
			defformatstring(l)("%s    %s", olines[i].actor, olines[i].target); // four spaces to subsitute for unknown obit icon
			int width, height;
			text_bounds(l, width, height, conwidth);
			linei -= -1 + floor(float(height/FONTH));
		}
        loopi(linei){
			oline &l = olines[i];
			if(totalmillis-l.millis < chatfade*1000 || con.fullconsole){
				int x = 0, fade = 255;
				if(l.millis + chatfade*1000 - totalmillis < 1000 && !con.fullconsole){ // fading out
					fade = (l.millis + chatfade*1000 - totalmillis) * 255/1000;
					y -= FONTH * (totalmillis + 1000 - l.millis - chatfade*1000) / 1000;
				}
				else if(i == 0 && lastmillis-l.millis < 500){ // fading in
					fade = (lastmillis - l.millis)*255/500;
					y += FONTH * (l.millis + 500 - lastmillis) / 500;
				}
				int width, height;
				text_bounds(l.actor, width, height, conwidth);
				y -= height;
				if(*l.actor){
					draw_text(l.actor, left, y, 0xFF, 0xFF, 0xFF, fade, -1, conwidth);
					x += width + text_width(" ") / 2;
				}
				// now draw weapon symbol
				x += drawobit(l.weap, left + x, y, fade);
				if(l.headshot) x += drawobit(OBIT_HEADSHOT, left + x, y, fade);
				else if(l.style & FRAG_GIB) x += drawobit(OBIT_GIB, left + x, y, fade);
				// next two shouldn't be grouped, but somehow is
				if(l.style & FRAG_FIRST) x += drawobit(OBIT_FIRST, left + x, y, fade);
				else if(l.style & FRAG_CRIT) x += drawobit(OBIT_CRIT, left + x, y, fade);
				// end of weapon symbol
				x += text_width(" ") / 2;
				draw_text(l.target, left + x, y, 0xFF, 0xFF, 0xFF, fade, -1, conwidth);
			}
        }
		glPopMatrix();
    }
};
obitlist obits;

void addobit(playerent *actor, int weap, int style, bool headshot, playerent *target) { extern int totalmillis; obits.addline(actor, weap, style, headshot, target, totalmillis); }
void renderobits() { obits.render(); }

textinputbuffer cmdline;
char *cmdaction = NULL, *cmdprompt = NULL;
bool saycommandon = false;

VARFP(maxcon, 10, 200, 1000, con.setmaxlines(maxcon));

void setconskip(int n) { con.setconskip(n); }
COMMANDN(conskip, setconskip, ARG_1INT);

void toggleconsole() { con.toggleconsole(); }
COMMANDN(toggleconsole, toggleconsole, ARG_NONE);

void renderconsole() { con.render(); chat.render(); }

inline void conout(consolebuffer<cline> &c, const char *s){
	string sp;
	filtertext(sp, s, 2);
	extern struct servercommandline scl;
	printf("%s%s\n", scl.logtimestamp ? timestring(true, "%b %d %H:%M:%S ") : "", sp);
	c.addline(s);
}

void chatoutf(const char *s, ...){ s_sprintfdv(sf, s); conout(chat, sf); con.addline(sf); }

void conoutf(const char *s, ...)
{
	s_sprintfdv(sf, s);
	conout(con, sf);
}

int rendercommand(int x, int y, int w)
{
	defformatstring(s)("> %s", cmdline.buf);
	int width, height;
	text_bounds(s, width, height, w);
	y -= height - FONTH;
	draw_text(s, x, y, 0xFF, 0xFF, 0xFF, 0xFF, cmdline.pos>=0 ? cmdline.pos+2 : (int)strlen(s), w);
	return height;
}

// keymap is defined externally in keymap.cfg

vector<keym> keyms;

void keymap(char *code, char *key, char *action)
{
	keym &km = keyms.add();
	km.code = atoi(code);
	km.name = newstring(key);
	km.action = newstring(action);
}

COMMAND(keymap, ARG_3STR);

keym *findbind(const char *key)
{
	loopv(keyms) if(!strcasecmp(keyms[i].name, key)) return &keyms[i];
	return NULL;
}

keym *findbinda(const char *action)
{
	loopv(keyms) if(!strcasecmp(keyms[i].action, action)) return &keyms[i];
	return NULL;
}

keym *findbindc(int code)
{
	loopv(keyms) if(keyms[i].code==code) return &keyms[i];
	return NULL;
}

keym *keypressed = NULL;
char *keyaction = NULL;

bool bindkey(keym *km, const char *action)
{
	if(!km) return false;
	if(!keypressed || keyaction!=km->action) delete[] km->action;
	km->action = newstring(action);
	return true;
}

void bindk(const char *key, const char *action)
{
	keym *km = findbind(key);
	if(!km) { conoutf("unknown key \"%s\"", key); return; }
	bindkey(km, action);
}

bool bindc(int code, const char *action)
{
	keym *km = findbindc(code);
	if(km) return bindkey(km, action);
	else return false;
}

COMMANDN(bind, bindk, ARG_2STR);

struct releaseaction
{
	keym *key;
	char *action;
};
vector<releaseaction> releaseactions;

char *addreleaseaction(const char *s)
{
	if(!keypressed) return NULL;
	releaseaction &ra = releaseactions.add();
	ra.key = keypressed;
	ra.action = newstring(s);
	return keypressed->name;
}

void onrelease(char *s)
{
	addreleaseaction(s);
}

COMMAND(onrelease, ARG_1STR);

void saycommand(char *init)						 // turns input to the command line on or off
{
	SDL_EnableUNICODE(saycommandon = (init!=NULL));
	setscope(false);
	if(!editmode) keyrepeat(saycommandon);
	copystring(cmdline.buf, init ? init : "");
	DELETEA(cmdaction);
	DELETEA(cmdprompt);
	cmdline.pos = -1;
}

void inputcommand(char *init, char *action, char *prompt)
{
	saycommand(init);
	if(action[0]) cmdaction = newstring(action);
	if(prompt[0]) cmdprompt = newstring(prompt);
}

void mapmsg(char *s)
{
	string text;
	filterrichtext(text, s);
	filterservdesc(text, text);
	copystring(hdr.maptitle, text, 128);
}

void getmapmsg(void)
{
	string text;
	copystring(text, hdr.maptitle, 128);
	result(text);
}

COMMAND(saycommand, ARG_CONC);
COMMAND(inputcommand, ARG_3STR);
COMMAND(mapmsg, ARG_1STR);
COMMAND(getmapmsg, ARG_NONE);

#if !defined(WIN32) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <SDL_syswm.h>
#endif

void pasteconsole(char *dst)
{
	#ifdef WIN32
	if(!IsClipboardFormatAvailable(CF_TEXT)) return;
	if(!OpenClipboard(NULL)) return;
	char *cb = (char *)GlobalLock(GetClipboardData(CF_TEXT));
	concatstring(dst, cb);
	GlobalUnlock(cb);
	CloseClipboard();
	#elif defined(__APPLE__)
	extern void mac_pasteconsole(char *commandbuf);

	mac_pasteconsole(dst);
	#else
	SDL_SysWMinfo wminfo;
	SDL_VERSION(&wminfo.version);
	wminfo.subsystem = SDL_SYSWM_X11;
	if(!SDL_GetWMInfo(&wminfo)) return;
	int cbsize;
	char *cb = XFetchBytes(wminfo.info.x11.display, &cbsize);
	if(!cb || !cbsize) return;
	int commandlen = strlen(dst);
	for(char *cbline = cb, *cbend; commandlen + 1 < _MAXDEFSTR && cbline < &cb[cbsize]; cbline = cbend + 1)
	{
		cbend = (char *)memchr(cbline, '\0', &cb[cbsize] - cbline);
		if(!cbend) cbend = &cb[cbsize];
		if(commandlen + cbend - cbline + 1 > _MAXDEFSTR) cbend = cbline + _MAXDEFSTR - commandlen - 1;
		memcpy(&dst[commandlen], cbline, cbend - cbline);
		commandlen += cbend - cbline;
		dst[commandlen] = '\n';
		if(commandlen + 1 < _MAXDEFSTR && cbend < &cb[cbsize]) ++commandlen;
		dst[commandlen] = '\0';
	}
	XFree(cb);
	#endif
}

struct hline
{
	char *buf, *action, *prompt;

	hline() : buf(NULL), action(NULL), prompt(NULL) {}
	~hline()
	{
		DELETEA(buf);
		DELETEA(action);
		DELETEA(prompt);
	}

	void restore()
	{
		copystring(cmdline.buf, buf);
		if(cmdline.pos >= (int)strlen(cmdline.buf)) cmdline.pos = -1;
		DELETEA(cmdaction);
		DELETEA(cmdprompt);
		if(action) cmdaction = newstring(action);
		if(prompt) cmdprompt = newstring(prompt);
	}

	bool shouldsave()
	{
		return strcmp(cmdline.buf, buf) ||
			   (cmdaction ? !action || strcmp(cmdaction, action) : action!=NULL) ||
			   (cmdprompt ? !prompt || strcmp(cmdprompt, prompt) : prompt!=NULL);
	}

	void save()
	{
		buf = newstring(cmdline.buf);
		if(cmdaction) action = newstring(cmdaction);
		if(cmdprompt) prompt = newstring(cmdprompt);
	}

	void run()
	{
		pushscontext(IEXC_PROMPT);
		if(action)
		{
			alias("cmdbuf", buf);
			execute(action);
		}
		else if(buf[0]=='/') execute(buf+1);
		else toserver(buf);
		popscontext();
	}
};
vector<hline *> history;
int histpos = 0;

void history_(int n)
{
	static bool inhistory = false;
	if(!inhistory && history.inrange(n))
	{
		inhistory = true;
		history[history.length()-n-1]->run();
		inhistory = false;
	}
}

COMMANDN(history, history_, ARG_1INT);

void execbind(keym &k, bool isdown)
{
	loopv(releaseactions)
	{
		releaseaction &ra = releaseactions[i];
		if(ra.key==&k)
		{
			if(!isdown) execute(ra.action);
			delete[] ra.action;
			releaseactions.remove(i--);
		}
	}
	if(isdown)
	{
		keyaction = k.action;
		keypressed = &k;
		execute(keyaction);
		keypressed = NULL;
		if(keyaction!=k.action) delete[] keyaction;
	}
	k.pressed = isdown;
}

void consolekey(int code, bool isdown, int cooked)
{
	if(isdown)
	{
		switch(code)
		{
			case SDLK_F1:
				toggledoc();
				break;

			case SDLK_F2:
				scrolldoc(-4);
				break;

			case SDLK_F3:
				scrolldoc(4);
				break;

			case SDLK_UP:
				if(histpos>0) history[--histpos]->restore();
				break;

			case SDLK_DOWN:
				if(histpos+1<history.length()) history[++histpos]->restore();
				break;

			case SDLK_TAB:
				if(!cmdaction)
				{
					complete(cmdline.buf);
					if(cmdline.pos>=0 && cmdline.pos>=(int)strlen(cmdline.buf)) cmdline.pos = -1;
				}
				break;

			default:
				resetcomplete();
				cmdline.key(code, isdown, cooked);
				break;
		}
	}
	else
	{
		if(code==SDLK_RETURN)
		{
			hline *h = NULL;
			if(cmdline.buf[0])
			{
				if(history.empty() || history.last()->shouldsave())
					history.add(h = new hline)->save(); // cap this?
				else h = history.last();
			}
			histpos = history.length();
			saycommand(NULL);
			if(h) h->run();
		}
		else if(code==SDLK_ESCAPE)
		{
			histpos = history.length();
			saycommand(NULL);
		}
	}
}

void keypress(int code, bool isdown, int cooked, SDLMod mod)
{
	keym *haskey = NULL;
	loopv(keyms) if(keyms[i].code==code) { haskey = &keyms[i]; break; }
	if(haskey && haskey->pressed) execbind(*haskey, isdown); // allow pressed keys to release
	else if(saycommandon) consolekey(code, isdown, cooked);  // keystrokes go to commandline
	else if(!menukey(code, isdown, cooked, mod))				  // keystrokes go to menu
	{
		if(haskey) execbind(*haskey, isdown);
	}
}

char *getcurcommand()
{
	return saycommandon ? cmdline.buf : NULL;
}

void writebinds(FILE *f)
{
	loopv(keyms)
	{
		if(*keyms[i].action) fprintf(f, "bind \"%s\" [%s]\n",	 keyms[i].name, keyms[i].action);
	}
}

