#include "cube.h"

VARP(animationinterpolationtime, 0, 150, 1000);

model *loadingmodel = NULL;

#include "tristrip.h"
#include "modelcache.h"
#include "vertmodel.h"
#include "md2.h"
#include "md3.h"

#define checkmdl if(!loadingmodel) { conoutf("not loading a model"); return; }

void mdlcullface(int *cullface)
{
    checkmdl;
    loadingmodel->cullface = *cullface!=0;
}

COMMAND(mdlcullface, "i");

void mdlvertexlight(int *vertexlight)
{
    checkmdl;
    loadingmodel->vertexlight = *vertexlight!=0;
}

COMMAND(mdlvertexlight, "i");

void mdltranslucent(int *translucency)
{
    checkmdl;
    loadingmodel->translucency = *translucency/100.0f;
}

COMMAND(mdltranslucent, "i");

void mdlalphatest(int *alphatest)
{
    checkmdl;
    loadingmodel->alphatest = *alphatest/100.0f;
}

COMMAND(mdlalphatest, "i");

void mdlalphablend(int *alphablend) //ALX Alpha channel models
{
    checkmdl;
    loadingmodel->alphablend = *alphablend!=0;
}
COMMAND(mdlalphablend, "i");

void mdlscale(int *percent)
{
    checkmdl;
    float scale = 0.3f;
    if(*percent>0) scale = *percent/100.0f;
    else if(*percent<0) scale = 0.0f;
    loadingmodel->scale = scale;
}

COMMAND(mdlscale, "i");

void mdltrans(float *x, float *y, float *z)
{
    checkmdl;
    loadingmodel->translate = vec(*x, *y, *z);
}

COMMAND(mdltrans, "fff");

void mdlshadowdist(int *dist)
{
    checkmdl;
    loadingmodel->shadowdist = *dist;
}

COMMAND(mdlshadowdist, "i");

void mdlcachelimit(int *limit)
{
    checkmdl;
    loadingmodel->cachelimit = *limit;
}

COMMAND(mdlcachelimit, "i");

vector<mapmodelinfo> mapmodels;

void mapmodel(int *rad, int *h, int *zoff, char *snap, char *name)
{
    mapmodelinfo &mmi = mapmodels.add();
    mmi.rad = *rad;
    mmi.h = *h;
    mmi.zoff = *zoff;
    mmi.m = NULL;
    formatstring(mmi.name)("mapmodels/%s", name);
}

void mapmodelreset()
{
    if(execcontext==IEXC_MAPCFG) mapmodels.shrink(0);
}

mapmodelinfo *getmminfo(int i) { return mapmodels.inrange(i) ? &mapmodels[i] : NULL; }

COMMAND(mapmodel, "iiiss");
COMMAND(mapmodelreset, "");

hashtable<const char *, model *> mdllookup;
hashtable<const char*, char> mdlnotfound;

model *loadmodel(const char *name, int i, bool trydl)
{
    if(!name)
    {
        if(!mapmodels.inrange(i)) return NULL;
        mapmodelinfo &mmi = mapmodels[i];
        if(mmi.m) return mmi.m;
        name = mmi.name;
    }
    if (mdlnotfound.access(name)) return NULL;
    model **mm = mdllookup.access(name);
    model *m;
    if(mm) m = *mm;
    else
    {
        pushscontext(IEXC_MDLCFG);
        m = new md2(name);
        loadingmodel = m;
        if(!m->load())
        {
            delete m;
            m = new md3(name);
            loadingmodel = m;
            if(!m->load())
            {
                delete m;
                m = loadingmodel = NULL;
                if(trydl)
                {
                    defformatstring(dl)("packages/models/%s", name);
                    requirepackage(PCK_MAPMODEL, dl);
                }
            }
        }
        popscontext();
        if(!loadingmodel)
        {
            if(!trydl)
            {
                conoutf("\f3failed to load model %s", name);
                m = NULL;
                mdlnotfound.access(newstring(name), 0);
            }
        }
        else mdllookup.access(m->name(), m);
        loadingmodel = NULL;
    }
    if(m == NULL) return NULL;
    if(mapmodels.inrange(i) && !mapmodels[i].m) mapmodels[i].m = m;
    return m;
}

void cleanupmodels()
{
    enumerate(mdllookup, model *, m, m->cleanup());
}

VARP(dynshadow, 0, 40, 100);
VARP(dynshadowdecay, 0, 1000, 3000);

struct batchedmodel
{
    vec o;
    int anim, varseed, tex;
    float yaw, pitch, speed;
    int basetime;
    playerent *d;
    int attached;
    float scale;
    float zoomed;
};
struct modelbatch
{
    model *m;
    vector<batchedmodel> batched;
};
static vector<modelbatch *> batches;
static vector<modelattach> modelattached;
static int numbatches = -1;

void startmodelbatches()
{
    numbatches = 0;
    modelattached.setsize(0);
}

batchedmodel &addbatchedmodel(model *m)
{
    modelbatch *b = NULL;
    if(m->batch>=0 && m->batch<numbatches && batches[m->batch]->m==m) b = batches[m->batch];
    else
    {
        if(numbatches<batches.length())
        {
            b = batches[numbatches];
            b->batched.setsize(0);
        }
        else b = batches.add(new modelbatch);
        b->m = m;
        m->batch = numbatches++;
    }
    return b->batched.add();
}

void renderbatchedmodel(model *m, batchedmodel &b)
{
    modelattach *a = NULL;
    if(b.attached>=0) a = &modelattached[b.attached];

    if(stenciling)
    {
        m->render(b.anim|ANIM_NOSKIN, b.varseed, b.speed, b.basetime, b.o, b.yaw, b.pitch, b.d, a, b.scale, b.zoomed);
        return;
    }

    int x = (int)b.o.x, y = (int)b.o.y;
    if(!OUTBORD(x, y))
    {
        sqr *s = S(x, y);
        glColor3ub(s->r, s->g, s->b);
    }
    else glColor3f(1, 1, 1);

    m->setskin(b.tex);

    if(b.anim&ANIM_TRANSLUCENT)
    {
        glDepthMask(GL_FALSE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m->render(b.anim|ANIM_NOSKIN, b.varseed, b.speed, b.basetime, b.o, b.yaw, b.pitch, b.d, a, b.scale, b.zoomed);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GLfloat color[4];
        glGetFloatv(GL_CURRENT_COLOR, color);
        glColor4f(color[0], color[1], color[2], m->translucency);
    }

    m->render(b.anim, b.varseed, b.speed, b.basetime, b.o, b.yaw, b.pitch, b.d, a, b.scale, b.zoomed);

    if(b.anim&ANIM_TRANSLUCENT)
    {
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }
}

void renderbatchedmodelshadow(model *m, batchedmodel &b)
{
    int x = (int)b.o.x, y = (int)b.o.y;
    if(OUTBORD(x, y)) return;
    sqr *s = S(x, y);
    vec center(b.o.x, b.o.y, s->floor);
    if(s->type==FHF) center.z -= s->vdelta/4.0f;
    if(dynshadowquad && center.z-0.1f>b.o.z) return;
    center.z += 0.1f;
    modelattach *a = NULL;
    if(b.attached>=0) a = &modelattached[b.attached];
    float intensity = dynshadow/100.0f;
    if(dynshadowdecay) switch(b.anim&ANIM_INDEX)
    {
        case ANIM_DECAY:
        case ANIM_LYING_DEAD:
            intensity *= max(1.0f - float(lastmillis - b.basetime)/dynshadowdecay, 0.0f);
            break;
    }
    glColor4f(0, 0, 0, intensity);
    m->rendershadow(b.anim, b.varseed, b.speed, b.basetime, dynshadowquad ? center : b.o, b.yaw, a);
}

static int sortbatchedmodels(const batchedmodel *x, const batchedmodel *y)
{
    if(x->tex < y->tex) return -1;
    if(x->tex > y->tex) return 1;
    return 0;
}

struct translucentmodel
{
    model *m;
    batchedmodel *batched;
    float dist;
};

static int sorttranslucentmodels(const translucentmodel *x, const translucentmodel *y)
{
    if(x->dist > y->dist) return -1;
    if(x->dist < y->dist) return 1;
    return 0;
}

void clearmodelbatches()
{
    numbatches = -1;
}

void endmodelbatches(bool flush)
{
    vector<translucentmodel> translucent;
    loopi(numbatches)
    {
        modelbatch &b = *batches[i];
        if(b.batched.empty()) continue;
        loopvj(b.batched) if(b.batched[j].tex) { b.batched.sort(sortbatchedmodels); break; }
        b.m->startrender();
        loopvj(b.batched)
        {
            batchedmodel &bm = b.batched[j];
            if(bm.anim&ANIM_TRANSLUCENT)
            {
                translucentmodel &tm = translucent.add();
                tm.m = b.m;
                tm.batched = &bm;
                tm.dist = camera1->o.dist(bm.o);
                continue;
            }
            renderbatchedmodel(b.m, bm);
        }
        if(dynshadow && b.m->hasshadows() && (!reflecting || refracting) && (!stencilshadow || !hasstencil || stencilbits < 8))
        {
            loopvj(b.batched)
            {
                batchedmodel &bm = b.batched[j];
                if(bm.anim&ANIM_TRANSLUCENT) continue;
                renderbatchedmodelshadow(b.m, bm);
            }
        }
        b.m->endrender();
    }
    if(translucent.length())
    {
        translucent.sort(sorttranslucentmodels);
        model *lastmodel = NULL;
        loopv(translucent)
        {
            translucentmodel &tm = translucent[i];
            if(lastmodel!=tm.m)
            {
                if(lastmodel) lastmodel->endrender();
                (lastmodel = tm.m)->startrender();
            }
            renderbatchedmodel(tm.m, *tm.batched);
        }
        if(lastmodel) lastmodel->endrender();
    }
    if(flush) clearmodelbatches();
}

const int dbgmbatch = 0;
//VAR(dbgmbatch, 0, 0, 1);

VARP(popdeadplayers, 0, 0, 1);
void rendermodel(const char *mdl, int anim, int tex, float rad, const vec &o, float yaw, float pitch, float speed, int basetime, playerent *d, modelattach *a, float scale, float zoomed)
{
    if(popdeadplayers && d && a)
    {
        int acv = anim&ANIM_INDEX;
        if( acv == ANIM_DECAY || acv == ANIM_LYING_DEAD || acv == ANIM_CROUCH_DEATH || acv == ANIM_DEATH ) return;
    }
    model *m = loadmodel(mdl);
    if(!m || (stenciling && (m->shadowdist <= 0 || anim&ANIM_TRANSLUCENT))) return;

    if(rad >= 0)
    {
        if(!rad) rad = m->radius;
        if(isoccluded(camera1->o.x, camera1->o.y, o.x-rad, o.y-rad, rad*2)) return;
    }

    if(stenciling && d && !raycubelos(camera1->o, o, d->radius))
    {
        vec target(o);
        target.z += d->eyeheight;
        if(!raycubelos(camera1->o, target, d->radius)) return;
    }

    int varseed = 0;
    if(d) switch(anim&ANIM_INDEX)
    {
        case ANIM_DEATH:
        case ANIM_LYING_DEAD: varseed = (int)(size_t)d + d->lastpain; break;
        default: varseed = (int)(size_t)d + d->lastaction; break;
    }

    if(a) for(int i = 0; a[i].tag; i++)
    {
        if(a[i].name) a[i].m = loadmodel(a[i].name);
        //if(a[i].m && a[i].m->type()!=m->type()) a[i].m = NULL;
    }

    if(numbatches>=0 && !dbgmbatch)
    {
        batchedmodel &b = addbatchedmodel(m);
        b.o = o;
        b.anim = anim;
        b.varseed = varseed;
        b.tex = tex;
        b.yaw = yaw;
        b.pitch = pitch;
        b.speed = speed;
        b.basetime = basetime;
        b.d = d;
        b.attached = a ? modelattached.length() : -1;
        if(a) for(int i = 0;; i++) { modelattached.add(a[i]); if(!a[i].tag) break; }
        b.scale = scale;
        b.zoomed = zoomed;
        return;
    }

    if(stenciling)
    {
        m->startrender();
        m->render(anim|ANIM_NOSKIN, varseed, speed, basetime, o, yaw, pitch, d, a, scale, zoomed);
        m->endrender();
        return;
    }

    m->startrender();

    int x = (int)o.x, y = (int)o.y;
    if(!OUTBORD(x, y))
    {
        sqr *s = S(x, y);
        if(!(anim&ANIM_TRANSLUCENT) && dynshadow && m->hasshadows() && (!reflecting || refracting) && (!stencilshadow || !hasstencil || stencilbits < 8))
        {
            vec center(o.x, o.y, s->floor);
            if(s->type==FHF) center.z -= s->vdelta/4.0f;
            if(!dynshadowquad || center.z-0.1f<=o.z)
            {
                center.z += 0.1f;
                float intensity = dynshadow/100.0f;
                if(dynshadowdecay) switch(anim&ANIM_INDEX)
                {
                    case ANIM_DECAY:
                    case ANIM_LYING_DEAD:
                        intensity *= max(1.0f - float(lastmillis - basetime)/dynshadowdecay, 0.0f);
                        break;
                }
                glColor4f(0, 0, 0, intensity);
                m->rendershadow(anim, varseed, speed, basetime, dynshadowquad ? center : o, yaw, a);
            }
        }
        glColor3ub(s->r, s->g, s->b);
    }
    else glColor3f(1, 1, 1);

    m->setskin(tex);

    if(anim&ANIM_TRANSLUCENT)
    {
        glDepthMask(GL_FALSE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m->render(anim|ANIM_NOSKIN, varseed, speed, basetime, o, yaw, pitch, d, a, scale, zoomed);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GLfloat color[4];
        glGetFloatv(GL_CURRENT_COLOR, color);
        glColor4f(color[0], color[1], color[2], m->translucency);
    }

    m->render(anim, varseed, speed, basetime, o, yaw, pitch, d, a, scale, zoomed);

    if(anim&ANIM_TRANSLUCENT)
    {
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }

    m->endrender();
}

int findanim(const char *name)
{
    const char *names[] = { "idle", "run", "attack", "pain", "jump", "land", "flipoff", "salute", "taunt", "wave", "point", "crouch idle", "crouch walk", "crouch attack", "crouch pain", "crouch death", "death", "lying dead", "flag", "gun idle", "gun shoot", "gun reload", "gun throw", "mapmodel", "trigger", "decay", "all" };
    loopi(sizeof(names)/sizeof(names[0])) if(!strcmp(name, names[i])) return i;
    return -1;
}

void loadskin(const char *dir, const char *altdir, Texture *&skin) // model skin sharing
{
    #define ifnoload if((skin = textureload(path))==notexture)
    defformatstring(path)("packages/models/%s/skin.jpg", dir);
    ifnoload
    {
        strcpy(path+strlen(path)-3, "png");
        ifnoload
        {
            formatstring(path)("packages/models/%s/skin.jpg", altdir);
            ifnoload
            {
                strcpy(path+strlen(path)-3, "png");
                ifnoload return;
            }
        }
    }
}

void preload_playermodels()
{
    model *playermdl = loadmodel("playermodels");
    if(dynshadow && playermdl) playermdl->genshadows(8.0f, 4.0f);
    loopi(NUMGUNS)
    {
        defformatstring(widn)("modmdlvwep%d", i);
        defformatstring(vwep)("weapons/%s/world", identexists(widn)?getalias(widn):guns[i].modelname);
        model *vwepmdl = loadmodel(vwep);
        if(dynshadow && vwepmdl) vwepmdl->genshadows(8.0f, 4.0f);
    }
}

void preload_entmodels()
{
     string buf;

     extern const char *entmdlnames[];
     loopi(I_AKIMBO-I_CLIPS+1)
     {
         strcpy(buf, "pickups/");

         defformatstring(widn)("modmdlpickup%d", i-3);

         if (identexists(widn))
             concatstring(buf, getalias(widn));
         else
             concatstring(buf, entmdlnames[i]);

         model *mdl = loadmodel(buf);

         if(dynshadow && mdl) mdl->genshadows(8.0f, 2.0f);
     }
     static const char *bouncemdlnames[] = { "misc/gib01", "misc/gib02", "misc/gib03", "weapons/grenade/static" };
     loopi(sizeof(bouncemdlnames)/sizeof(bouncemdlnames[0]))
     {
         model *mdl = NULL;
         defformatstring(widn)("modmdlbounce%d", i);

         if (identexists(widn))
         mdl = loadmodel(getalias(widn));
         else
         mdl = loadmodel(bouncemdlnames[i]);

         if(dynshadow && mdl) mdl->genshadows(8.0f, 2.0f);
     }
}

void preload_mapmodels(bool trydl)
{
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type!=MAPMODEL || !mapmodels.inrange(e.attr2)) continue;
        loadmodel(NULL, e.attr2, trydl);
        if(e.attr4) lookuptexture(e.attr4, notexture, trydl);
    }
}

VAR(dbghbox, 0, 0, 1); // restored from previous versions of AC

#define HBOXPRECISION 32
#define HBOXPRECISIONV 10

inline void renderhboxpart(playerent *d, vec top, vec bottom, vec up)
{
    vec spoke;
    spoke.orthogonal(up);
    spoke.normalize().mul(d->radius);

    glBegin(GL_LINE_LOOP);
    loopi(HBOXPRECISION)
    {
        vec pos(spoke);
        pos.rotate(2*M_PI*i/(float)HBOXPRECISION, up).add(top);
        glVertex3fv(pos.v);
    }
    glEnd();
    glBegin(GL_LINE_LOOP);
    loopi(HBOXPRECISION)
    {
        vec pos(spoke);
        pos.rotate(2*M_PI*i/ (float)HBOXPRECISION, up).add(bottom);
        glVertex3fv(pos.v);
    }
    glEnd();
    glBegin(GL_LINES);
    loopi(HBOXPRECISIONV)
    {
        vec pos(spoke);
        pos.rotate(2*M_PI*i/(float)HBOXPRECISIONV, up).add(bottom);
        glVertex3fv(pos.v);
        pos.sub(bottom).add(top);
        glVertex3fv(pos.v);
    }
    glEnd();
}

void renderhbox(playerent *d, const vec &oldhead, bool dark = false)
{
    typedef GLubyte hbox_color_t[3];
    static const hbox_color_t hbox_colors[][3] = {
        // { head, torso, legs },
        // friendly
        { { 128, 255,   0 }, {   0, 255,   0 }, {  64, 255,   0 } },
        // enemy
        { { 255, 128,   0 }, { 255,   0,   0 }, { 255,  64,   0 } },
        // darker versions
        { {  64, 128,   0 }, {   0, 128,   0 }, {  32, 128,   0 } },
        { { 128,  64,   0 }, { 128,   0,   0 }, { 128,  32,   0 } },
    };
    const hbox_color_t *hbox_color = hbox_colors[(focus == d || isteam(focus, d) ? 0 : 1) | (dark ? 2 : 0)];

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float y = d->yaw*RAD, p = (d->pitch/4+90)*RAD, c = cosf(p);
    vec bottom(d->o), up(sinf(y)*c, -cosf(y)*c, sinf(p)), top(up), mid(up);
    bottom.z -= d->eyeheight;
    float h = d->eyeheight /*+ d->aboveeye*/;          // shoulder limit
    mid.mul(h*LEGPART).add(bottom);
    top.mul(h).add(bottom);

    const vec &head = d->head.x >= 0 ? d->head : oldhead;
    if(head.x >= 0)
    {
        glColor3ubv(hbox_color[0]);
        glBegin(GL_LINE_LOOP);
        loopi(HBOXPRECISION)
        {
            vec pos(camright);
            pos.rotate(2*M_PI*i/(float)HBOXPRECISION, camdir).mul(HEADSIZE).add(head);
            glVertex3fv(pos.v);
        }
        glEnd();

        glBegin(GL_LINES);
        glVertex3fv(bottom.v);
        glVertex3fv(head.v);
        glEnd();
    }

    glColor3ubv(hbox_color[1]);
    renderhboxpart(d,top,mid,up);
    glColor3ubv(hbox_color[2]);
    renderhboxpart(d,mid,bottom,up);

    glEnable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
}

void renderclient(playerent *d, const char *mdlname, const char *vwepname, int tex)
{
    if(render_void)
    {
        (d->head = d->o).add(vec(0, -.25f, .25f).rotate_around_z(d->yaw * RAD));
        d->muzzle = d->eject = vec(-1, -1, -1);
        return;
    }

    int anim = ANIM_IDLE|ANIM_LOOP;
    float speed = 0.0;
    vec o(d->o);
    o.z -= d->eyeheight;
    int basetime = -((int)(size_t)d&0xFFF);
    if(d->state==CS_DEAD)
    {
        if(d==player1 && d->allowmove()) return;
        loopv(bounceents) if(bounceents[i]->bouncetype==BT_GIB && bounceents[i]->owner==d) return;
        d->pitch = 0.1f;
        anim = ANIM_DEATH;
        basetime = d->lastpain;
        int t = lastmillis-d->lastpain;
        if(t<0 || t>20000) return;
        if(t>2000)
        {
            anim = ANIM_LYING_DEAD|ANIM_NOINTERP|ANIM_LOOP;
            basetime += 2000;
            t -= 2000;
            o.z -= t*t/10000000000.0f*t;
        }
    }
    else if(d->state==CS_EDITING)                   { anim = ANIM_JUMP|ANIM_END; }
    else if(d->state==CS_WAITING)                   { anim = ANIM_SALUTE|ANIM_LOOP|ANIM_TRANSLUCENT; }
    else if(lastmillis-d->lastpain<300)             { anim = d->crouching ? ANIM_CROUCH_PAIN : ANIM_PAIN; speed = 300.0f/4; basetime = d->lastpain; }
//     else if(!d->onfloor && d->timeinair>50)         { anim = ANIM_JUMP|ANIM_END; }
    else if(!d->onfloor && d->timeinair>50)         { anim = (d->crouching ? ANIM_CROUCH_WALK : ANIM_JUMP)|ANIM_END; }
    else if(d->weaponsel==d->lastattackweapon && lastmillis-d->lastaction<300 && d->lastpain < d->lastaction) { anim = d->crouching ? ANIM_CROUCH_ATTACK : ANIM_ATTACK; speed = 300.0f/8; basetime = d->lastaction; }
    else if(!d->move && !d->strafe)                 { anim = (d->crouching ? ANIM_CROUCH_IDLE : ANIM_IDLE)|ANIM_LOOP; }
    else                                            { anim = (d->crouching ? ANIM_CROUCH_WALK : ANIM_RUN)|ANIM_LOOP; speed = 1860/d->maxspeed; }
    if(d->move < 0) anim |= ANIM_REVERSE;
    modelattach a[5];
    int numattach = 0;
    if(vwepname)
    {
        a[numattach].name = vwepname;
        a[numattach].tag = "tag_weapon";
        numattach++;
    }

    if(!stenciling && !reflecting && !refracting)
    {
        if(d->weaponsel==d->lastattackweapon && lastmillis-d->lastaction < d->weaponsel->flashtime())
            anim |= ANIM_PARTICLE;
        if(d != player1 && d->state==CS_ALIVE)
        {
            d->head = vec(-1, -1, -1);
            a[numattach].tag = "tag_head";
            a[numattach].pos = &d->head;
            numattach++;
            d->muzzle = vec(-1, -1, -1);
            a[numattach].tag = "tag_muzzle";
            a[numattach].pos = &d->muzzle;
            numattach++;
            d->eject = vec(-1, -1, -1);
            a[numattach].tag = "tag_eject";
            a[numattach].pos = &d->eject;
            numattach++;
        }
    }
    if ((isthirdperson && d == focus) || d->protect(lastmillis, gamemode, mutators))
    {
        anim |= ANIM_TRANSLUCENT; // see through followed player
        if(stenciling) return;
    }
    rendermodel(mdlname, anim|ANIM_DYNALLOC, tex, 1.5f, o, d->yaw+90, d->pitch/4, speed, basetime, d, a);
}

VARP(teamdisplaymode, 0, 1, 2);

#define SKINBASE "packages/models/playermodels"
VARP(hidecustomskins, 0, 0, 2);
static vector<char *> playerskinlist;

const char *getclientskin(const char *name, const char *suf)
{
    static string tmp;
    int suflen = (int)strlen(suf), namelen = (int)strlen(name);
    const char *s, *r = NULL;
    loopv(playerskinlist)
    {
        s = playerskinlist[i];
        int sl = (int)strlen(s) - suflen;
        if(sl > 0 && !strcmp(s + sl, suf))
        {
            if(namelen == sl && !strncmp(name, s, namelen)) return s; // exact match
            if(s[sl - 1] == '_')
            {
                copystring(tmp, s);
                tmp[sl - 1] = '\0';
                if(strstr(name, tmp)) r = s; // partial match
            }
        }
    }
    return r;
}

void updateclientname(playerent *d)
{
    static bool gotlist = false;
    if(!gotlist) listfiles(SKINBASE "/custom", "jpg", playerskinlist);
    gotlist = true;
    if(!d || !playerskinlist.length()) return;
    d->skin_noteam = getclientskin(d->name, "_ffa");
    d->skin_cla = getclientskin(d->name, "_cla");
    d->skin_rvsf = getclientskin(d->name, "_rvsf");
}

void renderclient(playerent *d)
{
    if(!d) return;
    int team = team_base(d->team);
    const char *cs = NULL, *skinbase = SKINBASE, *teamname = team_basestring(team);
    int skinid = 1 + d->skin();
    string skin;
    if(hidecustomskins == 0 || (hidecustomskins == 1 && !m_team(gamemode, mutators)))
    {
        cs = team ? d->skin_rvsf : d->skin_cla;
        if(!m_team(gamemode, mutators) && d->skin_noteam) cs = d->skin_noteam;
    }
    if(cs)
        formatstring(skin)("%s/custom/%s.jpg", skinbase, cs);
    else
    {
        if(!m_team(gamemode, mutators) || !teamdisplaymode) formatstring(skin)("%s/%s/%02i.jpg", skinbase, teamname, skinid);
        else switch(teamdisplaymode)
        {
            case 1: formatstring(skin)("%s/%s/%02i_%svest.jpg", skinbase, teamname, skinid, team ? "blue" : "red"); break;
            case 2: default: formatstring(skin)("%s/%s/%s.jpg", skinbase, teamname, team ? "blue" : "red"); break;
        }
    }
    string vwep;
    if (d->weaponsel)
    {
        defformatstring(widn)("modmdlvwep%d", d->weaponsel->type);
        formatstring(vwep)("weapons/%s/world", identexists(widn) ? getalias(widn) : d->weaponsel->info.modelname);
    }
    else vwep[0] = 0;

    vec oldhead = d->head;
    renderclient(d, "playermodels", vwep[0] ? vwep : NULL, -(int)textureload(skin)->id);

    if(!stenciling && !reflecting && !refracting)
    {
        renderaboveheadicon(d);
        if ((d->state == CS_ALIVE || d->state == CS_EDITING) && ((dbghbox && watchingdemo) || render_void || m_psychic(gamemode, mutators)))
        {
            if (m_psychic(gamemode, mutators))
            {
                //glDisable(GL_DEPTH_TEST);
                glDepthFunc(GL_GEQUAL);
                glDepthMask(GL_FALSE);
                renderhbox(d, oldhead, true);
                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LESS);
                //glEnable(GL_DEPTH_TEST);
            }
            renderhbox(d, oldhead);
        }
        extern int fakelasertest;
        if (fakelasertest >= 2)
        {
            glDisable(GL_TEXTURE_2D);
            glBegin(GL_LINES);
            linestyle(1.5f, 255, 0, 0);
            vec from(d->muzzle.x >= 0 ? d->muzzle : d->o);
            glVertex3f(from.x, from.y, from.z);
            vec to(sinf(RAD*d->yaw)*cosf(RAD*d->pitch), -cosf(RAD*d->yaw)*cosf(RAD*d->pitch), sinf(RAD*d->pitch));
            to.add(from);
            traceShot(from, to);
            glVertex3f(to.x, to.y, to.z);
            glEnd();
            glEnable(GL_TEXTURE_2D);
        }
    }
}

void renderclients()
{
    loopv(players)
    {
        playerent *d = players[i];
        if (!d || (player1->isspectating() && !isthirdperson && d == focus))
            continue;
        renderclient(d);
    }
    if (isthirdperson || player1->state == CS_DEAD || focus != player1 || (reflecting && !refracting)) renderclient(player1);
}
