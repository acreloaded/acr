// processing of server events

void processevent(client *c, explodeevent &e)
{
	clientstate &gs = c->state;
	switch(e.gun)
	{
		case GUN_GRENADE:
			if(!gs.grenades.remove(e.id)) return;
			break;

		default:
			return;
	}
	vec o(e.o[0], e.o[1], e.o[2]);
	loopv(clients){
		client *target = clients[i];
		if(!target) continue;
		vec dir;
		float dist = target->state.o.dist(o, dir);		
		if(dist >= guns[e.gun].endrange) continue;
		//dir.div(dist); // it gets normalized by the serverdamage() function
		serverdamage(target, c, effectiveDamage(e.gun, dist), e.gun, true, dir);
	}
}

void processevent(client *c, shotevent &e)
{
	clientstate &gs = c->state;
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
	if(e.gun==GUN_PISTOL && gs.akimbomillis>gamemillis) gs.gunwait[e.gun] /= 2;
	/*sendf(-1, 1, "ri3f6x", SV_SHOTFX, c->clientnum, e.gun,
		c->state.o.x, c->state.o.y, c->state.o.z,
		e.to[0], e.to[1], e.to[2], c->clientnum);*/
	ENetPacket *packet = enet_packet_create(NULL, 9 * sizeof(float), ENET_PACKET_FLAG_RELIABLE);
	ucharbuf p(packet->data, packet->dataLength);
	putint(p, SV_SHOTFX);
	putint(p, c->clientnum);
	putint(p, e.gun);
	putfloat(p, c->state.o.x);
	putfloat(p, c->state.o.y);
	putfloat(p, c->state.o.z);
	putfloat(p, e.to[0]);
	putfloat(p, e.to[1]);
	putfloat(p, e.to[2]);
	enet_packet_resize(packet, p.length());
	sendpacket(-1, 1, packet, c->clientnum);
	if(packet->referenceCount==0) enet_packet_destroy(packet);

	gs.shotdamage += guns[e.gun].damage*(e.gun==GUN_SHOTGUN ? SGRAYS : 1);
	switch(e.gun)
	{
		case GUN_GRENADE: gs.grenades.add(e.id); break;
		default:
		{
			int totalrays = 0, maxrays = e.gun==GUN_SHOTGUN ? SGRAYS : 1;
			for(int i = 1; i<c->events.length() && c->events[i].type==GE_HIT; i++)
			{
				hitevent &h = c->events[i].hit;
				if(!clients.inrange(h.target)) continue;
				client *target = clients[h.target];
				if(target->type==ST_EMPTY || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence) continue;

				int rays = e.gun==GUN_SHOTGUN ? h.info : 1;
				if(rays<1) continue;
				totalrays += rays;
				if(totalrays>maxrays) continue;

				bool gib = false;
				int damage = rays * effectiveDamage(e.gun, c->state.o.dist(vec(e.to)));
				if(e.gun==GUN_KNIFE){
					if(h.info == 2) damage *= 10;
					gib = true;
				}
				else if(e.gun==GUN_SHOTGUN) gib = damage > SGGIB;
				else gib = h.info == 2;
				if(e.gun!=GUN_SHOTGUN){
					if(h.info == 1) damage *= 0.6;
					else if(h.info == 2) damage *= e.gun==GUN_SNIPER ? 5 : 1.5;
				}
				serverdamage(target, c, damage, e.gun, gib, h.dir);
			}
			break;
		}
	}
}

void processevent(client *c, reloadevent &e)
{
	clientstate &gs = c->state;
	if(!gs.isalive(gamemillis) ||
	   e.gun<GUN_KNIFE || e.gun>=NUMGUNS ||
	   !reloadable_gun(e.gun) ||
	   gs.ammo[e.gun]<=0)
		return;

	bool akimbo = e.gun==GUN_PISTOL && gs.akimbomillis>e.millis;
	int mag = (akimbo ? 2 : 1) * magsize(e.gun), numbullets = min(gs.ammo[e.gun], mag - gs.mag[e.gun]);
	if(numbullets<=0) return;

	gs.mag[e.gun] += numbullets;
	gs.ammo[e.gun] -= numbullets;

	int wait = e.millis - gs.lastshot;
	sendf(-1, 1, "ri3", SV_RELOAD, c->clientnum, e.gun);
	if(gs.gunwait[e.gun] && wait<gs.gunwait[e.gun]) gs.gunwait[e.gun] += reloadtime(e.gun);
	else
	{
		loopi(NUMGUNS) if(gs.gunwait[i]) gs.gunwait[i] = max(gs.gunwait[i] - (e.millis-gs.lastshot), 0);
		gs.lastshot = e.millis;
		gs.gunwait[e.gun] += reloadtime(e.gun);
	}
}

void processevent(client *c, akimboevent &e)
{
	clientstate &gs = c->state;
	if(!gs.isalive(gamemillis) || gs.akimbos<=0) return;
	gs.akimbos--;
	gs.akimbomillis = e.millis+30000;
}

void clearevent(client *c)
{
	int n = 1;
	while(n<c->events.length() && c->events[n].type==GE_HIT) n++;
	c->events.remove(0, n);
}

void processevents()
{
	loopv(clients)
	{
		client *c = clients[i];
		if(!c || c->type==ST_EMPTY) continue;
		while(c->events.length())
		{
			gameevent &e = c->events[0];
			if(e.type<=GE_RELOAD) // timed
			{
				if(e.shot.millis>gamemillis) break;
				if(e.shot.millis<c->lastevent) { clearevent(c); continue; }
				c->lastevent = e.shot.millis;
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

