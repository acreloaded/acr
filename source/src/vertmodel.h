VARP(dynshadowsize, 4, 5, 8);
VARP(aadynshadow, 0, 2, 3);
VARP(saveshadows, 0, 1, 1);

VARP(dynshadowquad, 0, 0, 1);

VAR(shadowyaw, 0, 45, 360);
vec shadowdir(0, 0, -1), shadowpos(0, 0, 0);

VAR(dbgstenc, 0, 0, 2);
VAR(dbgvlight, 0, 0, 1);

VARP(mdldlist, 0, 1, 1);

vec modelpos;
float modelyaw, modelpitch;

#include "vertmodel_t.h"

bool vertmodel::enablealphablend = false, vertmodel::enablealphatest = false, vertmodel::enabledepthmask = true, vertmodel::enableoffset = false;
GLuint vertmodel::lasttex = 0;
float vertmodel::lastalphatest = -1;
void *vertmodel::lastvertexarray = NULL, *vertmodel::lasttexcoordarray = NULL, *vertmodel::lastcolorarray = NULL;
glmatrixf vertmodel::matrixstack[32];
int vertmodel::matrixpos = 0;

VARF(mdldyncache, 1, 2, 32, vertmodel::dynalloc.resize(mdldyncache<<20));
VARF(mdlstatcache, 1, 1, 32, vertmodel::statalloc.resize(mdlstatcache<<20));

modelcache vertmodel::dynalloc(mdldyncache<<20), vertmodel::statalloc(mdlstatcache<<20);

vec *getTagPos(const char *mdl, const char *tag){
	vertmodel *m = (vertmodel *)loadmodel(mdl);
	if(!m) return NULL;
	if(m && m->parts.length()) loopi(m->parts.last()->numtags) if(!strcmp(m->parts.last()->tags[i].name, tag)){
		return &m->parts.last()->tags[i].pos;
	}
	return NULL;
}

VAR(lol1, 0, 0, 360);
VAR(lol2, 0, 9, 360);
VAR(lol3, 0, 0, 360);

vec tagTrans(vec v, physent *p, bool mirror){
	vec ret = v;
	float f = ret.magnitude();
	if(f) ret.div(f);
	if(mirror) ret.y = -ret.y;
	ret.rotate_3d(lol1, -lol2, lol3);
	ret.rotate_3d(p->yaw - 90, -p->pitch, p->roll);
	/*
	conoutf("%f %f %f", p->yaw, p->pitch, p->roll);
	ret.mul(f);
	ret.add(p->o);
	extern void newparticle(const vec &o, const vec &d, int fade, int type);
	newparticle(ret, ret, 50, 12);
	ret.sub(p->o);
	*/
	return ret;
}

inline vec *hudgunTag(playerent *p, const char *tag, bool mirror = false){
	s_sprintfd(hudmdl)("weapons/%s", p->weaponsel->info.modelname);
	vec *v = getTagPos(hudmdl, tag);
	if(!v) return NULL;
	return &tagTrans(*v, p, mirror);
}

inline vec *hudEject(playerent *p, bool akimboflip){
	vec *v = hudgunTag(p, "tag_eject", akimboflip);
	return v;
	/*
	if(!v) return NULL;
	return &v->mul(p->ads).div(1000);
	//*/
}

inline vec *hudAds(playerent *p){
	vec *v = hudgunTag(p, "tag_aimpoint");
	if(!v) return NULL;
	return &v->rotate_3d(PI - 90, 5, 6).mul(/*p->ads*/1000).div(1000);
}
