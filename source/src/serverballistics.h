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

bool checkcrit(float dist, float m, int base = 0, int min = 1, int max = 100){
	return m_real || !rnd(base + clamp<int>(ceil(dist) * m, min, max));
}

// easy to send shot damage messages
inline void sendhit(client &actor, int gun, const float *o){
	sendf(-1, 1, "ri3f3", N_PROJ, actor.clientnum, gun, o[0], o[1], o[2]);
}

inline vec generateHead(const vec &o, float yaw){ // approximate location for the heads
	return vec(.2f, -.25f, .25f).rotate_around_z(yaw * RAD).add(o);
}

// explosions
int explosion(client &owner, const vec &o2, int weap){
	int damagedealt = 0;
	vec o(o2);
	checkpos(o);
	sendhit(owner, weap, o.v);
	loopv(clients){
		client &target = *clients[i];
		if(target.type == ST_EMPTY || target.state.state != CS_ALIVE) continue;
		float dist = target.state.o.dist(o);
		if(dist >= guns[weap].endrange) continue;
		vec ray(target.state.o);
		ray.sub(o).normalize();
		if(sraycube(o, ray) < dist) continue;
		ushort dmg = effectiveDamage(weap, dist, true);
		int expflags = FRAG_GIB;
		if(weap == WEAP_GRENADE && o.z >= target.state.o.z) expflags |= FRAG_FLAG;
		if(checkcrit(dist, 1.5f)){
			expflags |= FRAG_CRITICAL;
			dmg *= 2;
		}
		damagedealt += dmg;
		serverdamage(&target, &owner, dmg, weap, expflags, o);
	}
	return damagedealt;
}

// hit checks

// hitscans
int shot(client &owner, const vec &from, const vec &to, int weap, vec &surface, float dist = 0){
	int shotdamage = 0;
	clientstate &gs = owner.state;
	const int mulset = (weap == WEAP_SNIPER || weap == WEAP_BOLT) ? MUL_SNIPER : MUL_NORMAL;
	int playershit = 0;
	loopv(clients){ // one ray, potentially multiple players
		client &t = *clients[i];
		clientstate &ts = t.state;
		// basic checks
		if((!dist && i == owner.clientnum) || t.type == ST_EMPTY || ts.state != CS_ALIVE) continue;
		vec head = generateHead(ts.o, ts.aim[0]), end;
		
		// calculate the hit
		const int hitzone = hitplayer(from, gs.aim[0], gs.aim[1], to, ts.o, head, &end);
		if(!hitzone) continue;
		// damage check
		const float dist = end.dist(from);
		int damage = effectiveDamage(weap, dist);
		if(!damage) continue;
		// damage multipliers
		switch(hitzone){
			case HIT_HEAD: damage *= muls[mulset].head; break;
			case HIT_TORSO: damage *= muls[mulset].torso; break;
			case HIT_LEG: default: damage *= muls[mulset].leg; break;
		}
		// gib check
		const bool gib = weap == WEAP_KNIFE || hitzone == HIT_HEAD;
		int style = gib ? FRAG_GIB : FRAG_NONE;
		// critical shots
		if(checkcrit(dist, 2.5)){
			style |= FRAG_CRITICAL;
			damage *= 2.5f;
		}
		if(weap != WEAP_KNIFE) sendhit(owner, weap, end.v);
		else{
			if(hitzone == HIT_HEAD) style |= FRAG_FLAG;
			if(!isteam((&owner), (&t))){
				ts.cutter = owner.clientnum;
				ts.lastcut = gamemillis;
				sendf(-1, 1, "ri2", N_BLEED, i);
			}
		}
		shotdamage += damage;
		serverdamage(&t, &owner, damage, weap, style, gs.o);
		++playershit;
	}
	if(!dist && from.dist(to) < 100 && surface.magnitude() && weap != WEAP_KNIFE && weap != WEAP_WAVE){ // material absorbs the radiation. too bad
		const int penalty = 40 + playershit * 20; // 10 meters plus 5 meters per player
		vec dir(to), newsurface;
		loopi(3) surface[i] = 1 - fabs(surface[i]) * 2;
		dir.sub(from).mul(surface).add(to);
		straceShot(to, dir, &newsurface);
		sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, weap, to.x, to.y, to.z, dir.x, dir.y, dir.z);
		shotdamage += shot(owner, to, dir, weap, newsurface, dist + penalty);
	}
	return shotdamage + (false && !dist ? explosion(owner, to, WEAP_BOW) : 0);
}

int shotgun(client &owner, const vec &from, const vec &to){
	int damagedealt = 0;
	clientstate &gs = owner.state;
	loopv(clients){
		client &t = *clients[i];
		clientstate &ts = t.state;
		// basic checks
		if(i == owner.clientnum || t.type == ST_EMPTY || ts.state != CS_ALIVE) continue;

		int damage = 0;
		loopj(SGRAYS){ // check rays and sum damage
			vec head = generateHead(ts.o, ts.aim[0]), end;
			const int hitzone = hitplayer(from, gs.aim[0], gs.aim[1], gs.sg[j], ts.o, head, &end);
			if(!hitzone) continue;
			damage += effectiveDamage(WEAP_SHOTGUN, end.dist(gs.o)) * muls[MUL_SHOTGUN].val[hitzone == HIT_HEAD ? 0 : hitzone == HIT_TORSO ? 1 : 2];
		}
		damagedealt += damage;
		sendhit(owner, WEAP_SHOTGUN, ts.o.v);
		serverdamage(&t, &owner, damage, WEAP_SHOTGUN, damage >= SGGIB ? FRAG_GIB : FRAG_NONE, from);
	}
	return damagedealt + (false ? explosion(owner, to, WEAP_BOW) : 0);
}
