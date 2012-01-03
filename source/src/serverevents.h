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
				if(ts.state == CS_ALIVE && !ts.protect(gamemillis)){
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
					if(!m_zombie(gamemode) || !isteam(&c, hit)){
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
			sendf(-1, 1, "ri2f3", N_KNIFEADD, (k.id = sknives.length()-1), (k.o.x = o.x), (k.o.y = o.y), (k.o.z = o.z));
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
	int wait = millis - gs.lastshot;
	if(!gs.isalive(gamemillis) || // dead
	   weap<0 || weap>=WEAP_MAX || // invalid weapon
	   wait<gs.gunwait[weap] || // not allowed
	   gs.mag[weap]<=0) // out of ammo in mag
		return;
	if(!melee_weap(weap)) // ammo cost
		--gs.mag[weap];
	loopi(WEAP_MAX)
		if(gs.gunwait[i])
			gs.gunwait[i] = max(gs.gunwait[i] - (millis-gs.lastshot), 0);
	gs.lastshot = millis;
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
	float adsfactor = 1 - float(gs.scoping ? min(gamemillis - gs.scopemillis, ADSTIME) : ADSTIME - min(gamemillis - gs.scopemillis, ADSTIME)) / ADSTIME;
	if(weap==WEAP_SHOTGUN){
		// apply shotgun spread
		adsfactor = (adsfactor + SGADSSPREADFACTOR - 1) / SGADSSPREADFACTOR;
		if(m_classic(gamemode, mutators)) adsfactor *= .75f;
		if(spreadf*adsfactor) loopi(SGRAYS){
			gs.sg[i] = to;
			applyspread(gs.o, gs.sg[i], SGSPREAD, (gs.perk == PERK_STEADY ? .65f : 1)*spreadf*adsfactor);
			straceShot(from, gs.sg[i]);
		}
	}
	else{
		// apply normal ray spread
		const int spread = guns[weap].spread * (gs.vel.magnitude() / 3.f + gs.pitchvel / 5.f + 0.4f) * 1.2f * crouchfactor;
		if(m_classic(gamemode, mutators)) adsfactor *= .6f;
		applyspread(gs.o, to, spread, (gs.perk == PERK_STEADY ? .75f : 1)*spreadf*adsfactor);
	}
	// trace shot
	straceShot(from, to, &surface);
	// calculate shot properties
	int damagepotential = 0, damagedealt = 0;
	if(weap == WEAP_SHOTGUN){
		loopi(SGRAYS) damagepotential += effectiveDamage(weap, vec(gs.sg[i]).dist(gs.o));
	}
	else if(weap == WEAP_KNIFE) damagepotential = guns[WEAP_KNIFE].damage; // melee damage
	else if(weap == WEAP_BOW) damagepotential = 50; // potential stick damage
	else if(weap == WEAP_GRENADE) damagepotential = 0;
	else damagepotential = effectiveDamage(weap, to.dist(gs.o));

	switch(weap){
		case WEAP_GRENADE: gs.grenades.add(id); break;
		case WEAP_BOW: // explosive tip is stuck to a player
		{
			int hitzone = HIT_NONE;
			client *hit = nearesthit(c, from, to, hitzone, heads, &c);
			if(hit){
				int dmg = HEALTHSCALE;
				if(hitzone == HIT_HEAD){
					sendheadshot(from, to, dmg);
					dmg *= m_zombies_rounds(gamemode, mutators) ? (250) : (75);
				}
				else
					dmg *= m_zombies_rounds(gamemode, mutators) ? (hitzone * 75) : (50);
				sendhit(c, WEAP_BOW, to, dmg); // blood, not explosion
				serverdamage(hit, &c, dmg, WEAP_BOW, FRAG_GIB, hit->state.o);
				if(hit->state.state != CS_ALIVE){
					to = hit->state.o;
					hit = NULL; // warning
				}
			}
			if(hit) sendf(-1, 1, "ri2", N_STICK, hit->clientnum);
			else sendf(-1, 1, "ri2f3", N_STICK, -1, to.x, to.y, to.z);
			// timed explosion
			bowevent b;
			b.millis = gamemillis + TIPSTICKTTL;
			b.id = hit ? hit->clientnum : -1;
			loopi(3) b.o[i] = to[i];
			if(c.bows.length()<128) c.bows.add(b);
			break;
		}
		case WEAP_HEAL: // healing a player
		{
			int hitzone = HIT_NONE;
			vec end;
			client *hit = gs.scoping ? &c : nearesthit(c, from, to, hitzone, heads, &c, &end);
			if(!hit) break;
			const int flags = hitzone == HIT_HEAD ? FRAG_GIB : FRAG_NONE,
				dmg = effectiveDamage(weap, hit->state.o.dist(from));
			if(flags & FRAG_GIB)
				sendheadshot(from, to, dmg);
			serverdamage(hit, &c, dmg, weap, flags, gs.o);
			loopi(&c == hit ? 25 : 15){
				// heals over the next 1 to 2.5 seconds (no perk, for others)
				healevent h;
				h.id = c.clientnum; // from this person
				h.millis = gamemillis + (10 + i) * 100 / (gs.perk == PERK_PERSIST ? 2 : 1);
				h.hp = (gs.perk == PERK_PERSIST ? 2 : 1);
				if(hit->heals.length()<128) hit->heals.add(h);
			}
			if(hit == &c) (end = to).sub(from).normalize().add(from); // 25 cm fx
			// hide blood for healing weapon
			// sendhit(c, WEAP_HEAL, (to = end), dmg); // blood
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
		default:
		{
			if(weap == WEAP_SHOTGUN){ // many rays, many players
				damagedealt += shotgun(c, heads); // WARNING: modifies gs.sg
			}
			else damagedealt += shot(c, gs.o, to, heads, weap, surface, &c); // WARNING: modifies to
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
	int mag = magsize(weap), reload = reloadsize(weap);
	if(!gs.isalive(gamemillis) || // dead
	   weap<0 || weap>=WEAP_MAX || // invalid weapon
	   !reloadable_weap(weap) || // cannot reload
	   gs.mag[weap] >= mag || // already full
	   gs.ammo[weap] < reload) // no ammo
		return;

	gs.mag[weap]   = min(mag + (gs.mag[weap] && reload > 1), gs.mag[weap] + reload);
	gs.ammo[weap] -= reload;

	int wait = millis - gs.lastshot;
	sendf(-1, 1, "ri5", N_RELOAD, ci->clientnum, weap, gs.mag[weap], gs.ammo[weap]);
	if(gs.gunwait[weap] && wait<gs.gunwait[weap]) gs.gunwait[weap] += reloadtime(weap);
	else
	{
		loopi(WEAP_MAX) if(gs.gunwait[i]) gs.gunwait[i] = max(gs.gunwait[i] - (millis-gs.lastshot), 0);
		gs.lastshot = millis;
		gs.gunwait[weap] += reloadtime(weap);
	}
}

void akimboevent::process(client *ci){
	clientstate &gs = ci->state;
	if(!gs.isalive(gamemillis) || gs.akimbos<=0) return;
	--gs.akimbos;
	gs.akimbomillis = millis + 30000;
}

// unordered
void bowevent::process(client *ci){
	vec o((valid_client(id) && clients[id]->state.lastdeath + TIPSTICKTTL < millis) ? clients[id]->state.o : o);
	int bowexplodedmgdealt = explosion(*ci, o, WEAP_BOW, false);
	ci->state.damage += bowexplodedmgdealt;
	ci->state.shotdamage += max<int>(effectiveDamage(WEAP_BOW, 0), bowexplodedmgdealt);
}

void healevent::process(client *ci){
	const int heal = hp * HEALTHSCALE, todo = MAXHEALTH - ci->state.health;
	if(heal >= todo){
		// fully healed!
		ci->state.damagelog.setsize(0);
		return sendf(-1, 1, "ri4", N_HEAL, id, ci->clientnum, ci->state.health = MAXHEALTH);
	}
	// partial heal
	sendf(-1, 1, "ri3", N_REGEN, ci->clientnum, ci->state.health += heal);
}

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
		if(c.type==ST_EMPTY) continue;
		// game ending nuke...
		if(c.state.nukemillis && c.state.nukemillis <= gamemillis && minremain){
			// boom... gg
			//forceintermission = true;
			c.state.nukemillis = 0;
			sendf(-1, 1, "ri4", N_STREAKUSE, i, STREAK_NUKE, 0);
			// apply the nuke effect
			nuke(c);
		}
		// regen/bleed
		if(c.state.state == CS_ALIVE){ // can't regen or bleed if dead
			if(c.state.wounds.length()){ // bleeding; oh no!
				loopv(c.state.wounds){
					wound &w = c.state.wounds[i];
					if(!valid_client(w.inflictor)) c.state.wounds.remove(i--);
					else if(w.lastdealt + 500 < gamemillis){
						client &owner = *clients[w.inflictor];
						const int bleeddmg = (m_zombie(gamemode) ? BLEEDDMGZ : owner.state.perk == PERK_PERSIST ? BLEEDDMGPLUS : BLEEDDMG) * HEALTHSCALE;
						owner.state.damage += bleeddmg;
						owner.state.shotdamage += bleeddmg;
						// where were we wounded?
						vec woundloc = c.state.o;
						woundloc.add(w.offset);
						// blood fx and stuff
						sendhit(owner, WEAP_KNIFE, woundloc, bleeddmg);
						// use wounded location as damage source
						serverdamage(&c, &owner, bleeddmg, WEAP_KNIFE, FRAG_NONE, woundloc);
						w.lastdealt = gamemillis;
					}
				}
			}
			else if(m_regen(gamemode, mutators) && c.state.state == CS_ALIVE && c.state.health < STARTHEALTH && c.state.lastregen + (c.state.perk == PERK_PERSIST ? REGENINT * .7f : REGENINT) < gamemillis){
				int amt = round(float((STARTHEALTH - c.state.health) / 5 + 15));
				if(c.state.perk == PERK_PERSIST) amt *= 1.4f;
				if(amt >= STARTHEALTH - c.state.health){
					amt = STARTHEALTH - c.state.health;
					c.state.damagelog.setsize(0);
				}
				sendf(-1, 1, "ri3", N_REGEN, i, c.state.health += amt);
				c.state.lastregen = gamemillis;
			}
		}
		else if(c.state.state == CS_WAITING){ // spawn queue
			const int waitremain = SPAWNDELAY - gamemillis + c.state.lastdeath;
			if(canspawn(&c) && waitremain <= 0) sendspawn(&c);
			//else sendmsgi(41, waitremain, sender);
		}
		// events
		while(c.events.length()) // are ordered
		{
			timedevent *ev = c.events[0];
			if(ev->flush(&c, gamemillis)) delete c.events.remove(0);
			else break;
		}
		// timers
		#define processtimer(timer) \
			loopvj(c.timer##s) \
			{ \
				if(!c.timer##s[j].valid) \
				{ \
					c.timer##s.remove(j--); \
					continue; \
				} \
				else if(c.timer##s[j].millis <= gamemillis) \
				{ \
					c.timer##s[j].process(&c); \
					c.timer##s.remove(j--); \
				} \
			}
		if(c.state.health >= MAXHEALTH || c.state.state != CS_ALIVE) c.heals.shrink(0);
		else processtimer(heal);
		processtimer(bow);
	}
}
