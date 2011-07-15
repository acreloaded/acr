// processing of server events

// ballistics
#include "ballistics.h"
#include "serverballistics.h"

// easy to send shot damage messages
inline void sendhit(client &actor, int gun, float *o){
	sendf(-1, 1, "ri3f3", N_PROJ, actor.clientnum, gun, o[0], o[1], o[2]);
}

// processing events
void processevent(client &c, projevent &e){
	clientstate &gs = c.state;
	switch(e.gun){
		case GUN_GRENADE:
		{
			if(!gs.grenades.remove(e.flag)) return;
			vec o(e.o);
			checkpos(o);
			sendhit(c, GUN_GRENADE, o.v);
			loopv(clients){
				client &target = *clients[i];
				if(target.type == ST_EMPTY || target.state.state != CS_ALIVE) continue;
				float dist = target.state.o.dist(o);
				if(dist >= guns[e.gun].endrange) continue;
				vec ray(target.state.o);
				ray.sub(o).normalize();
				if(sraycube(o, ray) < dist) continue;
				ushort dmg = effectiveDamage(e.gun, dist, true);
				gs.damage += dmg;
				serverdamage(&target, &c, dmg, e.gun, FRAG_GIB, o);
			}
			break;
		}

		case GUN_KNIFE:
		{
			if(!gs.knives.numprojs) return;
			gs.knives.numprojs--;
			ushort dmg = effectiveDamage(GUN_KNIFE, 0);
			if(e.flag >= 0 && e.flag != c.clientnum && valid_client(e.flag)){
				client &target = *clients[e.flag];
				clientstate &ts = target.state;
				if(ts.state == CS_ALIVE){
					gs.damage += dmg;
					serverdamage(&target, &c, dmg, GUN_KNIFE, FRAG_FLAG, vec(0, 0, 0));

					e.o[0] = ts.o[0];
					e.o[1] = ts.o[1];
					int cubefloor = getblockfloor(getmaplayoutid(e.o[0], e.o[1]));
					e.o[2] = ts.o[2] > cubefloor ? (cubefloor + ts.o[2]) / 2 : cubefloor;
				}
			}

			sendhit(c, GUN_KNIFE, e.o);
			sknife &k = sknives.add();
			k.millis = gamemillis;
			sendf(-1, 1, "ri2f3", N_KNIFEADD, (k.id = sknifeid++), (k.o.x = e.o[0]), (k.o.y = e.o[1]), (k.o.z = e.o[2]));
			break;
		}

		default:
			return;
	}
}

void processevent(client &c, shotevent &e)
{
	clientstate &gs = c.state;
	int wait = e.millis - gs.lastshot;
	if(!gs.isalive(gamemillis) ||
	   e.gun<GUN_KNIFE || e.gun>=NUMGUNS ||
	   wait<gs.gunwait[e.gun] ||
	   gs.mag[e.gun]<=0)
		return;
	if(e.gun!=GUN_KNIFE) gs.mag[e.gun]--;
	loopi(NUMGUNS) if(gs.gunwait[i]) gs.gunwait[i] = max(gs.gunwait[i] - (e.millis-gs.lastshot), 0);
	gs.lastshot = e.millis;
	gs.gunwait[e.gun] = attackdelay(e.gun);
	// for ease of access
	vec from(gs.o), to(e.to);
	to.normalize().add(from);
	// apply spread
	const float spreadf = .001f,//to.dist(from)/1000,
		crouchfactor = 1 - (gs.crouching ? min(gamemillis - gs.crouchmillis, CROUCHTIME) : CROUCHTIME - min(gamemillis - gs.crouchmillis, CROUCHTIME)) * .25f / CROUCHTIME;
	float adsfactor = 1 - float(gs.scoping ? min(gamemillis - gs.scopemillis, ADSTIME) : ADSTIME - min(gamemillis - gs.scopemillis, ADSTIME)) / ADSTIME;
	if(e.gun==GUN_SHOTGUN){
		// apply shotgun spread
		adsfactor = (adsfactor + SGADSSPREADFACTOR - 1) / SGADSSPREADFACTOR;
		if(spreadf*adsfactor) loopi(SGRAYS){
			gs.sg[i] = to;
			applyspread(gs.o, gs.sg[i], SGSPREAD, spreadf*adsfactor);
			straceShot(from, gs.sg[i]);
		}
	}
	else{
		// apply normal ray spread
		const int spread = guns[e.gun].spread * (gs.vel.magnitude() / 3.f + gs.pitchvel / 5.f + 0.4f) * 1.2f * crouchfactor * adsfactor;
		applyspread(gs.o, to, spread, spreadf);
	}
	// trace shot
	straceShot(from, to);
	// create packet
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	// packet shotgun rays
	if(e.gun==GUN_SHOTGUN){ putint(p, N_SG); loopi(SGRAYS) loopj(3) putfloat(p, gs.sg[i][j]); }
	// packet shot message
	putint(p, e.compact ? N_SHOOTC : N_SHOOT);
	putint(p, c.clientnum);
	putint(p, e.gun);
	if(!e.compact){
		putfloat(p, from.x);
		putfloat(p, from.y);
		putfloat(p, from.z);
		putfloat(p, to.x);
		putfloat(p, to.y);
		putfloat(p, to.z);
	}
	enet_packet_resize(packet, p.length());
	sendpacket(-1, 1, packet, !e.compact && e.gun != GUN_GRENADE ? -1 : c.clientnum);
	if(packet->referenceCount==0) enet_packet_destroy(packet);
	if(e.gun == GUN_SHOTGUN){
		loopi(SGRAYS) gs.shotdamage += effectiveDamage(e.gun, vec(gs.sg[i]).dist(gs.o));
	}
	else if(e.gun == GUN_KNIFE) gs.shotdamage += guns[GUN_KNIFE].damage; // melee damage
	else if(e.gun == GUN_BOW) gs.shotdamage += guns[GUN_BOW].damage + 50; // potential stick damage
	else gs.shotdamage += effectiveDamage(e.gun, to.dist(gs.o));
	switch(e.gun){
		case GUN_GRENADE: gs.grenades.add(e.id); break;
		case GUN_BOW:
		{
			// fix to position
			vec tracer(to);
			tracer.sub(from).normalize();
			to = tracer.mul(sraycube(from, tracer) - .1f).add(from);
			// check for stick
			int cn = -1;
			float dist = 4e6f; // 1 million meters should be enough for a "stick"
			loopv(clients){
				client &t = *clients[i];
				clientstate &ts = t.state;
				// basic checks
				if(t.type == ST_EMPTY || ts.state != CS_ALIVE || &c == &t) continue;
				const float d = gs.o.dist(ts.o);
				if(d > dist) continue;
				vec head(ts.o);
				/*
				loopvj(heads) if(heads[j].cn == i){
					head.x = heads[j].o[0];
					head.y = heads[j].o[1];
					head.z = heads[j].o[2];
					// how can the center of our heads deviate more than 25 cm from our necks?
					if(head.magnitude() > 1) head.normalize();
					head.add(ts.o);
					break;
				}
				*/
				if(!hitplayer(gs.o, gs.aim[0], gs.aim[1], to, ts.o, head)) continue;
				cn = i;
				dist = d;
			}
			if(cn >= 0 && !m_expert){
				serverdamage(clients[cn], &c, 50, GUN_BOW, FRAG_NONE, clients[cn]->state.o);
				if(clients[cn]->state.state != CS_ALIVE){
					to = clients[cn]->state.o;
					cn = -1;
				}
			}
			if(cn >= 0) sendf(-1, 1, "ri2", N_STICK, cn);
			else sendf(-1, 1, "ri2f3", N_STICK, -1, to.x, to.y, to.z);
			// timed explosion
			gameevent &exp = c.timers.add();
			exp.type = GE_PROJ;
			//gs.tips.add(exp.proj.id = rand());
			exp.proj.millis = gamemillis + TIPSTICKTTL;
			exp.proj.gun = GUN_BOW;
			exp.proj.flag = cn;
			loopi(3) exp.proj.o[i] = to[i];
			break;
		}
		case GUN_KNIFE:
			if(e.compact){
				if(gs.ammo[GUN_KNIFE]){
					gs.knives.add(e.id);
					gs.ammo[GUN_KNIFE]--;
				}
				break;
			}
		default:
		{
			loopv(clients){ 
				client &t = *clients[i];
				clientstate &ts = t.state;
				// basic checks
				if(t.type == ST_EMPTY || ts.state != CS_ALIVE || &c == &t) continue;
				vec head(ts.o), end(gs.o);
				/*
				loopvj(heads) if(heads[j].cn == i){
					head.x = heads[j].o[0];
					head.y = heads[j].o[1];
					head.z = heads[j].o[2];
					// how can the center of our heads deviate more than 25 cm from our necks?
					if(head.magnitude() > 1) head.normalize();
					head.add(ts.o);
					break;
				}
				*/
				if(e.gun == GUN_SHOTGUN){ // many rays, many players
					int damage = 0;
					loopj(SGRAYS){ // check rays and sum damage
						int hitzone = hitplayer(gs.o, gs.aim[0], gs.aim[1], gs.sg[j], ts.o, head, &end);
						if(hitzone == HIT_NONE) continue;
						damage += effectiveDamage(e.gun, end.dist(gs.o)) * (hitzone == HIT_HEAD ? 4.f : hitzone == HIT_TORSO ? 1.2f : 1);
					}
					const bool gib = damage > SGGIB;
					if(m_expert && !gib) continue;
					int style = gib ? FRAG_GIB : FRAG_NONE;
					gs.damage += damage;
					sendhit(c, GUN_SHOTGUN, ts.o.v);
					serverdamage(&t, &c, damage, e.gun, style, gs.o);
				}
				else{ // one ray, potentially multiple players
					// calculate the hit
					int hitzone = hitplayer(gs.o, gs.aim[0], gs.aim[1], to, ts.o, head, &end);
					if(hitzone == HIT_NONE) continue;
					// damage check
					int damage = effectiveDamage(e.gun, end.dist(gs.o));
					// damage multipliers
					switch(hitzone){
						case HIT_HEAD:
							damage *= POWERGUN(e.gun) ? POWHEADMUL : DAMHEADMUL;
							break;
						case HIT_TORSO:
							// multiplying by one is pretty stupid to do
							if(POWERGUN(e.gun)) damage *= POWTORSOMUL;
							break;
						case HIT_LEG:
						default:
							// ditto to the above comment
							if(POWERGUN(e.gun)) break;
							damage *= DAMLEGMUL;
							break;
					}
					if(!damage) continue;
					if(e.gun != GUN_KNIFE) sendhit(c, e.gun, end.v);
					// gib check
					const bool gib = e.gun == GUN_KNIFE || hitzone == HIT_HEAD;
					if(m_expert && !gib) continue;
					// do the damage!
					int style = gib ? FRAG_GIB : FRAG_NONE;
					if(e.gun == GUN_KNIFE){
						if(hitzone == HIT_HEAD) style |= FRAG_FLAG;
						if(!isteam((&c), (&t))){
							ts.cutter = c.clientnum;
							ts.lastcut = gamemillis;
							sendf(-1, 1, "ri2", N_BLEED, i);
						}
					}
					gs.damage += damage;
					serverdamage(&t, &c, damage, e.gun, style, gs.o);
				}
			}
		}
	}
}

void processevent(client &c, reloadevent &e){
	clientstate &gs = c.state;
	int mag = magsize(e.gun), reload = reloadsize(e.gun);
	if(!gs.isalive(gamemillis) ||
	   e.gun<GUN_KNIFE || e.gun>=NUMGUNS ||
	   !reloadable_gun(e.gun) ||
	   gs.mag[e.gun] >= mag ||
	   gs.ammo[e.gun] < reload)
		return;

	gs.mag[e.gun]   = min(mag + (gs.mag[e.gun] && reload > 1), gs.mag[e.gun] + reload);
	gs.ammo[e.gun] -= reload;

	int wait = e.millis - gs.lastshot;
	sendf(-1, 1, "ri5", N_RELOAD, c.clientnum, e.gun, gs.mag[e.gun], gs.ammo[e.gun]);
	if(gs.gunwait[e.gun] && wait<gs.gunwait[e.gun]) gs.gunwait[e.gun] += reloadtime(e.gun);
	else
	{
		loopi(NUMGUNS) if(gs.gunwait[i]) gs.gunwait[i] = max(gs.gunwait[i] - (e.millis-gs.lastshot), 0);
		gs.lastshot = e.millis;
		gs.gunwait[e.gun] += reloadtime(e.gun);
	}
}

void processevent(client &c, akimboevent &e){
	clientstate &gs = c.state;
	if(!gs.isalive(gamemillis) || gs.akimbos<=0) return;
	gs.akimbos--;
	gs.akimbomillis = e.millis+30000;
}

void clearevent(client &c){
	/*
	int n = 1;
	while(n<c->events.length() && c->events[n].type==GE_HIT) n++;
	c->events.remove(0, n);
	*/
	c.events.remove(0);
}

void processtimer(client &c, projevent &e){
	vec o(valid_client(e.flag) ? clients[e.flag]->state.o : e.o);
	
	sendhit(c, GUN_BOW, o.v);
	loopv(clients){
		client &target = *clients[i];
		if(target.type == ST_EMPTY || target.state.state != CS_ALIVE) continue;
		float dist = target.state.o.dist(o);
		if(dist >= guns[e.gun].endrange) continue;
		vec ray(target.state.o);
		ray.sub(o).normalize();
		if(sraycube(o, ray) < dist) continue;
		ushort dmg = effectiveDamage(e.gun, dist, true);
		c.state.damage += dmg;
		serverdamage(&target, &c, dmg, e.gun, &c == &target ? FRAG_GIB | FRAG_FLAG : FRAG_GIB, o);
	}
	c.state.shotdamage += effectiveDamage(e.gun, 0);
}

void processtimer(client &c, reloadevent &e){
	// heaing from e.id;
	if(c.state.state == CS_ALIVE) c.state.health += e.gun;
	// network message?
}

void processevents(){
	loopv(clients)
	{
		client &c = *clients[i];
		if(c.type==ST_EMPTY) continue;
		if(c.state.state == CS_ALIVE){ // can't regen or bleed if dead
			if(c.state.lastcut){ // bleeding; oh no!
				if(c.state.lastcut + 500 < gamemillis && valid_client(c.state.cutter)){
					c.state.damage += 10;
					c.state.shotdamage += 10;
					serverdamage(&c, clients[c.state.cutter], 10, GUN_KNIFE, FRAG_NONE, clients[c.state.cutter]->state.o);
					c.state.lastcut = gamemillis;
				}
			}
			else if(!m_duel && c.state.state == CS_ALIVE && c.state.health < STARTHEALTH && c.state.lastregen + REGENINT < gamemillis){
				int amt = round(float((STARTHEALTH - c.state.health) / 5 + 15));
				if(amt >= STARTHEALTH - c.state.health){
					amt = STARTHEALTH - c.state.health;
					c.state.damagelog.setsize(0);
				}
				c.state.health += amt;
				sendf(-1, 1, "ri3", N_REGEN, i, amt);
				c.state.lastregen = gamemillis;
			}
		}
		while(c.events.length())
		{
			gameevent &e = c.events[0];
			if(e.type<=GE_RELOAD) // timed
			{
				if(e.shot.millis>gamemillis) break;
				if(e.shot.millis<c.lastevent) { clearevent(c); continue; }
				c.lastevent = e.shot.millis;
			}
			switch(e.type)
			{
				case GE_SHOT: processevent(c, e.shot); break;
				case GE_PROJ: processevent(c, e.proj); break;
				case GE_AKIMBO: processevent(c, e.akimbo); break;
				case GE_RELOAD: processevent(c, e.reload); break;
				// untimed events are GONE!
			}
			clearevent(c);
		}
		while(c.timers.length()){
			gameevent &e = c.timers[i];
			if(e.shot.millis>gamemillis) break;
			switch(e.type){
				case GE_PROJ: processtimer(c, e.proj); break;
				case GE_RELOAD: processtimer(c, e.reload); break;
			}
			c.timers.remove(i--);
		}
	}
}
