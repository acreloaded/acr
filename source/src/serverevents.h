// processing of server events

// server ballistics
#include "serverballistics.h"

// ordered
void destroyevent::process(client *ci)
{
    client &c = *ci;
    clientstate &cs = c.state;
    int damagepotential = effectiveDamage(weap, 0), damagedealt = 0;
    switch (weap)
    {
        case GUN_GRENADE:
        {
            if (!cs.grenades.remove(flags)) return;
            damagedealt += explosion(c, o, weap, !m_real(gamemode, mutators));
            break;
        }

        case GUN_KNIFE:
        {
            if (!cs.knives.removeany()) return;
            ushort dmg = effectiveDamage(GUN_KNIFE, 0);
            client *hit = valid_client(flags) && flags != c.clientnum ? clients[flags] : NULL;
            bool done = false;
            if (hit) // FIXME: change this to server-sided collision?
            {
                clientstate &ts = hit->state;
                if (ts.state == CS_ALIVE && !ts.protect(gamemillis, gamemode, mutators) && !isteam(&c, hit)) // cannot make teammates bleed
                {
                    int tknifeflags = FRAG_FLAG;
                    if (checkcrit(0, 0, 20)) // 5% critical hit chance
                    {
                        tknifeflags |= FRAG_CRIT;
                        dmg *= 1.6f; // 80 * 1.6 = 128 damage = almost instant kill!
                    }
                    else dmg /= 1.5f; // 80 / 2 = 53 just because of the bleeding effect
                    damagedealt += dmg;

                    int cubefloor = getblockfloor(getmaplayoutid((o.x = ts.o.x), (o.y = ts.o.y)));
                    o.z = ts.o.z + PLAYERHEIGHT > cubefloor ? (cubefloor + ts.o.z + PLAYERHEIGHT) / 2 : cubefloor;

                    // bleeding damage
                    if (!m_zombie(gamemode)) // && !isteam(&c, hit))
                    {
                        hit->state.addwound(c.clientnum, o);
                        sendf(NULL, 1, "ri2", SV_BLEED, flags);
                    }
                    done = true;
                    serverdamage(*hit, c, dmg, GUN_KNIFE, FRAG_FLAG, vec(0, 0, 0));
                }
            }

            sendhit(c, GUN_KNIFE, o, done ? dmg : 0);
            sknife &k = sknives.add();
            k.millis = gamemillis;
            k.o = o;
            sendf(NULL, 1, "ri5", SV_KNIFEADD, k.id = ++knifeseq, (int)(o.x*DMF), (int)(o.y*DMF), (int)(o.z*DMF));
            break;
        }

        default:
            return;
    }
    cs.damage += damagedealt;
    cs.shotdamage += max(damagedealt, damagepotential);
}

void shotevent::process(client *ci)
{
    client &c = *ci;
    clientstate &cs = c.state;
    int wait = millis - cs.lastshot;
    if (!cs.isalive(gamemillis) || // dead
        weap<0 || weap >= NUMGUNS || // invalid weapon
        weap != cs.gunselect || // not selected
        (weap == GUN_AKIMBO && cs.akimbomillis < gamemillis) || // akimbo after out
        wait<cs.gunwait[weap] || // not allowed
        cs.mag[weap] <= 0) // out of ammo in mag
        return;
    if (!melee_weap(weap)) // ammo cost
        --cs.mag[weap];
    cs.updateshot(millis);
    cs.gunwait[weap] = attackdelay(weap);

    vec from(cs.o), surface;
    // if using their aim position
        // #define RADD (PI/180.)
        // to = vec(sin(gs.aim[0]*RADD)*cos(gs.aim[1]*RADD), -cos(gs.aim[0]*RADD)*cos(gs.aim[1]*RADD), sin(gs.aim[1]*RADD));
    // if using delta position (or the above)
        // to.normalize().add(from);
    // apply spread
    const float spreadf = to.dist(from) / 1000.f, crouchfactor = cs.crouchfactor(gamemillis);
    from.z += PLAYERHEIGHT * crouchfactor; // both use 75% to 100%
    const int zoomtime = ADSTIME(cs.perk2 == PERK_TIME);
    float adsfactor = 1 - float(cs.scoping ? min(gamemillis - cs.scopemillis, zoomtime) : zoomtime - min(gamemillis - cs.scopemillis, zoomtime)) * guns[weap].spreadrem / 100 / zoomtime;
    if (weap == GUN_SHOTGUN)
    {
        // apply shotgun spread
        if (m_classic(gamemode, mutators)) adsfactor *= .75f;
        loopi(SGRAYS)
        {
            cs.sg[i] = to;
            applyspread(from, cs.sg[i], guns[weap].spread, (cs.perk2 == PERK2_STEADY ? .65f : 1)*spreadf*adsfactor);
            straceShot(from, cs.sg[i]);
        }
    }
    else
    {
        // apply normal ray spread
        const int spread = guns[weap].spread * (cs.vel.magnitude() / 3.f /*+ cs.pitchvel / 5.f*/ + 0.4f) * 1.2f * crouchfactor;
        if (m_classic(gamemode, mutators)) adsfactor *= .6f;
        applyspread(cs.o, to, spread, (cs.perk2 == PERK2_STEADY ? .75f : 1)*spreadf*adsfactor);
    }
    // trace shot
    straceShot(from, to, &surface);
    // calculate shot properties
    int damagepotential = 0, damagedealt = 0;
    if (weap == GUN_SHOTGUN)
        loopi(SGRAYS)
            damagepotential += effectiveDamage(weap, vec(cs.sg[i]).dist(from));
    else if (melee_weap(weap)) damagepotential = guns[weap].damage; // melee damage
    else if (weap == GUN_RPG) damagepotential = 50; // potential stick damage
    else if (weap == GUN_GRENADE) damagepotential = 0;
    else damagepotential = effectiveDamage(weap, to.dist(from));

    static vector<int> exclude;
    switch (weap)
    {
        case GUN_GRENADE: cs.grenades.add(id); break;
        case GUN_RPG: // formerly: crossbow explosive tip is stuck to a player
        {
            int hitzone = HIT_NONE;
            vec expc;
            exclude.setsize(0);
            exclude.add(c.clientnum);
            client *hit = nearesthit(c, from, to, !m_real(gamemode, mutators), hitzone, pos, exclude, expc);
            float dist = 0;
            if (hit)
            {
                int dmg = HEALTHSCALE;
                dist = expc.dist(from);
                if (dist <= 24) // close range puncture
                    dmg *= 300;
                else if (hitzone == HIT_HEAD)
                {
                    sendheadshot(from, to, dmg);
                    dmg *= m_progressive(gamemode, mutators) ? (250) : (150);
                }
                else
                    dmg *= m_progressive(gamemode, mutators) ? (hitzone * 75) : (55);
                damagedealt += dmg;
                sendhit(c, GUN_RPG, expc, dmg); // blood, not explosion
                serverdamage(*hit, c, dmg, GUN_RPG, FRAG_GIB | (hitzone == HIT_HEAD ? FRAG_FLAG : FRAG_NONE), expc, expc.dist(from));
            }
            // fix explosion on walls
            else
            {
                dist = to.dist(from) - .1f;
                (expc = to).sub(from).normalize().mul(dist).add(from);
            }
            // instant explosion (after 6m)
            int rpgexplodedmgdealt = dist >= 24 ? explosion(*ci, expc, GUN_RPG, !m_real(gamemode, mutators), false, hit) : 0;
            cs.damage += rpgexplodedmgdealt;
            cs.shotdamage += max<int>(effectiveDamage(GUN_RPG, 0), rpgexplodedmgdealt);
            break;
        }
        case GUN_HEAL: // healing a player
        {
            int hitzone = HIT_NONE;
            vec end;
            exclude.setsize(0);
            exclude.add(c.clientnum);
            client *hit = cs.scoping ? &c : nearesthit(c, from, to, false, hitzone, pos, exclude, end);
            if (!hit) break;
            if (hit->state.wounds.length())
            {
                // healing by a player
                addpt(*ci, HEALWOUNDPT * hit->state.wounds.length(), PR_HEALWOUND);
                if (&c != hit) addptreason(*hit, PR_HEALEDBYTEAMMATE);
                hit->state.wounds.shrink(0);
                // heal wounds = revive
                sendf(NULL, 1, "ri4", SV_HEAL, c.clientnum, hit->clientnum, hit->state.health);
            }
            if ((&c == hit) ? cs.health < MAXHEALTH : !isteam(&c, hit)) // that's right, no more self-heal abuse
            {
                const int flags = hitzone == HIT_HEAD ? FRAG_GIB : FRAG_NONE;
                const float dist = hit->state.o.dist(from);
                const int dmg = effectiveDamage(weap, dist) * (hitzone == HIT_HEAD ? muls[MUL_NORMAL].head : 1.f);
                if (hitzone == HIT_HEAD)
                    sendheadshot(from, to, dmg);
                serverdamage(*hit, c, dmg, weap, flags, cs.o, dist);
                damagedealt += dmg;
            }
            loopi(&c == hit ? 25 : 15)
                // heals over the next 1 to 2.5 seconds (no time perk, for others)
                hit->addtimer(new healevent(gamemillis + (10 + i) * 100 / (cs.perk1 == PERK_POWER ? 2 : 1), c.clientnum, cs.perk1 == PERK_POWER ? 2 : 1));
            if (hit == &c) (end = to).sub(from).normalize().add(from); // 25 cm fx
            // hide blood for healing weapon
            // sendhit(c, WEAP_HEAL, end, dmg); // blood
            to = end;
            break;
        }
        case GUN_KNIFE: // falls through if not "compact" (throw)
            if (compact)
            {
                if (cs.ammo[GUN_KNIFE])
                {
                    cs.knives.add(id);
                    cs.ammo[GUN_KNIFE]--;
                }
                break;
            }
        case GUN_SNIPER2:
            // gs.allowspeeding(gamemillis, 1500);
            // fallthrough
        default:
            if (weap == GUN_SHOTGUN) // many rays, many players
                damagedealt += shotgun(c, from, pos); // WARNING: modifies cs.sg
            else
            {
                exclude.setsize(0);
                exclude.add(c.clientnum);
                damagedealt += shot(c, from, to, pos, weap, FRAG_NONE, surface, exclude); // WARNING: modifies to
            }
            break;
    }
    cs.damage += damagedealt;
    cs.shotdamage += max(damagepotential, damagedealt);

    // disable spawn protection
    if (gamemillis < cs.lastspawn + SPAWNPROTECT)
        cs.lastspawn = gamemillis - SPAWNPROTECT;

    // create packet
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    // packet shotgun rays
    if(weap==GUN_SHOTGUN)
    {
        putint(p, SV_SG);
        loopi(SGRAYS)
        {
            putint(p, (int)(cs.sg[i].x*DMF));
            putint(p, (int)(cs.sg[i].y*DMF));
            putint(p, (int)(cs.sg[i].z*DMF));
        }
    }
    // packet shot message
    putint(p, compact ? SV_SHOOTC : SV_SHOOT);
    putint(p, c.clientnum);
    putint(p, weap);
    if (!compact)
    {
        putint(p, (int)(from.x*DMF));
        putint(p, (int)(from.y*DMF));
        putint(p, (int)(from.z*DMF));
        putint(p, (int)(to.x*DMF));
        putint(p, (int)(to.y*DMF));
        putint(p, (int)(to.z*DMF));
    }
    sendpacket(NULL, 1, p.finalize(), !compact && weap != GUN_GRENADE ? -1 : c.clientnum);
}

void reloadevent::process(client *ci)
{
    clientstate &cs = ci->state;
    if (!cs.isalive(gamemillis) || // dead
        weap<0 || weap >= NUMGUNS || // invalid weapon
        (weap == GUN_AKIMBO && cs.akimbomillis < gamemillis) || // akimbo after out
        cs.ammo[weap] < 1 /*reload*/) // no ammo
        return;
    const int mag = magsize(weap), reload = reloadsize(weap);

    if (!reload || // cannot reload
        cs.mag[weap] >= mag) // already full
        return;

    // perform the reload
    cs.mag[weap] = min(mag, cs.mag[weap] + reload);
#if !(SERVER_BUILTIN_MOD & 256)
    cs.ammo[weap] -= /*reload*/ 1;
#endif

    int wait = millis - cs.lastshot;
    sendf(NULL, 1, "ri5", SV_RELOAD, ci->clientnum, weap, cs.mag[weap], cs.ammo[weap]);
    if (!cs.gunwait[weap] || wait >= cs.gunwait[weap])
        cs.updateshot(millis);
    cs.gunwait[weap] += reloadtime(weap);
}

void akimboevent::process(client *ci)
{
    clientstate &cs = ci->state;
    if (!cs.isalive(gamemillis) || !cs.akimbo || cs.akimbomillis) return;
    // WHY DOES THIS EVENT TYPE EVEN EXIST?!
    cs.akimbomillis = gamemillis + 30000;
}

// unordered
void healevent::process(client *ci)
{
    const int heal = hp * HEALTHSCALE, remain = MAXHEALTH - ci->state.health;
    if (heal >= remain)
    {
        // fully healed!
        ci->invalidateheals();
        if (valid_client(id)){
            if (id == ci->clientnum) addpt(*clients[id], HEALSELFPT, PR_HEALSELF);
            else if (isteam(ci, clients[id])) addpt(*clients[id], HEALTEAMPT, PR_HEALTEAM);
            else addpt(*clients[id], HEALENEMYPT, PR_HEALENEMY);
        }
        ci->state.health = MAXHEALTH;
        if (!m_zombie(gamemode)) return sendf(NULL, 1, "ri4", SV_HEAL, id, ci->clientnum, ci->state.health);
    }
    // partial heal
    else ci->state.health += heal;
    sendf(NULL, 1, "ri3", SV_REGEN, ci->clientnum, ci->state.health);
}

void suicidebomberevent::process(client *ci)
{
    explosion(*ci, ci->state.o, GUN_GRENADE, !m_real(gamemode, mutators), true, valid_client(id) ? clients[id] : NULL);
}

void airstrikeevent::process(client *ci)
{
    explosion(*ci, o, GUN_GRENADE, !m_real(gamemode, mutators), false);
}

// processing events
bool timedevent::flush(client *ci, int fmillis)
{
    if (!valid) return true;
    else if (millis > fmillis) return false;
    else if (millis >= ci->lastevent)
    {
        ci->lastevent = millis;
        process(ci);
    }
    return true;
}

void processevents()
{
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type==ST_EMPTY || !c.isauthed || team_isspect(c.team)) continue;
        clientstate &cs = c.state;
        // (formerly) game ending nuke...
        if(cs.nukemillis && cs.nukemillis <= gamemillis && minremain)
        {
            // boom... gg
            //forceintermission = true;
            cs.nukemillis = 0;
            sendf(NULL, 1, "ri4", SV_STREAKUSE, i, STREAK_NUKE, 0);
            // apply the nuke effect
            nuke(c);
        }
        // drown, bleed, regen
        if(cs.state == CS_ALIVE)
        {
            // drown underwater
            if(cs.o.z + PLAYERHEIGHT * cs.crouchfactor(gamemillis) < smapstats.hdr.waterlevel)
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
                    serverdamage(c, c, (m_classic(gamemode, mutators) ? 5 : (cs.drownval + 10)) * HEALTHSCALE, OBIT_DROWN, FRAG_NONE, cs.o);
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
                        sendhit(owner, GUN_KNIFE, woundloc, bleeddmg);
                        // use wounded location as damage source
                        serverdamage(c, owner, bleeddmg, GUN_KNIFE, FRAG_NONE, woundloc, c.state.o.dist(owner.state.o));
                        w.lastdealt = gamemillis;
                    }
                }
            }
            // health regeneration
            else if(m_regen(gamemode, mutators) && cs.state == CS_ALIVE && cs.health < STARTHEALTH && cs.lastregen + (cs.perk1 == PERK_POWER ? REGENINT * .7f : REGENINT) < gamemillis)
            {
                int amt = round_(float((STARTHEALTH - cs.health) / 5 + 15));
                if(cs.perk1 == PERK_POWER) amt *= 1.4f;
                if(amt >= STARTHEALTH - cs.health)
                    amt = STARTHEALTH - cs.health;
                sendf(NULL, 1, "ri3", SV_REGEN, i, cs.health += amt);
                cs.lastregen = gamemillis;
            }
        }
        // not alive: spawn queue
        else if(cs.state == CS_WAITING || (c.type == ST_AI && valid_client(c.ownernum) && clients[c.ownernum]->isonrightmap && cs.state == CS_DEAD && cs.lastspawn<0))
        {
            const int waitremain = SPAWNDELAY - gamemillis + cs.lastdeath;
            if(canspawn(c) && waitremain <= 0)
                sendspawn(c);
        }
        // akimbo out!
        if(cs.akimbomillis && cs.akimbomillis < gamemillis) { cs.akimbomillis = 0; cs.akimbo = false; }
        // events
        while(c.events.length()) // are ordered
        {
            timedevent *ev = c.events[0];
            if (ev->flush(&c, gamemillis)) delete c.events.remove(0);
            else break;
        }
        // timers
        loopvj(c.timers)
        {
            if (!c.timers[j]->valid || (c.timers[j]->type == GE_HEAL && (cs.health >= MAXHEALTH || cs.state != CS_ALIVE)))
            {
                delete c.timers.remove(j--);
                continue;
            }
            else if (c.timers[j]->millis <= gamemillis)
            {
                c.timers[j]->process(&c);
                delete c.timers.remove(j--);
            }
        }
    }
}

