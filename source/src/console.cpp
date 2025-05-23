// console.cpp: the console buffer, its display, and command line control

#include "cube.h"

#define CONSPAD (FONTH/3)

VARP(altconsize, 0, 0, 100);
VARP(fullconsize, 0, 40, 100);
VARP(consize, 0, 6, 100);
VARP(confade, 0, 20, 60);
VAR(conopen, 0, 0, 1);
VAR(numconlines, 0, 0, 1);

int fullconsole;

struct console : consolebuffer<cline>
{
    int conskip;
    void setconskip(int n)
    {
        int visible_lines = (int)(min(fullconsole ? ((VIRTH * 2 - 2 * CONSPAD - 2 * FONTH / 3)*(fullconsole == 1 ? altconsize : fullconsize)) / 100 : FONTH*consize, (VIRTH * 2 - 2 * CONSPAD - 2 * FONTH / 3)) / (CONSPAD + 2 * FONTH / 3)) - 1;
        conskip = clamp(conskip + n, 0, clamp(conlines.length()-visible_lines, 0, conlines.length()));
    }

    static const int WORDWRAP = 80;

    void addline(const char *sf) { consolebuffer<cline>::addline(sf); numconlines++; }

    void render()
    {
        int conwidth = (fullconsole ? VIRTW : int(floor(getradarpos().x))) * 2 - 2 * CONSPAD - 2 * FONTH / 3;
        int h = VIRTH*2 - 2*CONSPAD - 2*FONTH/3;
        int conheight = min(fullconsole ? (h*(fullconsole == 1 ? altconsize : fullconsize)) / 100 : FONTH*consize, h);

        if (fullconsole) blendbox(CONSPAD, CONSPAD, conwidth + CONSPAD + 2 * FONTH / 3, conheight + CONSPAD + 2 * FONTH / 3, true);

        int numl = conlines.length(), offset = min(conskip, numl);

        if (!fullconsole && confade)
        {
            if(!conskip)
            {
                numl = 0;
                loopvrev(conlines) if(totalmillis-conlines[i].millis < confade*1000 + 1000) { numl = i+1; break; }
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
            if (totalmillis >= l.millis + confade * 1000 && !fullconsole)
            {
                // fading out
                fade = (l.millis + 1000 + confade*1000-totalmillis)*255/1000;
                y -= FONTH * (totalmillis - l.millis - confade*1000) / 1000;
            }
            else if(/*i+1 == numl &&*/ totalmillis - l.millis < 500)
            {
                // fading in
                fade = (totalmillis - l.millis)*255/500;
                y += FONTH * (l.millis + 500 - totalmillis) / 500;
            }

            draw_text(line, CONSPAD+FONTH/3, y, 0xFF, 0xFF, 0xFF, fade, -1, conwidth);
            int width, height;
            text_bounds(line, width, height, conwidth);
            y += height;
        }
    }

    console() : consolebuffer<cline>(200), conskip(0) {}
};

VARP(chatfade, 0, 15, 30);
struct chatlist : consolebuffer<cline>
{
    static const int FADEMAX = 6;

    void render()
    {
        const int conwidth = 2 * VIRTW * 3 / 10;
        int linei = 0, consumed = 0, y = 2 * VIRTH * 52 / 100;
        loopi(conlines.length())
        {
            char *l = conlines[i].line;
            int width, height;
            text_bounds(l, width, height, conwidth);
            consumed += ceil((float)height / FONTH);
            if (consumed > (fullconsole ? FADEMAX : maxlines))
                break;
            ++linei;
        }
        loopi(linei)
        {
            cline &l = conlines[i];
            if (totalmillis <= l.millis + chatfade * 1000 + 1000 || fullconsole)
            {
                int fade = 255;

                if (!fullconsole)
                {
                    if(totalmillis >= l.millis + chatfade*1000)
                    {
                        // fading out
                        fade = (l.millis + 1000 + chatfade*1000 - totalmillis) * 255/1000;
                        y -= FONTH * (totalmillis - l.millis - chatfade*1000) / 1000;
                    }
                    else if(i >= FADEMAX)
                        l.millis = totalmillis - chatfade*1000; // for next frame
                }
                if(/*!i &&*/ totalmillis - l.millis < 500)
                {
                    // fading in
                    fade = (totalmillis - l.millis)*255/500;
                    y += FONTH * (500 - totalmillis + l.millis) / 500;
                }
                int width, height;
                text_bounds(l.line, width, height, conwidth);
                y -= height;
                draw_text(l.line, CONSPAD+FONTH/3 + VIRTW / 100, y, 0xFF, 0xFF, 0xFF, fade, -1, conwidth);
            }
        }
    }

    chatlist() : consolebuffer<cline>(FADEMAX * 2) { }
};

VARP(obitfade, 0, 10, 60);
VARP(obitalpha, 0, 75, 100);
VARP(obitamt, 0, 1, 4); // 0: very compact, 1: show humans, 2: show humans and suicides, 3: show all, 4: show all plus the prefix

void obit_name(char *out, playerent *pl, bool dark, int type)
{
    if (!pl)
    {
        *out = '\0';
        return;
    }
    const char colorset[2][3] = { { '0', '1', '3' }, { 'm', 'o', '7' } };
    int color2 = pl == player1 ? 1 : isteam(pl, player1) ? 0 : 2;
    if (type == 0)
        formatstring(out)("\f%c%c", colorset[dark ? 1 : 0][color2], !color2 ? '+' : color2 == 1 ? '*' : '-');
    else if (type == 1)
        formatstring(out)("\f%c%s", colorset[dark ? 1 : 0][color2], !color2 ? "++" : color2 == 1 ? "**" : "--");
    else if (obitamt >= 4)
        formatstring(out)("\f%c%c%s", colorset[dark ? 1 : 0][color2], !color2 ? '+' : color2 == 1 ? '*' : '-', colorname(pl));
    else
        formatstring(out)("\f%c%s", colorset[dark ? 1 : 0][color2], colorname(pl));
}

enum
{
    OBIT_ICON_HEADSHOT = 0,
    OBIT_ICON_CRIT,
    OBIT_ICON_REVENGE,
    OBIT_ICON_FIRST,
    OBIT_ICON_SCOPE_NONE,
    OBIT_ICON_SCOPE_QUICK,
    OBIT_ICON_SCOPE_RECENT,
    OBIT_ICON_SCOPE_HARD,
    OBIT_ICON_NUM,
};
struct oline
{
    char *actor, *target, *str;
    int millis, obit, style, assist, combo, merges;
    bool headshot;
    void cleanup() const { delete[] actor; delete[] target; delete[] str; }
    bool mergable(const oline &o)
    {
        if (o.obit != obit || strcmp(o.actor, actor) || strcmp(o.target, target))
            return false;
        const int stylemask = obit == GUN_KNIFE || obit == GUN_RPG ? FRAG_GIB | FRAG_FLAG : obit == GUN_GRENADE ? FRAG_GIB : FRAG_NONE;
        return !((o.style ^ style) & stylemask); // (o.style & stylemask) == (style & stylemask);
    }
    void merge(const oline &o)
    {
        // obit matches
        style |= o.style & ~FRAG_SCOPE; // merge styles
        headshot |= o.headshot;
        millis = max(millis, o.millis); // merge time by using the later one
        assist = max(assist, o.assist); // merge assist by using the larger one
        combo = max(combo, o.combo); // merge combo by using the larger one
        merges += o.merges; // merge merge count
        o.cleanup();
        recompute();
    }
    int width, icons[OBIT_ICON_NUM];
    void recompute()
    {
#define SETICON(icon, cond, s) cond { icons[icon] = text_width(str) + FONTH * 2 / 5; concatstring(str, s); }
#define SETICON1(icon, cond) SETICON(icon, cond, "   ") // the width of a 1:1 icon is 2 spaces + 1 space
        memset(icons, 0, sizeof(icons));
        copystring(str, actor);
        if (assist)
            concatformatstring(str, " \f2(+%d)", assist);
        switch (style & FRAG_SCOPE)
        {
            case FRAG_SCOPE_NONE:
                SETICON1(OBIT_ICON_SCOPE_NONE, if (sniper_weap(obit)));
                break;
            case 0:
                SETICON1(OBIT_ICON_SCOPE_QUICK, if (obit < OBIT_START && ads_gun(obit)));
                break;
            case FRAG_SCOPE_NONE | FRAG_SCOPE_FULL:
                SETICON1(OBIT_ICON_SCOPE_RECENT, );
                break;
            case FRAG_SCOPE_FULL:
                SETICON1(OBIT_ICON_SCOPE_HARD, );
                break;
        }
        SETICON1(OBIT_ICON_FIRST, if (style & FRAG_FIRST));
        concatformatstring(str, " \f4[\f5%s\f4] ", actor[0] ? killname(obit, style) : suicname(obit));
        SETICON1(OBIT_ICON_HEADSHOT, if (headshot));
        SETICON1(OBIT_ICON_REVENGE, if (style & FRAG_REVENGE));
        SETICON1(OBIT_ICON_CRIT, if (style & FRAG_CRIT));
        // TODO other icons
        if(style & FRAG_RICOCHET)
            concatstring(str, "\f6< "); // temporary "icon"
        if(style & FRAG_PENETRATE)
            concatstring(str, "\f6> "); // temporary "icon"
        concatstring(str, target);
        if (combo > 1)
            concatformatstring(str, " \f3[#%d]", combo);
        if (merges > 1)
            concatformatstring(str, " \f9(x%d)", merges);
        width = text_width(str);
    }
};
struct obitlist : consolebuffer<oline>
{
    obitlist() : consolebuffer<oline>() {}

    static const int FADEMAX = 12;

    oline &addline(playerent *actor, int obit, int style, bool headshot, playerent *target, int combo, int assist, int millis)
    {
        // add a line to the obit buffer
        oline cl;
        // constrain the buffer size
        if (conlines.length() && conlines.length()>maxlines)
            conlines.pop().cleanup();
        cl.actor = newstringbuf("");
        cl.target = newstringbuf("");
        cl.str = newstringbuf("");
        cl.millis = millis; // for how long to keep line on screen
        cl.obit = obit;
        cl.style = style;
        cl.assist = assist;
        cl.combo = combo;
        cl.merges = 1;
        cl.headshot = headshot;
        if (actor && actor != target)
            obit_name(cl.actor, actor, false, (obitamt >= 3 || (actor->ownernum < 0 && obitamt >= 1)) ? 2 : (actor->ownernum < 0) ? 1 : 0);
        if (target)
            obit_name(cl.target, target, obit < OBIT_SPECIAL, (obitamt >= 3 || (actor == target && obitamt >= 2) || ((target->ownernum < 0) && obitamt >= 1)) ? 2 : (target->ownernum < 0) ? 1 : 0);
        cl.recompute();
        // try merge
        loopv(conlines)
            if (fullconsole || (i < FADEMAX && totalmillis - conlines[i].millis < obitfade * 1000))
                if (cl.mergable(conlines[i]))
                {
                    cl.merge(conlines.remove(i)); // remove, and "merge" into our line
                    break;
                }
        return conlines.insert(0, cl);
    }

    void mergeobits()
    {
        // merge all possible obits
        loopv(conlines) loopvjrev(conlines)
        {
            if (j <= i) break;
            else if (conlines[i].mergable(conlines[j]))
                conlines[i].merge(conlines.remove(j));
        }
    }

    void render()
    {
        const float ts = 1.8f; // factor that will alter the text size
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, VIRTW*ts, VIRTH*ts, 0, -1, 1);
        int origVIRTW = VIRTW;
        glTranslatef((float)ts*VIRTW*(monitors - 2 + (monitors&1))/(2.*monitors), 0., 0.);
        VIRTW /= (float)monitors/(float)(2 - (monitors & 1));
        int linei = 0, /*consumed = 0,*/ y = ts * VIRTH * .5f;
        // every line is 1 line
        linei = min(fullconsole ? FADEMAX : maxlines, conlines.length());
        loopi(linei)
        {
            oline &l = conlines[i];
            if (fullconsole || totalmillis <= l.millis + obitfade * 1000 + 1000)
            {
                float fade = 1;
                if (!fullconsole) // fading out
                {
                    if (totalmillis >= l.millis + obitfade * 1000)
                    {
                        fade = float(l.millis + 1000 + obitfade * 1000 - totalmillis) / 1000;
                        y -= FONTH * (totalmillis - l.millis - obitfade * 1000) / 1000;
                    }
                    else if (i >= FADEMAX) l.millis = totalmillis - obitfade * 1000; // for next frame
                }
                if (/*!i*/ totalmillis - l.millis < 500) // fading in
                {
                    fade = float(totalmillis - l.millis) / 500;
                    y += FONTH * (l.millis + 500 - totalmillis) / 500;
                }
                fade *= obitalpha / 100.f;
                int x = (VIRTW - 16) * ts - l.width;
                y -= FONTH;
                draw_text(l.str, x, y, 0xFF, 0xFF, 0xFF, fade * 0xFF);
                bool obit_loaded = false;
                loopi(OBIT_ICON_NUM)
                {
                    if (!l.icons[i]) continue;
                    static char iconvalue[2] = { '.', '\0' };
                    iconvalue[0] = i + '0';
                    if (!obit_loaded)
                    {
                        pushfont("obit");
                        obit_loaded = true;
                    }
                    draw_text(iconvalue, x + l.icons[i], y, 0xFF, 0xFF, 0xFF, fade * 0xFF);
                }
                if (obit_loaded)
                    popfont();
            }
        }
        VIRTW = origVIRTW;
        glPopMatrix();
    }
} obits;

console con;
chatlist chat;
textinputbuffer cmdline;
char *cmdaction = NULL, *cmdprompt = NULL;
bool saycommandon = false;

VARFP(maxcon, 10, 200, 1000, con.setmaxlines(maxcon));

void setconskip(int *n) { con.setconskip(*n); }
COMMANDN(conskip, setconskip, "i");

void toggleconsole()
{
    if (!fullconsole) fullconsole = altconsize ? 1 : 2;
    else fullconsole = (fullconsole + 1) % 3;
    conopen = fullconsole;
    if (fullconsole)
        obits.mergeobits();
}
COMMANDN(toggleconsole, toggleconsole, "");

void renderconsole() { con.render(); chat.render(); }
void renderobits() { obits.render(); }

void addobit(playerent *actor, int obit, int style, bool headshot, playerent *target, int combo, int assist)
{
    extern int totalmillis;
    obits.addline(actor, obit, style, headshot, target, combo, assist, totalmillis);
}

void clientlogf(const char *s, ...)
{
    defvformatstring(sp, s, s);
    filtertext(sp, sp, 2);
    extern struct servercommandline scl;
    const char *ts = scl.logtimestamp ? timestring(true, "%b %d %H:%M:%S ") : "";
    char *p, *l = sp;
    do
    { // break into single lines first
        if((p = strchr(l, '\n'))) *p = '\0';
        printf("%s%s\n", ts, l);
        if(p) l = p + 1;
    }
    while(p);
}
SVAR(conline,"n/a");
void conoutf(const char *s, ...)
{
    defvformatstring(sf, s, s);
    clientlogf("%s", sf);
    con.addline(sf);
    delete[] conline; conline=newstring(sf);
}

void chatonlyf(const char *s, ...)
{
    defvformatstring(sf, s, s);
    chat.addline(sf);
}

void chatoutf(const char *s, ...)
{
    defvformatstring(sf, s, s);
    clientlogf("%s", sf);
    con.addline(sf);
    chat.addline(sf);
}

COMMANDF(strstr, "ss", (char *a, char *b) { intret(strstr(a, b) ? 1 : 0); });

/** This is the 1.0.4 function
    It will substituted by rendercommand_wip
    I am putting this temporarily here because it is very difficult to chat in game with the current cursor behavior,
    and chatting in this test period is extremelly important : Brahma */
int rendercommand(int x, int y, int w)
{
    defformatstring(s)("# %s", cmdline.buf); /** I changed the symbol here to differentiate from the > (new talk symbol),
                                             and make clear the console changed to the old players (like me) : Brahma */
    int width, height;
    text_bounds(s, width, height, w);
    y -= height - FONTH;
    draw_text(s, x, y, 0xFF, 0xFF, 0xFF, 0xFF, cmdline.pos>=0 ? cmdline.pos+2 : (int)strlen(s), w);
    return height;
}

const char *getCONprefix(int n)
{
    const char* CONpreSTR[] = {
        ">", // ">>>", // "TALK" // "T" // ">"
        "/", // "CFG", // "EXEC" // "!" // "!"
        "%", // "TEAM" // ">" // "T"
    };
    return (n>=0 && size_t(n) < sizeof(CONpreSTR)/sizeof(CONpreSTR[0])) ? CONpreSTR[n] : "#";
}

int getCONlength(int n)
{
    const char* CURpreSTR = getCONprefix(n);
    return strlen(CURpreSTR);
}

/** WIP ALERT */
int rendercommand_wip(int x, int y, int w)
{
    int width, height = 0;
    if( strlen(cmdline.buf) > 0 )
    {
        int ctx = -1;
        switch( cmdline.buf[0] )
        {
            case '>': ctx = 0; break;
            case '/': ctx = 1; break;
            case '%': ctx = 2; break;
            default: break;
        }
        defformatstring(s)("%s %s", getCONprefix(ctx), cmdline.buf+1);
        text_bounds(s, width, height, w);
        y -= height - FONTH;
        draw_text(s, x, y, 0xFF, 0xFF, 0xFF, 0xFF, cmdline.pos>=0 ? cmdline.pos/*+1*/+getCONlength(ctx) : (int)strlen(s), w);
    }
    return height;
}

// keymap is defined externally in keymap.cfg

vector<keym> keyms;

const char *keycmds[keym::NUMACTIONS] = { "bind", "specbind", "editbind" };
inline const char *keycmd(int type) { return type >= 0 && type < keym::NUMACTIONS ? keycmds[type] : ""; }

void keymap(int *code, char *key)
{
    keym &km = keyms.add();
    km.code = *code;
    km.name = newstring(key);
}

COMMAND(keymap, "is");

keym *findbind(const char *key)
{
    loopv(keyms) if(!strcasecmp(keyms[i].name, key)) return &keyms[i];
    return NULL;
}

keym *findbinda(const char *action, int type)
{
    loopv(keyms) if(!strcasecmp(keyms[i].actions[type], action)) return &keyms[i];
    return NULL;
}

keym *findbindc(int code)
{
    loopv(keyms) if(keyms[i].code==code) return &keyms[i];
    return NULL;
}

void findkey(int *code)
{
    for (int i = 0; i < keyms.length(); i++)
    {
        if(keyms[i].code==*code)
        {
            defformatstring(out)("%s", keyms[i].name);
            result(out);
            return;
        }
    }
    result("-255");
    return;
}

void findkeycode(const char* s)
{
     for (int i = 0; i < keyms.length(); i++)
     {
         if(strcmp(s, keyms[i].name) == 0)
         {
             defformatstring(out)("%i", keyms[i].code);
             result(out);
             return;
         }
     }
     result("-255");
     return;
}

COMMAND(findkey, "i");
COMMAND(findkeycode, "s");

keym *keypressed = NULL;
char *keyaction = NULL;

bool bindkey(keym *km, const char *action, int type)
{
    if(!km) return false;
    if(type < keym::ACTION_DEFAULT || type >= keym::NUMACTIONS) { conoutf("invalid bind type \"%i\"", type); return false; }
    if(!keypressed || keyaction!=km->actions[type]) delete[] km->actions[type];
    km->actions[type] = newstring(action);
    return true;
}

void bindk(const char *key, const char *action, int type)
{
    keym *km = findbind(key);
    if(!km) { conoutf("unknown key \"%s\"", key); return; }
    bindkey(km, action, type);
}

void keybind(const char *key, int type)
{
    keym *km = findbind(key);
    if(!km) { conoutf("unknown key \"%s\"", key); return; }
    if(type < keym::ACTION_DEFAULT || type >= keym::NUMACTIONS) { conoutf("invalid bind type \"%i\"", type); return; }
    result(km->actions[type]);
}

bool bindc(int code, const char *action, int type)
{
    keym *km = findbindc(code);
    if(km) return bindkey(km, action, type);
    else return false;
}

void searchbinds(const char *action, int type)
{
    if(!action || !action[0]) return;
    if(type < keym::ACTION_DEFAULT || type >= keym::NUMACTIONS) { conoutf("invalid bind type \"%i\"", type); return; }
    vector<char> names;
    loopv(keyms)
    {
        if(!strcmp(keyms[i].actions[type], action))
        {
            if(names.length()) names.add(' ');
            names.put(keyms[i].name, strlen(keyms[i].name));
        }
    }
    names.add('\0');
    result(names.getbuf());
}

COMMANDF(keybind, "s", (const char *key) { keybind(key, keym::ACTION_DEFAULT); } );
COMMANDF(keyspecbind, "s", (const char *key) { keybind(key, keym::ACTION_SPECTATOR); } );
COMMANDF(keyeditbind, "s", (const char *key) { keybind(key, keym::ACTION_EDITING); } );

COMMANDF(bind, "ss", (const char *key, const char *action) { bindk(key, action, keym::ACTION_DEFAULT); } );
COMMANDF(specbind, "ss", (const char *key, const char *action) { bindk(key, action, keym::ACTION_SPECTATOR); } );
COMMANDF(editbind, "ss", (const char *key, const char *action) { bindk(key, action, keym::ACTION_EDITING); } );

COMMANDF(searchbinds, "s", (const char *action) { searchbinds(action, keym::ACTION_DEFAULT); });
COMMANDF(searchspecbinds, "s", (const char *action) { searchbinds(action, keym::ACTION_SPECTATOR); });
COMMANDF(searcheditbinds, "s", (const char *action) { searchbinds(action, keym::ACTION_EDITING); });

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

COMMAND(onrelease, "s");

void saycommand(char *init)                         // turns input to the command line on or off
{
    SDL_EnableUNICODE(saycommandon = (init!=NULL));
    setscope(false);
    setburst(false);
    if(!editmode) keyrepeat(saycommandon);
    copystring(cmdline.buf, init ? init : ">"); // ALL cmdline.buf[0] ARE flag-chars ! ">" is for talk - the previous "no flag-char" item
    DELETEA(cmdaction);
    DELETEA(cmdprompt);
    cmdline.pos = -1;

    addmsg(SV_TYPING, "ri", saycommandon ? 1 : 0);
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

COMMAND(saycommand, "c");
COMMAND(inputcommand, "sss");
COMMAND(mapmsg, "s");
COMMAND(getmapmsg, "");

#if !defined(WIN32) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <SDL_syswm.h>
#endif

void pasteconsole(char *dst)
{
    #ifdef WIN32
    if(!IsClipboardFormatAvailable(CF_TEXT)) return;
    if(!OpenClipboard(NULL)) return;
    char *cb;
    do cb = (char *)GlobalLock(GetClipboardData(CF_TEXT));
    while(!cb);
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
    for(char *cbline = cb, *cbend; commandlen + 1 < MAXSTRLEN && cbline < &cb[cbsize]; cbline = cbend + 1)
    {
        cbend = (char *)memchr(cbline, '\0', &cb[cbsize] - cbline);
        if(!cbend) cbend = &cb[cbsize];
        if(commandlen + cbend - cbline + 1 > MAXSTRLEN) cbend = cbline + MAXSTRLEN - commandlen - 1;
        memcpy(&dst[commandlen], cbline, cbend - cbline);
        commandlen += cbend - cbline;
        dst[commandlen] = '\n';
        if(commandlen + 1 < MAXSTRLEN && cbend < &cb[cbsize]) ++commandlen;
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
        else if(buf[0]=='>') toserver(buf+1);
        else if(buf[0]=='%') toserver(buf);
        else toserver(buf); // execute(buf); // still default to simple "say".
        popscontext();
    }
};
vector<hline *> history;
int histpos = 0;

VARP(maxhistory, 0, 1000, 10000);

void history_(int *n)
{
    static bool inhistory = false;
    if(!inhistory && history.inrange(*n))
    {
        inhistory = true;
        history[history.length() - *n - 1]->run();
        inhistory = false;
    }
}

COMMANDN(history, history_, "i");

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
        int state = keym::ACTION_DEFAULT;
        if(editmode) state = keym::ACTION_EDITING;
        else if(player1->isspectating()) state = keym::ACTION_SPECTATOR;
        char *&action = k.actions[state][0] ? k.actions[state] : k.actions[keym::ACTION_DEFAULT];
        keyaction = action;
        keypressed = &k;
        execute(keyaction);
        keypressed = NULL;
        if(keyaction!=action) delete[] keyaction;
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

            case SDL_AC_BUTTON_WHEELUP:
            case SDLK_UP:
                if(histpos > history.length()) histpos = history.length();
                if(histpos > 0) history[--histpos]->restore();
                break;

            case SDL_AC_BUTTON_WHEELDOWN:
            case SDLK_DOWN:
                if(histpos + 1 < history.length()) history[++histpos]->restore();
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
        if(code==SDLK_RETURN || code==SDLK_KP_ENTER || code==SDL_AC_BUTTON_LEFT || code==SDL_AC_BUTTON_MIDDLE)
        {
            // make laptop users happy; LMB shall only work with history
            if(code == SDL_AC_BUTTON_LEFT && histpos == history.length()) return;

            hline *h = NULL;
            if(cmdline.buf[0])
            {
                if(history.empty() || history.last()->shouldsave())
                {
                    if(maxhistory && history.length() >= maxhistory)
                    {
                        loopi(history.length()-maxhistory+1) delete history[i];
                        history.remove(0, history.length()-maxhistory+1);
                    }
                    history.add(h = new hline)->save();
                }
                else h = history.last();
            }
            histpos = history.length();
            saycommand(NULL);
            if(h) h->run();
        }
        else if(code==SDLK_ESCAPE || code== SDL_AC_BUTTON_RIGHT)
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
    else if(!menukey(code, isdown, cooked, mod))                  // keystrokes go to menu
    {
        if(haskey) execbind(*haskey, isdown);
    }
    if(isdown && identexists("KEYPRESS")) // TODO: Remove this if its misued. e.x: /KEYPRESS = [ echo You pressed key code: $arg1 ] // Output: You pressed key code: 32 (if you press the spacebar)
    {
        defformatstring(kpi)("KEYPRESS %d", code);
        execute(kpi);
    }
    if (!isdown && identexists("KEYRELEASE"))
    {
        defformatstring(kpo)("KEYRELEASE %d", code);
        execute(kpo);
    }
}

char *getcurcommand()
{
    return saycommandon ? cmdline.buf : NULL;
}

void writebinds(stream *f)
{
    loopv(keyms)
    {
        keym *km = &keyms[i];
        loopj(3) if(*km->actions[j]) f->printf("%s \"%s\" [%s]\n", keycmd(j), km->name, km->actions[j]);
    }
}

