// weapon.cpp: all shooting and effects code

#include "cube.h"
#include "bot/bot.h"
#include "hudgun.h"

VARP(autoreload, 0, 1, 1);
VARP(akimboautoswitch, 0, 1, 1);
VARP(akimboendaction, 0, 3, 3); // 0: switch to knife, 1: stay with pistol (if has ammo), 2: switch to grenade (if possible), 3: switch to primary (if has ammo) - all fallback to previous one w/o ammo for target

vec sg[SGRAYS];

int burstshotssettings[NUMGUNS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

void updatelastaction(playerent *d, int millis = lastmillis)
{
    loopi(NUMGUNS) d->weapons[i]->updatetimers(millis);
    d->lastaction = millis;
}

inline void checkweaponswitch_(playerent *p)
{
    if(!p->weaponchanging) return;
    int timeprogress = lastmillis-p->weaponchanging;
    if (timeprogress>SWITCHTIME(p->perk1 == PERK_TIME))
    {
        p->weaponchanging = 0;
    }
    else if (timeprogress>(SWITCHTIME(p->perk1 == PERK_TIME) >> 1) && p->weaponsel != p->nextweaponsel)
    {
        p->prevweaponsel = p->weaponsel;
        p->weaponsel = p->nextweaponsel;
    }
}

void checkweaponswitch()
{
    checkweaponswitch_(player1);
    loopv(players) if (players[i]) checkweaponswitch_(players[i]);
}

void selectweapon(weapon *w)
{
    if(!w || !player1->weaponsel->deselectable()) return;
    if(w->selectable())
    {
        int i = w->type;
        // substitute akimbo
        weapon *akimbo = player1->weapons[GUN_AKIMBO];
        if(w->type==GUN_PISTOL && akimbo->selectable()) w = akimbo;

        player1->weaponswitch(w);
        if(identexists("onWeaponSwitch"))
        {
            string o;
            formatstring(o)("onWeaponSwitch %d", i);
            execute(o);
        }
    }
}

void requestweapon(int *w)
{
    if(keypressed && player1->state == CS_ALIVE && *w >= 0 && *w < NUMGUNS )
    {
        if (player1->akimbo && *w==GUN_PISTOL) *w = GUN_AKIMBO;
        selectweapon(player1->weapons[*w]);
    }
}

void shiftweapon(int *s)
{
    if(keypressed && player1->state == CS_ALIVE)
    {
        if(!player1->weaponsel->deselectable()) return;

        weapon *curweapon = player1->weaponsel;
        weapon *akimbo = player1->weapons[GUN_AKIMBO];

        // collect available weapons
        vector<weapon *> availweapons;
        const int weap_check_order[NUMGUNS] =
        {
            GUN_AKIMBO,
            GUN_KNIFE,
            GUN_GRENADE,
            // secondary
            GUN_PISTOL,
            GUN_HEAL,
            GUN_RPG,
            // primary
            GUN_SHOTGUN,
            GUN_SUBGUN,
            GUN_SNIPER,
            GUN_SNIPER2,
            GUN_BOLT,
            GUN_ASSAULT,
            GUN_SWORD,
            GUN_ASSAULT2,
        };
        loopi(NUMGUNS)
        {
            weapon *w = player1->weapons[weap_check_order[i]];
            if(!w) continue;
            if(w->selectable() || w==curweapon || (w->type==GUN_PISTOL && player1->akimbo))
            {
                availweapons.add(w);
            }
        }

        // replace pistol by akimbo
        if(player1->akimbo)
        {
            availweapons.removeobj(akimbo); // and remove initial akimbo
            int pistolidx = availweapons.find(player1->weapons[GUN_PISTOL]);
            if(pistolidx>=0) availweapons[pistolidx] = akimbo; // insert at pistols position
            if(curweapon->type==GUN_PISTOL) curweapon = akimbo; // fix selection
        }

        // detect the next weapon
        int num = availweapons.length();
        int curidx = availweapons.find(curweapon);
        if(!num || curidx<0) return;
        int idx = (curidx + *s) % num;
        if(idx<0) idx += num;
        weapon *next = availweapons[idx];
        if(next->type!=player1->weaponsel->type) // different weapon
        {
            selectweapon(next);
        }
    }
    else if(player1->isspectating()) updatefollowplayer(*s);
}

bool quicknade = false, nadeattack = false;
VARP(quicknade_hold, 0, 0, 1);

void quicknadethrow(bool on)
{
    if(player1->state != CS_ALIVE) return;
    if(on)
    {
        if(player1->weapons[GUN_GRENADE]->mag > 0)
        {
            if(player1->weaponsel->type != GUN_GRENADE) selectweapon(player1->weapons[GUN_GRENADE]);
            if(player1->weaponsel->type == GUN_GRENADE || quicknade_hold) { player1->attacking = true; nadeattack = true; }
        }
    }
    else if (nadeattack)
    {
        nadeattack = player1->attacking = false;
        if(player1->weaponsel->type == GUN_GRENADE) quicknade = true;
    }
}

void setburst(bool enable)
{
    // TODO: AC implemented burst only for cpistol, which is removed
}
COMMAND(setburst, "d");

void currentprimary() { intret(player1->primary); }
void currentsecondary() { intret(player1->secondary); }
void prevweapon() { intret(player1->prevweaponsel->type); }
void curweapon() { intret(player1->weaponsel->type); }

void magcontent(int *w) { if(*w >= 0 && *w < NUMGUNS) intret(player1->weapons[*w]->mag); else intret(-1); }
void magreserve(int *w) { if(*w >= 0 && *w < NUMGUNS) intret(player1->weapons[*w]->ammo); else intret(-1); }

COMMANDN(weapon, requestweapon, "i");
COMMAND(shiftweapon, "i");
COMMAND(quicknadethrow, "d");
COMMAND(currentprimary, "");
COMMAND(currentsecondary, "");
COMMAND(prevweapon, "");
COMMAND(curweapon, "");
COMMAND(magcontent, "i");
COMMAND(magreserve, "i");

void tryreload(playerent *p)
{
    if(!p || p->state!=CS_ALIVE || p->weaponsel->reloading || p->weaponchanging) return;
    p->weaponsel->reload(false);
}

void selfreload() { tryreload(player1); }
COMMANDN(reload, selfreload, "");

void selfuse()
{
    // for now we're only using it for airstrikes
    addmsg(SV_STREAKUSE, "rf3", worldhitpos.x, worldhitpos.y, worldhitpos.z);
}
COMMANDN(use, selfuse, "");

#include "ballistics.h"

int intersect(playerent *d, const vec &from, const vec &to, vec *end)
{
    float dist;
    if(d->head.x >= 0)
    {
        if(intersectsphere(from, to, d->head, HEADSIZE, dist))
        {
            if(end) (*end = to).sub(from).mul(dist).add(from);
            return HIT_HEAD;
        }
    }
    float y = d->yaw*RAD, p = (d->pitch/4+90)*RAD, c = cosf(p);
    vec bottom(d->o), top(sinf(y)*c, -cosf(y)*c, sinf(p)), mid(top);
    bottom.z -= d->eyeheight;
    float h = d->eyeheight /*+ d->aboveeye*/; // this mod makes the shots pass over the shoulders
    mid.mul(h*(1-TORSOPART)).add(bottom);
    top.mul(h).add(bottom);
    if (intersectcylinder(from, to, mid, top, d->radius, dist))
    {
        if(end) (*end = to).sub(from).mul(dist).add(from);
        return HIT_TORSO;
    }
    if (intersectcylinder(from, to, bottom, mid, d->radius, dist))
    {
        if(end) (*end = to).sub(from).mul(dist).add(from);
        return HIT_LEG;
    }
    return HIT_NONE;

#if 0
    const float eyeheight = d->eyeheight;
    vec o(d->o);
    o.z += (d->aboveeye - eyeheight)/2;
    return intersectbox(o, vec(d->radius, d->radius, (d->aboveeye + eyeheight)/2), from, to, end) ? 1 : 0;
#endif
}

bool intersect(entity *e, const vec &from, const vec &to, vec *end)
{
    mapmodelinfo &mmi = getmminfo(e->attr2);
    if(!&mmi || !mmi.h) return false;

    float lo = float(S(e->x, e->y)->floor+mmi.zoff+e->attr3);
    return intersectbox(vec(e->x, e->y, lo+mmi.h/2.0f), vec(mmi.rad, mmi.rad, mmi.h/2.0f), from, to, end);
}

void playerincrosshair(playerent * &pl, int &hitzone, vec &pos)
{
    const vec &from = camera1->o, &to = worldpos;

    pl = NULL;
    hitzone = HIT_NONE;
    float bestdist = 1e16f;
    loopv(players)
    {
        playerent *o = players[i];
        if(!o || o==focus || o->state==CS_DEAD) continue;
        float dist = camera1->o.dist(o->o);
        int zone = HIT_NONE;
        vec end;
        if(dist < bestdist && (zone = intersect(o, from, to, &end)))
        {
            pl = o;
            hitzone = zone;
            pos = end;
            bestdist = dist;
        }
    }
}

void damageeffect(int damage, const vec &o)
{
    particle_splash(PART_BLOOD, clamp(damage/10/HEALTHSCALE, 0, 100), 1000, o);
}

struct hitweap
{
    float hits;
    int shots;
    hitweap() {hits=shots=0;}
};
hitweap accuracym[NUMGUNS];

inline void attackevent(playerent *owner, int weapon)
{
    if(owner == player1 && identexists("onAttack"))
    {
        defformatstring(onattackevent)("onAttack %d", weapon);
        execute(onattackevent);
    }
}

vector<bounceent *> bounceents;

void removebounceents(playerent *owner)
{
    loopv(bounceents) if(bounceents[i]->owner==owner) { delete bounceents[i]; bounceents.remove(i--); }
}

void movebounceents()
{
    loopv(bounceents) if(bounceents[i])
    {
        bounceent *p = bounceents[i];
        if ((p->bouncetype == BT_NADE || p->bouncetype == BT_GIB || p->bouncetype == BT_SHELL || p->bouncetype == BT_KNIFE) && p->applyphysics()) movebounceent(p, 1, false);
        if(!p->isalive(lastmillis))
        {
            p->destroy();
            delete p;
            bounceents.remove(i--);
        }
    }
}

void clearbounceents()
{
    if(gamespeed==100);
    else if(multiplayer(false)) bounceents.add((bounceent *)player1);
    loopv(bounceents) if(bounceents[i]) { delete bounceents[i]; bounceents.remove(i--); }
}

FVARP(shellsize, 0, 0.3f, 1);

void renderbounceents()
{
    loopv(bounceents)
    {
        bounceent *p = bounceents[i];
        if(!p) continue;
        string model;
        vec o(p->o);

        float scale = 1.f;
        int anim = ANIM_MAPMODEL, basetime = 0;
        switch(p->bouncetype)
        {
            case BT_KNIFE:
                copystring(model, "weapons/knife/static");
                break;
            case BT_NADE:
            if (identexists("modmdlbounce3"))
                copystring(model, getalias("modmdlbounce3"));
                else
                copystring(model, "weapons/grenade/static");
                break;
            case BT_SHELL:
            {
                copystring(model, "weapons/shell");
                scale = shellsize;
                int t = lastmillis - p->millis;
                if (t>p->timetolive - 2000)
                {
                    anim = ANIM_DECAY;
                    basetime = p->millis + p->timetolive - 2000;
                    t -= p->timetolive - 2000;
                    o.z -= t*t / 4000000000.0f*t;
                }
                break;
            }
            case BT_GIB:
            default:
            {
                uint n = (((4*(uint)(size_t)p)+(uint)p->timetolive)%3)+1;

                defformatstring(widn)("modmdlbounce%d", n-1);

                if (identexists(widn))
                copystring(model, getalias(widn));
                else
                formatstring(model)("misc/gib0%u", n);

                int t = lastmillis-p->millis;
                if(t>p->timetolive-2000)
                {
                    anim = ANIM_DECAY;
                    basetime = p->millis+p->timetolive-2000;
                    t -= p->timetolive-2000;
                    o.z -= t*t/4000000000.0f*t;
                }
                break;
            }
        }
        path(model);
        if (p->bouncetype == BT_SHELL) sethudgunperspective(true);
        rendermodel(model, anim|ANIM_LOOP|ANIM_DYNALLOC, 0, PLAYERRADIUS, o, p->yaw+90, p->pitch, 0, basetime, NULL, NULL, scale);
        if (p->bouncetype == BT_SHELL) sethudgunperspective(false);
    }
}

VARP(gib, 0, 1, 1);
VARP(gibnum, 0, 6, 1000);
VARP(gibttl, 0, 7000, 60000);
VARP(gibspeed, 1, 30, 100);

void addgib(playerent *d)
{
    if(!d || !gib || !gibttl) return;
    audiomgr.playsound(S_GIB, d);

    loopi(gibnum)
    {
        bounceent *p = bounceents.add(new bounceent);
        p->owner = d;
        p->millis = lastmillis;
        p->timetolive = gibttl+rnd(10)*100;
        p->bouncetype = BT_GIB;

        p->o = d->o;
        p->o.z -= d->aboveeye;
        p->inwater = hdr.waterlevel>p->o.z;

        p->yaw = (float)rnd(360);
        p->pitch = (float)rnd(360);

        p->maxspeed = 30.0f;
        p->rotspeed = 3.0f;

        const float angle = (float)rnd(360);
        const float speed = (float)gibspeed;

        p->vel.x = sinf(RAD*angle)*rnd(1000)/1000.0f;
        p->vel.y = cosf(RAD*angle)*rnd(1000)/1000.0f;
        p->vel.z = rnd(1000)/1000.0f;
        p->vel.mul(speed/100.0f);

        p->resetinterp();
    }
}

VARP(accuracy,0,0,1);

void r_accuracy(int h)
{
    if(!accuracy) return;
    vector <char*>lines;
    int rows = 0, cols = 0;
    float spacing = curfont->defaultw*2, x_offset = curfont->defaultw, y_offset = float(2*h) - 2*spacing;

    loopi(NUMGUNS) if(accuracym[i].shots)
    {
        float acc = 100.0f*accuracym[i].hits/(float)accuracym[i].shots;
        string line;
        rows++;
        if(i == GUN_GRENADE || i == GUN_SHOTGUN)
        {
            formatstring(line)("\f5%5.1f%s (%.1f/%d) :\f0%s", acc, "%", accuracym[i].hits, (int)accuracym[i].shots, killname(i, FRAG_NONE));
        }
        else
        {
            formatstring(line)("\f5%5.1f%s (%d/%d) :\f0%s", acc, "%", (int)accuracym[i].hits, (int)accuracym[i].shots, killname(i, FRAG_NONE));
        }
        cols=max(cols,(int)strlen(line));
        lines.add(newstring(line));
    }
    if(rows<1) return;
    cols++;
    blendbox(x_offset, spacing+y_offset, spacing+x_offset+curfont->defaultw*cols, y_offset-curfont->defaulth*rows, true, -1);
    int x=0;
    loopv(lines)
    {
        char *line = lines[i];
        draw_textf(line,spacing*0.5+x_offset,y_offset-x*curfont->defaulth-0.5*spacing);
        x++;
    }
}

void accuracyreset()
{
    loopi(NUMGUNS)
    {
        accuracym[i].hits=accuracym[i].shots=0;
    }
    conoutf(_("Your accuracy has been reset."));
}
COMMAND(accuracyreset, "");
// weapon

weapon::weapon(class playerent *owner, int type) : type(type), owner(owner), info(guns[type]),
    ammo(owner->ammo[type]), mag(owner->mag[type]), gunwait(owner->gunwait[type]), reloading(0)
{
}

int weapon::flashtime() const { return clamp((int)info.attackdelay/4, 60, 150); }

void weapon::sendshoot(const vec &to)
{
    if (owner != player1 && !isowned(owner)) return;
    owner->shoot = true;
    static uchar buf[MAXTRANS];
    ucharbuf p(buf, MAXTRANS);
    // standard shoot packet
    putint(p, SV_SHOOT);
    putint(p, owner->clientnum);
    putint(p, lastmillis);
    putint(p, owner->weaponsel->type);
    putint(p, (int)(to.x*DMF));
    putint(p, (int)(to.y*DMF));
    putint(p, (int)(to.z*DMF));
    // write positions
    loopv(players)
        if (players[i] && players[i]->state != CS_DEAD)
        {
            putint(p, i);
            putint(p, (int)(players[i]->o.x*DMF));
            putint(p, (int)(players[i]->o.y*DMF));
            putint(p, (int)((players[i]->o.z-players[i]->eyeheight)*DMF));
            putint(p, (int)(players[i]->head.x*DMF));
            putint(p, (int)(players[i]->head.y*DMF));
            putint(p, (int)(players[i]->head.z*DMF));
        }
    putint(p, -1);
    addmsgraw(p, true);
    owner->pstatshots[owner->weaponsel->type]++; //NEW
}

bool weapon::modelattacking()
{
    int animtime = min(owner->gunwait[owner->weaponsel->type], (int)owner->weaponsel->info.attackdelay);
    if(lastmillis - owner->lastaction < animtime) return true;
    else return false;
}

void weapon::attacksound()
{
    if(info.sound == S_NULL) return;
    if (!suppressed_weap(type))
    {
        owner->radarmillis = lastmillis;
        owner->lastloudpos.x = owner->o.x;
        owner->lastloudpos.y = owner->o.y;
        owner->lastloudpos.z = owner->yaw;
    }
    audiomgr.playsound(info.sound, owner, player1 == owner ? SP_HIGH : SP_NORMAL);
}

bool weapon::reload(bool autoreloaded)
{
    if(mag>=info.magsize || ammo<=0) return false;
    updatelastaction(owner);
    reloading = lastmillis;
    gunwait += info.reloadtime;

    int numbullets = min(info.magsize - mag, ammo);
    mag += numbullets;
    ammo -= numbullets;

    if (info.reload != S_NULL)
        audiomgr.playsound(info.reload, owner, player1 == owner ? SP_HIGH : SP_NORMAL);
    if (player1 == owner || isowned(owner))
    {
        addmsg(SV_RELOAD, "ri3", owner->clientnum, lastmillis, owner->weaponsel->type);
        if(identexists("onReload"))
        {
            defformatstring(str)("onReload %d", (int)autoreloaded);
            execute(str);
        }
    }
    return true;
}

VARP(oldfashionedgunstats, 0, 0, 1);

void weapon::renderstats()
{
    char gunstats[64];
    if(oldfashionedgunstats) sprintf(gunstats, "%i/%i", mag, ammo); else sprintf(gunstats, "%i", mag);
    draw_text(gunstats, 590, 823);
    if(!oldfashionedgunstats)
    {
        int offset = text_width(gunstats);
        glScalef(0.5f, 0.5f, 1.0f);
        sprintf(gunstats, "%i", ammo);
        draw_text(gunstats, (590 + offset)*2, 826*2);
        glLoadIdentity();
    }
}

void weapon::attackphysics(const vec &from, const vec &to) // physical fx to the owner
{
    vec unitv;
    float dist = to.dist(from, unitv);
    // kickback
    owner->vel.add(vec(unitv).mul(dynrecoil()*-0.01f / dist * owner->eyeheight / owner->maxeyeheight));
    // recoil
    const guninfo &g = info;
    owner->pitchvel = min(powf(shots/(float)(g.recoilincrease), 2.0f)+(float)(g.recoilbase)/10.0f, (float)(g.maxrecoil)/10.0f);
    // FIXME backport from ACR
}

void weapon::attackhit(const vec &o)
{
    particle_splash(PART_SPARK, 5, 250, o);
}

VARP(righthanded, 0, 1, 1); // flowtron 20090727

void weapon::renderhudmodel(int lastaction, int index)
{
    playerent *p = owner;
    vec unitv;
    float dist = worldpos.dist(p->o, unitv);
    unitv.div(dist);

    weaponmove wm;
    if(!intermission) wm.calcmove(unitv, lastaction, p);
//    if(!intermission) wm.calcmove(unitv, p->lastaction, p);
    defformatstring(widn)("modmdlweap%d", type);
    defformatstring(path)("weapons/%s", identexists(widn)?getalias(widn):info.modelname);
    bool emit = (wm.anim&ANIM_INDEX)==ANIM_GUN_SHOOT && (lastmillis - lastaction) < flashtime();
//    bool emit = (wm.anim&ANIM_INDEX)==ANIM_GUN_SHOOT && (lastmillis - p->lastaction) < flashtime();
    modelattach a[3]; // a null one is needed
    if (type != GUN_AKIMBO || ((akimbo *)this)->akimboside != index)
    {
        owner->eject = vec(-1, -1, -1);
        a[0].tag = "tag_eject";
        a[0].pos = &owner->eject;
        owner->muzzle = vec(-1, -1, -1);
        a[1].tag = "tag_muzzle";
        a[1].pos = &owner->muzzle;
    }
    rendermodel(path, wm.anim|ANIM_DYNALLOC|(righthanded==index ? ANIM_MIRROR : 0)|(emit ? ANIM_PARTICLE : 0), 0, -1, wm.pos, p->yaw+90, p->pitch+wm.k_rot, 40.0f, wm.basetime, owner, a, 1.28f);
}

void weapon::updatetimers(int millis)
{
    if(gunwait) gunwait = max(gunwait - (millis-owner->lastaction), 0);
}

void weapon::onselecting()
{
    updatelastaction(owner);
    audiomgr.playsound(S_GUNCHANGE, owner, owner == player1? SP_HIGH : SP_NORMAL);
}

void weapon::renderhudmodel() { renderhudmodel(owner->lastaction); }
void weapon::renderaimhelp(int teamtype) { drawcrosshair(owner, teamtype ? CROSSHAIR_TEAMMATE : CROSSHAIR_DEFAULT); }
int weapon::dynspread()
{
    if (info.spread <= 1) return 1;
    return (int)(info.spread * (owner->vel.magnitude() / 3.f + owner->pitchvel / 5.f + 0.4f) * 2.4f * owner->eyeheight / owner->maxeyeheight * (1 - sqrtf(owner->zoomed * info.spreadrem / 100.f)));
}
float weapon::dynrecoil() { return info.kick * (1 - owner->zoomed / 2.f); } // 1/2 recoil when ADS
bool weapon::selectable() { return this != owner->weaponsel && owner->state == CS_ALIVE && !owner->weaponchanging &&
    (type == GUN_KNIFE || type == GUN_GRENADE || type == GUN_AKIMBO || type == owner->primary || type == owner->secondary); }
bool weapon::deselectable() { return !reloading; }

void weapon::equipplayer(playerent *pl)
{
    if(!pl) return;
    pl->weapons[GUN_ASSAULT] = new m16(pl);
    pl->weapons[GUN_GRENADE] = new grenades(pl);
    pl->weapons[GUN_KNIFE] = new knife(pl);
    pl->weapons[GUN_PISTOL] = new pistol(pl);
    pl->weapons[GUN_SHOTGUN] = new shotgun(pl);
    pl->weapons[GUN_SNIPER] = new m21(pl);
    pl->weapons[GUN_SUBGUN] = new subgun(pl);
    pl->weapons[GUN_AKIMBO] = new akimbo(pl);
    pl->weapons[GUN_BOLT] = new boltrifle(pl);
    pl->weapons[GUN_HEAL] = new healgun(pl);
    pl->weapons[GUN_SWORD] = new sword(pl);
    pl->weapons[GUN_RPG] = new crossbow(pl);
    pl->weapons[GUN_ASSAULT2] = new ak47(pl);
    pl->weapons[GUN_SNIPER2] = new m82(pl);
    pl->selectweapon(GUN_ASSAULT);
    pl->setprimary(GUN_ASSAULT);
}

bool weapon::valid(int id) { return id>=0 && id<NUMGUNS; }

// grenadeent

enum { NS_NONE, NS_ACTIVATED = 0, NS_THROWN, NS_EXPLODED };

grenadeent::grenadeent (playerent *owner, int millis)
{
    ASSERT(owner);
    nadestate = NS_NONE;
    local = owner==player1 || isowned(owner);
    bounceent::owner = owner;
    bounceent::millis = id = lastmillis;
    timetolive = NADETTL-millis;
    bouncetype = BT_NADE;
    maxspeed = 30.0f;
    rotspeed = 6.0f;
    distsincebounce = 0.0f;
}

grenadeent::~grenadeent()
{
    if(owner && owner->weapons[GUN_GRENADE]) owner->weapons[GUN_GRENADE]->removebounceent(this);
}

void grenadeent::explode()
{
    if(nadestate!=NS_ACTIVATED && nadestate!=NS_THROWN ) return;
    nadestate = NS_EXPLODED;
    if(local)
        addmsg(SV_EXPLODE, "ri7", owner->clientnum, lastmillis, GUN_GRENADE, id, (int)(o.x*DMF), (int)(o.y*DMF), (int)(o.z*DMF));
}

void grenadeent::activate()
{
    if(nadestate!=NS_NONE) return;
    nadestate = NS_ACTIVATED;

    if(local)
    {
        addmsg(SV_SHOOTC, "ri3", owner->clientnum, millis, owner->weaponsel->type);
        audiomgr.playsound(S_GRENADEPULL, owner, SP_HIGH);
        player1->pstatshots[GUN_GRENADE]++; //NEW
    }
}

void grenadeent::_throw(const vec &from, const vec &vel)
{
    if(nadestate!=NS_ACTIVATED) return;
    nadestate = NS_THROWN;
    this->vel = vel;
    this->o = from;
    this->resetinterp();
    inwater = hdr.waterlevel>o.z;
    if(local)
    {
        addmsg(SV_THROWNADE, "ri8", owner->clientnum, int(o.x*DMF), int(o.y*DMF), int(o.z*DMF), int(vel.x*DNF), int(vel.y*DNF), int(vel.z*DNF), lastmillis-millis);
        audiomgr.playsound(S_GRENADETHROW, SP_HIGH);
    }
    else audiomgr.playsound(S_GRENADETHROW, owner);
}

void grenadeent::moveoutsidebbox(const vec &direction, playerent *boundingbox)
{
    vel = direction;
    o = boundingbox->o;
    inwater = hdr.waterlevel>o.z;

    boundingbox->cancollide = false;
    loopi(10) moveplayer(this, 10, true, 10);
    boundingbox->cancollide = true;
}

void grenadeent::destroy() { explode(); }
bool grenadeent::applyphysics() { return nadestate==NS_THROWN; }

void grenadeent::oncollision()
{
    if(distsincebounce>=1.5f) audiomgr.playsound(S_GRENADEBOUNCE1+rnd(2), &o);
    distsincebounce = 0.0f;
}

void grenadeent::onmoved(const vec &dist)
{
    distsincebounce += dist.magnitude();
}

// grenades

grenades::grenades(playerent *owner) : weapon(owner, GUN_GRENADE), inhandnade(NULL), throwwait(325), throwmillis(0), state(GST_NONE) {}

int grenades::flashtime() const { return 0; }

bool grenades::busy() { return state!=GST_NONE; }

bool grenades::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction;
    vec &to = targ;

    bool quickwait = attackmillis*3>=gunwait && !(m_duke(gamemode, mutators) && m_team(gamemode, mutators) && arenaintermission);
    bool waitdone = attackmillis>=gunwait && quickwait;
    if(waitdone) gunwait = reloading = 0;

    switch(state)
    {
        case GST_NONE:
            if(waitdone && owner->attacking && this==owner->weaponsel)
            {
                attackevent(owner, type);
                activatenade(); // activate
            }
        break;

        case GST_INHAND:
            if(waitdone || ( quicknade && quickwait ) )
            {
                if(!owner->attacking || this!=owner->weaponsel) thrownade(); // throw
                else if(!inhandnade->isalive(lastmillis)) dropnade(); // drop & have fun
            }
            break;

        case GST_THROWING:
            if(attackmillis >= throwwait) // throw done
            {
                reset();
                if(!mag && this==owner->weaponsel) // switch to primary immediately
                {
                    addmsg(SV_QUICKSWITCH, "ri", owner->clientnum);
                    owner->weaponchanging = lastmillis - 1 - (SWITCHTIME(owner->perk2 == PERK_TIME) / 2);
                    owner->nextweaponsel = owner->weaponsel = owner->weapons[owner->primary];
                }
                return false;
            }
            break;
    }
    return true;
}

void grenades::attackfx(const vec &from, const vec &to, int millis) // other player's grenades
{
    throwmillis = lastmillis-millis;
    if (millis < 0)
    {
        state = GST_INHAND;
        audiomgr.playsound(S_GRENADEPULL, owner); // activate
    }
    else // if(millis > 0) // throw
    {
        grenadeent *g = new grenadeent(owner, millis);
        state = GST_THROWING;
        bounceents.add(g);
        g->_throw(from, to);
    }
}

inline void explosioneffect(const vec &o)
{
    particle_splash(PART_SPARK, 50, 300, o);
    adddynlight(NULL, o, 16, 200, 100, 255, 255, 224);
    adddynlight(NULL, o, 16, 600, 600, 192, 160, 128);
    audiomgr.playsound(S_FEXPLODE, &o);
}

//VARP(nadedetail, 0, 9, 50);

void grenades::attackhit(const vec &o)
{
    particle_fireball(PART_FIREBALL, o, owner);
    addscorchmark(o);
    explosioneffect(o);
    // TODO: shot line fx
    if (owner == player1)
        accuracym[GUN_GRENADE].shots++;
}

int grenades::modelanim()
{
    if(state == GST_THROWING) return ANIM_GUN_THROW;
    else
    {
        int animtime = min(gunwait, (int)info.attackdelay);
        if(state == GST_INHAND || lastmillis - owner->lastaction < animtime) return ANIM_GUN_SHOOT;
    }
    return ANIM_GUN_IDLE;
}

void grenades::activatenade()
{
    if(!mag) return;
    throwmillis = 0;

    inhandnade = new grenadeent(owner);
    bounceents.add(inhandnade);

    updatelastaction(owner);
    mag--;
    gunwait = info.attackdelay;
    owner->lastattackweapon = this;
    state = GST_INHAND;
    inhandnade->activate();
}

void grenades::thrownade()
{
    if (quicknade && owner->weaponsel->type == GUN_GRENADE) selectweapon(owner->prevweaponsel);
    quicknade = false;
    if(!inhandnade) return;
    const float speed = cosf(RAD*owner->pitch);
    vec vel(sinf(RAD*owner->yaw)*speed, -cosf(RAD*owner->yaw)*speed, sinf(RAD*owner->pitch));
    vel.mul(NADEPOWER);
    thrownade(vel);
}

void grenades::thrownade(const vec &vel)
{
    inhandnade->moveoutsidebbox(vel, owner);
    inhandnade->_throw(inhandnade->o, vel);
    inhandnade = NULL;

    throwmillis = lastmillis;
    updatelastaction(owner);
    state = GST_THROWING;
    if(this==owner->weaponsel) owner->attacking = false;
}

void grenades::dropnade()
{
    vec n(0,0,0);
    thrownade(n);
}

void grenades::renderstats()
{
    char gunstats[64];
    sprintf(gunstats, "%i", mag);
    draw_text(gunstats, 830, 823);
}

bool grenades::selectable() { return weapon::selectable() && state != GST_INHAND && mag; }
void grenades::reset() { throwmillis = 0; state = GST_NONE; }

void grenades::onselecting() { reset(); weapon::onselecting(); }
void grenades::onownerdies()
{
    reset();
    if(owner==player1 && inhandnade) dropnade();
}

void grenades::removebounceent(bounceent *b)
{
    if(b == inhandnade) { inhandnade = NULL; reset(); }
}

// gun base class

gun::gun(playerent *owner, int type) : weapon(owner, type) {}

bool gun::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction - gunwait;
    if(attackmillis<0) return false;
    gunwait = reloading = 0;

    if(!owner->attacking)
    {
        shots = 0;
        checkautoreload();
        return false;
    }

    attackmillis = lastmillis - min(attackmillis, curtime);
    updatelastaction(owner, attackmillis);
    if(!mag)
    {
        audiomgr.playsoundc(S_NOAMMO);
        gunwait += 250;
        owner->lastattackweapon = NULL;
        shots = 0;
        checkautoreload();
        return false;
    }

    owner->lastattackweapon = this;
    shots++;

    if(!info.isauto) owner->attacking = false;

    if(burstshotssettings[this->type] > 0 && shots >= burstshotssettings[this->type]) owner->attacking = false;

    vec from = owner->o;
    vec to = targ;

    attackphysics(from, to);

    attackevent(owner, type);

    attacksound();
    attackshell(to);

    gunwait = info.attackdelay;
    mag--;

    sendshoot(to);

    return true;
}

VARP(shellttl, 0, 4000, 20000);

void gun::attackshell(const vec &to)
{
    extern int hudgun;
    if (!shellttl || (owner == focus && !hudgun)) return;
    bounceent *s = bounceents.add(new bounceent);
    s->owner = owner;
    s->millis = lastmillis;
    s->timetolive = gibttl;
    s->bouncetype = BT_SHELL;

    const bool akimboflip = (type != GUN_AKIMBO || !((akimbo *)this)->akimboside) ^ righthanded;
    s->vel = vec(1, rnd(101) / 800.f - .1f, (rnd(51) + 50) / 100.f);
    s->vel.rotate_around_z(owner->yaw*RAD);
    if (owner->eject.x >= 0)
        s->o = owner->eject;
    else
    {
        // "fake" shell position
        s->o = owner->o;
        s->o.add(vec(s->vel.x * owner->radius, s->vel.y * owner->radius, -WEAPONBELOWEYE));
    }
    s->vel.mul(.02f * (rnd(3) + 5));
    if (akimboflip) s->vel.rotate_around_z(180 * RAD);
    vec ownervel = owner->vel;
    ownervel.mul(0.6f); // tweaked until it "felt right"
    ownervel.z *= 0.3f; // tweaked until it "felt right"
    s->vel.add(ownervel);
    s->inwater = hdr.waterlevel > owner->o.z;
    s->cancollide = false;

    s->yaw = owner->yaw + 180;
    s->pitch = -owner->pitch;

    s->maxspeed = 30.f;
    s->rotspeed = 3.f;

    s->resetinterp();
}

void gun::attackfx(const vec &from, const vec &to, int millis)
{
    addbullethole(owner, from, to);
    addshotline(owner, from, to, millis & 1);
    particle_splash(PART_SPARK, 5, 250, to);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    if ((millis & 1) && owner != player1 && !isowned(owner))
    {
        attacksound();
        attackshell(to);
    }
}

int gun::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT|ANIM_LOOP : ANIM_GUN_IDLE; }
void gun::checkautoreload() { if(autoreload && owner==player1 && !mag) reload(true); }


// shotgun

shotgun::shotgun(playerent *owner) : gun(owner, GUN_SHOTGUN) {}

int shotgun::dynspread() { return info.spread * (1 - owner->zoomed * info.spreadrem / 100.f); }

void shotgun::attackfx(const vec &from, const vec &to, int millis)
{
    static uchar filter1 = 0, filter2 = 0;
    if (millis & 1)
    {
        loopi(SGRAYS)
            particle_splash(PART_SPARK, 5, 200, sg[i]);
        if (addbullethole(owner, from, to))
            loopi(SGRAYS)
            {
                if (++filter1 >= 3) filter1 = 0;
                else addshotline(owner, from, sg[i], 3);
                addbullethole(owner, from, sg[i], 0, false);
            }
        adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
        attackshell(to);
        if (owner != player1 && !isowned(owner))
            attacksound();
    }
    else
    {
        if (++filter2 >= 2) filter2 = 0;
        else addshotline(owner, from, to, 2);
        addbullethole(owner, from, to, 0, false);
    }
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
}

void shotgun::renderaimhelp(int teamtype){ drawcrosshair(owner, CROSSHAIR_SHOTGUN /*, teamtype*/); }


// sword

sword::sword(playerent *owner) : weapon(owner, GUN_SWORD) {}

bool sword::attack(vec &targ)
{
    int attackmillis = lastmillis - owner->lastaction;
    if (attackmillis<gunwait) return false;
    gunwait = reloading = 0;

    if (!owner->attacking) return false;
    updatelastaction(owner);

    owner->lastattackweapon = this;
    owner->attacking = info.isauto;

    attacksound();

    sendshoot(targ);
    gunwait = info.attackdelay;
    return true;
}
int sword::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT : ANIM_GUN_IDLE; }

void sword::attackfx(const vec &from, const vec &to, int millis) { if (owner != player1 && !isowned(owner)) attacksound(); }

int sword::flashtime() const { return 0; }

// crossbow (RPG)

crossbow::crossbow(playerent *owner) : gun(owner, GUN_RPG) {}
int crossbow::modelanim()
{
    // very simple and stupid animation system
    return mag ? ANIM_GUN_SHOOT : ANIM_GUN_IDLE;
}

void crossbow::attackfx(const vec &from2, const vec &to, int millis)
{
    vec from(from2);
    if (millis & 1)
    {
        if (owner->muzzle.x >= 0)
            from = owner->muzzle;
        else from.z -= WEAPONBELOWEYE;
        if (owner != player1 && !isowned(owner)) attacksound();
    }
    addshotline(owner, from, to, 0);
    particle_trail(PART_SHOTLINE_RPG, 400, from, to);
    particle_splash(PART_SPARK, 5, 250, to);
}

void crossbow::attackhit(const vec &o)
{
    particle_fireball(PART_FIREBALL_RPG, o, owner);
    explosioneffect(o);
}


// scopedprimary

void scopedprimary::attackfx(const vec &from2, const vec &to, int millis)
{
    vec from(from2);
    if (millis & 1)
    {
        if (owner->muzzle.x >= 0)
            from = owner->muzzle;
        else from.z -= WEAPONBELOWEYE;
        attackshell(to);
        if (owner != player1 && !isowned(owner)) attacksound();
    }

    addbullethole(owner, from, to);
    addshotline(owner, from, to, 0);
    particle_splash(PART_SPARK, 50, 200, to);
    particle_trail(PART_SMOKE, 500, from, to);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
}

float scopedprimary::dynrecoil() { return weapon::dynrecoil() * (1 - owner->zoomed / 3.f); } // 1/2 * 2/3 = 1/3 recoil when ADS
void scopedprimary::renderhudmodel() { if (owner->zoomed < ADSZOOM) weapon::renderhudmodel(); }

void scopedprimary::renderaimhelp(int teamtype)
{
    if (owner->zoomed < ADSZOOM)
        weapon::renderaimhelp(teamtype);
    else
    {
        drawscope();
        drawcrosshair(owner, CROSSHAIR_SCOPE, /*teamtype,*/ NULL, 24.0f);
    }
}


// assaultrifle

float assaultrifle::dynrecoil() { return weapon::dynrecoil() + (rnd(8)*-0.01f); }


// akimbo

akimbo::akimbo(playerent *owner) : gun(owner, GUN_AKIMBO), akimboside(0), akimbomillis(0)
{
    akimbolastaction[0] = akimbolastaction[1] = 0;
}

bool akimbo::attack(vec &targ){
    if (gun::attack(targ))
    {
        akimbolastaction[akimboside & 1] = lastmillis;
        akimboside ^= true;
        return true;
    }
    return false;
}

void akimbo::onammopicked()
{
    akimbomillis = lastmillis + 30000;
    if (owner == player1 || isowned(owner))
    {
        // if(owner->weaponsel->type!=GUN_SNIPER && owner->weaponsel->type!=GUN_GRENADE) owner->weaponswitch(this);
        if(akimboautoswitch || owner->weaponsel->type==GUN_PISTOL) owner->weaponswitch(this); // Give the client full control over akimbo auto-switching // Bukz 2011apr23
        addmsg(SV_AKIMBO, "ri2", owner->clientnum, lastmillis);
    }
}

void akimbo::onselecting()
{
    gun::onselecting();
    akimbolastaction[0] = akimbolastaction[1] = lastmillis;
}

bool akimbo::selectable() { return weapon::selectable() && !m_nosecondary(gamemode, mutators) && owner->akimbo; }
void akimbo::updatetimers(int millis) { weapon::updatetimers(millis); /*loopi(2) akimbolastaction[i] = millis;*/ }
void akimbo::reset() { akimbolastaction[0] = akimbolastaction[1] = akimbomillis = akimboside = 0; }

void akimbo::renderhudmodel()
{
    weapon::renderhudmodel(akimbolastaction[0], 0);
    weapon::renderhudmodel(akimbolastaction[1], 1);
}

bool akimbo::timerout() { return akimbomillis && akimbomillis <= lastmillis; }


// healgun

healgun::healgun(playerent *owner) : gun(owner, GUN_HEAL) {}

void healgun::attackfx(const vec &from2, const vec &to, int millis)
{
    vec from(from2);
    if (millis & 1)
    {
        if (owner->muzzle.x >= 0)
            from = owner->muzzle;
        else from.z -= WEAPONBELOWEYE;
        if (owner != player1 && !isowned(owner)) attacksound();
    }

    addshotline(owner, from, to, 0);
    particle_trail(PART_SHOTLINE_HEAL, 400, from, to);
    particle_splash(PART_SPARK, 3, 200, to);
}


// knife

knife::knife(playerent *owner) : weapon(owner, GUN_KNIFE) {}

int knife::flashtime() const { return 0; }

bool knife::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction - gunwait;
    if(attackmillis<0) return false;
    gunwait = reloading = 0;

    if(!owner->attacking) return false;

    attackmillis = lastmillis - min(attackmillis, curtime);
    updatelastaction(owner, attackmillis);

    owner->lastattackweapon = this;
    owner->attacking = info.isauto;

    attacksound();
    sendshoot(targ);
    gunwait = info.attackdelay;

    return true;
}

int knife::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT : ANIM_GUN_IDLE; }

void knife::drawstats() {}
void knife::attackfx(const vec &from, const vec &to, int millis) { attacksound(); }
void knife::renderstats() { }


void setscope(bool enable)
{
    if (intermission || player1->state != CS_ALIVE || player1->scoping == enable) return;
    if (player1->weaponsel->type == GUN_KNIFE || (ads_gun(player1->weaponsel->type) && ads_classic_allowed(player1->weaponsel->type)))
        player1->scoping = enable;
}

COMMANDF(setscope, "i", (int *on) { setscope(*on != 0); });


void shoot(playerent *p, vec &targ)
{
    if(p->state!=CS_ALIVE || p->weaponchanging) return;
    weapon *weap = p->weaponsel;
    if(weap)
    {
        weap->attack(targ);
        loopi(NUMGUNS)
        {
            weapon *bweap = p->weapons[i];
            if(bweap != weap && bweap->busy()) bweap->attack(targ);
        }
    }
}

void checkakimbo()
{
    if(player1->akimbo)
    {
        akimbo &a = *((akimbo *)player1->weapons[GUN_AKIMBO]);
        if(a.timerout() || player1->state == CS_DEAD)
        {
            weapon &p = *player1->weapons[GUN_PISTOL];
            player1->akimbo = false;
            a.reset();
            // transfer ammo to pistol
            p.mag = min((int)p.info.magsize, max(a.mag, p.mag));
            p.ammo = max(p.ammo, p.ammo);
            // fix akimbo magcontent
            a.mag = 0;
            a.ammo = 0;
            if(player1->weaponsel->type==GUN_AKIMBO)
            {

                switch(akimboendaction)
                {
                    case 0: player1->weaponswitch(player1->weapons[GUN_KNIFE]); break;
                    case 1:
                    {
                        if(player1->weapons[GUN_PISTOL]->ammo) player1->weaponswitch(&p);
                        else player1->weaponswitch(player1->weapons[GUN_KNIFE]);
                        break;
                    }
                    case 2:
                    {
                        if(player1->mag[GUN_GRENADE]) player1->weaponswitch(player1->weapons[GUN_GRENADE]);
                        else {
                            if(player1->weapons[GUN_PISTOL]->ammo) player1->weaponswitch(&p);
                            else player1->weaponswitch(player1->weapons[GUN_KNIFE]);
                        }
                        break;
                    }
                    case 3:
                    {
                        if(player1->ammo[player1->primary]) player1->weaponswitch(player1->weapons[player1->primary]);
                        else {
                            if(player1->mag[GUN_GRENADE]) player1->weaponswitch(player1->weapons[GUN_GRENADE]);
                            else {
                                if(player1->weapons[GUN_PISTOL]->ammo) player1->weaponswitch(&p);
                                else player1->weaponswitch(player1->weapons[GUN_KNIFE]);
                            }
                        }
                        break;
                    }
                    default: break;
                /*
                    case 0: player1->weaponswitch(&p); break;
                    case 1:
                    {
                        if( player1->ammo[player1->primary] ) player1->weaponswitch(player1->weapons[player1->primary]);
                        else player1->weaponswitch(&p);
                        break;
                    }
                    case 2:
                    {
                        if( player1->mag[GUN_GRENADE] ) player1->weaponswitch(player1->weapons[GUN_GRENADE]);
                        else
                        {
                            if( player1->ammo[player1->primary] ) player1->weaponswitch(player1->weapons[player1->primary]);
                            else player1->weaponswitch(&p);
                        }
                        break;
                    }
                    default: break;
                */
                }
            }
            if(player1->state != CS_DEAD) audiomgr.playsoundc(S_AKIMBOOUT);
        }
    }
}

void checkweaponstate()
{
    checkweaponswitch();
    checkakimbo();
}
