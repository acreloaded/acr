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

const vec *getTagPos(const char *mdl, const char *tag){
	vertmodel *m = (vertmodel *)loadmodel(mdl);
	if(m && m->parts.length()) loopi(m->parts.last()->numtags) if(!strcmp(m->parts.last()->tags[i].name, tag)){
		return &m->parts.last()->tags[i].pos;
	}
	return NULL;
}

inline vec *hudgunTag(playerent *p, const char *tag){
	s_sprintfd(hudmdl)("weapons/%s", p->weaponsel->info.modelname);
	const vec *v = getTagPos(hudmdl, tag);
	if(!v) return NULL;
	static vec v2;
	v2 = *v;
	return &v2;
}

inline vec *hudEject(playerent *p, bool akimboflip){
	vec *v = hudgunTag(p, "tag_eject");
	if(!v) return NULL;
	if(akimboflip) v->y = -v->y;
	v->mul(1.28f).rotate_around_x(p->roll * RAD).rotate_around_y(p->pitch * RAD).rotate_around_z((p->yaw - 90) * RAD);
	/*
	if(p->ads){
		vec *adstrans = hudAds(p, false);
		// fixme
		if(adstrans) v->add(*adstrans); // PI = 180 degrees in radians
	}//*/
	return v;
}

VAR(aimy, 0, 190, 360); // aim yaw correction
VAR(aimp, 0, 7, 360); // aim pitch correction

inline vec *hudAds(playerent *p, bool flip){
	vec *v = hudgunTag(p, "tag_aimpoint");
	if(!v || !p->ads) return NULL;
	/*
	v->rotate_around_y(aimp * RAD).rotate_around_z(aimy * RAD);
	v->rotate_around_x(-p->roll * RAD).rotate_around_y(-p->pitch * RAD).rotate_around_z((p->yaw + 90) * RAD);
	*/
	/*
		matrixstack[0].identity();
		matrixstack[0].translate(o);
		matrixstack[0].rotate_around_z((yaw+180)*RAD);
		matrixstack[0].rotate_around_y(-pitch*RAD);
		if(anim&ANIM_MIRROR || scale!=1) matrixstack[0].scale(scale, anim&ANIM_MIRROR ? -scale : scale, scale);
	*/
	if(flip) v->div(1.28f).rotate_around_y(p->pitch*RAD).rotate_around_z((p->yaw+270)*-RAD);
	//else v->rotate_around_z((p->yaw + 270)*RAD).rotate_around_y(p->pitch*-RAD).mul(1.28f);
	return &v->mul(p->ads).div(1000);
}
