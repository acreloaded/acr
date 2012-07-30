// entities.cpp: map entity related functions (pickup etc.)

#include "pch.h"
#include "cube.h"

vector<entity> ents;

const char *entmdlnames[I_AKIMBO-I_CLIPS+1] =
{
	"pickups/pistolclips", "pickups/ammobox", "pickups/nade", "pickups/health", "pickups/helmet", "pickups/kevlar", "pickups/akimbo",
};

void renderent(entity &e)
{
	const char *mdlname = entmdlnames[e.type-I_CLIPS];
	float z = (float)(1+sinf(lastmillis/100.0f+e.x+e.y)/20),
		  yaw = lastmillis/10.0f;
	rendermodel(mdlname, ANIM_MAPMODEL|(e.spawned ? 0 : ANIM_TRANSLUCENT)|ANIM_LOOP|ANIM_DYNALLOC, 0, 0, vec(e.x, e.y, z+S(e.x, e.y)->floor+e.attr1), yaw, 0);
}

void renderclip(entity &e)
{
	float xradius = max(float(e.attr2), 0.1f), yradius = max(float(e.attr3), 0.1f);
	vec bbmin(e.x - xradius, e.y - yradius, float(S(e.x, e.y)->floor+e.attr1)),
		bbmax(e.x + xradius, e.y + yradius, bbmin.z + max(float(e.attr4), 0.1f));

	glDisable(GL_TEXTURE_2D);
	switch(e.type){
        case CLIP:		linestyle(1, 0xFF, 0xFF, 0); break;  // yellow
		case MAPMODEL:	linestyle(1, 0xFF, 0, 0xFF);    break;  // magenta
        case PLCLIP:	linestyle(1, 0, 0xFF, 0); break;  // green
		default:		linestyle(1, 0xFF, 0, 0); break;  // red
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

VARP(showmodelclipping, 0, 1, 1);

void renderentities()
{
	if(editmode && !reflecting && !refracting && !stenciling)
	{
		static int lastsparkle = 0;
		if(lastmillis - lastsparkle >= 20)
		{
			lastsparkle = lastmillis - (lastmillis%20);
			int closest = closestent();
			loopv(ents)
			{
				entity &e = ents[i];
				if(e.type==NOTUSED) continue;
				vec v(e.x, e.y, e.z);
				if(vec(v).sub(camera1->o).dot(camdir) < 0) continue;
				particle_splash(i == closest ? 12 : 2, 2, 40, v);
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
		else if(editmode || m_edit(gamemode))
		{
			if(e.type==CTF_FLAG)
			{
				defformatstring(path)("pickups/flags/%s", team_string(e.attr2));
				rendermodel(path, ANIM_FLAG|ANIM_LOOP, 0, 0, vec(e.x, e.y, (float)S(e.x, e.y)->floor), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 120.0f);
			}
			else if((e.type==CLIP || e.type==PLCLIP) && !stenciling) renderclip(e);
			else if(e.type == PLAYERSTART){
				defformatstring(skin)(e.attr2 < 2 ? "packages/models/playermodels/%s/%s.jpg" : "packages/models/playermodels/skin.jpg",
					team_string(e.attr2), e.attr2 ? "blue" : "red");
				rendermodel("playermodels", ANIM_IDLE|ANIM_TRANSLUCENT|/*ANIM_LOOP*/ANIM_END|ANIM_DYNALLOC, -(int)textureload(skin)->id, 1.5f, vec(e.x, e.y, (float)S(e.x, e.y)->floor), e.attr1+90, 0/4);
			}
			else if(showmodelclipping && e.type == MAPMODEL && !stenciling)
			{
				mapmodelinfo &mmi = getmminfo(e.attr2);
				if(&mmi && mmi.h)
				{
					entity ce = e;
					ce.attr1 = mmi.zoff+e.attr3;
					ce.attr2 = ce.attr3 = mmi.rad;
					ce.attr4 = mmi.h;
					renderclip(ce);
				}
			}
		}
	}
	/*if(m_confirm(gamemode, mutators))*/ loopv(confirms){
		// unconfirmed kills
		const int fixteam = player1->team == TEAM_SPECT ? TEAM_BLUE : player1->team;
		const float yaw = lastmillis/10.0f;
		vec o = confirms[i].o;
		o.z += (float)(1+sinf(lastmillis/100.0f+confirms[i].o.x+confirms[i].o.y)/20) - PLAYERHEIGHT;
		rendermodel(fixteam == confirms[i].team ? "pickups/flags/ktf" : "pickups/flags/RED_htf", ANIM_FLAG|ANIM_LOOP|ANIM_DYNALLOC, 0, 0, o, yaw, 0);
	}
	if(m_affinity(gamemode)) loopi(2)
	{
		flaginfo &f = flaginfos[i];
		entity &e = *f.flagent;
		defformatstring(fpath)("pickups/flags/%s%s", m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "" : team_string(i),  (m_hunt(gamemode) || m_bomber(gamemode)) ? "_htf" : m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "ktf" : "");
		defformatstring(sfpath)("pickups/flags/small_%s%s", m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "" : team_string(i), (m_hunt(gamemode) || m_bomber(gamemode)) ? "_htf" : m_keep(gamemode) && !m_ktf2(gamemode, mutators) ? "ktf" : "");
		switch(f.state)
		{
			case CTFF_STOLEN:
			{
				if(f.actor == player1) break;
				if(OUTBORD(f.actor->o.x, f.actor->o.y)) break;
				vec flagpos(f.actor->o);
				flagpos.add(vec(0, 0, 0.3f+(sinf(lastmillis/100.0f)+1)/10));
				rendermodel(sfpath, ANIM_FLAG|ANIM_START|ANIM_DYNALLOC, 0, 0, flagpos, lastmillis/2.5f + (i ? 180 : 0), 0, 120.0f);
				break;
			}
			case CTFF_DROPPED:
				if(OUTBORD(f.pos.x, f.pos.y)) break;
				rendermodel(fpath, ANIM_FLAG|ANIM_LOOP, 0, 0, f.pos, (float)((e.attr1+7)-(e.attr1+7)%15), 0, 120.0f);
				break;
			/*
			case CTFF_INBASE:
			case CTFF_IDLE:
			default:
				break;
			*/
		}
		if(!OUTBORD(e.x, e.y) && numflagspawn[i])
		rendermodel(fpath, ANIM_FLAG|ANIM_LOOP|(f.state == CTFF_INBASE ? ANIM_IDLE : ANIM_TRANSLUCENT), 0, 0, vec(e.x, e.y, (float)S(int(e.x), int(e.y))->floor), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 120.0f);
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
	itemstat &is = d->itemstats(e.type);
	if(&is) playsound(is.sound, d);

	weapon *w = NULL;
	switch(e.type)
	{
		case I_AKIMBO: w = d->weapons[WEAP_AKIMBO]; break;
		case I_CLIPS: w = d->weapons[d->secondary]; break;
		case I_AMMO: w = d->weapons[d->primary]; break;
		case I_GRENADE: w = d->weapons[WEAP_GRENADE]; break;
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

void spawnallitems() // spawns items them locally
{
	loopv(ents)
		if(ents[i].fitsmode(gamemode, mutators) || (multiplayer(false) && gamespeed!=100 && (i=-1)))
			ents[i].spawned = true;
}

void resetspawns()
{
	loopv(ents) ents[i].spawned = false;
	if(m_noitemsnade(gamemode, mutators) || m_pistol(gamemode, mutators) || m_noitemsammo(gamemode, mutators))
	{
		loopv(ents) ents[i].transformtype(gamemode, mutators);
	}
}
void setspawn(int i) {
	if(!ents.inrange(i)) return;
	ents[i].spawned = true;
	ents[i].spawntime = 0;
}

extern bool sendloadout;
void selectnextprimary(int weap) { player1->nextprimary = weap; sendloadout = true; }
void selectnextsecondary(int weap) { player1->nextsecondary = weap; sendloadout = true; }
void selectnextperk1(int perk) { player1->nextperk1 = perk; sendloadout = true; }
void selectnextperk2(int perk) { player1->nextperk2 = perk; sendloadout = true; }

VARFP(nextprimary, 0, WEAP_ASSAULT, WEAP_MAX, selectnextprimary(nextprimary));
VARFP(nextsecondary, 0, WEAP_PISTOL, WEAP_MAX, selectnextsecondary(nextsecondary));
VARFP(nextperk1, PERK1_NONE, PERK1_NONE, PERK1_MAX-1, selectnextperk1(nextperk1));
VARFP(nextperk2, PERK2_NONE, PERK2_NONE, PERK2_MAX-1, selectnextperk2(nextperk2));

// flag ent actions done by the local player

void tryflagdrop(){ addmsg(N_DROPFLAG, "r"); }

// flag ent actions from the net

void flagstolen(int flag, int act)
{
	playerent *actor = act == getclientnum() ? player1 : getclient(act);
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
	*/
	/*
	f.pos.x = round(p.o.x);
	f.pos.y = round(p.o.y);
	f.pos.z = round(p.o.z);
	if(f.pos.z < hdr.waterlevel) f.pos.z = (short) hdr.waterlevel;
	*/
	f.pos.x = x;
	f.pos.y = y;
	f.pos.z = z - (f.actor ? f.actor->eyeheight : PLAYERHEIGHT);
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
			case MAPMODEL:	  conoutf(" %d %s, %d clipped", entcnt[i], entnames[i], clipents); break;
			case PLAYERSTART:   conoutf(" %d %s, %d RED, %d BLUE, %d FFA", entcnt[i], entnames[i], spawncnt[0], spawncnt[1], spawncnt[2]); break;
			case CTF_FLAG:	  conoutf(" %d %s, %d RED, %d BLUE", entcnt[i], entnames[i], spawncnt[3], spawncnt[4]); break;
			default:			conoutf(" %d %s", entcnt[i], entnames[i]); break;
		}
	}
	conoutf("total entities: %d", ents.length());
}

COMMAND(entstats, ARG_NONE);

