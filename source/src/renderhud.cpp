// renderhud.cpp: HUD rendering

#include "cube.h"

void drawicon(Texture *tex, float x, float y, float s, int col, int row, float ts)
{
    if(tex && tex->xs == tex->ys) quad(tex->id, x, y, s, ts*col, ts*row, ts);
}

inline void turn_on_transparency(int alpha = 255)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(255, 255, 255, alpha);
}

void drawequipicon(float x, float y, int col, int row, bool blend)
{
    static Texture *tex = NULL;
    if(!tex) tex = textureload("packages/misc/items.png", 4);
    if(tex)
    {
        turn_on_transparency();
        drawicon(tex, x, y, 120, col, row, 1/5.0f);
        glDisable(GL_BLEND);
    }
}

VARP(radarentsize, 4, 12, 64);

void drawradaricon(float x, float y, float s, int col, int row)
{
    static Texture *tex = NULL;
    if(!tex) tex = textureload("packages/misc/radaricons.png", 3);
    if(tex)
    {
        glEnable(GL_BLEND);
        drawicon(tex, x, y, s, col, row, 1/4.0f);
        glDisable(GL_BLEND);
    }
}

void drawctficon(float x, float y, float s, int col, int row, float ts, int alpha)
{
    static Texture *ctftex = NULL, *htftex = NULL, *ktftex = NULL;
    if(!ctftex) ctftex = textureload("packages/misc/ctficons.png", 3);
    if(!htftex) htftex = textureload("packages/misc/htficons.png", 3);
    if(!ktftex) ktftex = textureload("packages/misc/ktficons.png", 3);
    glColor4ub(255, 255, 255, alpha);
    if(m_hunt(gamemode))
    {
        if(htftex) drawicon(htftex, x, y, s, col, row, ts);
    }
    else if(m_keep(gamemode))
    {
        if(ktftex) drawicon(ktftex, x, y, s, col, row, ts);
    }
    else
    {
        if(ctftex) drawicon(ctftex, x, y, s, col, row, ts);
    }
}

void drawvoteicon(float x, float y, int col, int row, bool noblend)
{
    static Texture *tex = NULL;
    if(!tex) tex = textureload("packages/misc/voteicons.png", 3);
    if(tex)
    {
        if(noblend) glDisable(GL_BLEND);
        // else turn_on_transparency(); // if(transparency && !noblend)
        drawicon(tex, x, y, 240, col, row, 1/2.0f);
        if(noblend) glEnable(GL_BLEND);
    }
}

VARP(crosshairsize, 0, 15, 50);
VARP(showstats, 0, 1, 2);
VARP(crosshairfx, 0, 1, 1);
VARP(crosshairteamsign, 0, 1, 1);
VARP(hideradar, 0, 0, 1);
VARP(hidecompass, 0, 0, 1);
VARP(hideteam, 0, 0, 1);
VARP(hidectfhud, 0, 0, 1); // hardcore doesn't override
VARP(hidevote, 0, 0, 2);
VARP(hidehudmsgs, 0, 0, 1);
VARP(hidehudequipment, 0, 0, 1); // TODO hardcore
VARP(hideconsole, 0, 0, 1);
VARP(hideobits, 0, 0, 1);
VARP(hidespecthud, 0, 0, 1); // hardcore doesn't override
VARP(hidehardcore, 0, 2, 6);
VAR(showmap, 0, 0, 1);
#define show_hud_element(setting, hardcorelevel) (setting && (!m_real(gamemode, mutators) || hidehardcore < hardcorelevel))
#define hud_must_not_override(setting) setting // this is purely a decorator


//shotty::
/*
VAR(showsgpat, 0, 0, 1);

void drawsgpat(int w, int h)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glColor3ub(0, 0, 0);
    float sz = min(VIRTW, VIRTH),
    x1 = VIRTW/2 - sz/2,
    x2 = VIRTW/2 + sz/2,
    y1 = VIRTH/2 - sz/2,
    y2 = VIRTH/2 + sz/2,
    border = (512 - 64*2)/512.0f;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x1 + 0.5f*sz, y1 + 0.5f*sz);
    int rgbcv = 0;
    loopi(8+1)
    {
        // if((i%3)==0) { glColor3ub(rgbcv,rgbcv,rgbcv); rgbcv += 4; //rgbcv -= 255/(8+1); }
        if(i%2) glColor3ub(64,64,64); else glColor3ub(32,32,32);
        float c = 0.5f*(1 + border*cosf(i*2*M_PI/8.0f)), s = 0.5f*(1 + border*sinf(i*2*M_PI/8.0f));
        glVertex2f(x1 + c*sz, y1 + s*sz);
    }
    glColor3ub(255,255,255);
    glEnd();

    glDisable(GL_BLEND);

    rgbcv = 32;
    glBegin(GL_TRIANGLE_STRIP);
    loopi(8+1)
    {
        // if((i%3)==0) { glColor3ub(rgbcv,rgbcv,rgbcv); //,128); rgbcv += 8; //rgbcv -= 255/(8+1); }
        if(i%2) glColor3ub(16,16,16); else glColor3ub(32,32,32);
        float c = 0.5f*(1 + border*cosf(i*2*M_PI/8.0f)), s = 0.5f*(1 + border*sinf(i*2*M_PI/8.0f));
        glVertex2f(x1 + c*sz, y1 + s*sz);
        c = c < 0.4f ? 0 : (c > 0.6f ? 1 : 0.5f);
        s = s < 0.4f ? 0 : (s > 0.6f ? 1 : 0.5f);
        glVertex2f(x1 + c*sz, y1 + s*sz);
    }
    glColor3ub(255,255,255);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    static Texture *pattex = NULL;
    if(!pattex) pattex = textureload("packages/misc/sgpat.png", 4);
    loopk(3)
    {
        switch(k)
        {
            case 0:  glColor3ub(  32, 250, 250); break; // center
            case 1:  glColor3ub( 250,  64,  64); break; // middle
            case 2:  glColor3ub( 250, 250,  64); break; // outer
            default: glColor3ub( 255, 255, 255); break;
        }
        extern sgray pat[SGRAYS*3];
        int j = k * SGRAYS;
        loopi(SGRAYS)
        {
            if(pattex)
            {
                vec p = pat[j+i].rv;
                int ppx = VIRTW/2 + p.x*(sz/2);
                int ppy = VIRTH/2 + p.y*(sz/2);
                drawicon(pattex, ppx, ppy, 16, 1, 1, 1);
            }
        }
    }
    glEnable(GL_BLEND);
    /\*
     // 2011may31: dmg/hits output comes upon each shot, let the pattern be shown "pure"
     extern int lastsgs_hits;
     extern int lastsgs_dmgt;
     //draw_textf("H: %d DMG: %d", 8, 32, lastsgs_hits, lastsgs_dmgt);
     defformatstring(t2show4hitdmg)("H: %d DMG: %d", lastsgs_hits, lastsgs_dmgt);
     draw_text(t2show4hitdmg, VIRTW/2-text_width(t2show4hitdmg), VIRTH/2-3*FONTH/4);
     *\/
}
*/
//::shotty

void drawscope(bool preload)
{
    static Texture *scopetex = NULL;
    if(!scopetex) scopetex = textureload("packages/misc/scope.png", 3);
    if(preload) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, scopetex->id);
    glColor3ub(255, 255, 255);

    // figure out the bounds of the scope given the desired aspect ratio
    float sz = min(VIRTW, VIRTH),
          x1 = VIRTW/2 - sz/2,
          x2 = VIRTW/2 + sz/2,
          y1 = VIRTH/2 - sz/2,
          y2 = VIRTH/2 + sz/2,
          border = (512 - 64*2)/512.0f;

    // draw center viewport
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0.5f, 0.5f);
    glVertex2f(x1 + 0.5f*sz, y1 + 0.5f*sz);
    loopi(8+1)
    {
        float c = 0.5f*(1 + border*cosf(i*2*M_PI/8.0f)), s = 0.5f*(1 + border*sinf(i*2*M_PI/8.0f));
        glTexCoord2f(c, s);
        glVertex2f(x1 + c*sz, y1 + s*sz);
    }
    glEnd();

    glDisable(GL_BLEND);

    // draw outer scope
    glBegin(GL_TRIANGLE_STRIP);
    loopi(8+1)
    {
        float c = 0.5f*(1 + border*cosf(i*2*M_PI/8.0f)), s = 0.5f*(1 + border*sinf(i*2*M_PI/8.0f));
        glTexCoord2f(c, s);
        glVertex2f(x1 + c*sz, y1 + s*sz);
        c = c < 0.4f ? 0 : (c > 0.6f ? 1 : 0.5f);
        s = s < 0.4f ? 0 : (s > 0.6f ? 1 : 0.5f);
        glTexCoord2f(c, s);
        glVertex2f(x1 + c*sz, y1 + s*sz);
    }
    glEnd();

    // fill unused space with border texels
    if(x1 > 0 || x2 < VIRTW || y1 > 0 || y2 < VIRTH)
    {
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(0,  0);
        glTexCoord2f(0, 0); glVertex2f(x1, y1);
        glTexCoord2f(0, 1); glVertex2f(0,  VIRTH);
        glTexCoord2f(0, 1); glVertex2f(x1, y2);

        glTexCoord2f(1, 1); glVertex2f(VIRTW, VIRTH);
        glTexCoord2f(1, 1); glVertex2f(x2, y2);
        glTexCoord2f(1, 0); glVertex2f(VIRTW, 0);
        glTexCoord2f(1, 0); glVertex2f(x2, y1);

        glTexCoord2f(0, 0); glVertex2f(0,  0);
        glTexCoord2f(0, 0); glVertex2f(x1, y1);
        glEnd();
    }

    glEnable(GL_BLEND);
}

const char *crosshairnames[CROSSHAIR_NUM] = { "default", "scope", "shotgun", "v", "h", "hit", "reddot" };
Texture *crosshairs[CROSSHAIR_NUM] = { NULL }; // weapon specific crosshairs

Texture *loadcrosshairtexture(const char *c)
{
    defformatstring(p)("packages/crosshairs/%s", c);
    Texture *crosshair = textureload(p, 3);
    if(crosshair==notexture) crosshair = textureload("packages/crosshairs/default.png", 3);
    return crosshair;
}

void loadcrosshair(char *c, char *name)
{
    if (strcmp(name, "") == 0 || strcmp(name, "all") == 0)
    {
        for (int i = 0; i < CROSSHAIR_NUM; i++)
        {
            if (i == CROSSHAIR_SCOPE) continue;
            crosshairs[i] = loadcrosshairtexture(c);
        }
        return;
    }

    int n = -1;

    for (int i = 0; i < CROSSHAIR_NUM; i++)
    {
       if(strcmp(crosshairnames[i], name) == 0) { n = i; break; }
    }

    if (n < 0 || n >= CROSSHAIR_NUM) return;

    crosshairs[n] = loadcrosshairtexture(c);
}

COMMAND(loadcrosshair, "ss");

void drawcrosshair(playerent *p, int n, int teamtype)
{
    if (!show_hud_element(crosshairsize, 2) || intermission)
        return;

    Texture *crosshair = crosshairs[n];
    if(!crosshair)
    {
        crosshair = crosshairs[CROSSHAIR_DEFAULT];
        if(!crosshair) crosshair = crosshairs[CROSSHAIR_DEFAULT] = loadcrosshairtexture("default.png");
    }

    color col = color(1, 1, 1); // default: white
    if (teamtype)
    {
        if (teamtype == 1) col = color(0, 1, 0); // green
        else if (teamtype == 2) col = color(1, 0, 0); // red
    }
    else if (crosshairfx && !m_insta(gamemode, mutators))
    {
        if (p->health <= 50 * HEALTHSCALE) col = color(.5f, .25f, 0); // orange-red
        if (p->health <= 25 * HEALTHSCALE) col = color(.5f, .125f, 0); // red-orange
    }
    //if (n == CROSSHAIR_DEFAULT) col.alpha = 1.f - p->weaponsel->dynspread() / 1200.f;
    if (n != CROSSHAIR_SCOPE && p->zoomed) col.alpha = 1 - sqrtf(p->zoomed * (n == CROSSHAIR_SHOTGUN ? 0.5f : 1) * 1.6f);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(col.r, col.g, col.b, col.alpha * 0.8f);
    float chsize = (n == CROSSHAIR_SCOPE) ? 24.f : crosshairsize;
    if (n == CROSSHAIR_DEFAULT)
    {
        float clen = chsize * 3.6f;
        float cthickness = chsize * 2.f;
        chsize = p->weaponsel->dynspread() * 100 * (p->perk2 == PERK2_STEADY ? .65f : 1) / dynfov();
        //if (isthirdperson) chsize *= worldpos.dist(focus->o) / worldpos.dist(camera1->o);
        if (m_classic(gamemode, mutators)) chsize *= .6f;

        Texture *cv = crosshairs[CROSSHAIR_V], *ch = crosshairs[CROSSHAIR_H];
        if (!cv) cv = textureload("packages/crosshairs/vertical.png", 3);
        if (!ch) ch = textureload("packages/crosshairs/horizontal.png", 3);

        // horizontal
        glBindTexture(GL_TEXTURE_2D, ch->id);
        glBegin(GL_QUADS);
        // top
        glTexCoord2f(0, 0); glVertex2f(VIRTW / 2 - cthickness, VIRTH / 2 - chsize - clen);
        glTexCoord2f(1, 0); glVertex2f(VIRTW / 2 + cthickness, VIRTH / 2 - chsize - clen);
        glTexCoord2f(1, 1); glVertex2f(VIRTW / 2 + cthickness, VIRTH / 2 - chsize);
        glTexCoord2f(0, 1); glVertex2f(VIRTW / 2 - cthickness, VIRTH / 2 - chsize);
        // bottom
        glTexCoord2f(1, 1); glVertex2f(VIRTW / 2 - cthickness, VIRTH / 2 + chsize);
        glTexCoord2f(0, 1); glVertex2f(VIRTW / 2 + cthickness, VIRTH / 2 + chsize);
        glTexCoord2f(0, 0); glVertex2f(VIRTW / 2 + cthickness, VIRTH / 2 + chsize + clen);
        glTexCoord2f(1, 0); glVertex2f(VIRTW / 2 - cthickness, VIRTH / 2 + chsize + clen);
        glEnd();

        // vertical
        glBindTexture(GL_TEXTURE_2D, cv->id);
        glBegin(GL_QUADS);
        // left
        glTexCoord2f(0, 0); glVertex2f(VIRTW / 2 - chsize - clen, VIRTH / 2 - cthickness);
        glTexCoord2f(1, 0); glVertex2f(VIRTW / 2 - chsize, VIRTH / 2 - cthickness);
        glTexCoord2f(1, 1); glVertex2f(VIRTW / 2 - chsize, VIRTH / 2 + cthickness);
        glTexCoord2f(0, 1); glVertex2f(VIRTW / 2 - chsize - clen, VIRTH / 2 + cthickness);
        // right
        glTexCoord2f(1, 1); glVertex2f(VIRTW / 2 + chsize, VIRTH / 2 - cthickness);
        glTexCoord2f(0, 1); glVertex2f(VIRTW / 2 + chsize + clen, VIRTH / 2 - cthickness);
        glTexCoord2f(0, 0); glVertex2f(VIRTW / 2 + chsize + clen, VIRTH / 2 + cthickness);
        glTexCoord2f(1, 0); glVertex2f(VIRTW / 2 + chsize, VIRTH / 2 + cthickness);
        glEnd();
    }
    else
    {
        if (n == CROSSHAIR_SHOTGUN)
        {
            chsize = p->weaponsel->dynspread() * 100 * (p->perk2 == PERK2_STEADY ? .75f : 1) / dynfov();
            //if (isthirdperson) chsize *= worldpos.dist(focus->o) / worldpos.dist(camera1->o);
            if (m_classic(gamemode, mutators)) chsize *= .75f;
        }

        glBindTexture(GL_TEXTURE_2D, crosshair->id);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(VIRTW / 2 - chsize, VIRTH / 2 - chsize);
        glTexCoord2f(1, 0); glVertex2f(VIRTW / 2 + chsize, VIRTH / 2 - chsize);
        glTexCoord2f(0, 1); glVertex2f(VIRTW / 2 - chsize, VIRTH / 2 + chsize);
        glTexCoord2f(1, 1); glVertex2f(VIRTW / 2 + chsize, VIRTH / 2 + chsize);
        glEnd();
    }
}

VARP(hitmarkerfade, 0, 750, 5000);

void drawhitmarker()
{
    if (!show_hud_element(hitmarkerfade, 3) || !focus->lasthit || focus->lasthit + hitmarkerfade <= lastmillis)
        return;

    glColor4f(1, 1, 1, (focus->lasthit + hitmarkerfade - lastmillis) / 1000.f);
    Texture *ch = crosshairs[CROSSHAIR_HIT];
    if (!ch) ch = textureload("packages/crosshairs/hit.png", 3);
    if (ch->bpp == 32) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glBindTexture(GL_TEXTURE_2D, ch->id);
    glBegin(GL_TRIANGLE_STRIP);
    const float hitsize = 56.f;
    glTexCoord2f(0, 0); glVertex2f(VIRTW / 2 - hitsize, VIRTH / 2 - hitsize);
    glTexCoord2f(1, 0); glVertex2f(VIRTW / 2 + hitsize, VIRTH / 2 - hitsize);
    glTexCoord2f(0, 1); glVertex2f(VIRTW / 2 - hitsize, VIRTH / 2 + hitsize);
    glTexCoord2f(1, 1); glVertex2f(VIRTW / 2 + hitsize, VIRTH / 2 + hitsize);
    glEnd();
}

VARP(hidedamageindicator, 0, 0, 1);
VARP(damageindicatorsize, 0, 200, 10000);
VARP(damageindicatordist, 0, 500, 10000);
VARP(damageindicatortime, 1, 1000, 10000);
VARP(damageindicatoralpha, 1, 50, 100);
int damagedirections[4] = {0};

void updatedmgindicator(vec &attack)
{
    if(hidedamageindicator || !damageindicatorsize) return;
    float bestdist = 0.0f;
    int bestdir = -1;
    loopi(4)
    {
        vec d;
        d.x = (float)(cosf(RAD*(player1->yaw-90+(i*90))));
        d.y = (float)(sinf(RAD*(player1->yaw-90+(i*90))));
        d.z = 0.0f;
        d.add(player1->o);
        float dist = d.dist(attack);
        if(dist < bestdist || bestdir==-1)
        {
            bestdist = dist;
            bestdir = i;
        }
    }
    damagedirections[bestdir] = lastmillis+damageindicatortime;
}

void drawdmgindicator()
{
    if(!damageindicatorsize) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    float size = (float)damageindicatorsize;
    loopi(4)
    {
        if(!damagedirections[i] || damagedirections[i] < lastmillis) continue;
        float t = damageindicatorsize/(float)(damagedirections[i]-lastmillis);
        glPushMatrix();
        glColor4f(0.5f, 0.0f, 0.0f, damageindicatoralpha/100.0f);
        glTranslatef(VIRTW/2, VIRTH/2, 0);
        glRotatef(i*90, 0, 0, 1);
        glTranslatef(0, (float)-damageindicatordist, 0);
        glScalef(max(0.0f, 1.0f-t), max(0.0f, 1.0f-t), 0);

        glBegin(GL_TRIANGLES);
        glVertex3f(size/2.0f, size/2.0f, 0.0f);
        glVertex3f(-size/2.0f, size/2.0f, 0.0f);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glEnd();
        glPopMatrix();
    }
    glEnable(GL_TEXTURE_2D);
}

void drawequipicons(playerent *p)
{
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 0.2f+(sinf(lastmillis/100.0f)+1.0f)/2.0f);

    // health & armor
    if(p->armour) drawequipicon(560, 1650, (p->armour-1)/25, 2, false);
    drawequipicon(20, 1650, 2, 3, (p->state!=CS_DEAD && p->health<=20 && !m_sniper(gamemode, mutators)));
    if(p->mag[GUN_GRENADE]) drawequipicon(1520, 1650, 3, 1, false);

    // weapons
    int c = p->weaponsel->type != GUN_GRENADE ? p->weaponsel->type : p->prevweaponsel->type, r = 0;
    if(c==GUN_AKIMBO) c = GUN_PISTOL; // same icon for akimb & pistol
    if(c>3) { c -= 4; r = 1; }

    if(p->weaponsel && p->weaponsel->type>=GUN_KNIFE && p->weaponsel->type<NUMGUNS)
        drawequipicon(1020, 1650, c, r, (!p->weaponsel->mag && p->weaponsel->type != GUN_KNIFE && p->weaponsel->type != GUN_GRENADE));
    glEnable(GL_BLEND);
}

void drawradarent(float x, float y, float yaw, int col, int row, float iconsize, bool pulse, const char *label = NULL, ...)
{
    glPushMatrix();
    if(pulse) glColor4f(1.0f, 1.0f, 1.0f, 0.2f+(sinf(lastmillis/30.0f)+1.0f)/2.0f);
    else glColor4f(1, 1, 1, 1);
    glTranslatef(x, y, 0);
    glRotatef(yaw, 0, 0, 1);
    drawradaricon(-iconsize/2.0f, -iconsize/2.0f, iconsize, col, row);
    glPopMatrix();
    if(label && showmap)
    {
        glPushMatrix();
        glEnable(GL_BLEND);
        glTranslatef(iconsize/2, iconsize/2, 0);
        glScalef(1/2.0f, 1/2.0f, 1/2.0f);
        defvformatstring(lbl, label, label);
        draw_text(lbl, (int)(x*2), (int)(y*2));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
        glPopMatrix();
    }
}

struct hudline : cline
{
    int type;

    hudline() : type(HUDMSG_INFO) {}
};

struct hudmessages : consolebuffer<hudline>
{
    hudmessages() : consolebuffer<hudline>(20) {}

    void addline(const char *sf)
    {
        if(conlines.length() && conlines[0].type&HUDMSG_OVERWRITE)
        {
            conlines[0].millis = totalmillis;
            conlines[0].type = HUDMSG_INFO;
            copystring(conlines[0].line, sf);
        }
        else consolebuffer<hudline>::addline(sf, totalmillis);
    }
    void editline(int type, const char *sf)
    {
        if(conlines.length() && ((conlines[0].type&HUDMSG_TYPE)==(type&HUDMSG_TYPE) || conlines[0].type&HUDMSG_OVERWRITE))
        {
            conlines[0].millis = totalmillis;
            conlines[0].type = type;
            copystring(conlines[0].line, sf);
        }
        else consolebuffer<hudline>::addline(sf, totalmillis).type = type;
    }
    void render()
    {
        if(!conlines.length()) return;
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, VIRTW*0.9f, VIRTH*0.9f, 0, -1, 1);
        int dispmillis = arenaintermission ? 6000 : 3000;
        loopi(min(conlines.length(), 3)) if(totalmillis-conlines[i].millis<dispmillis)
        {
            cline &c = conlines[i];
            int tw = text_width(c.line);
            draw_text(c.line, int(tw > VIRTW*0.9f ? 0 : (VIRTW*0.9f-tw)/2), int(((VIRTH*0.9f)/4*3)+FONTH*i+pow((totalmillis-c.millis)/(float)dispmillis, 4)*VIRTH*0.9f/4.0f));
        }
        glPopMatrix();
    }
};

hudmessages hudmsgs;

void hudoutf(const char *s, ...)
{
    defvformatstring(sf, s, s);
    hudmsgs.addline(sf);
    conoutf("%s", sf);
}

void hudonlyf(const char *s, ...)
{
    defvformatstring(sf, s, s);
    hudmsgs.addline(sf);
}

void hudeditf(int type, const char *s, ...)
{
    defvformatstring(sf, s, s);
    hudmsgs.editline(type, sf);
}

bool insideradar(const vec &centerpos, float radius, const vec &o)
{
    if(showmap) return !o.reject(centerpos, radius);
    return o.distxy(centerpos)<=radius;
}

bool isattacking(playerent *p) { return lastmillis-p->lastaction < 500; }

vec getradarpos()
{
    float radarviewsize = VIRTH/6;
    float overlaysize = radarviewsize*4.0f/3.25f;
    return vec(VIRTW-10-VIRTH/28-overlaysize, 10+VIRTH/52, 0);
}

VARP(showmapbackdrop, 0, 0, 2);
VARP(showmapbackdroptransparency, 0, 75, 100);
VARP(radarheight, 5, 150, 500);
VAR(showradarvalues, 0, 0, 1); // DEBUG

void drawradar_showmap(playerent *p, int w, int h)
{
    float minimapviewsize = 3*min(VIRTW,VIRTH)/4; //minimap default size
    float halfviewsize = minimapviewsize/2.0f;
    float iconsize = radarentsize/0.2f;
    glColor3f(1.0f, 1.0f, 1.0f);
    glPushMatrix();
    extern GLuint minimaptex;
    vec centerpos(VIRTW/2 , VIRTH/2, 0.0f);
    if(showmapbackdrop)
    {
        glDisable(GL_TEXTURE_2D);
        if(showmapbackdrop==2) glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_COLOR);
        loopi(2)
        {
            int cg = i?(showmapbackdrop==2?((int)(255*(100-showmapbackdroptransparency)/100.0f)):0):(showmapbackdrop==2?((int)(255*(100-showmapbackdroptransparency)/100.0f)):64);
            int co = i?0:4;
            glColor3ub(cg, cg, cg);
            glBegin(GL_QUADS);
            glVertex2f( centerpos.x - halfviewsize - co, centerpos.y + halfviewsize + co);
            glVertex2f( centerpos.x + halfviewsize + co, centerpos.y + halfviewsize + co);
            glVertex2f( centerpos.x + halfviewsize + co, centerpos.y - halfviewsize - co);
            glVertex2f( centerpos.x - halfviewsize - co, centerpos.y - halfviewsize - co);
            glEnd();
        }
        glColor3ub(255,255,255);
        glEnable(GL_TEXTURE_2D);
    }
    glTranslatef(centerpos.x - halfviewsize, centerpos.y - halfviewsize , 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
    quad(minimaptex, 0, 0, minimapviewsize, 0.0f, 0.0f, 1.0f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);

    float gdim = max(mapdims[4], mapdims[5]); //no border
    float coordtrans = (minimapviewsize)/(gdim);

    float offd = fabs(float(mapdims[5])-float(mapdims[4])) /2.0f;
    if(!gdim) { gdim = ssize/2.0f; offd = 0; }
    float offx = gdim==mapdims[5] ? offd : 0;
    float offy = gdim==mapdims[4] ? offd : 0;

    vec mdd = vec(mapdims[0]-offx, mapdims[1]-offy, 0);
    vec cod(offx, offy, 0);
    vec ppv = vec(p->o).sub(mdd).mul(coordtrans);

    if(team_isactive(p->team)) drawradarent(ppv.x, ppv.y, p->yaw, p->state==CS_ALIVE ? (isattacking(p) ? 2 : 0) : 1, 1, iconsize, isattacking(p), "%s", colorname(p)); // local player
    loopv(players) // other players
    {
        playerent *pl = players[i];
        if(!pl || pl==p || !isteam(p, pl) || !team_isactive(pl->team)) continue;
        vec rtmp = vec(pl->o).sub(mdd).mul(coordtrans);
        drawradarent(rtmp.x, rtmp.y, pl->yaw, pl->state == CS_ALIVE ? (isattacking(pl) ? 2 : 0) : 1, isteam(p, pl) ? 2 : 0, iconsize, isattacking(pl), "%s", colorname(pl));
    }
    if(m_flags(gamemode))
    {
        glColor4f(1.0f, 1.0f, 1.0f, (sinf(lastmillis / 100.0f) + 1.0f) / 2.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        loopi(2) // flag items
        {
            flaginfo &f = flaginfos[i];
            entity *e = f.flagent;
            if(!e) continue;
            if(e->x == -1 && e-> y == -1) continue; // flagdummies
            vec pos = vec(e->x, e->y, 0).sub(mdd).mul(coordtrans);
            drawradarent(pos.x, pos.y, 0, m_keep(gamemode) ? 2 : f.team, 3, iconsize, false); // draw bases
            vec fltxoff = vec(8, -8, 0);
            vec cpos = vec(f.pos.x, f.pos.y, f.pos.z).sub(mdd).mul(coordtrans).add(fltxoff);
            if(f.state!=CTFF_STOLEN && !(m_keep(gamemode) && f.state == CTFF_IDLE))
            {
                float flgoff=fabs((radarentsize*2.1f)-8);
                drawradarent(cpos.x+flgoff, cpos.y-flgoff, 0, 3, m_keep(gamemode) ? 2 : f.team, iconsize, false); // draw on entity pos
            }
            if(m_keep(gamemode) && f.state == CTFF_IDLE) continue;
            if(f.state==CTFF_STOLEN)
            {
                float d2c = 1.6f * radarentsize/16.0f;
                vec apos(d2c, -d2c, 0);
                if(f.actor)
                {
                    apos.add(f.actor->o);
                    bool tm = i != team_base(p->team);
                    if(m_hunt(gamemode)) tm = !tm;
                    else if(m_keep(gamemode)) tm = true;
                    if(tm)
                    {
                        apos.sub(mdd).mul(coordtrans);
                        drawradarent(apos.x, apos.y, 0, 3, m_keep(gamemode) ? 2 : f.team, iconsize, true); // draw near flag thief
                    }
                }
            }
        }
    }
    glEnable(GL_BLEND);
    glPopMatrix();
}

void drawradar_vicinity(playerent *p, int w, int h)
{
    extern GLuint minimaptex;
    int gdim = max(mapdims[4], mapdims[5]);
    float radarviewsize = min(VIRTW,VIRTH)/5;
    float halfviewsize = radarviewsize/2.0f;
    float iconsize = radarentsize/0.4f;
    float scaleh = radarheight/(2.0f*gdim);
    float scaled = radarviewsize/float(radarheight);
    float offd = fabs((mapdims[5]-mapdims[4]) /2.0f);
    if(!gdim) { gdim = ssize/2; offd = 0; }
    float offx = gdim==mapdims[5]?offd:0;
    float offy = gdim==mapdims[4]?offd:0;
    vec rtr = vec(mapdims[0]-offx, mapdims[1]-offy, 0);
    vec rsd = vec(mapdims[0]+mapdims[4]/2, mapdims[1]+mapdims[5]/2, 0);
    float d2s = radarheight/2.0f*.8f;
    glColor3f(1.0f, 1.0f, 1.0f);
    glPushMatrix();
    vec centerpos(VIRTW-halfviewsize-72, halfviewsize+64, 0);
    glTranslatef(centerpos.x, centerpos.y, 0);
    glRotatef(-camera1->yaw, 0, 0, 1);
    glTranslatef(-halfviewsize, -halfviewsize, 0);
    vec d4rc = vec(p->o).sub(rsd).normalize().mul(0);
    vec usecenter = vec(p->o).sub(rtr).sub(d4rc);
    if(showradarvalues)
    {
        conoutf("vicinity @ gdim = %d | scaleh = %.2f", gdim, scaleh);
        conoutf("offd: %.2f [%.2f:%.2f]", offd, offx, offy);
        conoutf("RTR: %.2f %.2f", rtr.x, rtr.y);
        conoutf("RSD: %.2f %.2f", rsd.x, rsd.y);
        conoutf("P.O: %.2f %.2f", p->o.x, p->o.y);
        conoutf("U4C: %.2f %.2f | %.2f %.2f", usecenter.x, usecenter.y, usecenter.x/gdim, usecenter.y/gdim);
        //showradarvalues = 0;
    }
    glDisable(GL_BLEND);
    circle(minimaptex, halfviewsize, halfviewsize, halfviewsize, usecenter.x/(float)gdim, usecenter.y/(float)gdim, scaleh, 31); //Draw mimimaptext as radar background
    glTranslatef(halfviewsize, halfviewsize, 0);
    if(team_isactive(p->team)) drawradarent(0, 0, p->yaw, p->state==CS_ALIVE ? (isattacking(p) ? 2 : 0) : 1, 1, iconsize, isattacking(p), "%s", colorname(p)); // local player
    loopv(players) // other players
    {
        playerent *pl = players[i];
        if(!pl || pl==p || !isteam(p, pl) || !team_isactive(pl->team)) continue;
        vec rtmp = vec(pl->o).sub(p->o);
        if (rtmp.magnitude() > d2s)
            rtmp.normalize().mul(d2s);
        rtmp.mul(scaled);
        drawradarent(rtmp.x, rtmp.y, pl->yaw, pl->state==CS_ALIVE ? (isattacking(pl) ? 2 : 0) : 1, isteam(p, pl) ? 2 : 0, iconsize, isattacking(pl), "%s", colorname(pl));
    }
    if(m_flags(gamemode))
    {
        glColor4f(1.0f, 1.0f, 1.0f, (sinf(lastmillis / 100.0f) + 1.0f) / 2.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        float d2c = 1.6f * radarentsize/16.0f;
        loopi(2) // flag items
        {
            flaginfo &f = flaginfos[i];
            entity *e = f.flagent;
            if(!e) continue;
            if(e->x == -1 && e-> y == -1) continue; // flagdummies
            vec pos = vec(e->x, e->y, 0).sub(p->o);
            vec cpos = vec(f.pos.x, f.pos.y, f.pos.z).sub(p->o);
            //if(showradarvalues) { conoutf("dist2F[%d]: %.2f|%.2f || %.2f|%.2f", i, pos.x, pos.y, cpos.x, cpos.y); }
            if (pos.magnitude() > d2s)
                pos.normalize().mul(d2s);
            pos.mul(scaled);
            drawradarent(pos.x, pos.y, 0, m_keep(gamemode) ? 2 : f.team, 3, iconsize, false); // draw bases [circle doesn't need rotating]
            if(f.state!=CTFF_STOLEN && !(m_keep(gamemode) && f.state == CTFF_IDLE))
            {
                if (cpos.magnitude() > d2s)
                    cpos.normalize().mul(d2s);
                cpos.mul(scaled);
                float flgoff=radarentsize/0.68f;
                float ryaw=(camera1->yaw-45)*(2*PI/360);
                float offx=flgoff*cosf(-ryaw);
                float offy=flgoff*sinf(-ryaw);
                drawradarent(cpos.x+offx, cpos.y-offy, camera1->yaw, 3, m_keep(gamemode) ? 2 : f.team, iconsize, false); // draw flag on entity pos
            }
            if(m_keep(gamemode) && f.state == CTFF_IDLE) continue;
            if(f.state==CTFF_STOLEN)
            {
                vec apos(d2c, -d2c, 0);
                if(f.actor)
                {
                    apos.add(f.actor->o);
                    bool tm = i != team_base(p->team);
                    if(m_hunt(gamemode)) tm = !tm;
                    else if(m_keep(gamemode)) tm = true;
                    if(tm)
                    {
                        apos.sub(p->o);
                        if (apos.magnitude() > d2s)
                            apos.normalize().mul(d2s);
                        apos.mul(scaled);
                        drawradarent(apos.x, apos.y, camera1->yaw, 3, m_keep(gamemode) ? 2 : f.team, iconsize, true); // draw near flag thief
                    }
                }
            }
        }
    }
    showradarvalues = 0; // DEBUG - also see two bits commented-out above
    glEnable(GL_BLEND);
    glPopMatrix();
    // eye candy:
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor3f(1, 1, 1);
    static Texture *bordertex = NULL;
    if(!bordertex) bordertex = textureload("packages/misc/compass-base.png", 3);
    quad(bordertex->id, centerpos.x-halfviewsize-16, centerpos.y-halfviewsize-16, radarviewsize+32, 0, 0, 1, 1);
    if (show_hud_element(!hidecompass, 5))
    {
        static Texture *compasstex = NULL;
        if(!compasstex) compasstex = textureload("packages/misc/compass-rose.png", 3);
        glPushMatrix();
        glTranslatef(centerpos.x, centerpos.y, 0);
        glRotatef(-camera1->yaw, 0, 0, 1);
        quad(compasstex->id, -halfviewsize-8, -halfviewsize-8, radarviewsize+16, 0, 0, 1, 1);
        glPopMatrix();
    }

}

void drawradar(playerent *p, int w, int h)
{
    if(showmap) drawradar_showmap(p,w,h);
    else drawradar_vicinity(p,w,h);
}

void drawteamicons(int w, int h)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor3f(1, 1, 1);
    static Texture *icons = NULL;
    if(!icons) icons = textureload("packages/misc/teamicons.png", 3);
    quad(icons->id, VIRTW-VIRTH/12-10, 10, VIRTH/12, team_base(focus->team) ? 0.5f : 0, 0, 0.49f, 1.0f);
}

int damageblendmillis = 0;

VARFP(damagescreen, 0, 1, 1, { if(!damagescreen) damageblendmillis = 0; });
VARP(damagescreenfactor, 1, 7, 100);
VARP(damagescreenalpha, 1, 45, 100);
VARP(damagescreenfade, 0, 125, 1000);

void damageblend(int n)
{
    if(!damagescreen) return;
    if(lastmillis > damageblendmillis) damageblendmillis = lastmillis;
    damageblendmillis += n*damagescreenfactor;
}

string enginestateinfo = "";
void CSgetEngineState() { result(enginestateinfo); }
COMMANDN(getEngineState, CSgetEngineState, "");

VARP(clockdisplay,0,0,2);
VARP(dbgpos,0,0,1);
VARP(showtargetname,0,1,1);
VARP(showspeed, 0, 0, 1);
VARP(monitors, 1, 1, 12);

static char lastseen [20];
void lasttarget() { result(lastseen); }
COMMAND(lasttarget, "");

inline int mm_adjust(int x)
{
    return ((monitors + (((x << 1) - 1) << ((monitors & 1) ^ 1))) * VIRTW / monitors) >> 1;
    /*
    // original
    if(monitors & 1) return (2*x + monitors - 1)*VIRTW/(2*monitors);
    else return (4*x + monitors - 2)*VIRTW/(2*monitors);
    */
}

int votersort(playerent **a, playerent **b)
{
    return (*a)->voternum - (*b)->voternum;
}

void gl_drawhud(int w, int h, int curfps, int nquads, int curvert, bool underwater)
{
    playerent *p = camera1->type<ENT_CAMERA ? (playerent *)camera1 : player1;
    bool spectating = player1->isspectating();
    int origVIRTW = VIRTW;

    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, VIRTW, VIRTH, 0, -1, 1);
    glTranslatef((float)VIRTW*(monitors - 2 + (monitors&1))/(2.*monitors), 0., 0.);
    VIRTW /= (float)monitors/(float)(2 - (monitors & 1));
    glEnable(GL_BLEND);

    if(underwater)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4ub(hdr.watercolor[0], hdr.watercolor[1], hdr.watercolor[2], 102);

        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(0, 0);
        glVertex2f(VIRTW, 0);
        glVertex2f(0, VIRTH);
        glVertex2f(VIRTW, VIRTH);
        glEnd();
    }

    if(lastmillis < damageblendmillis)
    {
        static Texture *damagetex = NULL;
        if(!damagetex) damagetex = textureload("packages/misc/damage.png", 3);

        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, damagetex->id);
        float fade = damagescreenalpha/100.0f;
        if(damageblendmillis - lastmillis < damagescreenfade)
            fade *= float(damageblendmillis - lastmillis)/damagescreenfade;
        glColor4f(fade, fade, fade, fade);

        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(0, 0);
        glTexCoord2f(1, 0); glVertex2f(VIRTW, 0);
        glTexCoord2f(0, 1); glVertex2f(0, VIRTH);
        glTexCoord2f(1, 1); glVertex2f(VIRTW, VIRTH);
        glEnd();
    }

    glEnable(GL_TEXTURE_2D);

    if (worldhit) strcpy(lastseen, worldhit->name);
    bool menu = menuvisible();
    bool command = getcurcommand() ? true : false;
    bool reloading = lastmillis < p->weaponsel->reloading + p->weaponsel->info.reloadtime;
    if(p->state != CS_DEAD && !reloading)
    {
        const int teamtype = worldhit && worldhit->state == CS_ALIVE ? isteam(worldhit, p) ? 1 : 2 : 0;
        p->weaponsel->renderaimhelp(teamtype);
    }

    drawhitmarker();

    drawdmgindicator();

    if(p->state==CS_ALIVE && !hidehudequipment) drawequipicons(p);

    if (/*!menu &&*/ (show_hud_element(!hideradar, 5) || showmap)) drawradar(p, w, h);
    //if(showsgpat) drawsgpat(w,h); // shotty
    if(!editmode)
    {
        glMatrixMode(GL_MODELVIEW);
        if (show_hud_element(!hideteam, 1) && m_team(gamemode, mutators)) drawteamicons(w, h);
        glMatrixMode(GL_PROJECTION);
    }

    char *infostr = editinfo();
    int commandh = 1570 + FONTH;
    if(command) commandh -= rendercommand(20, 1570, VIRTW);
    else if(infostr) draw_text(infostr, 20, 1570);
    else // if(show_hud_element(true, 1))
    {
        defformatstring(hudtext)("\f0[\f1%04.1f\f3m\f0]", focus->o.dist(worldhitpos) / 4.f);
        static string hudtarget;
        static int lasttarget = INT_MIN;
        if(worldhit)
        {
            formatstring(hudtarget)(" \f2[\f%d%s\f2] \f4[\f%s\f4]", team_rel_color(focus, worldhit), colorname(worldhit),
                worldhitzone==HIT_HEAD?"3HEAD":worldhitzone==HIT_TORSO?"2TORSO":"0LEGS");
            concatstring(hudtext, hudtarget);
            lasttarget = lastmillis;
        }
        else if(lastmillis - lasttarget < 800)
        {
            const short a = (800 - lastmillis + lasttarget) * 255 / 800;
            draw_text(hudtarget, 20 + text_width(hudtext), 1570, a, a, a, a);
        }
        draw_text(hudtext, 20, 1570);
    }

    extern int lastexpadd, lastexptexttime;
    if (lastmillis <= lastexpadd + COMBOTIME)
    {
        extern int lastexpaddamt;
        defformatstring(scoreaddtxt)("\f%c%+d", !lastexpaddamt ? '4' : lastexpaddamt >= 0 ? '2' : '3', lastexpaddamt);
        const short a = (lastexpadd + COMBOTIME - lastmillis) * 255 / COMBOTIME;
        draw_text(scoreaddtxt, VIRTW * 11 / 20, VIRTH * 8 / 20, a, a, a, a);
    }

    if (lastmillis <= lastexptexttime + COMBOTIME)
    {
        extern string lastexptext;
        const short a = (lastexptexttime + COMBOTIME - lastmillis) * 255 / COMBOTIME;
        draw_text(lastexptext, VIRTW * 11 / 20, VIRTH * 8 / 20 + FONTH, a, a, a, a);
    }

    glLoadIdentity();
    glOrtho(0, origVIRTW*2, VIRTH*2, 0, -1, 1);
    glTranslatef((float)origVIRTW*(float)((float)monitors - 2. + (float)(monitors&1))/((float)monitors), 0., 0.);
    extern int tsens(int x);
    tsens(-2000);
    extern void r_accuracy(int h);
    if (!spectating) r_accuracy(commandh);
    if (hud_must_not_override(!hideconsole)) renderconsole();
    if (show_hud_element(!hideobits, 6)) renderobits();
    formatstring(enginestateinfo)("%d %d %d %d %d", curfps, lod_factor(), nquads, curvert, xtraverts);
    if(showstats)
    {
        if(showstats==2)
        {
            const int left = (VIRTW-225-10)*2, top = (VIRTH*7/8)*2;
            const int ttll = VIRTW*2 - 3*FONTH/2;
            blendbox(left - 24, top - 24, VIRTW*2 - 72, VIRTH*2 - 48, true, -1);
            int c_num;
            int c_r = 255;      int c_g = 255;      int c_b = 255;
            string c_val;
    #define TXTCOLRGB \
            switch(c_num) \
            { \
                case 0: c_r = 120; c_g = 240; c_b = 120; break; \
                case 1: c_r = 120; c_g = 120; c_b = 240; break; \
                case 2: c_r = 230; c_g = 230; c_b = 110; break; \
                case 3: c_r = 250; c_g = 100; c_b = 100; break; \
                default: \
                    c_r = 255; c_g = 255; c_b =  64; \
                break; \
            }

            draw_text("fps", left - (text_width("fps") + FONTH/2), top    );
            draw_text("lod", left - (text_width("lod") + FONTH/2), top+ 80);
            draw_text("wqd", left - (text_width("wqd") + FONTH/2), top+160);
            draw_text("wvt", left - (text_width("wvt") + FONTH/2), top+240);
            draw_text("evt", left - (text_width("evt") + FONTH/2), top+320);

            //ttll -= 3*FONTH/2;

            formatstring(c_val)("%d", curfps);
            c_num = curfps > 150 ? 0 : (curfps > 100 ? 1 : (curfps > 30 ? 2 : 3)); TXTCOLRGB
            draw_text(c_val, ttll - text_width(c_val), top    , c_r, c_g, c_b);

            int lf = lod_factor();
            formatstring(c_val)("%d", lf);
            c_num = lf>199?(lf>299?(lf>399?3:2):1):0; TXTCOLRGB
            draw_text(c_val, ttll - text_width(c_val), top+ 80, c_r, c_g, c_b);

            formatstring(c_val)("%d", nquads);
            c_num = nquads>3999?(nquads>5999?(nquads>7999?3:2):1):0; TXTCOLRGB
            draw_text(c_val, ttll - text_width(c_val), top+160, c_r, c_g, c_b);

            formatstring(c_val)("%d", curvert);
            c_num = curvert>3999?(curvert>5999?(curvert>7999?3:2):1):0; TXTCOLRGB
            draw_text(c_val, ttll - text_width(c_val), top+240, c_r, c_g, c_b);

            formatstring(c_val)("%d", xtraverts);
            c_num = xtraverts>3999?(xtraverts>5999?(xtraverts>7999?3:2):1):0; TXTCOLRGB
            draw_text(c_val, ttll - text_width(c_val), top+320, c_r, c_g, c_b);
        }
        else
        {
            if(dbgpos)
            {
                pushfont("mono");
                defformatstring(o_yw)("%05.2f YAW", player1->yaw);
                draw_text(o_yw, VIRTW*2 - ( text_width(o_yw) + FONTH ), VIRTH*2 - 15*FONTH/2);
                defformatstring(o_p)("%05.2f PIT", player1->pitch);
                draw_text(o_p, VIRTW*2 - ( text_width(o_p) + FONTH ), VIRTH*2 - 13*FONTH/2);
                defformatstring(o_x)("%05.2f X  ", player1->o.x);
                draw_text(o_x, VIRTW*2 - ( text_width(o_x) + FONTH ), VIRTH*2 - 11*FONTH/2);
                defformatstring(o_y)("%05.2f Y  ", player1->o.y);
                draw_text(o_y, VIRTW*2 - ( text_width(o_y) + FONTH ), VIRTH*2 - 9*FONTH/2);
                defformatstring(o_z)("%05.2f Z  ", player1->o.z);
                draw_text(o_z, VIRTW*2 - ( text_width(o_z) + FONTH ), VIRTH*2 - 7*FONTH/2);
                popfont();
            }
            defformatstring(c_val)("fps %d", curfps);
            draw_text(c_val, VIRTW*2 - ( text_width(c_val) + FONTH ), VIRTH*2 - 3*FONTH/2);
        }
    }
    if(!intermission && clockdisplay!=0 && lastgametimeupdate!=0)
    {
        string gtime;
        int cssec = (gametimecurrent+(lastmillis-lastgametimeupdate))/1000;
        int gtsec = cssec%60;
        int gtmin = cssec/60;
        if(clockdisplay==1)
        {
            int gtmax = gametimemaximum/60000;
            gtmin = gtmax - gtmin;
            if(gtsec!=0)
            {
                gtmin -= 1;
                gtsec = 60 - gtsec;
            }
        }
        formatstring(gtime)("%02d:%02d", gtmin, gtsec);
        draw_text(gtime, (2*VIRTW - text_width(gtime))/2, 2);
    }

    if (hud_must_not_override(hidevote < 2))
    {
        extern votedisplayinfo *curvote;

        if (curvote && curvote->millis >= totalmillis && !(hud_must_not_override(hidevote == 1) && player1->vote != VOTE_NEUTRAL && curvote->result == VOTE_NEUTRAL))
        {
            int left = 20*2, top = VIRTH;
            if (curvote->result == VOTE_NEUTRAL)
                draw_textf("%s called a vote: %.2f seconds remaining", left, top + 240, curvote->owner ? colorname(curvote->owner) : "(unknown)", (curvote->expiremillis - lastmillis) / 1000.0f);
            else
                draw_textf("%s called a vote:", left, top+240, curvote->owner ? colorname(curvote->owner) : "(unknown)");
            draw_textf("%s", left, top+320, curvote->desc);
            draw_textf("----", left, top+400);

            vector<playerent *> votepl[VOTE_NUM];
            string votestr[VOTE_NUM];
            if (!watchingdemo) votepl[player1->vote].add(player1);
            loopv(players)
            {
                playerent *vpl = players[i];
                if (!vpl || vpl->ownernum >= 0) continue;
                votepl[vpl->vote].add(vpl);
            }
            loopl(VOTE_NUM)
            {
                copystring(votestr[l], "");
                if (!votepl[l].length()) continue;
                // special case: hide if too many are neutral
                if (l == VOTE_NEUTRAL && votepl[VOTE_NEUTRAL].length() > 5) continue;
                votepl[l].sort(votersort);
                loopv(votepl[l])
                {
                    playerent *vpl = votepl[l][i];
                    if (!vpl) continue;
                    concatformatstring(votestr[l], "\f%d%s \f6(%d)", vpl->clientrole ? 0 : vpl == player1 ? 6 : team_color(vpl->team), vpl->name, vpl->clientnum);
                    if (vpl->clientrole >= CR_ADMIN) concatstring(votestr[l], " \f8(!)");
                    concatstring(votestr[l], "\f5, ");
                }
                // trim off last space, comma, 5, and line feed
                votestr[l][strlen(votestr[l]) - 4] = '\0';
                //copystring(votestr[l], votestr[l], strlen(votestr[l])-1);
            }
            draw_textf("\fs\f%c%d yes\fr vs. \fs\f%c%d no\fr", left, top + 480,
                curvote->expiryresult == VOTE_YES ? '0' : '5',
                votepl[VOTE_YES].length(),
                curvote->expiryresult == VOTE_NO ? '3' : '5',
                votepl[VOTE_NO].length());

            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glColor4f(1.0f, 1.0f, 1.0f, (sinf(lastmillis/100.0f)+1.0f) / 2.0f);
            switch(curvote->result)
            {
                case VOTE_NEUTRAL:
                    drawvoteicon(left, top, 0, 0, true);
                    if (player1->vote == VOTE_NEUTRAL)
                        draw_textf("\f3please vote yes or no (F1/F2)", left, top+560);
                    else
                        draw_textf("\f2you voted \f%s \f1(F%d to change)", left, top + 560, player1->vote == VOTE_NO ? "3no" : "0yes", player1->vote == VOTE_NO ? 1 : 2);
                    break;
                default:
                    drawvoteicon(left, top, (curvote->result-1)&1, 1, false);
                    draw_textf("\f%s \f%s", left, top+560, curvote->veto ? "1VETO" : "2vote", curvote->result == VOTE_YES ? "0PASSED" : "3FAILED");
                    break;
            }
            glLoadIdentity();
            glOrtho(0, VIRTW*2.2, VIRTH*2.2, 0, -1, 1);
            left *= 1.1; top += 560; top *= 1.1;
            if (*votestr[VOTE_YES])
            {
                draw_textf("\f1Vote \f0Yes \f5(\f4%d/%d\f5)", left, top += 88, votepl[VOTE_YES].length(), curvote->yes_remain);
                draw_text(votestr[VOTE_YES], left, top += 88);
            }
            if (*votestr[VOTE_NO])
            {
                draw_textf("\f1Vote \f3No \f5(\f4%d/%d\f5)", left, top += 88, votepl[VOTE_NO].length(), curvote->no_remain);
                draw_text(votestr[VOTE_NO], left, top += 88);
            }
            if (*votestr[VOTE_NEUTRAL])
            {
                draw_textf("\f1Vote \f2Neutral \f5(\f4%d\f5)", left, top += 88, votepl[VOTE_NEUTRAL].length());
                draw_text(votestr[VOTE_NEUTRAL], left, top += 88);
            }
        }
    }
    //else draw_textf("%c%d here F1/F2 will be praised during a vote", 20*2, VIRTH+560, '\f', 0); // see position (left/top) setting in block above

    if(menu) rendermenu();
    else if(command) renderdoc(40, VIRTH, max(commandh*2 - VIRTH, 0));

    if (hud_must_not_override(!hidehudmsgs)) hudmsgs.render();

    if(!hidespecthud && !menu && p->state==CS_DEAD && p->spectatemode<=SM_DEATHCAM)
    {
        glLoadIdentity();
        glOrtho(0, origVIRTW*3/2, VIRTH*3/2, 0, -1, 1);
        glTranslatef((float)origVIRTW*3*(monitors - 2 + (monitors&1))/(4.*monitors), 0., 0.);
        const int left = (VIRTW*3/2)*6/8, top = (VIRTH*3/2)*3/4;
        draw_textf("SPACE to change view", left, top);
        draw_textf("SCROLL to change player", left, top+80);
    }

    /* * /
    glLoadIdentity();
    glOrtho(0, VIRTW*3/2, VIRTH*3/2, 0, -1, 1);
    const int tbMSGleft = (VIRTW*3/2)*5/6;
    const int tbMSGtop = (VIRTH*3/2)*7/8;
    draw_textf("!TEST BUILD!", tbMSGleft, tbMSGtop);
    / * */

    if(showspeed)
    {
        glLoadIdentity();
        glPushMatrix();
        glOrtho(0, origVIRTW, VIRTH, 0, -1, 1);
        glTranslatef((float)origVIRTW*(monitors - 2 + (monitors&1))/(2.*monitors), 0., 0.);
        glScalef(0.8, 0.8, 1);
        draw_textf("Speed: %.2f", VIRTW/2, VIRTH, p->vel.magnitudexy());
        glPopMatrix();
    }

    if(!hidespecthud && spectating && player1->spectatemode!=SM_DEATHCAM)
    {
        glLoadIdentity();
        glOrtho(0, origVIRTW, VIRTH, 0, -1, 1);
        glTranslatef((float)origVIRTW*(monitors - 2 + (monitors&1))/(2.*monitors), 0., 0.);
        const char *specttext = "GHOST";
        if(player1->team == TEAM_SPECT) specttext = "GHOST";
        else if(player1->team == TEAM_CLA_SPECT) specttext = "[CLA]";
        else if(player1->team == TEAM_RVSF_SPECT) specttext = "[RVSF]";
        draw_text(specttext, VIRTW/40, VIRTH/10*7);
        if(focus != player1)
        {
            defformatstring(name)("Player %s", focus->name);
            draw_text(name, VIRTW/40, VIRTH/10*8);
        }
    }

    if(p->state==CS_ALIVE)
    {
        glLoadIdentity();
        glOrtho(0, origVIRTW/2, VIRTH/2, 0, -1, 1);
        glTranslatef((float)origVIRTW*(monitors - 2 + (monitors&1))/(4.*monitors), 0., 0.);

        if(!hidehudequipment)
        {
            pushfont("huddigits");
            draw_textf("%d",  90, 823, p->health/HEALTHSCALE);
            if(p->armour) draw_textf("%d", 360, 823, p->armour);
            if(p->weaponsel && p->weaponsel->type>=GUN_KNIFE && p->weaponsel->type<NUMGUNS)
            {
                glMatrixMode(GL_MODELVIEW);
                if (p->weaponsel->type!=GUN_GRENADE) p->weaponsel->renderstats();
                else p->prevweaponsel->renderstats();
                if(p->mag[GUN_GRENADE]) p->weapons[GUN_GRENADE]->renderstats();
                glMatrixMode(GL_PROJECTION);
            }
            popfont();
        }


        if(m_flags(gamemode) && !hidectfhud)
        {
            glLoadIdentity();
            glOrtho(0, origVIRTW, VIRTH, 0, -1, 1);
            glTranslatef((float)origVIRTW*(monitors - 2 + (monitors&1))/(2.*monitors), 0., 0.);
            glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
            turn_on_transparency(255);
            int flagscores[2];
            teamflagscores(flagscores[0], flagscores[1]);

            loopi(2) // flag state
            {
                drawctficon(i*120+VIRTW/4.0f*3.0f, 1650, 120, i, 0, 1/4.0f, flaginfos[i].state == CTFF_INBASE ? 255 : 100);
                if(m_team(gamemode, mutators))
                {
                    defformatstring(count)("%d", flagscores[i]);
                    int cw, ch;
                    text_bounds(count, cw, ch);
                    draw_textf(count, i*120+VIRTW/4.0f*3.0f+60-cw/2, 1590);
                }
            }

            // big flag-stolen icon
            int ft = 0;
            if((flaginfos[0].state==CTFF_STOLEN && flaginfos[0].actor == p) ||
               (flaginfos[1].state==CTFF_STOLEN && flaginfos[1].actor == p && ++ft))
            {
                drawctficon(VIRTW-225-10, VIRTH*5/8, 225, ft, 1, 1/2.0f, (sinf(lastmillis/100.0f)+1.0f) *128);
            }
        }
    }

    VIRTW = origVIRTW;

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_MODELVIEW);
}

void loadingscreen(const char *fmt, ...)
{
    static Texture *logo = NULL;
    if(!logo) logo = textureload("packages/misc/startscreen.png", 3);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, VIRTW, VIRTH, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0, 0, 0, 1);
    glColor3f(1, 1, 1);

    loopi(fmt ? 1 : 2)
    {
        glClear(GL_COLOR_BUFFER_BIT);
        quad(logo->id, (VIRTW-VIRTH)/2, 0, VIRTH, 0, 0, 1);
        if(fmt)
        {
            glEnable(GL_BLEND);
            defvformatstring(str, fmt, fmt);
            int w = text_width(str);
            draw_text(str, w>=VIRTW ? 0 : (VIRTW-w)/2, VIRTH*3/4);
            glDisable(GL_BLEND);
        }
        SDL_GL_SwapBuffers();
    }

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
}

static void bar(float bar, int o, float r, float g, float b)
{
    int side = 2*FONTH;
    float x1 = side, x2 = bar*(VIRTW*1.2f-2*side)+side;
    float y1 = o*FONTH;
    glColor3f(0.3f, 0.3f, 0.3f);
    glBegin(GL_TRIANGLE_STRIP);
    loopk(10)
    {
       float c = 1.2f*cosf(M_PI/2 + k/9.0f*M_PI), s = 1 + 1.2f*sinf(M_PI/2 + k/9.0f*M_PI);
       glVertex2f(x2 - c*FONTH, y1 + s*FONTH);
       glVertex2f(x1 + c*FONTH, y1 + s*FONTH);
    }
    glEnd();

    glColor3f(r, g, b);
    glBegin(GL_TRIANGLE_STRIP);
    loopk(10)
    {
       float c = cosf(M_PI/2 + k/9.0f*M_PI), s = 1 + sinf(M_PI/2 + k/9.0f*M_PI);
       glVertex2f(x2 - c*FONTH, y1 + s*FONTH);
       glVertex2f(x1 + c*FONTH, y1 + s*FONTH);
    }
    glEnd();
}

void show_out_of_renderloop_progress(float bar1, const char *text1, float bar2, const char *text2)   // also used during loading
{
    c2skeepalive();

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, VIRTW*1.2f, VIRTH*1.2f, 0, -1, 1);

    glLineWidth(3);

    if(text1)
    {
        bar(1, 1, 0.1f, 0.1f, 0.1f);
        if(bar1>0) bar(bar1, 1, 0.2f, 0.2f, 0.2f);
    }

    if(bar2>0)
    {
        bar(1, 3, 0.1f, 0.1f, 0.1f);
        bar(bar2, 3, 0.2f, 0.2f, 0.2f);
    }

    glLineWidth(1);

    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    if(text1) draw_text(text1, 2*FONTH, 1*FONTH + FONTH/2);
    if(bar2>0) draw_text(text2, 2*FONTH, 3*FONTH + FONTH/2);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glEnable(GL_DEPTH_TEST);
    SDL_GL_SwapBuffers();
}

