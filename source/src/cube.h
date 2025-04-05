#ifndef __CUBE_H__
#define __CUBE_H__

#include "platform.h"
#include "tools.h"
#include "geom.h"
#include "model.h"
#include "protocol.h"
#include "sound.h"
#include "weapon.h"
#include "entity.h"
#include "world.h"
#include "command.h"

#ifndef STANDALONE
 #include "varray.h"
 #include "vote.h"
 #include "console.h"
 enum
 {
   SDL_AC_BUTTON_WHEELDOWN = -5,
   SDL_AC_BUTTON_WHEELUP = -4,
   SDL_AC_BUTTON_RIGHT = -3,
   SDL_AC_BUTTON_MIDDLE = -2,
   SDL_AC_BUTTON_LEFT = -1
 };
#endif

extern sqr *world, *wmip[];             // map data, the mips are sequential 2D arrays in memory
extern header hdr;                      // current map header
extern int sfactor, ssize;              // ssize = 2^sfactor
extern int cubicsize, mipsize;          // cubicsize = ssize^2
extern physent *camera1;                // camera representing perspective of player, usually player1
extern playerent *player1;              // special client ent that receives input and acts as camera
extern playerent *focus;                // the camera points here, or else it's player1
extern vector<playerent *> players;     // all the other clients (in multiplayer)
extern vector<bounceent *> bounceents;
extern bool editmode;
extern vector<entity> ents;             // map entities
extern vector<int> eh_ents;             // edithide entities
extern vec worldpos, camup, camright, camdir; // current target of the crosshair in the world
extern playerent *worldhit; extern int worldhitzone; extern vec worldhitpos;
extern int lastmillis, totalmillis, nextmillis; // last time
extern int curtime;                     // current frame time
extern int curtime_real;                //  ^, but for offline with gamespeed!=100
extern int interm;
extern int gamemode, nextmode, mutators, nextmuts;
extern int gamespeed;
extern int xtraverts;
extern float fovy, aspect;
extern int farplane;
extern bool minimap, reflecting, refracting;
extern int stenciling, stencilshadow;
extern bool render_void;
extern bool intermission;
extern int arenaintermission;
extern hashtable<char *, enet_uint32> mapinfo;
extern int hwtexsize, hwmaxaniso;
extern int numspawn[3], maploaded, numflagspawn[2];
extern int verbose;

#define AC_VERSION 180200
#define AC_MASTER_URI "acrms.victorz.ca"
#define AC_MASTER_PORT 80
#ifndef AC_MASTER_URI
#define AC_MASTER_DOMAIN "ms.acr"
#define AC_MASTER_URI AC_MASTER_DOMAIN
#define AC_MASTER_IPS "216.34.181.97"
#endif
//#define MAXCL 16

#include "protos.h"                     // external function decls

#endif

