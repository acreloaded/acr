// entities.cpp: map entity related functions (pickup etc.)

#include "cube.h"

VAR(showclips, 0, 1, 1);
VAR(showmodelclipping, 0, 1, 1);

vector<entity> ents;
vector<int> eh_ents; // edithide entities
const char *entmdlnames[] =
 {
     "pistolclips", "ammobox", "nade", "health", "helmet", "kevlar", "akimbo", "nades", //FIXME
 };

 void renderent(entity &e)
 {
     /* FIXME: if the item list change, this hack will be messed */

     defformatstring(widn)("modmdlpickup%d", e.type-3);
     defformatstring(mdlname)("pickups/%s", identexists(widn)?getalias(widn):

     entmdlnames[e.type-I_CLIPS+(m_lss(gamemode, mutators) && e.type==I_GRENADE ? 5:0)]);

     float z = (float)(1+sinf(lastmillis/100.0f+e.x+e.y)/20), yaw = lastmillis/10.0f;
     rendermodel(mdlname, ANIM_MAPMODEL|(e.spawned ? 0 : ANIM_TRANSLUCENT)|ANIM_LOOP|ANIM_DYNALLOC, 0, 0, vec(e.x, e.y, z+S(e.x, e.y)->floor+e.attr1), yaw, 0);
 }

void renderclip(entity &e)
{
    float xradius = max(float(e.attr2), 0.1f), yradius = max(float(e.attr3), 0.1f);
    vec bbmin(e.x - xradius, e.y - yradius, float(S(e.x, e.y)->floor+e.attr1)),
        bbmax(e.x + xradius, e.y + yradius, bbmin.z + max(float(e.attr4), 0.1f));

    glDisable(GL_TEXTURE_2D);
    switch(e.type)
    {
        case CLIP:     linestyle(1, 0xFF, 0xFF, 0); break;  // yellow
        case MAPMODEL: linestyle(1, 0, 0xFF, 0);    break;  // green
        case PLCLIP:   linestyle(1, 0xFF, 0, 0xFF); break;  // magenta
        default:       linestyle(1, 0xFF, 0, 0);    break;  // red
    }
    glBegin(GL_LINES);

    glVertex3f(bbmin.x, bbmin.y, bbmin.z);
    loopi(2) glVertex3f(bbmax.x, bbmin.y, bbmin.z);
    loopi(2) glVertex3f(bbmax.x, bbmax.y, bbmin.z);
    loopi(2) glVertex3f(bbmin.x, bbmax.y, bbmin.z);
    glVertex3f(bbmin.x, bbmin.y, bbmin.z);

    glVertex3f(bbmin.x, bbmin.y, bbmax.z);
    loopi(2) glVertex3f(bbmax.x, bbmin.y, bbmax.z);
    loopi(2) glVertex3f(bbmax.x, bbmax.y, bbmax.z);
    loopi(2) glVertex3f(bbmin.x, bbmax.y, bbmax.z);
    glVertex3f(bbmin.x, bbmin.y, bbmax.z);

    loopi(8) glVertex3f(i&2 ? bbmax.x : bbmin.x, i&4 ? bbmax.y : bbmin.y, i&1 ? bbmax.z : bbmin.z);

    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void rendermapmodels()
{
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type==MAPMODEL)
        {
            mapmodelinfo &mmi = getmminfo(e.attr2);
            if(!&mmi) continue;
            rendermodel(mmi.name, ANIM_MAPMODEL|ANIM_LOOP, e.attr4, 0, vec(e.x, e.y, (float)S(e.x, e.y)->floor+mmi.zoff+e.attr3), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 10.0f);
        }
    }
}

void showedithide()
{
    loopv(eh_ents)
    {
        if(eh_ents[i]>0 && eh_ents[i]<MAXENTTYPES) { conoutf("#%02d: %d : %s", i, eh_ents[i], entnames[eh_ents[i]]); }
        else { conoutf("#%02d: %d : -n/a-", i, eh_ents[i]);  }
    }
}
COMMAND(showedithide, "");

void setedithide(char *text) // FIXME: human indexing inside
{
    eh_ents.setsize(0);
    if(text && text[0] != '\0')
    {
        const char *s = strtok(text, " ");
        do
        {
            bool k = false;
            int sn = -1;
            int tn = atoi(s);
            loopi(MAXENTTYPES) if(!strcmp(entnames[i], s)) sn = i;
            if(sn!=-1) { loopv(eh_ents) { if(eh_ents[i]==sn) { k = true; } } }
            else sn = tn;
            if(!k) { if(sn>0 && sn<MAXENTTYPES) eh_ents.add(sn); }
            s = strtok(NULL, " ");
        }
        while(s);
    }
}
COMMAND(setedithide, "c");

void seteditshow(char *just)
{
    eh_ents.setsize(0);
    if(just && just[0] != '\0')
    {
        const char *s = strtok(just, " ");
        int sn = -1;
        int tn = atoi(s);
        loopi(MAXENTTYPES) if(!strcmp(entnames[i], s)) sn = i;
        if(sn==-1) sn = tn;
        loopi(MAXENTTYPES-1)
        {
            int j = i+1;
            if(j!=sn) eh_ents.add(j);
        }
    }
}
COMMAND(seteditshow, "s");

void renderentarrow(const entity &e, const vec &dir, float radius)
{
    if(radius <= 0) return;
    float arrowsize = min(radius/8, 0.5f);
    vec epos(e.x, e.y, e.z);
    vec target = vec(dir).mul(radius).add(epos), arrowbase = vec(dir).mul(radius - arrowsize).add(epos), spoke;
    spoke.orthogonal(dir);
    spoke.normalize();
    spoke.mul(arrowsize);
    glDisable(GL_TEXTURE_2D); // this disables reaction to light, but also emphasizes shadows .. a nice effect, but should be independent
    glDisable(GL_CULL_FACE);
    glLineWidth(3);
    glBegin(GL_LINES);
    glVertex3fv(epos.v);
    glVertex3fv(target.v);
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glVertex3fv(target.v);
    loopi(5)
    {
        vec p(spoke);
        p.rotate(2*M_PI*i/4.0f, dir);
        p.add(arrowbase);
        glVertex3fv(p.v);
    }
    glEnd();
    glLineWidth(1);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
}

void renderentities()
{
    int closest = editmode ? closestent() : -1;
    if(editmode && !reflecting && !refracting && !stenciling)
    {
        static int lastsparkle = 0;
        if(lastmillis - lastsparkle >= 20)
        {
            lastsparkle = lastmillis - (lastmillis%20);
            // closest see above
            loopv(ents)
            {
                entity &e = ents[i];
                if(e.type==NOTUSED) continue;
                bool ice = false;
                loopk(eh_ents.length()) if(eh_ents[k]==e.type) ice = true;
                if(ice) continue;
                vec v(e.x, e.y, e.z);
                if(vec(v).sub(camera1->o).dot(camdir) < 0) continue;
                //particle_splash(i == closest ? PART_ELIGHT : PART_ECLOSEST, 2, 40, v);
                int sc = PART_ECARROT; // "carrot" (orange) - entity slot currently unused, possibly "reserved"
                if(i==closest)
                {
                    sc = PART_ECLOSEST; // blue
                }
                else switch(e.type)
                {
                    case LIGHT : sc = PART_ELIGHT; break; // white
                    case PLAYERSTART: sc = PART_ESPAWN; break; // green
                    case I_CLIPS:
                    case I_AMMO:
                    case I_GRENADE: sc = PART_EAMMO; break; // red
                    case I_HEALTH:
                    case I_HELMET:
                    case I_ARMOUR:
                    case I_AKIMBO: sc = PART_EPICKUP; break; // yellow
                    case MAPMODEL:
                    case SOUND: sc = PART_EMODEL; break; // magenta
                    case LADDER:
                    case CLIP:
                    case PLCLIP: sc = PART_ELADDER; break; // grey
                    case CTF_FLAG: sc = PART_EFLAG; break; // turquoise
                    default: break;
                }
                //particle_splash(sc, i==closest?6:2, i==closest?120:40, v);
                particle_splash(sc, 2, 40, v);
            }
        }
    }
    loopv(ents)
    {
        entity &e = ents[i];
        if(isitem(e.type))
        {
            if(!OUTBORD(e.x, e.y) || editmode)
            {
                renderent(e);
            }
        }
        else if (e.type == CTF_FLAG && m_secure(gamemode))
        {
            const int team = e.attr2 - 2;
            defformatstring(path)("pickups/flags/%s", team != TEAM_SPECT ? team_basestring(team) : "ktf");
            rendermodel(path, ANIM_FLAG | ANIM_LOOP | ANIM_IDLE, 0, 0, vec(e.x, e.y, (float)S(e.x, e.y)->floor), (float)((e.attr1 + 7) - (e.attr1 + 7) % 15), 0, 120.0f);
        }
        else if(editmode || m_edit(gamemode))
        {
            if(e.type==CTF_FLAG)
            {
                defformatstring(path)("pickups/flags/%s", (e.attr2 == TEAM_CLA || e.attr2 == TEAM_RVSF) ? team_basestring(e.attr2) : "ktf");
                rendermodel(path, ANIM_FLAG|ANIM_LOOP, 0, 0, vec(e.x, e.y, (float)S(e.x, e.y)->floor), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 120.0f);
            }
            else if((e.type == CLIP || e.type == PLCLIP) && showclips && !stenciling) renderclip(e);
            else if(e.type == PLAYERSTART)
            {
                defformatstring(skin)(e.attr2 < 2 ? "packages/models/playermodels/%s/%s.jpg" : "packages/models/playermodels/skin.jpg",
                    team_basestring(e.attr2), e.attr2 ? "blue" : "red");
                rendermodel("playermodels", ANIM_IDLE|ANIM_TRANSLUCENT|/*ANIM_LOOP*/ANIM_END|ANIM_DYNALLOC, -(int)textureload(skin)->id, 1.5f, vec(e.x, e.y, (float)S(e.x, e.y)->floor), e.attr1+90, 0/4);
            }
            else if(e.type == MAPMODEL && showclips && showmodelclipping && !stenciling)
            {
                mapmodelinfo &mmi = getmminfo(e.attr2);
                if(&mmi && mmi.h)
                {
                    entity ce = e;
                    ce.type = MAPMODEL;
                    ce.attr1 = mmi.zoff+e.attr3;
                    ce.attr2 = ce.attr3 = mmi.rad;
                    ce.attr4 = mmi.h;
                    renderclip(ce);
                }
            }
        }
        if(editmode && i==closest && !stenciling)//closest see above
        {
            switch(e.type)
            {
                case PLAYERSTART:
                {
                    glColor3f(0, 1, 1);
                    vec dir;
                    vecfromyawpitch(e.attr1, 0, -1, 0, dir);
                    renderentarrow(e, dir, 4);
                    glColor3f(1, 1, 1);
                }
                default: break;
            }
        }
    }
    // TODO: confirms
    if (m_flags(gamemode) && !m_secure(gamemode)) loopi(2)
    {
        flaginfo &f = flaginfos[i];
        entity &e = *f.flagent;
        defformatstring(fpath)("pickups/flags/%s%s", m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "" : team_basestring(i), (m_hunt(gamemode) || m_bomber(gamemode)) ? "_htf" : m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "ktf" : "");
        defformatstring(sfpath)("pickups/flags/small_%s%s", m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "" : team_basestring(i), (m_hunt(gamemode) || m_bomber(gamemode)) ? "_htf" : m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "ktf" : "");
        switch(f.state)
        {
            case CTFF_STOLEN:
            {
                if((f.actor == focus && !isthirdperson) || OUTBORD(f.actor->o.x, f.actor->o.y)) break;
                vec flagpos(f.actor->o);
                flagpos.add(vec(0, 0, 0.3f + (sinf(lastmillis / 100.0f) + 1) / 10));
                rendermodel(sfpath, ANIM_FLAG | ANIM_START | ANIM_DYNALLOC, 0, 0, flagpos, lastmillis / 2.5f + (i ? 180 : 0), 0, 120.0f);
                break;
            }
            case CTFF_DROPPED:
            {
                if(OUTBORD(f.pos.x, f.pos.y)) break;
                rendermodel(fpath, ANIM_FLAG | ANIM_LOOP, 0, 0, f.pos, (float)((e.attr1 + 7) - (e.attr1 + 7) % 15), 0, 120.0f);
                break;
            }
            /*
            default:
            case CTFF_INBASE:
            case CTFF_IDLE:
                break;
            */
        }
        if (!OUTBORD(e.x, e.y) && numflagspawn[i])
            rendermodel(fpath, ANIM_FLAG | ANIM_LOOP | (f.state == CTFF_INBASE ? ANIM_IDLE : ANIM_TRANSLUCENT), 0, 0, vec(e.x, e.y, (float)S(int(e.x), int(e.y))->floor), (float)((e.attr1 + 7) - (e.attr1 + 7) % 15), 0, 120.0f);
    }
}

// these two functions are called when the server acknowledges that you really
// picked up the item (in multiplayer someone may grab it before you).

void pickupeffects(int n, playerent *d, int spawntime)
{
    if(!ents.inrange(n)) return;
    entity &e = ents[n];
    e.spawned = false;
    e.spawntime = lastmillis + spawntime;
    if(!d) return;
    d->pickup(e.type);
    const itemstat &is = d->itemstats(e.type);
    if(&is)
    {
        audiomgr.playsound(is.sound, d);
        if(d==player1)
        {
            /*
                onPickup arg1 legend:
                  0 = pistol clips
                  1 = ammo box
                  2 = grenade
                  3 = health pack
                  4 = helmet
                  5 = armour
                  6 = akimbo
            */
            if(identexists("onPickup"))
            {
                string o;
                itemstat *tmp = NULL;
                switch(e.type)
                {
                    case I_CLIPS:   tmp = &ammostats[GUN_PISTOL]; break;
                    case I_AMMO:    tmp = &ammostats[player1->primary]; break;
                    case I_GRENADE: tmp = &ammostats[GUN_GRENADE]; break;
                    case I_AKIMBO:  tmp = &ammostats[GUN_AKIMBO]; break;
                    case I_HEALTH:
                    case I_HELMET:
                    case I_ARMOUR:  tmp = &powerupstats[e.type-I_HEALTH]; break;
                    default: break;
                }
                if(tmp)
                {
                    formatstring(o)("onPickup %d %d", e.type - 3, m_lss(gamemode, mutators) && e.type == I_GRENADE ? 2 : tmp->add);
                    execute(o);
                }
            }
        }
    }

    weapon *w = NULL;
    switch(e.type)
    {
        case I_AKIMBO: w = d->weapons[GUN_AKIMBO]; break;
        case I_CLIPS: w = d->weapons[d->secondary]; break;
        case I_AMMO: w = d->weapons[d->primary]; break;
        case I_GRENADE: w = d->weapons[GUN_GRENADE]; break;
    }
    if(w) w->onammopicked();
}

// these functions are called when the client touches the item

void trypickup(int n, playerent *d)
{
    entity &e = ents[n];
    switch(e.type)
    {
        case LADDER:
            if(!d->crouching) d->onladder = true;
            break;
    }
}

void checkitems(playerent *d)
{
    if(editmode || d->state!=CS_ALIVE) return;
    d->onladder = false;
    float eyeheight = d->eyeheight;
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type==NOTUSED) continue;
        if(e.type==LADDER)
        {
            if(OUTBORD(e.x, e.y)) continue;
            vec v(e.x, e.y, d->o.z);
            float dist1 = d->o.dist(v);
            float dist2 = d->o.z - (S(e.x, e.y)->floor+eyeheight);
            if(dist1<1.5f && dist2<e.attr1) trypickup(i, d);
            continue;
        }

        if(!e.spawned) continue;
        if(OUTBORD(e.x, e.y)) continue;

        if(e.type==CTF_FLAG) continue;
        // simple 2d collision
        vec v(e.x, e.y, S(e.x, e.y)->floor+eyeheight);
        if(isitem(e.type)) v.z += e.attr1;
        if(d->o.dist(v)<2.5f) trypickup(i, d);
    }
}

void spawnallitems()            // spawns items locally
{
    loopv(ents)
        if(ents[i].fitsmode(gamemode, mutators) || (multiplayer(false) && gamespeed!=100 && (i==-1)))
            ents[i].spawned = true;
}

void resetspawns(int type)
{
    loopv(ents) if(type < 0 || type == ents[i].type) ents[i].spawned = false;
    if(m_noitemsnade(gamemode, mutators) || m_pistol(gamemode, mutators) || m_noitemsammo(gamemode, mutators))
    {
        loopv(ents) ents[i].transformtype(gamemode, mutators);
    }
}

void setspawn(int i)
{
    if(ents.inrange(i))
    {
        ents[i].spawned = true;
        ents[i].spawntime = 0;
    }
}

extern bool sendloadout;
VARFP(nextprimary, 0, GUN_ASSAULT, NUMGUNS - 1,
{
    if (player1->nextprimary != nextprimary)
    {
        player1->nextprimary = nextprimary;
        conoutf("Selected next primary: %s", killname(nextprimary, FRAG_NONE));
    }
    sendloadout = true;
});
VARFP(nextsecondary, 0, GUN_PISTOL, NUMGUNS - 1,
{
    if (player1->nextsecondary != nextsecondary)
    {
        player1->nextsecondary = nextsecondary;
        conoutf("Selected next secondary: %s", killname(nextsecondary, FRAG_NONE));
    }
    sendloadout = true;
});
VARFP(nextperk1, PERK1_NONE, PERK1_NONE, PERK1_MAX - 1,
{
    if (player1->nextperk1 != nextperk1)
    {
        player1->nextperk1 = nextperk1;
        conoutf("Selected next perk 1: %d", nextperk1);
    }
    sendloadout = true;
});
VARFP(nextperk2, PERK2_NONE, PERK2_NONE, PERK2_MAX - 1,
{
    if (player1->nextperk2 != nextperk2)
    {
        player1->nextperk2 = nextperk2;
        conoutf("Selected next perk 2: %d", nextperk2);
    }
    sendloadout = true;
});

// flag ent actions done by the local player

void tryflagdrop() { addmsg(SV_DROPFLAG, "r"); }

// flag ent actions from the net

void flagstolen(int flag, int act)
{
    playerent *actor = getclient(act);
    flaginfo &f = flaginfos[flag];
    f.actor = actor; // could be NULL if we just connected
    f.actor_cn = act;
    f.flagent->spawned = false;
}

void flagdropped(int flag, float x, float y, float z)
{
    flaginfo &f = flaginfos[flag];
    if(OUTBORD(x, y)) return; // valid pos
    /*
    bounceent p;
    p.plclipped = true;
    p.rotspeed = 0.0f;
    p.o.x = x;
    p.o.y = y;
    p.o.z = z;
    p.vel.z = -0.8f;
    p.aboveeye = p.eyeheight = p.maxeyeheight = 0.4f;
    p.radius = 0.1f;

    bool oldcancollide = false;
    if(f.actor)
    {
        oldcancollide = f.actor->cancollide;
        f.actor->cancollide = false; // avoid collision with owner
    }
    loopi(100) // perform physics steps
    {
        moveplayer(&p, 10, true, 50);
        if(p.stuck) break;
    }
    if(f.actor) f.actor->cancollide = oldcancollide; // restore settings

    f.pos.x = round_(p.o.x);
    f.pos.y = round_(p.o.y);
    f.pos.z = round_(p.o.z);
    if(f.pos.z < hdr.waterlevel) f.pos.z = (short) hdr.waterlevel;
    f.flagent->spawned = true;
    */
    f.pos.x = x;
    f.pos.y = y;
    f.pos.z = z;
    f.flagent->spawned = true;
}

void flaginbase(int flag)
{
    flaginfo &f = flaginfos[flag];
    f.actor = NULL; f.actor_cn = -1;
    f.pos = vec(f.flagent->x, f.flagent->y, f.flagent->z);
    f.flagent->spawned = true;
}

void flagidle(int flag)
{
    flaginbase(flag);
    flaginfos[flag].flagent->spawned = false;
}

void entstats(void)
{
    int entcnt[MAXENTTYPES] = {0}, clipents = 0, spawncnt[5] = {0};
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type >= MAXENTTYPES) continue;
        entcnt[e.type]++;
        switch(e.type)
        {
            case MAPMODEL:
            {
                mapmodelinfo &mmi = getmminfo(e.attr2);
                if(&mmi && mmi.h) clipents++;
                break;
            }
            case PLAYERSTART:
                if(e.attr2 < 2) spawncnt[e.attr2]++;
                if(e.attr2 == 100) spawncnt[2]++;
                break;
            case CTF_FLAG:
                if(e.attr2 < 2) spawncnt[e.attr2 + 3]++;
                break;
        }
    }
    loopi(MAXENTTYPES)
    {
        if(entcnt[i]) switch(i)
        {
            case MAPMODEL:      conoutf(" %d %s, %d clipped", entcnt[i], entnames[i], clipents); break;
            case PLAYERSTART:   conoutf(" %d %s, %d CLA, %d RVSF, %d FFA", entcnt[i], entnames[i], spawncnt[0], spawncnt[1], spawncnt[2]); break;
            case CTF_FLAG:      conoutf(" %d %s, %d CLA, %d RVSF", entcnt[i], entnames[i], spawncnt[3], spawncnt[4]); break;
            default:            conoutf(" %d %s", entcnt[i], entnames[i]); break;
        }
    }
    conoutf("total entities: %d", ents.length());
}

COMMAND(entstats, "");

vector<int> changedents;
int lastentsync = 0;

void syncentchanges(bool force)
{
    if(lastmillis - lastentsync < 1000 && !force) return;
    loopv(changedents) if(ents.inrange(changedents[i]))
    {
        entity &e = ents[changedents[i]];
        addmsg(SV_EDITENT, "ri9", changedents[i], e.type, e.x, e.y, e.z, e.attr1, e.attr2, e.attr3, e.attr4);
    }
    changedents.setsize(0);
    lastentsync = lastmillis;
}
