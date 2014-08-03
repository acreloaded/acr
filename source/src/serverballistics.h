#include "ballistics.h"

float srayclip(const vec &o, const vec &ray, vec *surface = NULL)
{
    float dist = sraycube(o, ray, surface);
    vec to = ray;
    to.mul(dist).add(o);
    bool collided = false;
    vec end;
    loopv(sents)
    {
        if (sents[i].type != CLIP /*&& sents[i] != MAPMODEL*/) continue;
        entity &e = sents[i];
        // attr1, attr2, attr3, attr4
        // elevation, xrad, yrad, height
        if (intersectbox(vec(e.x, e.y, getblockfloor(getmaplayoutid(e.x, e.y)) + e.attr1 + e.attr4 / 2), vec(max(0.1f, (float)e.attr2), max(0.1f, (float)e.attr3), max(0.1f, e.attr4 / 2.f)), o, to, &end))
        {
            to = end;
            collided = true;
            if (surface)
            {
                *surface = vec(0, 0, 0);
                // which surface did it hit?
            }
        }
    }
    return collided ? to.dist(o) : dist;
}

// trace a shot
void straceShot(const vec &from, vec &to, vec *surface = NULL)
{
    vec tracer(to);
    tracer.sub(from).normalize();
    const float dist = srayclip(from, tracer, surface);
    to = tracer.mul(dist - .1f).add(from);
}

// normal shots (ray through sphere and cylinder check)
static inline int hitplayer(const vec &from, float yaw, float pitch, const vec &to, const vec &target, const vec &head, vec *end = NULL)
{
    float dist;
    // intersect head
    if (!head.iszero() && intersectsphere(from, to, head, HEADSIZE, dist))
    {
        if (end) (*end = to).sub(from).mul(dist).add(from);
        return HIT_HEAD;
    }
    float y = yaw*RAD, p = (pitch / 4 + 90)*RAD, c = cosf(p);
    vec bottom(target), top(sinf(y)*c, -cosf(y)*c, sinf(p));
    bottom.z -= PLAYERHEIGHT;
    top.mul(PLAYERHEIGHT/* + d->aboveeye*/).add(bottom); // space above shoulders removed
    // torso
    bottom.sub(top).mul(TORSOPART).add(top);
    if (intersectcylinder(from, to, bottom, top, PLAYERRADIUS, dist))
    {
        if (end) (*end = to).sub(from).mul(dist).add(from);
        return HIT_TORSO;
    }
    // restore to body
    bottom.sub(top).div(TORSOPART).add(top);
    // legs
    top.sub(bottom).mul(LEGPART).add(bottom);
    if (intersectcylinder(from, to, bottom, top, PLAYERRADIUS, dist)){
        if (end) (*end = to).sub(from).mul(dist).add(from);
        return HIT_LEG;
    }
    return HIT_NONE;
}

// apply spread
void applyspread(const vec &from, vec &to, int spread, float factor){
    if (spread <= 1) return;
#define RNDD (rnd(spread)-spread/2.f)*factor
    vec r(RNDD, RNDD, RNDD);
#undef RNDD
    to.add(r);
}

// check for critical
bool checkcrit(float dist, float m, int base = 0, int low = 4, int high = 100)
{
    return !m_real(gamemode, mutators) && !rnd((base + clamp(int(ceil(dist) * m), low, high)) * (m_classic(gamemode, mutators) ? 2 : 1));
}

// easy to send shot damage messages
inline void sendhit(client &actor, int gun, const vec &o, int dmg)
{
    // no blood or explosions if using moon jump
#if (SERVER_BUILTIN_MOD & 34) == 34 // 2 | 32
#if (SERVER_BUILTIN_MOD & 4) != 4
    if (m_gib(gamemode, mutators))
#endif
        return;
#endif
    sendf(-1, 1, "ri7", SV_EXPLODE, actor.clientnum, gun, dmg, (int)(o.x*DMF), (int)(o.y*DMF), (int)(o.z*DMF));
}

inline void sendheadshot(const vec &from, const vec &to, int damage)
{
    sendf(-1, 1, "ri8", SV_HEADSHOT, (int)(from.x*DMF), (int)(from.y*DMF), (int)(from.z*DMF), (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF), damage);
}

void parsepos(client &c, const vector<posinfo> &pos, vec &out_o, vec &out_head)
{
    const posinfo *info = NULL;
    loopv(pos) if (pos[i].cn == c.clientnum) { info = &pos[i]; break; }
    // position
    if (scl.lagtrust >= 2 && info) out_o = info->o;
    else out_o = c.state.o; // don't trust the client's position, or not provided
    // fix z
    out_o.z += PLAYERHEIGHT * c.state.crouchfactor(gamemillis);
    // head delta
    if (scl.lagtrust >= 1 && info && info->head.x > 0 && info->head.y > 0 && info->head.z > 0)
    {
        out_head = info->head;
        // sanity check (no insane headshot OPK)
        out_head.sub(out_o);
        if (out_head.magnitude() > 2) out_head.normalize().mul(2); // the center of our head cannot deviate from our neck more than 50 cm
        out_head.add(out_o);
    }
    // no match? not trusted? approximate a location for the head
    else out_head = vec(.2f, -.25f, .25f).rotate_around_z(c.y * RAD).add(out_o);
}

// explosions

// order the explosion hits by distance
struct explosivehit
{
    client *target, *owner;
    int damage, flags;
    float dist;
    vec o;

    static int compare(explosivehit *a, explosivehit *b)
    {
        // if there is more damage, the distance is closer, therefore move it up: (-a) - (-b) = b - a
        return b->damage - a->damage;
    }
};

// explosions call this to check
int radialeffect(client &owner, client &target, vector<explosivehit> &hits, const vec &o, int weap, bool gib, bool max_damage = false)
{
    vec hit_location = target.state.o;
    hit_location.z += (PLAYERABOVEEYE + PLAYERHEIGHT) / 2.f;
    // distance calculations
    float dist = max_damage ? 0 : min(hit_location.dist(o), target.state.o.dist(o));
    const bool useReciprocal = !m_classic(gamemode, mutators);
    if (dist >= (useReciprocal ? guns[weap].endrange : guns[weap].rangesub)) return 0; // too far away
    vec ray1(hit_location), ray2(target.state.o);
    ray1.sub(o).normalize();
    ray2.z += PLAYERHEIGHT * target.state.crouchfactor(gamemillis);
    ray2.sub(o).normalize();
    if (srayclip(o, ray1) < dist && srayclip(o, ray2) < dist) return 0; // not visible
    float dmg = effectiveDamage(weap, dist, true, useReciprocal);
    int expflags = gib ? FRAG_GIB : FRAG_NONE;
    // check for critical
    if (checkcrit(dist, 2.5f)) // 1 : clamp(10 * meter, 4, 100) chance
    {
        expflags |= FRAG_CRIT;
        dmg *= 1.4f;
    }
    // did the nade headshot?
    if (weap == GUN_GRENADE && owner.clientnum != target.clientnum && o.z > ray2.z)
    {
        expflags |= FRAG_FLAG;
        sendheadshot(o, (hit_location = target.state.o), dmg);
        dmg *= 1.2f;
    }
    // was the RPG direct?
    else if (weap == GUN_RPG && max_damage)
        expflags |= FRAG_FLAG;
    explosivehit &hit = hits.add();
    hit.damage = (int)dmg;
    hit.flags = expflags;
    hit.target = &target;
    hit.owner = &owner;
    hit.dist = dist;
    hit.o = hit_location;
    return hit.damage;
}

// explosion call
int explosion(client &owner, const vec &o2, int weap, bool teamcheck, bool gib, client *cflag)
{
    int damagedealt = 0;
    vec o(o2);
    checkpos(o);
    sendhit(owner, weap, o, 0); // 0 means display explosion
    // these are our hits
    vector<explosivehit> hits;
    // give credits to the shooter for killing the zombie!
    // find the hits
    loopv(clients)
    {
        client &target = *clients[i];
        if (target.type == ST_EMPTY || target.state.state != CS_ALIVE ||
            target.state.protect(gamemillis, gamemode, mutators) ||
            (&owner != &target && teamcheck && isteam(&owner, &target))) continue;
        damagedealt += radialeffect((weap == GUN_GRENADE && cflag && cflag != &target) ? *cflag : owner, target, hits, o, weap, gib, (weap == GUN_RPG && clients[i] == cflag));
    }
    // sort the hits
    hits.sort(explosivehit::compare);
    // apply the hits
    loopv(hits)
    {
        sendhit(owner, weap, hits[i].o, hits[i].damage);
        serverdamage(hits[i].target, hits[i].owner, hits[i].damage, weap, hits[i].flags, o, hits[i].dist);
    }
    return damagedealt;
}

// order the nuke hits by distance
struct nukehit
{
    client *target;
    float distance;

    static int compare(nukehit *a, nukehit *b)
    {
        if (a->distance < b->distance) return -1; // less distance, deal it faster
        if (a->distance > b->distance) return 1; // more distance, deal it slower
        return 0; // same?
    }
};

void nuke(client &owner, bool suicide, bool forced_all, bool friendly_fire)
{
    vector<nukehit> hits;
    loopvj(clients)
    {
        client *cl = clients[j];
        if (cl->type != ST_EMPTY && cl->team != TEAM_SPECT && cl != &owner && (friendly_fire || !isteam(cl, &owner)) && (forced_all || cl->state.state == CS_ALIVE))
        {
            // sort hits
            nukehit &hit = hits.add();
            hit.distance = cl->state.o.dist(owner.state.o);
            if (cl->type != ST_AI) hit.distance += 80; // 20 meters to prioritize non-bots
            hit.target = cl;
        }
    }
    hits.sort(nukehit::compare);
    loopv(hits)
    {
        serverdied(hits[i].target, &owner, 0, OBIT_NUKE, !rnd(3) ? FRAG_GIB : FRAG_NONE, owner.state.o, hits[i].distance);
        // fx
        sendhit(owner, GUN_GRENADE, hits[i].target->state.o, 0);
    }
    // save the best for last!
    if (suicide)
    {
        owner.suicide(OBIT_NUKE, FRAG_NONE);
        // fx
        sendhit(owner, GUN_GRENADE, owner.state.o, 0);
    }
}

// Hitscans

struct shothit
{
    client *target;
    int damage, flags;
    float dist;
};

// hit checks
client *nearesthit(client &actor, const vec &from, const vec &to, bool teamcheck, int &hitzone, const vector<posinfo> &pos, vector<int> &exclude, vec &end, bool melee = false)
{
    client *result = NULL;
    float dist = 4e9f; // a billion meters...
#define MELEE_PRECISION 11
    vec melees[MELEE_PRECISION];
    if (melee)
    {
        loopi(MELEE_PRECISION)
        {
            melees[i] = to;
            melees[i].sub(from);
            /*
            const float angle = ((i + 1.f) / MELEE_PRECISION - 0.5f) * 85.f * RAD; // from -85 to 85
            melees[i].rotate_around_x(angle * sinf(owner->aim[0]));
            melees[i].rotate_around_x(angle * cosf(owner->aim[0]));
            */
            melees[i].rotate_around_z(((i + 1.f) / MELEE_PRECISION - 0.5f) * 25.f * RAD); // from 25 to 25 (50 degrees)
            melees[i].add(from);
        }
    }
    loopv(clients)
    {
        client &t = *clients[i];
        clientstate &ts = t.state;
        // basic checks
        if (t.type == ST_EMPTY || ts.state != CS_ALIVE || exclude.find(i) >= 0 ||
            (teamcheck && &actor != &t && isteam(&actor, &t)) || ts.protect(gamemillis, gamemode, mutators)) continue;
        const float d = ts.o.dist(from);
        if (d > dist) continue;
        vec o, head;
        parsepos(t, pos, o, head);
        int hz = HIT_NONE;
        if (melee)
        {
            loopi(MELEE_PRECISION)
            {
                hz = hitplayer(from, actor.y, actor.p, melees[i], o, head, &end);
                if (hz) continue; // one of the knife rays hit
            }
            if (!hz) continue; // none of the knife rays hit
        }
        else
        {
            hz = hitplayer(from, actor.y, actor.p, to, o, head, &end);
            if (!hz) continue; // no hit
        }
        result = &t;
        dist = d;
        hitzone = hz;
    }
    return result;
}

// do a single line
int shot(client &owner, const vec &from, vec &to, const vector<posinfo> &pos, int weap, int style, const vec &surface, vector<int> &exclude, float dist = 0, float penaltydist = 0, vector<shothit> *save = NULL)
{
    const int mulset = sniper_weap(weap) ? MUL_SNIPER : MUL_NORMAL;
    int hitzone = HIT_NONE; vec end = to;
    // calculate the hit
    client *hit = nearesthit(owner, from, to, !m_real(gamemode, mutators), hitzone, pos, exclude, end, melee_weap(weap));
    // damage check
    const float dist2 = dist + end.dist(from);
    int damage = effectiveDamage(weap, dist2 + penaltydist);
    // out of range? (super knife code)
    if (melee_weap(weap))
    {
#if (SERVER_BUILTIN_MOD & 1) == 1
        if (m_gib(gamemode, mutators))
        {
            static const int lulz[3] = { WEAP_SNIPER, WEAP_HEAL, WEAP_RPG };
            sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, lulz[rnd(3)], from.x, from.y, from.z, to.x, to.y, to.z);
        }
        else
#endif
        if (dist2 > guns[weap].endrange) return 0;
    }
    #if (SERVER_BUILTIN_MOD & 16) == 16
    if (!dist)
    {
        //sendf(-1, 1, "ri3f6", N_RICOCHET, owner.clientnum, WEAP_RPG, owner.state.o.x, owner.state.o.y, owner.state.o.z, end.x, end.y, end.z);
        explosion(owner, end, WEAP_RPG, !m_real(gamemode, mutators), false);
    }
    #endif
    // we hit somebody
    if (hit && damage)
    {
        // damage multipliers
        if (!m_classic(gamemode, mutators) || hitzone >= HIT_HEAD)
        {
            if (hitzone == HIT_HEAD)
                damage *= m_progressive(gamemode, mutators) ? 7 : muls[mulset].head;
            else if (hitzone == HIT_TORSO)
                damage *= muls[mulset].torso;
        }
        // gib check
        if ((melee_weap(weap) || hitzone == HIT_HEAD) && !save) style |= FRAG_GIB;
        // critical shots
        if (checkcrit(dist2, 3.5f)) // 1 in clamp(14 * meter, 4, 100)
        {
            style |= FRAG_CRIT;
            damage *= 1.5f;
        }

        // melee weapons (bleed/check for self)
        if (melee_weap(weap))
        {
            if (hitzone == HIT_HEAD) style |= FRAG_FLAG;
            if (&owner == hit) return 0; // not possible
            else if (!isteam(&owner, hit)) // do not cause teammates to bleed
            {
                hit->state.addwound(owner.clientnum, end);
                sendf(-1, 1, "ri2", SV_BLEED, hit->clientnum);
            }
        }

        // send bloody headshot hits...
        if (hitzone == HIT_HEAD) sendheadshot(from, end, damage);
        // send the real hit (blood fx)
        sendhit(owner, weap, end, damage);
        // apply damage
        if (save)
        {
            // save damage for shotgun rays
            shothit &h = save->add();
            h.target = hit;
            h.damage = damage;
            h.flags = style;
            h.dist = dist2;
        }
        else serverdamage(hit, &owner, damage, weap, style, from, dist2);

        // add hit to the exclude list
        exclude.add(hit->clientnum);

        // penetration
        //if(!m_classic(gamemode, mutators) && dist2 < 100) // only penetrate players before 25 meters
        // {
            // distort ray and continue through...
            vec dir(to = end), newsurface;
            // 35 degrees (both ways = 70 degrees) distortion
            //dir.sub(from).normalize().rotate_around_z((rnd(71)-35)*RAD).add(end);
            // 5 degrees (both ways = 10 degrees) distortion on all axis
            dir.sub(from).normalize().rotate_around_x((rnd(45) - 22)*RAD).rotate_around_y((rnd(11) - 5)*RAD).rotate_around_z((rnd(11) - 5)*RAD).add(end);
            // retrace
            straceShot(end, dir, &newsurface);
            const int penetratedamage = shot(owner, end, dir, pos, weap, style, newsurface, exclude, dist2, penaltydist + 40, save); // 10 meters penalty for penetrating the player
            sendf(-1, 1, "ri9", SV_RICOCHET, owner.clientnum, weap, (int)(end.x), (int)(end.y), (int)(end.z), (int)(dir.x), (int)(dir.y), (int)(dir.z));
            return damage + penetratedamage;
        //}
    }
    // ricochet
    else if (!dist && from.dist(to) < 100 && surface.magnitude()) // ricochet once before 25 meters or going through a player
    {
        // reset exclusion to the owner, so a penetrated player can be hit twice
        if (exclude.length() > 1)
            exclude.setsize(1);
        vec dir(to), newsurface;
        // calculate reflected ray from incident ray and surface normal
        dir.sub(from).normalize();
        // r = i - 2 n (i . n)
        dir
            .sub(
                vec(surface)
                    .mul(2 * dir.dot(surface))
            );
        // 2 degrees (both ways = 4 degrees) distortion on all axis
        dir.rotate_around_x((rnd(5) - 2)*RAD).rotate_around_y((rnd(5) - 2)*RAD).rotate_around_z((rnd(5) - 2)*RAD).add(to);
        // retrace
        straceShot(to, dir, &newsurface);
        const int ricochetdamage = shot(owner, to, dir, pos, weap, style, newsurface, exclude, dist2, penaltydist + 60, save); // 15 meters penalty for ricochet
        sendf(-1, 1, "ri9", SV_RICOCHET, owner.clientnum, weap, (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF), (int)(dir.x*DMF), (int)(dir.y*DMF), (int)(dir.z*DMF));
        return damage + ricochetdamage;
    }
    return 0;
}

int shotgun(client &owner, const vec &from, vector<posinfo> &pos)
{
    int damagedealt = 0;
    clientstate &gs = owner.state;
    // many rays many hits, but we want each client to get all the damage at once...
    static vector<shothit> hits;
    hits.setsize(0);
    loopi(SGRAYS)
    {
        // check rays and sum damage
        vec surface;
        straceShot(from, gs.sg[i], &surface);
        static vector<int> exclude;
        exclude.setsize(0);
        exclude.add(owner.clientnum);
        shot(owner, from, gs.sg[i], pos, GUN_SHOTGUN, FRAG_NONE, surface, exclude, 0, 0, &hits);
    }
    loopv(clients)
    {
        // apply damage
        client &t = *clients[i];
        clientstate &ts = t.state;
        // basic checks
        if (t.type == ST_EMPTY || ts.state != CS_ALIVE) continue;
        int damage = 0, shotgunflags = 0;
        float bestdist = 0;
        loopvrev(hits)
            if (hits[i].target == &t)
            {
                damage += hits[i].damage;
                shotgunflags |= hits[i].flags; // merge crit, etc.
                if (hits[i].dist > bestdist) bestdist = hits[i].dist;
                hits.remove(i/*--*/);
            }
        if (!damage) continue;
        damagedealt += damage;
        shotgunflags |= damage >= SGGIB * HEALTHSCALE ? FRAG_GIB : FRAG_NONE;
        if (m_progressive(gamemode, mutators) && shotgunflags & FRAG_GIB)
        damage = max(damage, 350 * HEALTHSCALE);
        serverdamage(&t, &owner, damage, GUN_SHOTGUN, shotgunflags, from, bestdist);
    }
    return damagedealt;
}
