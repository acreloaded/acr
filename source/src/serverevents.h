// processing of server events

// ballistics
#include "ballistics.h"
#include "serverballistics.h"

// processing events
void processevent(client &c, projevent &e){
	clientstate &gs = c.state;
	int damagepotential = effectiveDamage(e.gun, 0), damagedealt = 0;
	switch(e.gun){
		case GUN_GRENADE:
		{
			if(!gs.grenades.remove(e.flag)) return;
			vec o(e.o);
			checkpos(o);
			
			damagedealt += explosion(c, o, GUN_GRENADE);
			break;
		}

		case GUN_KNIFE:
		{
			if(!gs.knives.numprojs) return;
			--gs.knives.numprojs;
			ushort dmg = effectiveDamage(GUN_KNIFE, 0);
			client *hit = valid_client(e.flag) && e.flag != c.clientnum ? clients[e.flag] : NULL;
			if(hit){
				client &target = *hit;
				clientstate &ts = target.state;
				int tknifeflags = FRAG_FLAG;
				if(checkcrit(0, 0, 20)){ // 5% critical hit chance
					tknifeflags |= FRAG_CRITICAL;
					dmg *= 2; // 80 * 2 = 160 damage instant kill!
				}
				damagedealt += dmg;
				serverdamage(&target, &c, dmg, GUN_KNIFE, FRAG_FLAG, vec(0, 0, 0));

				e.o[0] = ts.o[0];
				e.o[1] = ts.o[1];
				int cubefloor = getblockfloor(getmaplayoutid(e.o[0], e.o[1]));
				e.o[2] = ts.o[2] > cubefloor ? (cubefloor + ts.o[2]) / 2 : cubefloor;
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
	gs.damage += damagedealt;
	gs.shotdamage += max(damagedealt, damagepotential);
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
	// to = vec(sinf(c.state.aim[0]*RAD)*cosf(c.state.aim[1]*RAD), -cosf(c.state.aim[0]*RAD)*cosf(c.state.aim[1]*RAD), sinf(c.state.aim[1]*RAD));
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
	int damagepotential = 0, damagedealt = 0;
	if(e.gun == GUN_SHOTGUN){
		loopi(SGRAYS) damagepotential = effectiveDamage(e.gun, vec(gs.sg[i]).dist(gs.o));
	}
	else if(e.gun == GUN_KNIFE) damagepotential = guns[GUN_KNIFE].damage; // melee damage
	else if(e.gun == GUN_BOW) damagepotential = 50; // potential stick damage
	else if(e.gun == GUN_GRENADE) damagepotential = 0;
	else damagepotential = effectiveDamage(e.gun, to.dist(gs.o));

	switch(e.gun){
		case GUN_GRENADE: gs.grenades.add(e.id); break;
		case GUN_BOW:
		case GUN_HEAL:
		{
			// fix to position
			vec tracer(to);
			tracer.sub(from).normalize();
			to = tracer.mul(sraycube(from, tracer) - .1f).add(from);
			// check for stick
			int cn = -1, hitzone = HIT_NONE;
			float dist = 4e6f; // 1 million meters should be enough for a "stick"
			loopv(clients){
				client &t = *clients[i];
				clientstate &ts = t.state;
				// basic checks
				if(t.type == ST_EMPTY || ts.state != CS_ALIVE || &c == &t) continue;
				const float d = gs.o.dist(ts.o);
				if(d > dist) continue;
				vec head = generateHead(ts.o, ts.aim[0]);
				const int hz = hitplayer(gs.o, gs.aim[0], gs.aim[1], to, ts.o, head);
				if(!hz) continue;
				cn = i;
				dist = d;
				hitzone = hz;
			}
			switch(e.gun){
				case GUN_BOW: // explosive tip is stuck to a player
				{
					if(cn >= 0 && !m_expert){
						serverdamage(clients[cn], &c, hitzone == HIT_HEAD ? 50 : 25, GUN_BOW, FRAG_NONE, clients[cn]->state.o);
						if(clients[cn]->state.state != CS_ALIVE){
							to = clients[cn]->state.o;
							cn = -1;
						}
					}
					if(cn >= 0) sendf(-1, 1, "ri2", N_STICK, cn);
					else sendf(-1, 1, "ri2f3", N_STICK, -1, to.x, to.y, to.z);
					// timed explosion
					projevent &exp = c.addtimer().proj;
					exp.type = GE_PROJ;
					//gs.tips.add(exp.proj.id = rand());
					exp.millis = gamemillis + TIPSTICKTTL;
					exp.gun = GUN_BOW;
					exp.flag = cn;
					loopi(3) exp.o[i] = to[i];
					break;
				}
				case GUN_HEAL: // healing a player
				{
					//cn = c.clientnum;
					if(cn < 0) break;
					const int flags = (cn == c.clientnum ? FRAG_FLAG : FRAG_NONE) | (hitzone == HIT_HEAD ? FRAG_GIB : FRAG_NONE);
					serverdamage(clients[cn], &c, effectiveDamage(e.gun, dist), e.gun, flags, gs.o);
					loopi(15){ // 15 x 1hp heals over the next 1 to 2.5 seconds
						reloadevent &heal = clients[cn]->addtimer().reload;
						heal.type = GE_RELOAD;
						heal.id = c.clientnum;
						heal.millis = gamemillis + 1000 + i * 100;
						heal.gun = 1;
					}
					break;
				}
			}
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
			if(e.gun == GUN_SHOTGUN){ // many rays, many players
				damagedealt += shotgun(c, gs.o, to);
			}
			else damagedealt += shot(c, gs.o, to, e.gun, c.clientnum);
		}
	}
	gs.damage += damagedealt;
	gs.shotdamage += max(damagepotential, damagedealt);
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
	/*int n = 1;
	while(n<c.events.length() && c.events[n].type==GE_HEAD) n++;
	c.events.remove(0, n);*/
	c.events.remove(0);
}

void processtimer(client &c, projevent &e){
	vec o(valid_client(e.flag) ? clients[e.flag]->state.o : e.o);
	int bowexplodedmgdealt = explosion(c, o, GUN_BOW);
	c.state.damage += bowexplodedmgdealt;
	c.state.shotdamage += max<int>(effectiveDamage(e.gun, 0), bowexplodedmgdealt);
}

void processtimer(client &c, reloadevent &e){
	// healer: e.id
	int heal = e.gun;
	if(heal >= MAXHEALTH - c.state.health){
		heal = MAXHEALTH - c.state.health;
		c.state.damagelog.setsize(0);
	}
	if(c.state.state == CS_ALIVE) c.state.health += heal;
	sendf(-1, 1, "ri3", N_REGEN, c.clientnum, heal);
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
		while(c.events.length()) // ordered
		{
			gameevent &e = c.events[0];
			if(e.millis>gamemillis) break;
			if(e.millis<c.lastevent) { clearevent(c); continue; }
			c.lastevent = e.millis;
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
		loopvj(c.timers){ // unordered
			gameevent &e = c.timers[j];
			if(e.millis>gamemillis) continue;
			if(e.type == GE_RELOAD && (c.state.state == CS_DEAD || c.state.health >= MAXHEALTH)){
				c.removetimers(GE_RELOAD);
				continue;
			}
			switch(e.type){
				case GE_PROJ: processtimer(c, e.proj); break;
				case GE_RELOAD: processtimer(c, e.reload); break;
			}
			c.timers.remove(j--);
		}
	}
}
