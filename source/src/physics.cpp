// physics.cpp: no physics books were hurt nor consulted in the construction of this code.
// All physics computations and constants were invented on the fly and simply tweaked until
// they "felt right", and have no basis in reality. Collision detection is simplistic but
// very robust (uses discrete steps at fixed fps).

#include "pch.h"
#include "cube.h"

inline char cornertype(int x, int y){
	sqr &me = *S(x, y);
	if(me.type != CORNER) return 0;
	sqr &up = *S(x, y-1);
	sqr &left = *S(x-1, y);
	sqr &right = *S(x+1, y);
	sqr &down = *S(x, y+1);
	const uchar mes = me.ceil - me.floor;
	const bool
		u = up.type == SOLID || uchar(up.ceil - up.floor) < mes,
		l = left.type == SOLID || uchar(left.ceil - left.floor) < mes,
		r = right.type == SOLID || uchar(right.ceil - right.floor) < mes,
		d = down.type == SOLID || uchar(down.ceil - down.floor) < mes;
	if((u && d) || (l && r)) return 0; // more than 2 cubes, or two adjecent ones
	if((u && !l) || (l && !u)) return 2; // topright
	return 1; // topleft
}

float raycube(const vec &o, const vec &ray, vec &surface)
{
	surface = vec(0, 0, 0);

	if(ray.iszero()) return 0;

	vec v = o;
	float dist = 0, dx = 0, dy = 0, dz = 0;

	for(;;)
	{
		int x = int(v.x), y = int(v.y);
		if(x < 0 || y < 0 || x >= ssize || y >= ssize) return dist;
		sqr *s = S(x, y);
		float floor = s->floor, ceil = s->ceil;
		if(s->type==FHF) floor -= s->vdelta/4.0f;
		if(s->type==CHF) ceil += s->vdelta/4.0f;
		if(SOLID(s) || v.z < floor || v.z > ceil)
		{
			if((!dx && !dy) || s->wtex==DEFAULT_SKY || (!SOLID(s) && v.z > ceil && s->ctex==DEFAULT_SKY)) return dist;
			int cornert = 0;
			if(s->type==CORNER && (cornert = cornertype(x, y))){
				float angle = atan2(v.y - o.y, v.x - o.x) / RAD;
					while(angle < 0) angle += 360;
					while(angle > 360) angle -= 360;
					// maybe there is a faster way?

					// topleft
					if(cornert == 1)
						surface.x = surface.y = (angle >= 135 && angle <= 315) ? -.7071f : .7071f;
					// topright
					else if(cornert == 2){
						surface.x = (angle >= 45 && angle <= 225) ? -.7071f : .7071f;
						surface.y = -surface.x;
					}
			}
			else{ // make one for heightfields?
				if(dx<dy) surface.x = ray.x>0 ? -1 : 1;
				else surface.y = ray.y>0 ? -1 : 1;
				sqr *n = S(x+(int)surface.x, y+(int)surface.y);
				if(SOLID(n) || (v.z < floor && v.z < n->floor) || (v.z > ceil && v.z > n->ceil))
				{
					surface = dx<dy ? vec(0, ray.y>0 ? -1 : 1, 0) : vec(ray.x>0 ? -1 : 1, 0, 0);
					n = S(x+(int)surface.x, y+(int)surface.y);
					if(SOLID(n) || (v.z < floor && v.z < n->floor) || (v.z > ceil && v.z > n->ceil))
						surface = vec(0, 0, ray.z>0 ? -1 : 1);
				}
			}
			dist = max(dist-0.1f, 0.0f);
			break;
		}
		dx = ray.x ? (x + (ray.x > 0 ? 1 : 0) - v.x)/ray.x : 1e16f;
		dy = ray.y ? (y + (ray.y > 0 ? 1 : 0) - v.y)/ray.y : 1e16f;
		dz = ray.z ? ((ray.z > 0 ? ceil : floor) - v.z)/ray.z : 1e16f;
		if(dz < dx && dz < dy)
		{
			/*
			if((s->ctex!=DEFAULT_SKY || ray.z<0) && ((s->type!=(ray.z>0?CHF:FHF)) ||
				S(x, y)->vdelta ==
				S(x+1, y)->vdelta &&
				S(x, y)->vdelta ==
				S(x, y+1)->vdelta &&
				S(x, y)->vdelta ==
				S(x+1, y+1)->vdelta
				)) surface.z = ray.z>0 ? -1 : 1;
			*/
			if(s->ctex!=DEFAULT_SKY || ray.z<0){
				if(s->type!=(ray.z>0?CHF:FHF)) // flat
					surface.z = ray.z>0 ? -1 : 1;
				else{ // top left surface
					const char f = (ray.z > 0) ? 1 : -1;
					vec b(1, 0, S(x+1, y)->vdelta + s->vdelta * f), c(0, 1, S(x, y+1)->vdelta + s->vdelta * f);
					surface = vec(0, 0, s->vdelta); // as a
					b.sub(surface);
					c.sub(surface);
					dz *= surface.cross(c, b).normalize().z;
				}
			}
			dist += dz;
			break;
		}
		float disttonext = 0.1f + min(dx, dy);
		v.add(vec(ray).mul(disttonext));
		dist += disttonext;
	}
	return dist;
}

#include "ballistics.h"

float rayclip(const vec &o, const vec &ray, vec &surface){
	float dist = raycube(o, ray, surface);
	vec to = ray;
	to.mul(dist).add(o);
	bool collided = false;
	vec end;
	// rectangular prisms
	loopv(ents){
		entity &c = ents[i];
		if(c.type != CLIP) continue;
		extern bool intersectbox(const vec &o, const vec &rad, const vec &from, const vec &to, vec *end);
		const short z = OUTBORD(c.x, c.y) ? 0 : S(c.x, c.y)->floor;
		if(intersectbox(vec(c.x, c.y, z + c.attr1 + c.attr4 / 2), vec(max(0.1f, (float)c.attr2), max(0.1f, (float)c.attr3), max(0.1f, (float)c.attr4 / 2)), o, to, &end)){
			to = end;
			collided = true;
			surface = vec(0, 0, 0);
			// which surface did it hit?
		}
	}
	return collided ? to.dist(o) : dist;
}

bool raycubelos(const vec &from, const vec &to, float margin)
{
	vec dir(to);
	dir.sub(from);
	float limit = dir.magnitude();
	dir.mul(1.0f/limit);
	vec surface;
	float dist = raycube(from, dir, surface);
	return dist > max(limit - margin, 0.0f);
}

physent *hitplayer = NULL;

bool plcollide(physent *d, physent *o, float &headspace, float &hi, float &lo)		  // collide with physent
{
	if(o->state!=CS_ALIVE || !o->cancollide || !d->cancollide) return true;
	const float r = o->radius+d->radius, dx = o->o.x-d->o.x, dy = o->o.y-d->o.y;
	const float deyeheight = d->eyeheight, oeyeheight = o->eyeheight;
	if(d->type==ENT_PLAYER && o->type==ENT_PLAYER ? dx*dx + dy*dy < r*r : fabs(dx)<r && fabs(dy)<r)
	{
		if(d->o.z-deyeheight<o->o.z-oeyeheight) { if(o->o.z-oeyeheight<hi) hi = o->o.z-oeyeheight-1; }
		else if(o->o.z+o->aboveeye>lo) lo = o->o.z+o->aboveeye+1;

		//if(fabs(o->o.z-d->o.z)<o->aboveeye+deyeheight) { hitplayer = o; return false; }
		if((d->o.z >= o->o.z && d->o.z-o->o.z <= o->aboveeye+deyeheight) || (o->o.z >= d->o.z && o->o.z-d->o.z <= d->aboveeye+oeyeheight)) { hitplayer = o; return false; }
		headspace = d->o.z-o->o.z-o->aboveeye-deyeheight;
		if(headspace<0) headspace = 10;
	}
	return true;
}

bool cornertest(int mip, int x, int y, int dx, int dy, int &bx, int &by, int &bs)	// recursively collide with a mipmapped corner cube
{
	sqr *w = wmip[mip];
	int mfactor = sfactor - mip;
	bool stest = SOLID(SWS(w, x+dx, y, mfactor)) && SOLID(SWS(w, x, y+dy, mfactor));
	++mip;
	x /= 2;
	y /= 2;
	if(SWS(wmip[mip], x, y, mfactor-1)->type==CORNER)
	{
		bx = x<<mip;
		by = y<<mip;
		bs = 1<<mip;
		return cornertest(mip, x, y, dx, dy, bx, by, bs);
	}
	return stest;
}

bool mmcollide(physent *d, float &hi, float &lo)		   // collide with a mapmodel
{
	const float eyeheight = d->eyeheight;
	const float playerheight = eyeheight + d->aboveeye;
	loopv(ents)
	{
		entity &e = ents[i];
		if(e.type==CLIP || (e.type == PLCLIP && d->type == ENT_PLAYER))
		{
			if(fabs(e.x-d->o.x) < e.attr2 + d->radius && fabs(e.y-d->o.y) < e.attr3 + d->radius)
			{
				const float cz = float(S(e.x, e.y)->floor+e.attr1), ch = float(e.attr4);
				const float dz = d->o.z-d->eyeheight;
				if(dz < cz - 0.001) { if(cz<hi) hi = cz; }
				else if(cz+ch>lo) lo = cz+ch;
				if(hi-lo < playerheight) return false;
			}
		}
		else if(e.type==MAPMODEL)
		{
			mapmodelinfo &mmi = getmminfo(e.attr2);
			if(!&mmi || !mmi.h) continue;
			const float r = mmi.rad+d->radius;
			if(fabs(e.x-d->o.x)<r && fabs(e.y-d->o.y)<r)
			{
				const float mmz = float(S(e.x, e.y)->floor+mmi.zoff+e.attr3);
				const float dz = d->o.z-eyeheight;
				if(dz<mmz) { if(mmz<hi) hi = mmz; }
				else if(mmz+mmi.h>lo) lo = mmz+mmi.h;
				if(hi-lo < playerheight) return false;
			}
		}
	}
	return true;
}

bool objcollide(physent *d, const vec &objpos, float objrad, float objheight) // collide with custom/typeless objects
{
	const float r = d->radius+objrad;
	if(fabs(objpos.x-d->o.x)<r && fabs(objpos.y-d->o.y)<r)
	{
		const float maxdist = (d->eyeheight+d->aboveeye+objheight)/2.0f;
		const float dz = d->o.z+(-d->eyeheight+d->aboveeye)/2.0f;
		const float objz = objpos.z+objheight/2.0f;
		return dz-objz <= maxdist && dz-objz >= -maxdist;
	}
	return false;
}

// all collision happens here
// spawn is a dirty side effect used in spawning
// drop & rise are supplied by the physics below to indicate gravity/push for current mini-timestep
static int cornersurface = 0;

bool collide(physent *d, bool spawn, float drop, float rise)
{
	cornersurface = 0;
	const float fx1 = d->o.x-d->radius;	 // figure out integer cube rectangle this entity covers in map
	const float fy1 = d->o.y-d->radius;
	const float fx2 = d->o.x+d->radius;
	const float fy2 = d->o.y+d->radius;
	const int x1 = int(fx1);
	const int y1 = int(fy1);
	const int x2 = int(fx2);
	const int y2 = int(fy2);
	float hi = 127, lo = -128;
	const float eyeheight = d->eyeheight;
	const float playerheight = eyeheight + d->aboveeye;

	for(int y = y1; y<=y2; y++) for(int x = x1; x<=x2; x++)	 // collide with map
	{
		if(OUTBORD(x,y)) return false;
		sqr *s = S(x,y);
		float ceil = s->ceil;
		float floor = s->floor;
		switch(s->type)
		{
			case SOLID:
				return false;

			case CORNER:
			{
				int bx = x, by = y, bs = 1;
				cornersurface = 1;
				if((x==x1 && y==y2 && cornertest(0, x, y, -1,  1, bx, by, bs) && fx1-bx<=fy2-by)
				|| (x==x2 && y==y1 && cornertest(0, x, y,  1, -1, bx, by, bs) && fx2-bx>=fy1-by) || !(++cornersurface)
				|| (x==x1 && y==y1 && cornertest(0, x, y, -1, -1, bx, by, bs) && fx1-bx+fy1-by<=bs)
				|| (x==x2 && y==y2 && cornertest(0, x, y,  1,  1, bx, by, bs) && fx2-bx+fy2-by>=bs))
					return false;
				cornersurface = 0;
				break;
			}

			case FHF:	   // FIXME: too simplistic collision with slopes, makes it feels like tiny stairs
				floor -= (s->vdelta+S(x+1,y)->vdelta+S(x,y+1)->vdelta+S(x+1,y+1)->vdelta)/16.0f;
				break;

			case CHF:
				ceil += (s->vdelta+S(x+1,y)->vdelta+S(x,y+1)->vdelta+S(x+1,y+1)->vdelta)/16.0f;

		}
		if(ceil<hi) hi = ceil;
		if(floor>lo) lo = floor;
	}

	if(hi-lo < playerheight) return false;

	float headspace = 10;

	if(d->type!=ENT_PLAYER && d->type!=ENT_CAMERA)
	{
		loopv(players)	   // collide with other players
		{
			playerent *o = players[i];
			if(!o || o==d) continue;
			if(!plcollide(d, o, headspace, hi, lo)) return false;
		}
		if(d!=player1) if(!plcollide(d, player1, headspace, hi, lo)) return false;
	}

	headspace -= 0.01f;
	if(!mmcollide(d, hi, lo)) return false;	// collide with map models

	if(spawn)
	{
		d->o.z = lo+eyeheight;	   // just drop to floor (sideeffect)
		d->onfloor = true;
	}
	else
	{
		const float spacelo = d->o.z-eyeheight-lo;
		if(spacelo<0)
		{
			if(spacelo>-0.01)
			{
				d->o.z = lo+eyeheight;   // stick on step
			}
			else if(spacelo>-1.26f && d->type!=ENT_BOUNCE) d->o.z += rise;	   // rise thru stair
			else return false;
		}
		else
		{
			d->o.z -= min(min(drop, spacelo), headspace);	   // gravity
		}

		const float spacehi = hi-(d->o.z+d->aboveeye);
		if(spacehi<0)
		{
			if(spacehi<-0.1) return false;	 // hack alert!
			if(spacelo>0.1f) d->o.z = hi-d->aboveeye; // glue to ceiling if in midair
			d->vel.z = 0;					 // cancel out jumping velocity
		}

		const float floorclamp = d->crouching ? 0.1f : 0.01f;
		d->onfloor = d->o.z-eyeheight-lo < floorclamp;
	}
	return true;
}

VARP(maxroll, 0, 0, 10);

void resizephysent(physent *pl, int moveres, int curtime, float min, float max)
{
	if(pl->eyeheightvel==0.0f) return;

	const bool water = hdr.waterlevel>pl->o.z;
	const float speed = curtime*pl->maxspeed/(water ? 2000.0f : 1000.0f);
	float h = pl->eyeheightvel * speed / moveres;

	loopi(moveres)
	{
		pl->eyeheight += h;
		if((!pl->crouching || pl->onfloor) && pl->timeinair < 50 /*|| pl->timeinair < 50*/) pl->o.z += h;
		if(!collide(pl))
		{
			pl->eyeheight -= h; // collided, revert mini-step
			if((!pl->crouching || pl->onfloor) && pl->timeinair < 50 /*|| pl->timeinair < 50*/) pl->o.z -= h;
			break;
		}
		if(pl->eyeheight<min) // clamp to min
		{
			pl->o.z += min - pl->eyeheight;
			pl->eyeheight = min;
			pl->eyeheightvel = 0.0f;
			break;
		}
		if(pl->eyeheight>max)
		{
			pl->o.z -= pl->eyeheight - max;
			pl->eyeheight = max;
			pl->eyeheightvel = 0.0f;
			break;
		}
	}
}

// main physics routine, moves a player/monster for a curtime step
// moveres indicated the physics precision (which is lower for monsters and multiplayer prediction)
// local is false for multiplayer prediction

void clamproll(physent *pl)
{
	if(pl->roll > maxroll) pl->roll = maxroll;
	else if(pl->roll < -maxroll) pl->roll = -maxroll;
}

inline void applyrecoil(float addfactor, float fric, float &dir, float &vel, float &back){
	const float addpitch = vel * addfactor;
	back += addpitch;
	dir += addpitch;

	if(vel < 0){ // negative
		if(vel + fric < 0) vel += fric;
		else vel = 0;
	}
	else if(vel > 0){ // positive
		if(vel - fric > 0) vel -= fric;
		else vel = 0;
	}

	if(fabs(vel) < .1f){  // new slide back!
		const float returnamount = sqrtf(fabs(back)) * (back < 0 ? -.030f : .030f);
		dir -= returnamount;
		back -= returnamount;
	}
}

void moveplayer(physent *pl, int moveres, bool local, int curtime)
{
	bool water = false;
	const bool editfly = pl->state==CS_EDITING;
	const bool specfly = pl->type==ENT_PLAYER && ((playerent *)pl)->spectatemode==SM_FLY;

	vec d;	  // vector of direction we ideally want to move in

	float drop = 0, rise = 0;

	if(pl->type==ENT_BOUNCE)
	{
		bounceent* bounce = (bounceent *) pl;
		water = hdr.waterlevel>pl->o.z;

		const float speed = curtime*pl->maxspeed/(water ? 2000.0f : 1000.0f);
		const float friction = water ? 20.0f : (pl->onfloor || editfly || specfly ? 6.0f : 30.0f);
		const float fpsfric = max(friction*20.0f/curtime, 1.0f);

		if(pl->onfloor) // apply friction
		{
			pl->vel.mul(fpsfric-1);
			pl->vel.div(fpsfric);
		}
		else // apply gravity
		{
			const float CUBES_PER_METER = 4; // assumes 4 cubes make up 1 meter
			const float BOUNCE_MASS = 0.5f; // sane default mass of 0.5 kg
			const float GRAVITY = BOUNCE_MASS*9.81f/CUBES_PER_METER/1000.0f;
			bounce->vel.z -= GRAVITY*curtime;
		}

		d = bounce->vel;
		d.mul(speed);
		if(water) d.div(6.0f); // incorrect

		// rotate
		float rotspeed = bounce->rotspeed*d.magnitude();
		pl->pitch = fmod(pl->pitch+rotspeed, 360.0f);
		pl->yaw = fmod(pl->yaw+rotspeed, 360.0f);
	}
	else // fake physics for player ents to create _the_ cube movement (tm)
	{
		const int timeinair = pl->timeinair;
		int move = pl->onladder && !pl->onfloor && pl->move == -1 ? 0 : pl->move; // movement on ladder
		water = hdr.waterlevel>pl->o.z-0.5f;

		const bool crouching = pl->crouching || pl->eyeheight < pl->maxeyeheight;
		const float speed = curtime/(water ? 2000.0f : 1000.0f)*pl->maxspeed*(crouching ? pl->onfloor ? 0.4f : 0.8f : 1.0f)*(specfly ? 2.0f : 1.0f);
		const float friction = water ? 20.0f : (pl->onfloor || editfly || specfly ? 6.0f : (pl->onladder ? 1.5f : 30.0f));
		const float fpsfric = max(friction/curtime*20.0f, 1.0f);

		d.x = (float)(move*cosf(RAD*(pl->yaw-90)));
		d.y = (float)(move*sinf(RAD*(pl->yaw-90)));
		d.z = 0.0f;

		if(editfly || specfly || water)
		{
			d.x *= (float)cosf(RAD*(pl->pitch));
			d.y *= (float)cosf(RAD*(pl->pitch));
			d.z = (float)(move*sinf(RAD*(pl->pitch)));
		}

		d.x += (float)(pl->strafe*cosf(RAD*(pl->yaw-180)));
		d.y += (float)(pl->strafe*sinf(RAD*(pl->yaw-180)));

		if(pl->type == ENT_PLAYER){
			playerent *p = (playerent *)pl;
			float spd = 1;
			if(p->weaponsel) spd = gunspeed(p->weaponsel->type, p->ads, p->perk1 == PERK1_AGILE);
			if(p->sprinting) spd *= .6f; // sprint = walk lol
			d.mul(vec(spd, spd, p->perk1 == PERK1_LIGHT ? 1.05f : 1));
		}

		pl->vel.mul(fpsfric-1);   // slowly apply friction and direction to velocity, gives a smooth movement
		pl->vel.add(d);
		pl->vel.div(fpsfric);
		d = pl->vel;
		d.mul(speed);

		if(editfly)				// just apply velocity
		{
			extern int editspeed;
			d.mul(editspeed);
			pl->o.add(d);
			if(pl->jumpnext)
			{
				pl->jumpnext = false;
				pl->vel.z = 2;
			}
		}
		else if(specfly)
		{
			rise = speed/moveres/1.2f;
			if(pl->jumpnext)
			{
				pl->jumpnext = false;
				pl->vel.z = 2;
			}
		}
		else						// apply velocity with collisions
		{
			if(pl->type!=ENT_CAMERA)
			{
				playerent *p = pl->type == ENT_PLAYER ? ((playerent *)pl) : NULL;
				if(pl->onladder)
				{
					float climbspeed = 1.0f;

					if(pl->type==ENT_PLAYER)
					{
						if(p->perk1 == PERK1_HAND) climbspeed = 1.5f;
						if(p->k_up || p->ownernum >= 0) pl->vel.z = climbspeed;
						else if(p->k_down) pl->vel.z = -climbspeed;
					}
					pl->timeinair = 0;
				}
				else
				{
					if(pl->onfloor || water)
					{
						if(pl->jumpnext)
						{
							pl->jumpnext = false;
							pl->vel.z = 2.0f;								  // physics impulse upwards
							if(water) { pl->vel.x /= 8; pl->vel.y /= 8; }	  // dampen velocity change even harder, gives correct water feel
							else if(p && p->type == ENT_PLAYER && (p == player1 || isowned(p)) && p->perk2 != PERK_NINJA)
								playsoundc(S_JUMP, (playerent *)pl);
						}
						pl->timeinair = 0;
					}
					else
					{
						pl->timeinair += curtime;
					}
				}

				if(!pl->timeinair)
				{
					if(timeinair > 200 && pl->state!=CS_DEAD && p && (p == player1 || isowned(p))){
						playsoundc((timeinair > 800 && p->perk2 != PERK_NINJA) ? S_HARDLAND : S_SOFTLAND, p);
					}
				}
			}

			const float gravity = 20.0f;
			float dropf = (gravity-1)+pl->timeinair/15.0f;		   // incorrect, but works fine
			if(water) { dropf = 5; pl->timeinair = 0; }			// float slowly down in water
			if(pl->onladder) { dropf = 0; pl->timeinair = 0; }

			drop = dropf*curtime/gravity/100/moveres;				// at high fps, gravity kicks in too fast
			rise = speed/moveres/1.2f;								// extra smoothness when lifting up stairs
			//if(pl->maxspeed-16.0f>0.5f) pl += 0xF0F0;
		}
	}

	bool collided = false;
	vec oldorigin = pl->o;

	if(!editfly) loopi(moveres)								// discrete steps collision detection & sliding
	{
		const float f = 1.0f/moveres;

		// try move forward
		pl->o.x += f*d.x;
		pl->o.y += f*d.y;
		pl->o.z += f*d.z;
		hitplayer = NULL;
		if(collide(pl, false, drop, rise)) continue;
		else collided = true;
		if(pl->type==ENT_BOUNCE)
		{
			// stick to players
			if(hitplayer && hitplayer->type == ENT_PLAYER && pl->trystick((playerent *)hitplayer)) return;
			if(cornersurface)
			{ // try corner bounce
				float ct2f = cornersurface == 2 ? -1.0 : 1.0;
				vec oo = pl->o, xd = d;
				xd.x = d.y * ct2f;
				xd.y = d.x * ct2f;
				pl->o.x += f * (-d.x + xd.x);
				pl->o.y += f * (-d.y + xd.y);
				if(collide(pl, false, drop, rise))
				{
					d = xd;
					float sw = pl->vel.x * ct2f;
					pl->vel.x = pl->vel.y * ct2f;
					pl->vel.y = sw;
					pl->vel.mul(0.7f);
					continue;
				}
				//pl->o = oo;
			}
		}
		if(pl->type==ENT_CAMERA || (pl->type==ENT_PLAYER && pl->state==CS_DEAD && ((playerent *)pl)->spectatemode != SM_FLY))
		{
			pl->o.x -= f*d.x;
			pl->o.y -= f*d.y;
			pl->o.z -= f*d.z;
			break;
		}
		if(pl->type==ENT_PLAYER && hitplayer)
		{
			float dx = hitplayer->o.x-pl->o.x, dy = hitplayer->o.y-pl->o.y,
				  push = (dx*d.x + dy*d.y)/max(dx*dx + dy*dy, 1e-3f),
				  px = push*dx, py = push*dy;
			pl->o.x -= f*px;
			pl->o.y -= f*py;
			if(collide(pl, false, drop, rise)) continue;
			pl->o.x += f*px;
			pl->o.y += f*py;
		}
		// player stuck, try slide along y axis
		pl->o.x -= f*d.x;
		if(collide(pl, false, drop, rise))
		{
			d.x = 0;
			if(pl->type==ENT_BOUNCE) { pl->vel.x = -pl->vel.x; pl->vel.mul(0.7f); }
			continue;
		}
		pl->o.x += f*d.x;
		// still stuck, try x axis
		pl->o.y -= f*d.y;
		if(collide(pl, false, drop, rise))
		{
			d.y = 0;
			if(pl->type==ENT_BOUNCE) { pl->vel.y = -pl->vel.y; pl->vel.mul(0.7f); }
			continue;
		}
		pl->o.y += f*d.y;
		// try just dropping down
		pl->o.x -= f*d.x;
		pl->o.y -= f*d.y;
		if(collide(pl, false, drop, rise))
		{
			d.y = d.x = 0;
			continue;
		}
		pl->o.z -= f*d.z;
		if(pl->type==ENT_BOUNCE) { pl->vel.z = -pl->vel.z; pl->vel.mul(0.5f); }
		break;
	}

	pl->stuck = (oldorigin==pl->o);
	if(collided) pl->oncollision();
	else pl->onmoved(oldorigin.sub(pl->o));

	if(pl->type==ENT_CAMERA) return;

	if(pl->type!=ENT_BOUNCE && pl==player1){
		// automatically apply smooth roll when strafing
		if(pl->strafe==0) pl->roll = pl->roll/(1+(float)sqrt((float)curtime)/25);
		else{
			pl->roll += pl->strafe*curtime/-30.0f;
			clamproll(pl);
		}

		// smooth pitch and yaw recoil
		const float fric = curtime/30.f * pl->maxeyeheight / pl->eyeheight;
		const float addfactor = (curtime/1000.0f) * pl->maxspeed;
		applyrecoil(addfactor, fric, pl->pitch, pl->pitchvel, pl->pitchreturn);
		applyrecoil(addfactor, fric, pl->yaw, pl->yawvel, pl->yawreturn);

		fixcamerarange(pl);
	}

	// play sounds on water transitions
	if(pl->type!=ENT_CAMERA)
	{
		if(!pl->inwater && water)
		{
			if(!pl->lastsplash || lastmillis-pl->lastsplash>500)
			{
				playsound(S_SPLASH2, pl);
				pl->lastsplash = lastmillis;
			}
			if(pl==player1) pl->vel.z = 0;
		}
		else if(pl->inwater && !water) playsound(S_SPLASH1, &pl->o);
		pl->inwater = water;
	}

	// store previous locations of all players/bots
	if(pl->type==ENT_PLAYER){
		((playerent *)pl)->history.update(pl->o, lastmillis);
	}

	if(pl->type==ENT_PLAYER){
		// apply volume-resize when crouching
		playerent *ppl = (playerent *)pl;
		if(pl==player1 && !intermission && !player1->onladder) updatecrouch(player1, player1->trycrouch);
		const float croucheyeheight = pl->maxeyeheight*3.0f/4.0f;
		resizephysent(pl, moveres, curtime, croucheyeheight, pl->maxeyeheight);
		if(!intermission && pl->state == CS_ALIVE && (ppl->scoping ? ppl->ads < 1000 : ppl->ads > 0) && ads_gun(ppl->weaponsel->type) &&
				!ppl->weaponsel->reloading && !ppl->weaponchanging ){
			ppl->ads += curtime * (ppl->scoping ? 1000 : -1000) / ADSTIME(ppl->perk2 == PERK_TIME);
			ppl->ads = clamp(ppl->ads, 0, 1000);
			if(!ppl->ads && ppl == player1){
				bool shouldscope = ppl->delayedscope;
				if(ppl->wantsreload){
					ppl->wantsreload = false;
					tryreload(ppl);
				}
				else if(ppl->wantsswitch >= 0){
					shouldscope = shouldscope && ads_gun(ppl->wantsswitch);
					ppl->weaponswitch(ppl->weapons[ppl->wantsswitch]);
				}
				if(shouldscope) setscope(true);
			}
		}
	}
}

const int PHYSFPS = 200;
const int PHYSFRAMETIME = 1000 / PHYSFPS;
int physsteps = 0, physframetime = PHYSFRAMETIME, lastphysframe = 0;

void physicsframe()		  // optimally schedule physics frames inside the graphics frames
{
	int diff = lastmillis - lastphysframe;
	if(diff <= 0) physsteps = 0;
	else
	{
		extern int gamespeed;
		physframetime = clamp((PHYSFRAMETIME*gamespeed)/100, 1, PHYSFRAMETIME);
		physsteps = (diff + physframetime - 1)/physframetime;
		lastphysframe += physsteps * physframetime;
	}
}

VAR(physinterp, 0, 1, 1);

void interppos(physent *pl)
{
	pl->o = pl->newpos;
	pl->o.z += pl->eyeheight;

	int diff = lastphysframe - lastmillis;
	if(diff <= 0 || !physinterp) return;

	vec deltapos(pl->deltapos);
	deltapos.mul(min(diff, physframetime)/float(physframetime));
	pl->o.add(deltapos);
}

void moveplayer(physent *pl, int moveres, bool local)
{
	if(physsteps <= 0)
	{
		if(local) interppos(pl);
		return;
	}

	if(local)
	{
		pl->o = pl->newpos;
		pl->o.z += pl->eyeheight;
	}
	loopi(physsteps-1) moveplayer(pl, moveres, local, physframetime);
	if(local) pl->deltapos = pl->o;
	moveplayer(pl, moveres, local, physframetime);
	if(local)
	{
		pl->newpos = pl->o;
		pl->deltapos.sub(pl->newpos);
		pl->newpos.z -= pl->eyeheight;
		interppos(pl);
	}
}

void movebounceent(bounceent *p, int moveres, bool local)
{
	moveplayer(p, moveres, local);
}

// movement input code

#define dir(name,v,d,s,os) void name(bool isdown) { player1->s = isdown; player1->v = isdown ? d : (player1->os ? -(d) : 0); player1->lastmove = lastmillis; }

dir(backward, move,   -1, k_down,  k_up)
dir(forward,  move,	1, k_up,	k_down)
dir(left,	 strafe,  1, k_left,  k_right)
dir(right,	strafe, -1, k_right, k_left)

void attack(bool on)
{
	if(intermission) return;
	static bool died = false;
	if(editmode) editdrag(on);
	else if(player1->state==CS_DEAD)
	{
		if(!on && died) tryrespawn();
		died = true;
	}
	else{
		extern int burst, burstfull;
		player1->attacking = on || (player1->attacking && !on && burst && burstfull && player1->weaponsel && (player1->weaponsel->type == WEAP_ASSAULT || player1->weaponsel->type == WEAP_SUBGUN) && player1->weaponsel->shots < burst);
		died = false;
	}
}

void jumpn(bool on)
{
	if(intermission) return;
	if(player1->isspectating())
	{
		if(lastmillis - player1->respawnoffset > 1000 && on) togglespect();
	}
	else if(player1->crouching) return;
	else player1->jumpnext = on;
}

void updatecrouch(playerent *p, bool on)
{
	if(p->crouching == on) return;
	const float crouchspeed = 0.35f;
	p->crouching = on;
	p->eyeheightvel = on ? -crouchspeed : crouchspeed;
	playsound(on ? S_CROUCH : S_UNCROUCH, p);
}

void crouch(bool on)
{
	if(player1->isspectating()) return;
	player1->trycrouch = on;
}

void sprint(bool on)
{
	if(intermission) return;
	player1->sprinting = on;
}

COMMAND(backward, ARG_DOWN);
COMMAND(forward, ARG_DOWN);
COMMAND(left, ARG_DOWN);
COMMAND(right, ARG_DOWN);
COMMANDN(jump, jumpn, ARG_DOWN);
COMMAND(attack, ARG_DOWN);
COMMAND(crouch, ARG_DOWN);
COMMAND(sprint, ARG_DOWN);

void fixcamerarange(physent *cam)
{
	const float MAXPITCH = 90.0f;
	if(cam->pitch>MAXPITCH) cam->pitch = MAXPITCH;
	if(cam->pitch<-MAXPITCH) cam->pitch = -MAXPITCH;
	while(cam->yaw<0.0f) cam->yaw += 360.0f;
	while(cam->yaw>=360.0f) cam->yaw -= 360.0f;
}

FVARP(sensitivity, 1e-3f, 3.0f, 1000.0f);
VARP(invmouse, 0, 0, 1);

inline void adjustangle(float &angle, const float &dangle, float &ret)
{
	angle += dangle;
	if(ret * dangle < 0){
		if(fabs(dangle) > fabs(ret)) ret = 0;
		else ret += dangle;
	}
}

void mousemove(int dx, int dy)
{
	if(intermission) return;
	if(player1->isspectating() && (player1->spectatemode==SM_FOLLOWSAME || player1->spectatemode==SM_FOLLOWALT)) return;

	const float SENSF = 33.0f;	 // try match quake sens
	const float dyaw = (dx/SENSF)*sensitivity, dpitch = (dy/SENSF) * sensitivity * (invmouse ? 1 : -1);
	adjustangle(camera1->yaw, dyaw, camera1->yawreturn);
	adjustangle(camera1->pitch, dpitch, camera1->pitchreturn);
	fixcamerarange();
	if(camera1!=player1 && player1->spectatemode!=SM_DEATHCAM)
	{
		player1->yaw = camera1->yaw;
		player1->pitch = camera1->pitch;
	}
}

void entinmap(physent *d)	// brute force but effective way to find a free spawn spot in the map
{
	vec orig(d->o);
	loopi(100)			  // try max 100 times
	{
		float dx = (rnd(21)-10)/10.0f*i;  // increasing distance
		float dy = (rnd(21)-10)/10.0f*i;
		d->o.x += dx;
		d->o.y += dy;
		if(collide(d, true))
		{
			d->resetinterp();
			return;
		}
		d->o = orig;
	}
	// leave ent at original pos, possibly stuck
	d->resetinterp();
	conoutf("can't find entity spawn spot! (%d, %d)", d->o.x, d->o.y);
}

