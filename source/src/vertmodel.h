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
