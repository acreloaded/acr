// processing of server events

void processevent(client &c, explodeevent &e)
{
	clientstate &gs = c.state;
	switch(e.gun)
	{
		case GUN_GRENADE:
			if(!gs.grenades.remove(e.id)) return;
			break;

		default:
			return;
	}
	loopv(clients){
		client &target = *clients[i];
		float dist = target.state.o.dist(e.o);
		if(dist >= guns[e.gun].endrange) continue;
		serverdamage(&target, &c, effectiveDamage(e.gun, dist), e.gun, true, e.o);
	}
}

void processevent(client &c, shotevent &e)
{
	vector<hitevent> hits;
	while(c.events.length() > 1 && c.events[1].type == GE_HIT){
		hits.add(c.events[1].hit);
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
	/*sendf(-1, 1, "ri3f6x", N_SHOOT, c->clientnum, e.gun,
		c->state.o.x, c->state.o.y, c->state.o.z,
		e.to[0], e.to[1], e.to[2], c->clientnum);*/
	ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	if(e.gun==GUN_SHOTGUN){
		putint(p, N_SG);
		loopi(SGRAYS) loopj(3) putfloat(p, gs.sg[i][j]);
	}
	putint(p, e.gun == GUN_GRENADE ? N_SHOOTC : N_SHOOT);
	putint(p, c.clientnum);
	putint(p, e.gun);
	if(e.gun != GUN_GRENADE){
		putfloat(p, gs.o.x);
		putfloat(p, gs.o.y);
		putfloat(p, gs.o.z);
		putfloat(p, e.to[0]);
		putfloat(p, e.to[1]);
		putfloat(p, e.to[2]);
	}
	enet_packet_resize(packet, p.length());
	sendpacket(-1, 1, packet, c.clientnum);
	if(packet->referenceCount==0) enet_packet_destroy(packet);

	gs.shotdamage += guns[e.gun].damage*(e.gun==GUN_SHOTGUN ? SGRAYS : 1);
	switch(e.gun){
		case GUN_GRENADE: gs.grenades.add(e.id); break;
		default:
		{
			int totalrays = 0, maxrays = e.gun==GUN_SHOTGUN ? SGRAYS : 1;
			loopv(hits){
				hitevent &h = hits[i];
				if(!clients.inrange(h.target)) continue;
				client *target = clients[h.target];
				if(target->type==ST_EMPTY || target->state.state!=CS_ALIVE || &c==target || h.lifesequence!=target->state.lifesequence) continue;

				int rays = e.gun==GUN_SHOTGUN ? popcount(h.info) : 1;
				if(e.gun==GUN_SHOTGUN){
					uint hitflags = h.info;
					loopi(SGRAYS) if((hitflags & (1 << i)) && gs.sg[i].dist(e.to) > 60.f) // 2 meters for height x3 for unknown reasons + 3m for lag
						rays--;
				} else if (target->state.o.dist(vec(e.to)) > 20.f) continue; // 2 meters for height + 3 meters for lag
				if(rays<1) continue;
				if(totalrays + rays > maxrays) continue;

				bool gib = false;
				int damage = 0;
				damage = rays * effectiveDamage(e.gun, vec(e.to).dist(gs.o));
				if(e.gun == GUN_SHOTGUN){
					uint hitflags = h.info;
					loopi(SGRAYS) if(hitflags & (1 << i)) damage += effectiveDamage(GUN_SHOTGUN, gs.sg[i].dist(gs.o));
				}
				if(!damage) continue;

				totalrays += rays;
				if(e.gun==GUN_SHOTGUN) gib = damage > SGGIB;
				else gib = e.gun==GUN_KNIFE || h.info == 2;
				if(e.gun!=GUN_SHOTGUN){
					if(h.info == 1) damage *= 0.67;
					else if(h.info == 2) damage *= e.gun == GUN_SNIPER || e.gun == GUN_SLUG || e.gun == GUN_KNIFE ? 5 : 2.5;
				} else if(h.info & 0x80) gib = true;
				if(e.gun == GUN_KNIFE){
					target->state.cutter = c.clientnum;
					target->state.lastcut = gamemillis;
				}
				serverdamage(target, &c, damage, e.gun, gib);
			}
			break;
		}
	}
}

void processevent(client &c, reloadevent &e)
{
	clientstate &gs = c.state;
	int mag = magsize(e.gun);
	if(!gs.isalive(gamemillis) ||
	   e.gun<GUN_KNIFE || e.gun>=NUMGUNS ||
	   !reloadable_gun(e.gun) ||
	   gs.mag[e.gun] >= mag ||
	   gs.ammo[e.gun] < mag)
		return;

	gs.mag[e.gun]   = mag;
	gs.ammo[e.gun] -= mag;

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

void processevent(client &c, akimboevent &e)
{
	clientstate &gs = c.state;
	if(!gs.isalive(gamemillis) || gs.akimbos<=0) return;
	gs.akimbos--;
	gs.akimbomillis = e.millis+30000;
}

void clearevent(client &c)
{
	/*
	int n = 1;
	while(n<c->events.length() && c->events[n].type==GE_HIT) n++;
	c->events.remove(0, n);
	*/
	c.events.remove(0);
}

void processevents()
{
	loopv(clients)
	{
		client &c = *clients[i];
		if(c.type==ST_EMPTY) continue;
		if(c.state.state == CS_ALIVE){ // can't regen or bleed if dead
			if(c.state.lastcut){ // bleeding; oh no!
				if(c.state.lastcut + 500 < gamemillis && valid_client(c.state.cutter)){
					serverdamage(&c, clients[c.state.cutter], 10, GUN_KNIFE, false);
					c.state.lastcut = gamemillis;
				}
			}
			else if(!m_arena && c.state.state == CS_ALIVE && c.state.health < STARTHEALTH && c.state.lastregen + 2500 < gamemillis){
				int amt = 15;
				if(amt > STARTHEALTH - c.state.health){
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
				case GE_EXPLODE: processevent(c, e.explode); break;
				case GE_AKIMBO: processevent(c, e.akimbo); break;
				case GE_RELOAD: processevent(c, e.reload); break;
				// untimed events are GONE!
			}
			clearevent(c);
		}
	}
}

