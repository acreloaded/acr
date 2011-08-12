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

// hit checks

// hitscans (todo)

// explosions
int explosion(client &owner, const vec &o, int weap){
	int damagedealt = 0;
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
		if(checkcrit(dist, 1.5f)){
			expflags |= FRAG_CRITICAL;
			dmg *= 2;
		}
		damagedealt += dmg;
		serverdamage(&target, &owner, dmg, weap, expflags, o);
	}
	return damagedealt;
}

// throwing knife
client *knifehit(client &owner, const vec &o){ // checks for knife hit
	client *hit = NULL;
	float hitdist = 40; // a knife hit 10 meters from a throwing knife? hard to get!
	loopv(clients){
		client &h = *clients[i];
		if(h.type == ST_EMPTY || i == owner.clientnum || h.state.state != CS_ALIVE) continue;
		// check for xy
		float d = h.state.o.distxy(o);
		if((hitdist > 0 && hitdist < d) || PLAYERRADIUS * 1.5f < d) continue;
		// check for z
		if(o.z > h.state.o.z + PLAYERABOVEEYE || h.state.o.z > o.z + PLAYERHEIGHT) continue;
		hit = &h;
		hitdist = d;
	}
	return hit;
}