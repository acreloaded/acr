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
			if(!gs.grenades.remove(e.proj)) return;
			vec o(e.o);
			checkpos(o);
			sendhit(c, GUN_GRENADE, o.v);
			loopv(clients){
				client &target = *clients[i];
				if(target.type == ST_EMPTY || target.state.state != CS_ALIVE) continue;
				float dist = target.state.o.dist(o);
				if(dist >= guns[e.gun].endrange) continue;
				ushort dmg = effectiveDamage(e.gun, dist, DAMAGESCALE, true);
				gs.damage += dmg;
				serverdamage(&target, &c, dmg, e.gun, FRAG_GIB, o);
			}
			break;
		}

		case GUN_KNIFE:
		{
			if(gs.mag[GUN_KNIFE] || !gs.ammo[GUN_KNIFE]) return;
			gs.ammo[GUN_KNIFE] = 0;

			ushort dmg = effectiveDamage(GUN_KNIFE, 0, DAMAGESCALE);
			if(e.proj >= 0 && e.proj != c.clientnum && valid_client(e.proj)){
				client &target = *clients[e.proj];
				clientstate &ts = target.state;
				if(ts.state == CS_ALIVE){
					gs.damage += dmg;
					/*
					if(!isteam((&target), (&c))){
						ts.cutter = c.clientnum;
						ts.lastcut = gamemillis;
					}
					*/
					serverdamage(&target, &c, dmg, GUN_KNIFE, FRAG_OVER, vec(0, 0, 0));

					e.o[0] = ts.o[0];
					e.o[1] = ts.o[1];
					int cubefloor = getblockfloor(getmaplayoutid(e.o[0], e.o[1]));
					e.o[2] = ts.o[2] > cubefloor ? (cubefloor + ts.o[2]) / 2 : cubefloor;
				}
			}

			sendhit(c, GUN_KNIFE, e.o);
			gs.knifepos = vec(e.o);
			gs.knifemillis = servmillis;
			break;
		}

		default:
			return;
	}
	gs.shotdamage += effectiveDamage(e.gun, 0, DAMAGESCALE, true);
}

void processevent(client &c, shotevent &e)
{
	vector<headevent> heads;
	vector<int> headi;
	heads.setsize(0);
	while(c.events.length() > 1 && c.events[1].type == GE_HEAD){
		headevent &head = c.events[1].head;
		if(headi.find(head.cn) < 0){
			heads.add(head);
			headi.add(head.cn);
		}
		c.events.remove(1);
	}
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
	to.normalize().mul(sraycube(from, to.normalize())).add(from);
	// packet
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	const float spreadf = to.dist(from)/1000,
		crouchfactor = 1 - (gs.crouching ? min(gamemillis - gs.crouchmillis, CROUCHTIME) : CROUCHTIME - min(gamemillis - gs.crouchmillis, CROUCHTIME)) * .25f / CROUCHTIME;
	float adsfactor = 1 - float(gs.scoping ? min(gamemillis - gs.scopemillis, ADSTIME) : ADSTIME - min(gamemillis - gs.scopemillis, ADSTIME)) / ADSTIME;
	if(e.gun==GUN_SHOTGUN){
		// apply shotgun spread
		adsfactor = (adsfactor + SGADSSPREADFACTOR - 1) / SGADSSPREADFACTOR;
		if(spreadf*adsfactor) loopi(SGRAYS){
			gs.sg[i] = to;
			applyspread(gs.o, gs.sg[i], SGSPREAD, spreadf*adsfactor);
		}
		// send message
		putint(p, N_SG);
		loopi(SGRAYS) loopj(3) putfloat(p, gs.sg[i][j]);
	}
	else{
		// apply normal ray spread
		const int spread = guns[e.gun].spread * (gs.vel.magnitude() / 3.f + gs.pitchvel / 5.f + 0.4f) * 2.4f * crouchfactor * adsfactor;
		applyspread(gs.o, to, spread, spreadf);
	}
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
		loopi(SGRAYS) gs.shotdamage += effectiveDamage(e.gun, vec(gs.sg[i]).dist(gs.o), DAMAGESCALE);
	}
	else gs.shotdamage += effectiveDamage(e.gun, to.dist(gs.o), DAMAGESCALE);
	switch(e.gun){
		case GUN_GRENADE: gs.grenades.add(e.id); break;
		case GUN_KNIFE:
			if(e.compact){
				gs.knives.add(e.id);
				gs.mag[GUN_KNIFE]--;
				break;
			}
		default:
		{
			loopv(clients){ 
				client &t = *clients[i];
				clientstate &ts = t.state;
				// basic checks
				if(t.type == ST_EMPTY || ts.state != CS_ALIVE || &c == &t) continue;
				vec head(0, 0, 0), end(gs.o);
				loopvj(heads) if(heads[j].cn == i){
					head.x = heads[j].o[0];
					head.y = heads[j].o[1];
					head.z = heads[j].o[2];
					// how can the center of our heads deviate more than 25 cm from our necks?
					if(head.magnitude() > 1) head.normalize();
					head.add(ts.o);
					break;
				}
				if(e.gun == GUN_SHOTGUN){ // many rays, many players
					int damage = 0;
					loopj(SGRAYS){ // check rays and sum damage
						int hitzone = hitplayer(gs.o, gs.aim[0], gs.aim[1], gs.sg[j], ts.o, head, &end);
						if(hitzone == HIT_NONE) continue;
						damage += effectiveDamage(e.gun, end.dist(gs.o), DAMAGESCALE * hitzone == HIT_HEAD ? 4.f : hitzone == HIT_TORSO ? 1.2f : 1);
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
					int damage = effectiveDamage(e.gun, end.dist(gs.o), DAMAGESCALE);
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
						if(hitzone == HIT_HEAD) style |= FRAG_OVER;
						if(!isteam((&c), (&t))){
							ts.cutter = c.clientnum;
							ts.lastcut = gamemillis;
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

	gs.mag[e.gun]   = min(mag, gs.mag[e.gun] + reload);
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
	}
}
