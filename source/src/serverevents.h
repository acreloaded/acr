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
    for(int i = 1; i<c->events.length() && c->events[i].type==GE_HIT; i++)
    {
        hitevent &h = c->events[i].hit;
        if(!clients.inrange(h.target)) continue;
        client *target = clients[h.target];
        if(target->type==ST_EMPTY || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.dist<0 || h.dist>EXPDAMRAD) continue;

        int j = 1;
        for(j = 1; j<i; j++) if(c->events[j].hit.target==h.target) break;
        if(j<i) continue;

        int damage = int(guns[e.gun].damage*(1-h.dist/EXPDAMRAD));
        bool chk_gun = e.gun==GUN_GRENADE;
        bool chk_dir = h.dir[0]+h.dir[1]+h.dir[2]==0;
        bool chk_dst = h.dist < 2.0f;
        bool chk_cnr = c->clientnum == target->clientnum;
        if(chk_gun && chk_dir && chk_dst && chk_cnr) damage = INT_MAX; // nade suicide
        serverdamage(target, c, damage, e.gun, true, h.dir);
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
    sendf(-1, 1, "ri6x", SV_SHOTFX, c->clientnum, e.gun,
//         int(e.from[0]*DMF), int(e.from[1]*DMF), int(e.from[2]*DMF),
        int(e.to[0]*DMF), int(e.to[1]*DMF), int(e.to[2]*DMF),
        c->clientnum);
    gs.shotdamage += guns[e.gun].damage*(e.gun==GUN_SHOTGUN ? SGMAXDMGLOC : 1); // 2011jan17:ft: so accuracy stays correct, since SNIPER:headshot also "exceeds expectations" we use SGMAXDMGLOC instead of SGMAXDMGABS!
    switch(e.gun)
    {
        case GUN_GRENADE: gs.grenades.add(e.id); break;
        default:
        {
            int totalrays = 0, maxrays = e.gun==GUN_SHOTGUN ? 3*SGRAYS: 1;
            int tothits_c = 0, tothits_m = 0, tothits_o = 0; // sgrays
            for(int i = 1; i<c->events.length() && c->events[i].type==GE_HIT; i++)
            {
                hitevent &h = c->events[i].hit;
                if(!clients.inrange(h.target)) continue;
                client *target = clients[h.target];
                if(target->type==ST_EMPTY || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence) continue;

                int rays = 1, damage = 0;
                bool gib = false;
                if(e.gun == GUN_SHOTGUN)
                {
                    h.info = isbigendian() ? endianswap(h.info) : h.info;
                    int bonusdist = h.info&0xFF;
                    int numhits_c = (h.info & 0x0000FF00) >> 8, numhits_m = (h.info & 0x00FF0000) >> 16, numhits_o = (h.info & 0xFF000000) >> 24;
                    tothits_c += numhits_c; tothits_m += numhits_m; tothits_o += numhits_o;
                    rays = numhits_c + numhits_m + numhits_o;
                    
                    if(rays < 1 || tothits_c > SGRAYS || tothits_m > SGRAYS || tothits_o > SGRAYS || bonusdist > SGDMGBONUS) continue;

                    gib = rays == maxrays;
                    float fdamage = (SGDMGTOTAL/(21*100.0f)) * (numhits_o * SGCOdmg/10.0f + numhits_m * SGCMdmg/10.0f + numhits_c * SGCCdmg/10.0f);
                    fdamage += (float)bonusdist;
                    damage = (int)ceil(fdamage);
#ifdef ACAC
                    if (!sg_engine(target, c, numhits_c, numhits_m, numhits_o, bonusdist)) continue;
#endif
                }
                else
                {
                    damage = rays*guns[e.gun].damage;
                    gib = e.gun == GUN_KNIFE;
                    if(e.gun == GUN_SNIPER && h.info != 0)
                    {
                        gib = true;
                        damage *= 3;
                    }
                }
                totalrays += rays;

                if(totalrays>maxrays) continue;
                serverdamage(target, c, damage, e.gun, gib, h.dir);
            }
            break;
        }
    }
}

void processevent(client *c, suicideevent &e)
{
    serverdamage(c, c, INT_MAX, GUN_KNIFE, false);
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
    if(!gs.isalive(gamemillis) || gs.akimbomillis) return;
    gs.akimbomillis = e.millis+30000;
}

void clearevent(client &c)
{
    int n = 1;
    while(n<c.events.length() && c.events[n].type==GE_HIT) n++;
    c.events.remove(0, n);
}

void processevents()
{
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type==ST_EMPTY || !c.isauthed || team_isspect(c.team)) continue;
        clientstate &cs = c.state;
        /*
        // game ending nuke...
		if(cs.nukemillis && cs.nukemillis <= gamemillis && minremain)
        {
			// boom... gg
			//forceintermission = true;
			cs.nukemillis = 0;
			sendf(-1, 1, "ri4", N_STREAKUSE, i, STREAK_NUKE, 0);
			// apply the nuke effect
			nuke(c);
		}
        */
        // drown, bleed, regen
        if(cs.state == CS_ALIVE)
        {
            /*
            // drown underwater
			if(cs.o.z < smapstats.hdr.waterlevel)
            {
				if(cs.drownmillis <= 0)
                {
					if(cs.drownmillis) // resume partial drowning
						cs.drownval = max(cs.drownval - ((servmillis + cs.drownmillis) / 1000), 0);
					cs.drownmillis = gamemillis;
				}
				char drownstate = max(0, (gamemillis - cs.drownmillis) / 1000 - 10);
				while(cs.drownval < drownstate)
                {
					++cs.drownval;
					serverdamage(&c, &c, (m_classic(gamemode, mutators) ? 5 : (cs.drownval + 10)) * HEALTHSCALE, WEAP_MAX + 13, FRAG_NONE, cs.o);
					if(cs.state != CS_ALIVE) break; // dead!
				}
			}
			else if(cs.drownmillis > 0)
                cs.drownmillis = -cs.drownmillis; // save partial drowning
			// bleeding--oh no!
            if(cs.wounds.length())
            {
				loopv(cs.wounds)
                {
					wound &w = cs.wounds[i];
					if(!valid_client(w.inflictor)) cs.wounds.remove(i--);
					else if(w.lastdealt + 500 < gamemillis)
                    {
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
            // health regeneration
			else if(m_regen(gamemode, mutators) && cs.state == CS_ALIVE && cs.health < STARTHEALTH && cs.lastregen + (cs.perk1 == PERK_POWER ? REGENINT * .7f : REGENINT) < gamemillis){
				int amt = round(float((STARTHEALTH - cs.health) / 5 + 15));
				if(cs.perk1 == PERK_POWER) amt *= 1.4f;
				if(amt >= STARTHEALTH - cs.health)
					amt = STARTHEALTH - cs.health;
				sendf(-1, 1, "ri3", N_REGEN, i, cs.health += amt);
				cs.lastregen = gamemillis;
			}
            */
        }
        // not alive: spawn queue
        else if(/*cs.state == CS_WAITING ||*/ (c.type == ST_AI && valid_client(c.ownernum) && clients[c.ownernum]->isonrightmap && cs.state == CS_DEAD && cs.lastspawn<0))
        {
			const int waitremain = SPAWNDELAY - gamemillis + cs.lastdeath;
            extern int canspawn(client *c);
			if(canspawn(&c) == SP_OK && waitremain <= 0)
                sendspawn(&c);
		}
        // akimbo out!
        if(cs.akimbomillis && cs.akimbomillis < gamemillis) { cs.akimbomillis = 0; cs.akimbo = false; }
        while(c.events.length())
        {
            gameevent &e = c.events[0];
            if(e.type<GE_SUICIDE)
            {
                if(e.shot.millis>gamemillis) break;
                if(e.shot.millis<c.lastevent) { clearevent(c); continue; }
                c.lastevent = e.shot.millis;
            }
            switch(e.type)
            {
                case GE_SHOT: processevent(&c, e.shot); break;
                case GE_EXPLODE: processevent(&c, e.explode); break;
                case GE_AKIMBO: processevent(&c, e.akimbo); break;
                case GE_RELOAD: processevent(&c, e.reload); break;
                // untimed events
                case GE_SUICIDE: processevent(&c, e.suicide); break;
            }
            clearevent(c);
        }
    }
}

