#include "ballistics.h"

float srayclip(const vec &o, const vec &ray, vec *surface = NULL)
{
	float dist = sraycube(o, ray, surface);
	vec to = ray;
	to.mul(dist).add(o);
	bool collided = false;
	vec end;
	loopv(sents)
	{
		if(sents[i].type != CLIP /*&& sents[i] != MAPMODEL*/) continue;
		entity &e = sents[i];
		// attr1, attr2, attr3, attr4
		// elevation, xrad, yrad, height
		if(intersectbox(vec(e.x, e.y, getblockfloor(getmaplayoutid(e.x, e.y)) + e.attr1 + e.attr4 / 2), vec(max(0.1f, (float)e.attr2), max(0.1f, (float)e.attr3), max(0.1f, e.attr4 / 2.f)), o, to, &end)){
			to = end;
			collided = true;
			if(surface){
				*surface = vec(0, 0, 0);
				// which surface did it hit?
			}
		}
	}
	return collided ? to.dist(o) : dist;
}

// trace a shot
void straceShot(const vec &from, vec &to, vec *surface = NULL){
	vec tracer(to);
	tracer.sub(from).normalize();
	const float dist = srayclip(from, tracer, surface);
	to = tracer.mul(dist - .1f).add(from);
}

// normal shots (ray through sphere and cylinder check)
static inline int hitplayer(const vec &from, float yaw, float pitch, const vec &to, const vec &target, const vec &head, vec *end = NULL){
	// intersect head
	float dist;
	if(!head.iszero() && intersectsphere(from, to, head, HEADSIZE, dist)){
		if(end) (*end = to).sub(from).mul(dist).add(from);
		return HIT_HEAD;
	}
	float y = yaw*RAD, p = (pitch/4+90)*RAD, c = cosf(p);
	vec bottom(target), top(sinf(y)*c, -cosf(y)*c, sinf(p));
	bottom.z -= PLAYERHEIGHT;
	top.mul(PLAYERHEIGHT/* + d->aboveeye*/).add(bottom); // space above shoulders removed
	// torso
	bottom.sub(top).mul(TORSOPART).add(top);
	if(intersectcylinder(from, to, bottom, top, PLAYERRADIUS, dist))
	{
		if(end) (*end = to).sub(from).mul(dist).add(from);
		return HIT_TORSO;
	}
	// restore to body
	bottom.sub(top).div(TORSOPART).add(top);
	// legs
	top.sub(bottom).mul(LEGPART).add(bottom);
	if(intersectcylinder(from, to, bottom, top, PLAYERRADIUS, dist)){
		if(end) (*end = to).sub(from).mul(dist).add(from);
		return HIT_LEG;
	}
	return HIT_NONE;
}

// apply spread
void applyspread(const vec &from, vec &to, int spread, float factor){
	if(spread <=1 ) return;
	#define RNDD (rnd(spread)-spread/2.f)*factor
	vec r(RNDD, RNDD, RNDD);
	#undef RNDD
	to.add(r);
}

bool checkcrit(float dist, float m, int base = 0, int min = 4, int max = 100){
	return m_real(gamemode, mutators) || !rnd((base + clamp<int>(ceil(dist) * m, min, max)) * (m_classic(gamemode, mutators) ? 3 : 1));
}

// easy to send shot damage messages
inline void sendhit(client &actor, int gun, const float *o, int dmg){
	sendf(-1, 1, "ri4f3", N_PROJ, actor.clientnum, gun, dmg, o[0], o[1], o[2]);
}

inline void sendheadshot(const vec &from, const vec &to, int damage){
	sendf(-1, 1, "rif6i", N_HEADSHOT, from.x, from.y, from.z, to.x, to.y, to.z, damage);
}

inline vec generateHead(client &c, const vector<head_t> &h){
	//ts.o, ts.aim[0]
	loopv(h) if(h[i].cn == c.clientnum){
		vec result(h[i].delta);
		if(result.magnitude() > 2) result.normalize().mul(2); // the center of our head cannot deviate from our neck more than 50 cm
		return result.add(c.state.o);
	}
	// no match? approximate location for the head
	return vec(.2f, -.25f, .25f).rotate_around_z(c.state.aim[0] * RAD).add(c.state.o);
}

// explosions

// let's order the explosion hits by distance
struct explosivehit{
	client *target;
	int damage, flags;
};

// a way to sort it
int cmphitsort(explosivehit *a, explosivehit *b){ return b->damage - a->damage; }
// if there is more damage, the distance is closer, therefore move it up ((-a) - (-b) = -a + b = b - a)

// explosion call
int explosion(client &owner, const vec &o2, int weap, bool gib){
	int damagedealt = 0;
	vec o(o2);
	checkpos(o);
	sendhit(owner, weap, o.v, 0); // 0 means display explosion
	// these are our hits
	vector<explosivehit> hits;
	// find the hits
	loopv(clients){
		client &target = *clients[i];
		if(target.type == ST_EMPTY || target.state.state != CS_ALIVE || target.state.protect(gamemillis)) continue;
		float dist = target.state.o.dist(o);
		if(dist >= guns[weap].endrange) continue; // too far away
		vec ray(target.state.o);
		ray.sub(o).normalize();
		if(srayclip(o, ray) < dist) continue; // not visible
		ushort dmg = effectiveDamage(weap, dist, !m_classic(gamemode, mutators));
		int expflags = gib ? FRAG_GIB : FRAG_NONE;
		// check for critical
		if(checkcrit(dist, 1.5f)){
			expflags |= FRAG_CRIT;
			dmg *= 1.4f;
		}
		// was the bow stuck? or did the nade headshot?
		// did the nade headshot?
		if(weap == WEAP_GRENADE && owner.clientnum != i && o.z >= target.state.o.z){
			expflags |= FRAG_FLAG;
			sendheadshot(o, target.state.o, dmg);
		}
		else if(weap == WEAP_BOW && !dist)
			expflags |= FRAG_FLAG;
		damagedealt += dmg;
		//serverdamage(&target, &owner, dmg, weap, expflags, o);
		explosivehit &hit = hits.add();
		hit.damage = dmg;
		hit.flags = expflags;
		hit.target = &target;
	}
	// sort the hits
	hits.sort(cmphitsort);
	// apply the hits
	loopv(hits){
		sendhit(owner, weap, hits[i].target->state.o.v, hits[i].damage);
		serverdamage(hits[i].target, &owner, hits[i].damage, weap, hits[i].flags, o);
	}
	return damagedealt;
}

// let's order the nuke hits by distance
struct nukehit{
	client *target;
	float distance; // it would be double if this engine wasn't so conservative
};

// a way to sort it
int cmpnukesort(nukehit *a, nukehit *b){
	if(a->distance < b->distance) return -1; // less distance, deal it faster
	if(a->distance > b->distance) return 1; // more distance, deal it slower
	return 0; // same?
}

void nuke(client &owner){
	vector<nukehit> hits;
	loopvj(clients){
		client *cl = clients[j];
		if(cl->type != ST_EMPTY && cl != &owner){
			cl->state.state = CS_ALIVE;
			// sort hits
			nukehit &hit = hits.add();
			hit.distance = cl->state.o.dist(owner.state.o);
			if(cl->type != ST_AI) hit.distance += 40; // 10 meters, no big deal
			hit.target = cl;
		}
	}
	hits.sort(cmpnukesort);
	loopv(hits) serverdied(hits[i].target, &owner, 0, WEAP_MAX, !rnd(3) ? FRAG_GIB : FRAG_NONE, owner.state.o);
	// save the best for last!
	owner.suicide(WEAP_MAX + 6, FRAG_NONE);
}

// hit checks
client *nearesthit(client &actor, const vec &from, const vec &to, int &hitzone, const vector<head_t> &h, client *exclude, vec *end = NULL){
	client *result = NULL;
	float dist = 4e6f; // 1 million meters...
	clientstate &gs = actor.state;
	loopv(clients){
		client &t = *clients[i];
		clientstate &ts = t.state;
		// basic checks
		if(t.type == ST_EMPTY || ts.state != CS_ALIVE || &t == exclude || ts.protect(gamemillis)) continue;
		const float d = ts.o.dist(from);
		if(d > dist) continue;
		vec head = generateHead(t, h);
		const int hz = hitplayer(from, gs.aim[0], gs.aim[1], to, ts.o, head, end);
		if(!hz) continue;
		result = &t;
		dist = d;
		hitzone = hz;
	}
	return result;
}

// hitscans
int shot(client &owner, const vec &from, vec &to, const vector<head_t> &h, int weap, const vec &surface, client *exclude, float dist = 0, ushort *save = NULL){
	int shotdamage = 0;
	const int mulset = (weap == WEAP_SNIPER || weap == WEAP_BOLT) ? MUL_SNIPER : MUL_NORMAL;
	int hitzone = HIT_NONE; vec end = to;
	// calculate the hit
	client *hit = nearesthit(owner, from, to, hitzone, h, exclude, &end);
	// damage check
	const float dist2 = dist + end.dist(from);
	int damage = effectiveDamage(weap, dist2);
	// we hit somebody
	if(hit && damage){
		// damage multipliers
		if(!m_classic(gamemode, mutators)) switch(hitzone){
			case HIT_HEAD: if(m_zombies_rounds(gamemode, mutators)) damage *= 7; else damage *= muls[mulset].head; break;
			case HIT_TORSO: damage *= muls[mulset].torso; break;
			case HIT_LEG: default: damage *= muls[mulset].leg; break;
		}
		// gib check
		const bool gib = weap == WEAP_KNIFE || hitzone == HIT_HEAD;
		int style = gib ? FRAG_GIB : FRAG_NONE;
		// critical shots
		if(checkcrit(dist, 2.5)){
			style |= FRAG_CRIT;
			damage *= 1.5f;
		}
		// melee weapons (bleed/check for self)
		if(melee_weap(weap)){
			if(hitzone == HIT_HEAD) style |= FRAG_FLAG;
			if(&owner == hit) return shotdamage; // not possible
			else if(!isteam((&owner), hit)){
				hit->state.addwound(owner.clientnum, end);
				sendf(-1, 1, "ri2", N_BLEED, hit->clientnum);
			}
		}

		// send bloody headshot hits...
		if(hitzone == HIT_HEAD) sendheadshot(from, end, damage);
		// send the real hit (blood fx)
		sendhit(owner, weap, end.v, damage);
		// apply damage
		if(save) save[hit->clientnum] += damage; // save damage for shotgun ray
		else serverdamage(hit, &owner, damage, weap, style, from);

		// penetration
		if(!m_classic(gamemode, mutators) && dist2 < 100){ // only penetrate players before 25 meters
			// distort ray and continue through...
			vec dir(to = end), newsurface;
			dir.sub(from).normalize().rotate_around_z((rnd(71)-35)*RAD).add(end); // 35 degrees (both ways = 70 degrees) distortion
			// retrace
			straceShot(end, dir, &newsurface);
			shotdamage += shot(owner, end, dir, h, weap, newsurface, hit, dist + 40, save); // 10 meters penalty for penetrating the player
			sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, weap, end.x, end.y, end.z, dir.x, dir.y, dir.z);
		}
	}
	// ricochet
	else if(!dist && from.dist(to) < 100 && surface.magnitude() && !melee_weap(weap)){ // ricochet once before 25 meters or going through a player
		vec dir(to), newsurface;
		// calculate reflected ray from incident ray and surface normal
		dir.sub(from).normalize();
		// r = i - 2 n (i . n)
		const float dot = dir.dot(surface);
		loopi(3) dir[i] = dir[i] - (2 * surface[i] * dot);
		dir.add(to);
		// retrace
		straceShot(to, dir, &newsurface);
		shotdamage += shot(owner, to, dir, h, weap, newsurface, NULL, dist + 60, save); // 15 meters penalty for ricochet
		sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, weap, to.x, to.y, to.z, dir.x, dir.y, dir.z);
	}
	return shotdamage;
}

int shotgun(client &owner, vector<head_t> &h){
	int damagedealt = 0;
	clientstate &gs = owner.state;
	const vec &from = gs.o;
	ushort sgdamage[MAXCLIENTS] = {0}; // many rays many hits, but we want each client to get all the damage at once...
	loopi(SGRAYS){// check rays and sum damage
		vec surface;
		straceShot(from, gs.sg[i], &surface);
		shot(owner, from, gs.sg[i], h, WEAP_SHOTGUN, surface, &owner, 0, sgdamage);
	}
	loopv(clients){ // apply damage
		client &t = *clients[i];
		clientstate &ts = t.state;
		// basic checks
		if(t.type == ST_EMPTY || !sgdamage[i] || ts.state != CS_ALIVE) continue;
		damagedealt += sgdamage[i];
		//sendhit(owner, WEAP_SHOTGUN, ts.o.v);
		const int shotgunflags = sgdamage[i] >= SGGIB ? FRAG_GIB : FRAG_NONE;
		serverdamage(&t, &owner, max<int>(sgdamage[i], (m_zombies_rounds(gamemode, mutators) && shotgunflags & FRAG_GIB) ? (350 * HEALTHSCALE) : 0), WEAP_SHOTGUN, shotgunflags, from);
	}
	return damagedealt;
}
