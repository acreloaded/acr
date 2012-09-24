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
	return !m_real(gamemode, mutators) && !rnd((base + clamp<int>(ceil(dist) * m, min, max)) * (m_classic(gamemode, mutators) ? 3 : 1));
}

// easy to send shot damage messages
inline void sendhit(client &actor, int gun, const vec &o, int dmg){
// no blood or explosions if using moon jump
#if (SERVER_BUILTIN_MOD & 3) != 3
#if (SERVER_BUILTIN_MOD & 2)
	if(!m_gib(gamemode, mutators))
#endif
	sendf(-1, 1, "ri4f3", N_PROJ, actor.clientnum, gun, dmg, o.x, o.y, o.z);
#endif
}

inline void sendheadshot(const vec &from, const vec &to, int damage){
	sendf(-1, 1, "rif6i", N_HEADSHOT, from.x, from.y, from.z, to.x, to.y, to.z, damage);
}

void parsepos(client &c, const vector<posinfo> &pos, vec &out_o, vec &out_head){
	const posinfo *info = NULL;
	loopv(pos) if(pos[i].cn == c.clientnum) { info = &pos[i]; break; }
	// position
	if(scl.lagtrust >= 2 && info) out_o = info->o;
	else out_o = c.state.o; // don't trust the client's position, or not provided
	// head delta
	if(scl.lagtrust >= 1 && info && info->head.x > 0 && info->head.y > 0 && info->head.z > 0)
	{
		out_head = info->head;
		// sanity check (no insane headshot OPK)
		out_head.sub(out_o);
		if(out_head.magnitude() > 2) out_head.normalize().mul(2); // the center of our head cannot deviate from our neck more than 50 cm
		out_head.add(out_o);
	}
	// no match? not trusted? approximate location for the head
	else out_head = vec(.2f, -.25f, .25f).rotate_around_z(c.state.aim[0] * RAD).add(out_o);
}

// explosions

// let's order the explosion hits by distance
struct explosivehit{
	client *target, *owner;
	int damage, flags;
	float dist;
	vec o;
};

// a way to sort it
int cmphitsort(explosivehit *a, explosivehit *b){ return b->damage - a->damage; }
// if there is more damage, the distance is closer, therefore move it up ((-a) - (-b) = -a + b = b - a)

// explosions call this to check
int radialeffect(client &owner, client &target, vector<explosivehit> &hits, const vec &o, int weap, bool gib, bool max_damage = false){
	// which is closer? the head or middle?
	vec hit_location = target.state.o;
	hit_location.z += (PLAYERABOVEEYE-PLAYERHEIGHT)/2.f;
	// distance calculations
	float dist = max_damage ? 0 : min(hit_location.dist(o), target.state.o.dist(o));
	if(dist >= guns[weap].endrange) return 0; // too far away
	vec ray1(hit_location), ray2(target.state.o);
	ray1.sub(o).normalize();
	ray2.sub(o).normalize();
	if(srayclip(o, ray1) < dist && srayclip(o, ray2) < dist) return 0; // not visible
	ushort dmg = effectiveDamage(weap, dist, !m_classic(gamemode, mutators));
	int expflags = gib ? FRAG_GIB : FRAG_NONE;
	// check for critical
	if(checkcrit(dist, 1.5f)){
		expflags |= FRAG_CRIT;
		dmg *= 1.4f;
	}
	// did the nade headshot?
	// was the RPG direct?
	if(weap == WEAP_GRENADE && owner.clientnum != target.clientnum && o.z > target.state.o.z){
		expflags |= FRAG_FLAG;
		sendheadshot(o, (hit_location = target.state.o), dmg);
		dmg *= 1.2f;
	}
	else if(weap == WEAP_RPG && max_damage)
		expflags |= FRAG_FLAG;
	explosivehit &hit = hits.add();
	hit.damage = dmg;
	hit.flags = expflags;
	hit.target = &target;
	hit.owner = &owner;
	hit.dist = dist;
	hit.o = hit_location;
	return dmg;
}

// explosion call
int explosion(client &owner, const vec &o2, int weap, bool gib, client *cflag){
	int damagedealt = 0;
	vec o(o2);
	checkpos(o);
	sendhit(owner, weap, o, 0); // 0 means display explosion
	// these are our hits
	vector<explosivehit> hits;
	// give credits to the shooter for killing the zombie!
	// find the hits
	loopv(clients){
		client &target = *clients[i];
		if(target.type == ST_EMPTY || target.state.state != CS_ALIVE || target.state.protect(gamemillis, gamemode, mutators)) continue;
		damagedealt += radialeffect((weap == WEAP_GRENADE && cflag && cflag != &target) ? *cflag : owner, target, hits, o, weap, gib, (weap == WEAP_RPG && clients[i] == cflag));
	}
	// sort the hits
	hits.sort(cmphitsort);
	// apply the hits
	loopv(hits){
		sendhit(owner, weap, hits[i].o, hits[i].damage);
		serverdamage(hits[i].target, hits[i].owner, hits[i].damage, weap, hits[i].flags, o, hits[i].dist);
	}
	return damagedealt;
}

// let's order the nuke hits by distance
struct nukehit{
	client *target;
	float distance; // it would be double if this engine wern't so conservative
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
		if(cl->type != ST_EMPTY && cl->team != TEAM_SPECT && cl != &owner && !isteam(cl, &owner)){
			cl->state.state = CS_ALIVE;
			// sort hits
			nukehit &hit = hits.add();
			hit.distance = cl->state.o.dist(owner.state.o);
			if(cl->type != ST_AI) hit.distance += 40; // 10 meters, no big deal
			hit.target = cl;
		}
	}
	hits.sort(cmpnukesort);
	loopv(hits) serverdied(hits[i].target, &owner, 0, WEAP_MAX + 1, !rnd(3) ? FRAG_GIB : FRAG_NONE, owner.state.o, hits[i].distance);
	// save the best for last!
	owner.suicide(WEAP_MAX + 1, FRAG_NONE);
}

// hitscans
struct shothit{
	client *target;
	int damage, flags;
	float dist;
};

// hit checks
client *nearesthit(client &actor, const vec &from, const vec &to, int &hitzone, const vector<posinfo> &pos, ivector &exclude, vec *end = NULL){
	client *result = NULL;
	float dist = 4e6f; // 1 million meters...
	clientstate &gs = actor.state;
	loopv(clients){
		client &t = *clients[i];
		clientstate &ts = t.state;
		// basic checks
		if(t.type == ST_EMPTY || ts.state != CS_ALIVE || exclude.find(i) >= 0 || ts.protect(gamemillis, gamemode, mutators)) continue;
		const float d = ts.o.dist(from);
		if(d > dist) continue;
		vec o, head;
		parsepos(t, pos, o, head);
		const int hz = hitplayer(from, gs.aim[0], gs.aim[1], to, o, head, end);
		if(!hz) continue;
		result = &t;
		dist = d;
		hitzone = hz;
	}
	return result;
}

// do a single line
int shot(client &owner, const vec &from, vec &to, const vector<posinfo> &pos, int weap, int style, const vec &surface, ivector &exclude, float dist = 0, float penaltydist = 0, vector<shothit> *save = NULL){
	const int mulset = (weap == WEAP_SNIPER || weap == WEAP_BOLT) ? MUL_SNIPER : MUL_NORMAL;
	int hitzone = HIT_NONE; vec end = to;
	// calculate the hit
	client *hit = nearesthit(owner, from, to, hitzone, pos, exclude, &end);
	// damage check
	const float dist2 = dist + end.dist(from);
	int damage = effectiveDamage(weap, dist2 + penaltydist);
	// out of range? (super knife code)
	if(melee_weap(weap)){
#if (SERVER_BUILTIN_MOD & 1)
		if(m_gib(gamemode, mutators)){
			const int lulz[3] = {WEAP_SNIPER, WEAP_HEAL, WEAP_RPG};
			sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, lulz[rnd(3)], from.x, from.y, from.z, to.x, to.y, to.z);
		}
		else
#endif
		if(dist2 > guns[weap].endrange) return 0;
	}
	// we hit somebody
	if(hit && damage){
		// damage multipliers
		if(!m_classic(gamemode, mutators) || hitzone >= HIT_HEAD) switch(hitzone){
			case HIT_HEAD: if(m_progressive(gamemode, mutators)) damage *= 7; else damage *= muls[mulset].head; break;
			case HIT_TORSO: damage *= muls[mulset].torso; break;
			case HIT_LEG: default: damage *= muls[mulset].leg; break;
		}
		// gib check
		if((melee_weap(weap) || hitzone == HIT_HEAD) && !save) style |= FRAG_GIB;
		// critical shots
		if(checkcrit(dist, 2.5)){
			style |= FRAG_CRIT;
			damage *= 1.5f;
		}

		// melee weapons (bleed/check for self)
		if(melee_weap(weap)){
			if(hitzone == HIT_HEAD) style |= FRAG_FLAG;
			if(&owner == hit) return 0; // not possible
			else if(!isteam(&owner, hit)){
				hit->state.addwound(owner.clientnum, end);
				sendf(-1, 1, "ri2", N_BLEED, hit->clientnum);
			}
		}

		// send bloody headshot hits...
		if(hitzone == HIT_HEAD) sendheadshot(from, end, damage);
		// send the real hit (blood fx)
		sendhit(owner, weap, end, damage);
		// apply damage
		if(save)
		{
			// save damage for shotgun rays
			shothit &h = save->add();
			h.target = hit;
			h.damage = damage;
			h.flags = style;
			h.dist = dist2;
		}
		else serverdamage(hit, &owner, damage, weap, style, from, dist2);

		// add hit to the exclude list
		exclude.add(hit->clientnum);

		// penetration
		//if(!m_classic(gamemode, mutators) && dist2 < 100){ // only penetrate players before 25 meters
			// distort ray and continue through...
			vec dir(to = end), newsurface;
			//dir.sub(from).normalize().rotate_around_z((rnd(71)-35)*RAD).add(end); // 35 degrees (both ways = 70 degrees) distortion
			dir.sub(from).normalize().rotate_around_x((rnd(45)-22)*RAD).rotate_around_y((rnd(11)-5)*RAD).rotate_around_z((rnd(11)-5)*RAD).add(end); // 5 degrees (both ways = 10 degrees) distortion on all axis
			// retrace
			straceShot(end, dir, &newsurface);
			const int penetratedamage = shot(owner, end, dir, pos, weap, style, newsurface, exclude, dist2, penaltydist + 40, save); // 10 meters penalty for penetrating the player
			sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, weap, end.x, end.y, end.z, dir.x, dir.y, dir.z);
			return damage + penetratedamage;
		//}
	}
	// ricochet
	else if(!dist && from.dist(to) < 100 && surface.magnitude()){ // ricochet once before 25 meters or going through a player
		vec dir(to), newsurface;
		// calculate reflected ray from incident ray and surface normal
		dir.sub(from).normalize();
		// r = i - 2 n (i . n)
		dir
			.sub(
				vec(surface)
					.mul(2 * dir.dot(surface))
			)
			.add(to);
		// retrace
		straceShot(to, dir, &newsurface);
		const int ricochetdamage = shot(owner, to, dir, pos, weap, style, newsurface, exclude, dist2, penaltydist + 60, save); // 15 meters penalty for ricochet
		sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, weap, to.x, to.y, to.z, dir.x, dir.y, dir.z);
		return damage + ricochetdamage;
	}
	return 0;
}

int shotgun(client &owner, vector<posinfo> &pos){
	int damagedealt = 0;
	clientstate &gs = owner.state;
	const vec &from = gs.o;
	// many rays many hits, but we want each client to get all the damage at once...
	static vector<shothit> hits;
	hits.setsize(0);
	loopi(SGRAYS){// check rays and sum damage
		vec surface;
		straceShot(from, gs.sg[i], &surface);
		static ivector exclude;
		exclude.setsize(0);
		exclude.add(owner.clientnum);
		shot(owner, from, gs.sg[i], pos, WEAP_SHOTGUN, FRAG_NONE, surface, exclude, 0, 0, &hits);
	}
	loopv(clients){ // apply damage
		client &t = *clients[i];
		clientstate &ts = t.state;
		// basic checks
		if(t.type == ST_EMPTY || ts.state != CS_ALIVE) continue;
		int damage = 0, shotgunflags = 0;
		float bestdist = 0;
		loopvrev(hits) if(hits[i].target == &t)
		{
			damage += hits[i].damage;
			shotgunflags |= hits[i].flags; // merge crit, etc.
			if(hits[i].dist > bestdist) bestdist = hits[i].dist;
			hits.remove(i/*--*/);
		}
		if(!damage) continue;
		damagedealt += damage;
		shotgunflags |= damage >= SGGIB ? FRAG_GIB : FRAG_NONE;
		if(m_progressive(gamemode, mutators) && shotgunflags & FRAG_GIB)
			damage = max(damage, 350 * HEALTHSCALE);
		serverdamage(&t, &owner, damage, WEAP_SHOTGUN, shotgunflags, from, bestdist);
		//sendhit(owner, WEAP_SHOTGUN, ts.o);
	}
	return damagedealt;
}
