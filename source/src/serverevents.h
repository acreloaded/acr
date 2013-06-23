// processing of server events

// server ballistics (will automatically include ballistic.h)
#include "serverballistics.h"

// ordered
void destroyevent::process(client *ci)
{
	client &c = *ci;
	clientstate &gs = c.state;
	int damagepotential = effectiveDamage(weap, 0), damagedealt = 0;
	switch(weap){
		case WEAP_GRENADE:
		{
			if(!gs.grenades.remove(flags)) return;
			damagedealt += explosion(c, o, WEAP_GRENADE);
			break;
		}

		case WEAP_KNIFE:
		{
			if(!gs.knives.removeany()) return;
			ushort dmg = effectiveDamage(WEAP_KNIFE, 0);
			client *hit = valid_client(flags) && flags != c.clientnum ? clients[flags] : NULL;
			bool done = false;
			if(hit){ // maybe change this to server-sided collision?
				client &target = *hit;
				clientstate &ts = target.state;
				if(ts.state == CS_ALIVE && !ts.protect(gamemillis, gamemode, mutators)){
					int tknifeflags = FRAG_FLAG;
					if(checkcrit(0, 0, 20)){ // 5% critical hit chance
						tknifeflags |= FRAG_CRIT;
						dmg *= 1.6f; // 80 * 1.6 = 128 damage = almost instant kill!
					}
					else dmg /= 1.5f; //80 / 2 = 53 just because of the bleeding effect
					damagedealt += dmg;

					int cubefloor = getblockfloor(getmaplayoutid((o.x = ts.o.x), (o.y = ts.o.y)));
					o.z = ts.o.z > cubefloor ? (cubefloor + ts.o.z) / 2 : cubefloor;

					// bleeding damage
					if(!m_zombie(gamemode) && !isteam(&c, hit)){
						target.state.addwound(c.clientnum, o);
						sendf(-1, 1, "ri2", N_BLEED, target.clientnum);
					}
					done = true;
					serverdamage(&target, &c, dmg, WEAP_KNIFE, FRAG_FLAG, vec(0, 0, 0));
				}
			}

			sendhit(c, WEAP_KNIFE, o, done ? dmg : 0);
			sknife &k = sknives.add();
			k.millis = gamemillis;
			sendf(-1, 1, "ri2f3", N_KNIFEADD, k.id = ++knifeseq, (k.o.x = o.x), (k.o.y = o.y), (k.o.z = o.z));
			break;
		}

		default:
			return;
	}
	gs.damage += damagedealt;
	gs.shotdamage += max(damagedealt, damagepotential);
}

void shotevent::process(client *ci)
{
	client &c = *ci;
	clientstate &gs = c.state;
	int wait = millis - gs.lastshot; // use event millis, not gamemillis
	if(!gs.isalive(gamemillis) || // dead
		weap<0 || weap>=WEAP_MAX || // invalid weapon
		weap!=gs.gunselect || // not selected
		(weap == WEAP_AKIMBO && gs.akimbomillis < gamemillis) || // akimbo when out
		wait<gs.gunwait[weap] || // not allowed
		gs.mag[weap]<=0) // out of ammo in mag
			return;
	if(!melee_weap(weap)) // ammo cost
		--gs.mag[weap];
	gs.updateshot(millis); // use event millis, not gamemillis
	gs.gunwait[weap] = attackdelay(weap);
	// for ease of access
	vec from(gs.o), /*to(to), */surface;
	// if using their aim position
		// #define RADD (PI/180.)
		// to = vec(sin(gs.aim[0]*RADD)*cos(gs.aim[1]*RADD), -cos(gs.aim[0]*RADD)*cos(gs.aim[1]*RADD), sin(gs.aim[1]*RADD));
	// if using delta position (or the above)
		// to.normalize().add(from);
	// apply spread
	const float spreadf = to.dist(from)/1000.f,
		crouchfactor = 1 - (gs.crouching ? min(gamemillis - gs.crouchmillis, CROUCHTIME) : CROUCHTIME - min(gamemillis - gs.crouchmillis, CROUCHTIME)) * .25f / CROUCHTIME;
	const int zoomtime = ADSTIME(gs.perk2 == PERK_TIME);
	float adsfactor = 1 - float(gs.scoping ? min(gamemillis - gs.scopemillis, zoomtime) : zoomtime - min(gamemillis - gs.scopemillis, zoomtime)) * guns[weap].spreadrem / 100 / zoomtime;
	if(weap==WEAP_SHOTGUN){
		// apply shotgun spread
		if(m_classic(gamemode, mutators)) adsfactor *= .75f;
		if(spreadf*adsfactor) loopi(SGRAYS){
			gs.sg[i] = to;
			applyspread(gs.o, gs.sg[i], guns[weap].spread, (gs.perk2 == PERK2_STEADY ? .65f : 1)*spreadf*adsfactor);
			straceShot(from, gs.sg[i]);
		}
	}
	else{
		// apply normal ray spread
		const int spread = guns[weap].spread * (gs.vel.magnitude() / 3.f + gs.pitchvel / 5.f + 0.4f) * 1.2f * crouchfactor;
		if(m_classic(gamemode, mutators)) adsfactor *= .6f;
		applyspread(gs.o, to, spread, (gs.perk2 == PERK2_STEADY ? .75f : 1)*spreadf*adsfactor);
	}
	// trace shot
	straceShot(from, to, &surface);
	// calculate shot properties
	int damagepotential = 0, damagedealt = 0;
	if(weap == WEAP_SHOTGUN){
		loopi(SGRAYS) damagepotential += effectiveDamage(weap, vec(gs.sg[i]).dist(gs.o));
	}
	else if(melee_weap(weap)) damagepotential = guns[weap].damage; // melee damage
	else if(weap == WEAP_RPG) damagepotential = 50; // potential stick damage
	else if(weap == WEAP_GRENADE) damagepotential = 0;
	else damagepotential = effectiveDamage(weap, to.dist(gs.o));

	switch(weap){
		case WEAP_GRENADE: gs.grenades.add(id); break;
		case WEAP_RPG: // explosive tip is stuck to a player
		{
			int hitzone = HIT_NONE;
			vec expc;
			static ivector exclude;
			exclude.setsize(0);
			exclude.add(c.clientnum);
			client *hit = nearesthit(c, from, to, hitzone, pos, exclude, expc);
			if(hit){
				int dmg = HEALTHSCALE;
				if(hitzone == HIT_HEAD){
					sendheadshot(from, to, dmg);
					dmg *= m_progressive(gamemode, mutators) ? (250) : (150);
				}
				else
					dmg *= m_progressive(gamemode, mutators) ? (hitzone * 75) : (55);
				damagedealt += dmg;
				sendhit(c, WEAP_RPG, to, dmg); // blood, not explosion
				serverdamage(hit, &c, dmg, WEAP_RPG, FRAG_GIB | (hitzone == HIT_HEAD ? FRAG_FLAG : FRAG_NONE), expc, expc.dist(from));
			}
			// fix explosion on walls
			else (expc = to).sub(from).normalize().mul(to.dist(from) - .1f).add(from);
			// instant explosion
			int rpgexplodedmgdealt = explosion(*ci, expc, WEAP_RPG, false, hit);
			gs.damage += rpgexplodedmgdealt;
			gs.shotdamage += max<int>(effectiveDamage(WEAP_RPG, 0), rpgexplodedmgdealt);
			break;
		}
		case WEAP_HEAL: // healing a player
		{
			int hitzone = HIT_NONE;
			vec end;
			static ivector exclude;
			exclude.setsize(0);
			exclude.add(c.clientnum);
			client *hit = gs.scoping ? &c : nearesthit(c, from, to, hitzone, pos, exclude, end);
			if(!hit) break;
			if(hit->state.wounds.length()){
				// healing by a player
				addpt(ci, HEALWOUNDPT * hit->state.wounds.length(), PR_HEALWOUND);
				if(&c != hit) addptreason(hit->clientnum, PR_HEALEDBYTEAMMATE);
				hit->state.wounds.shrink(0);
				// heal wounds = revive
				sendf(-1, 1, "ri4", N_HEAL, c.clientnum, hit->clientnum, hit->state.health);
			}
			if((&c == hit) ? gs.health < MAXHEALTH : !isteam(&c, hit)){ // that's right, no more self-heal abuse
				const int flags = hitzone == HIT_HEAD ? FRAG_GIB : FRAG_NONE;
				const float dist = hit->state.o.dist(from);
				int dmg = effectiveDamage(weap, dist) * (hitzone == HIT_HEAD ? muls[MUL_NORMAL].head : muls[MUL_NORMAL].leg);
				if(hitzone == HIT_HEAD)
					sendheadshot(from, to, dmg);
				serverdamage(hit, &c, dmg, weap, flags, gs.o, dist);
				damagedealt += dmg;
			}
			loopi(&c == hit ? 25 : 15)
				// heals over the next 1 to 2.5 seconds (no time perk, for others)
				hit->addtimer(new healevent(gamemillis + (10 + i) * 100 / (gs.perk1 == PERK_POWER ? 2 : 1), c.clientnum, gs.perk1 == PERK_POWER ? 2 : 1));
			if(hit == &c) (end = to).sub(from).normalize().add(from); // 25 cm fx
			// hide blood for healing weapon
			// sendhit(c, WEAP_HEAL, end, dmg); // blood
			to = end;
			break;
		}
		case WEAP_KNIFE: // falls through if not "compact" (throw)
			if(compact){
				if(gs.ammo[WEAP_KNIFE]){
					gs.knives.add(id);
					gs.ammo[WEAP_KNIFE]--;
				}
				break;
			}
		case WEAP_SNIPER2:
			gs.allowspeeding(gamemillis, 1500);
			// fallthrough
		default:
		{
			if(weap == WEAP_SHOTGUN) // many rays, many players
				damagedealt += shotgun(c, pos); // WARNING: modifies gs.sg
			else{
				static ivector exclude;
				exclude.setsize(0);
				exclude.add(c.clientnum);
				damagedealt += shot(c, gs.o, to, pos, weap, FRAG_NONE, surface, exclude); // WARNING: modifies to
				// collateral
				if(exclude.length() > 3)
				{
					int collat = 0;
					loopv(exclude) if(/*exclude[i] != c.clientnum && */valid_client(exclude[i]) && clients[exclude[i]]->state.state == CS_DEAD) ++collat;
					if(collat >= 2) sendf(-1, 1, "ri3", N_MULTI, c.clientnum, collat);
				}
			}
		}
	}
	gs.damage += damagedealt;
	gs.shotdamage += max(damagepotential, damagedealt);

	// create packet
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	// packet shotgun rays
	if(weap==WEAP_SHOTGUN){ putint(p, N_SG); loopi(SGRAYS) loopj(3) putfloat(p, gs.sg[i][j]); }
	// packet shot message
	putint(p, compact ? N_SHOOTC : N_SHOOT);
	putint(p, c.clientnum);
	putint(p, weap);
	if(!compact){
		putfloat(p, from.x);
		putfloat(p, from.y);
		putfloat(p, from.z);
		putfloat(p, to.x);
		putfloat(p, to.y);
		putfloat(p, to.z);
	}
	enet_packet_resize(packet, p.length());
	sendpacket(-1, 1, packet, !compact && weap != WEAP_GRENADE ? -1 : c.clientnum);
	if(packet->referenceCount==0) enet_packet_destroy(packet);
}

void reloadevent::process(client *ci){
	clientstate &gs = ci->state;
	if(!gs.isalive(gamemillis) || // dead
		weap<0 || weap>=WEAP_MAX || // invalid weapon
		(weap == WEAP_AKIMBO && gs.akimbomillis < gamemillis) || // akimbo when out
		gs.ammo[weap] < 1 /*reload*/) // no ammo
			return;
	const int mag = magsize(weap), reload = reloadsize(weap);

	if(!reload || // cannot reload
		gs.mag[weap] >= mag) // already full
			return;

	// perform the reload
	gs.mag[weap]   = min(mag, gs.mag[weap] + reload);
	gs.ammo[weap] -= /*reload*/ 1;

	int wait = millis - gs.lastshot;
	sendf(-1, 1, "ri5", N_RELOAD, ci->clientnum, weap, gs.mag[weap], gs.ammo[weap]);
	if(!gs.gunwait[weap] || wait >= gs.gunwait[weap]) gs.updateshot(gamemillis);
	gs.gunwait[weap] += reloadtime(weap);
}

void akimboevent::process(client *ci){
	clientstate &gs = ci->state;
	if(!gs.isalive(gamemillis) || !gs.akimbo || gs.akimbomillis) return;
	// WHY DOES THIS EVEN EXIST?!
	gs.akimbomillis = gamemillis + 30000;
}

// unordered

void healevent::process(client *ci){
	const int heal = hp * HEALTHSCALE, todo = MAXHEALTH - ci->state.health;
	if(heal >= todo){
		// fully healed!
		ci->invalidateheals();
		if(valid_client(id)){
			if(id==ci->clientnum) addpt(clients[id], HEALSELFPT, PR_HEALSELF);
			else if(isteam(ci, clients[id])) addpt(clients[id], HEALTEAMPT, PR_HEALTEAM);
			else addpt(clients[id], HEALENEMYPT, PR_HEALENEMY);
		}
		ci->state.health = MAXHEALTH;
		if(!m_zombie(gamemode)) return sendf(-1, 1, "ri4", N_HEAL, id, ci->clientnum, ci->state.health);
	}
	// partial heal
	else ci->state.health += heal;
	sendf(-1, 1, "ri3", N_REGEN, ci->clientnum, ci->state.health);
}

void suicidebomberevent::process(client *ci){ explosion(*ci, ci->state.o, WEAP_GRENADE, true, valid_client(id) ? clients[id] : NULL); }

void airstrikeevent::process(client *ci){ explosion(*ci, o, WEAP_GRENADE, false); }

// processing events
bool timedevent::flush(client *ci, int fmillis)
{
	if(!valid) return true;
	if(millis > fmillis) return false;
	else if(millis >= ci->lastevent){
		ci->lastevent = millis;
		process(ci);
	}
	return true;
}

void processevents(){
	loopv(clients)
	{
		client &c = *clients[i];
		if(c.type==ST_EMPTY || !c.connected || c.team == TEAM_SPECT) continue;
		clientstate &cs = c.state;
		// game ending nuke...
		if(cs.nukemillis && cs.nukemillis <= gamemillis && minremain){
			// boom... gg
			//forceintermission = true;
			cs.nukemillis = 0;
			sendf(-1, 1, "ri4", N_STREAKUSE, i, STREAK_NUKE, 0);
			// apply the nuke effect
			nuke(c);
		}
		// drown,(bleed+regen)
		if(cs.state == CS_ALIVE){ // can't regen or drown or bleed if dead
			// drown underwater
			if(!m_classic(gamemode, mutators) && cs.o.z < smapstats.hdr.waterlevel){
				if(cs.drownmillis <= 0){
					if(cs.drownmillis) // resume partial drowning
						cs.drownval = max(cs.drownval - ((servmillis + cs.drownmillis) / 1000), 0);
					cs.drownmillis = gamemillis;
				}
				char drownstate = (gamemillis - cs.drownmillis) / 1000;
				while(cs.drownval < drownstate){
					serverdamage(&c, &c, powf(++cs.drownval, 7.f)/1000000, WEAP_MAX + 13, FRAG_NONE, cs.o);
					if(cs.state != CS_ALIVE) break; // dead!
				}
			}
			else if(cs.drownmillis > 0) cs.drownmillis = -cs.drownmillis;
			// bleed = no drown
			if(cs.wounds.length()){ // bleeding; oh no!
				loopv(cs.wounds){
					wound &w = cs.wounds[i];
					if(!valid_client(w.inflictor)) cs.wounds.remove(i--);
					else if(w.lastdealt + 500 < gamemillis){
						client &owner = *clients[w.inflictor];
						const int bleeddmg = (m_zombie(gamemode) ? BLEEDDMGZ : owner.state.perk2 == PERK_POWER ? BLEEDDMGPLUS : BLEEDDMG) * HEALTHSCALE;
						owner.state.damage += bleeddmg;
						owner.state.shotdamage += bleeddmg;
						// where were we wounded?
						vec woundloc = cs.o;
						woundloc.add(w.offset);
						// blood fx and stuff
						sendhit(owner, WEAP_KNIFE, woundloc, bleeddmg);
						// use wounded location as damage source
						serverdamage(&c, &owner, bleeddmg, WEAP_KNIFE, FRAG_NONE, woundloc, c.state.o.dist(owner.state.o));
						w.lastdealt = gamemillis;
					}
				}
			}
			else if(m_regen(gamemode, mutators) && cs.state == CS_ALIVE && cs.health < STARTHEALTH && cs.lastregen + (cs.perk1 == PERK_POWER ? REGENINT * .7f : REGENINT) < gamemillis){
				int amt = round(float((STARTHEALTH - cs.health) / 5 + 15));
				if(cs.perk1 == PERK_POWER) amt *= 1.4f;
				if(amt >= STARTHEALTH - cs.health)
					amt = STARTHEALTH - cs.health;
				sendf(-1, 1, "ri3", N_REGEN, i, cs.health += amt);
				cs.lastregen = gamemillis;
			}
		}
		else if(cs.state == CS_WAITING || (c.type == ST_AI && valid_client(c.state.ownernum) && clients[c.state.ownernum]->isonrightmap && cs.state == CS_DEAD && cs.lastspawn<0)){ // spawn queue
			const int waitremain = SPAWNDELAY - gamemillis + cs.lastdeath;
			if(canspawn(&c) && waitremain <= 0) sendspawn(&c);
			//else sendmsgi(41, waitremain, sender);
		}
		// akimbo out!
		if(cs.akimbomillis && cs.akimbomillis < gamemillis) {
			cs.akimbomillis = 0;
			cs.akimbo = false;
			sendf(-1, 1, "ri2", N_AKIMBO, c.clientnum);
		}
		// events
		while(c.events.length()) // are ordered
		{
			timedevent *ev = c.events[0];
			if(ev->flush(&c, gamemillis)) delete c.events.remove(0);
			else break;
		}
		// timers
		loopvj(c.timers)
		{
			if(!c.timers[j]->valid || (c.timers[j]->type == GE_HEAL && (cs.health >= MAXHEALTH || cs.state != CS_ALIVE)))
			{
				delete c.timers.remove(j--);
				continue;
			}
			else if(c.timers[j]->millis <= gamemillis)
			{
				c.timers[j]->process(&c);
				delete c.timers.remove(j--);
			}
		}
		// new nickname
		if(c.name_relay && servmillis >= c.name_relay){
			logline(ACLOG_INFO,"[%s] %s is now called %s", gethostname(i), formatname(&c), c.newname);
			copystring(c.name, c.newname, MAXNAMELEN+1);
			sendf(-1, 1, "ri2s", N_NEWNAME, i, c.name);
			c.name_relay = 0;
		}
	}
}
