// server map info

#define getmaplayoutid(x, y) (clamp((int)(x), 2, (1 << maplayout_factor) - 2) + (clamp((int)(y), 2, (1 << maplayout_factor) - 2) << maplayout_factor))

// server map geometry tools
ssqr &getsblock(int id)
{
    if(!maplayout || id < 2 || id >= ((1 << (maplayout_factor * 2)) - 2))
    {
        static ssqr dummy;
        dummy.type = SPACE;
        dummy.ceil = 16;
        dummy.floor = 0;
        dummy.vdelta = 0;
        return dummy;
    }
    return maplayout[id];
}

inline char cornertype(int x, int y)
{
    if(!maplayout) return 0;
    ssqr &me = getsblock(getmaplayoutid(x, y));
    if(me.type != CORNER) return 0;
    ssqr &up = getsblock(getmaplayoutid(x, y-1));
    ssqr &left = getsblock(getmaplayoutid(x-1, y));
    ssqr &right = getsblock(getmaplayoutid(x+1, y));
    ssqr &down = getsblock(getmaplayoutid(x, y+1));
    const uchar mes = me.ceil - me.floor;
    const bool
        u = up.type == SOLID || uchar(up.ceil - up.floor) < mes,
        l = left.type == SOLID || uchar(left.ceil - left.floor) < mes,
        r = right.type == SOLID || uchar(right.ceil - right.floor) < mes,
        d = down.type == SOLID || uchar(down.ceil - down.floor) < mes;
    if((u && d) || (l && r)) return 0; // more than 2 cubes, or two adjecent ones
    if((u && !l) || (l && !u)) return 2; // topright
    return 1; // topleft
}

inline uchar maxvdelta(int id)
{
    if(!maplayout) return 0;
    ssqr *s = &getsblock(id);
    uchar vdelta = s++->vdelta;
    if(uchar(s->vdelta) > vdelta) vdelta = s->vdelta;
    s += (1 << maplayout_factor) - 1; // new row, left one
    if(uchar(s->vdelta) > vdelta) vdelta = s->vdelta;
    ++s;
    if(uchar(s->vdelta) > vdelta) vdelta = s->vdelta;
    return vdelta;
}

float getblockfloor(int id, bool check_vdelta = true)
{
    if (!maplayout) return 127;
    ssqr &s = getsblock(id);
    if (s.type == SOLID)
        return 127;
    else if (check_vdelta && s.type == FHF)
        return s.floor - maxvdelta(id) / 4.f;
    else
        return s.floor;
}

float getblockceil(int id)
{
    if (!maplayout) return -128;
    ssqr &s = getsblock(id);
    if (s.type == SOLID)
        return -128;
    else if (s.type == CHF)
        return s.ceil + maxvdelta(id) / 4.f;
    else
        return s.ceil;
}

bool outofborder(const vec &p)
{
    loopi(2)
        if(p[i] < 2 || p[i] > (1 << maplayout_factor) - 2)
            return true;
    return false;
}

bool checkpos(vec &p, bool alter = true)
{
    bool ret = false;
    vec fix = p;
    const float epsilon = .1f; // the real value is much smaller than that
    // xy
    loopi(2)
    {
        if(fix[i] <= 2)
        {
            fix[i] = 2 + epsilon;
            ret = true;
        }
        else if((1 << maplayout_factor) - 2 <= fix[i])
        {
            fix[i] = (1 << maplayout_factor) - 2 - epsilon;
            ret = true;
        }
    }
    if(!ret)
    {
        // z
        const int mapi = getmaplayoutid(fix.x, fix.y);
        const char ceil = getblockceil(mapi), floor = getblockfloor(mapi);
        if(fix.z >= ceil)
        {
            fix.z = ceil - epsilon;
            ret = true;
        }
        else if(floor >= fix.z)
        {
            fix.z = floor + epsilon;
            ret = true;
        }
    }
    if(alter)
        p = fix;
    return ret;
}

void snewmap(int factor)
{
    sents.shrink(0);
    maplayout_factor = clamp(factor, SMALLEST_FACTOR, LARGEST_FACTOR);
    smapstats.hdr.waterlevel = -100000;
    const int layoutsize = 1 << (maplayout_factor * 2);
    ssqr defaultblock;
    defaultblock.type = SPACE;
    defaultblock.floor = 0;
    defaultblock.ceil = 16;
    defaultblock.vdelta = 0;
    maplayout = new ssqr[layoutsize + 256];
    loopi(layoutsize)
        memcpy(maplayout + i, &defaultblock, sizeof(ssqr));
}

float sraycube(const vec &o, const vec &ray, vec *surface = nullptr)
{ // server counterpart of raycube
    if(surface) *surface = vec(0, 0, 0);
    if(ray.iszero()) return 0;

    vec v = o;
    float dist = 0;

    const int maxtraces = (1 << maplayout_factor << 2);
    for(int numtraces = 0; numtraces < maxtraces; numtraces++)
    {
        float dx = 0, dy = 0, dz = 0;
        int x = int(v.x), y = int(v.y);
        if(x < 0 || y < 0 || x >= (1 << maplayout_factor) || y >= (1 << maplayout_factor)) return dist;
        const int mapid = getmaplayoutid(x, y);
        ssqr s = getsblock(getmaplayoutid(x, y));
        float floor = getblockfloor(mapid), ceil = getblockceil(mapid);
        if(s.type == SOLID || v.z < floor || v.z > ceil)
        {
            if((!dx && !dy) || s.wtex==DEFAULT_SKY || (s.type != SOLID && v.z > ceil && s.ctex==DEFAULT_SKY)) return dist;
            if(surface)
            {
                int cornert = 0;
                if(s.type == CORNER && (cornert = cornertype(x, y)))
                {
                    float angle = atan2(v.y - o.y, v.x - o.x) / RAD;
                    while(angle < 0) angle += 360;
                    while(angle > 360) angle -= 360;
                    // maybe there is a faster way?

                    // topleft
                    if(cornert == 1)
                        surface->x = surface->y = (angle >= 135 && angle <= 315) ? -.7071f : .7071f;
                    // topright
                    else if(cornert == 2)
                    {
                        surface->x = (angle >= 45 && angle <= 225) ? -.7071f : .7071f;
                        surface->y = -surface->x;
                    }
                }
                else
                { // make one for heightfields?
                    if(dx<dy) surface->x = ray.x>0 ? -1 : 1;
                    else surface->y = ray.y>0 ? -1 : 1;
                    ssqr n = getsblock(getmaplayoutid(x+surface->x, y+surface->y));
                    if(n.type == SOLID || (v.z < floor && v.z < n.floor) || (v.z > ceil && v.z > n.ceil))
                    {
                        *surface = dx<dy ? vec(0, ray.y>0 ? -1 : 1, 0) : vec(ray.x>0 ? -1 : 1, 0, 0);
                        n = getsblock(getmaplayoutid(x+surface->x, y+surface->y));
                        if(n.type == SOLID || (v.z < floor && v.z < n.floor) || (v.z > ceil && v.z > n.ceil))
                            *surface = vec(0, 0, ray.z>0 ? -1 : 1);
                    }
                }
            }
            dist = max(dist-0.1f, 0.0f);
            break;
        }
        dx = ray.x ? (x + (ray.x > 0 ? 1 : 0) - v.x)/ray.x : 1e16f;
        dy = ray.y ? (y + (ray.y > 0 ? 1 : 0) - v.y)/ray.y : 1e16f;
        dz = ray.z ? ((ray.z > 0 ? ceil : floor) - v.z)/ray.z : 1e16f;
        if(dz < dx && dz < dy)
        {
            if(surface && (s.ctex!=DEFAULT_SKY || ray.z<0))
            {
                if(s.type != ((ray.z > 0) ? CHF : FHF)) // flat part
                    surface->z = ray.z>0 ? -1 : 1;
                else
                { // use top left surface
                    // of a plane, n = (b - a) x (c - a)
                    const float f = (ray.z > 0) ? .25f : -.25f;
                    *surface = vec(0, 0, s.vdelta * f); // as a
                    vec b(1, 0, getsblock(getmaplayoutid(x+1, y)).vdelta * f), c(0, 1, getsblock(getmaplayoutid(x, y+1)).vdelta * f);
                    /*
                    conoutf("a %.2f %.2f %.2f", surface->x, surface->y, surface->z);
                    conoutf("b %.2f %.2f %.2f", b.x, b.y, b.z);
                    conoutf("c %.2f %.2f %.2f", c.x, c.y, c.z);
                    /*/
                    b.sub(*surface);
                    c.sub(*surface);
                    dz *= surface->cross(b, c).normalize().z;
                    /*
                    conoutf("n %.2f %.2f %.2f", surface->x, surface->y, surface->z);
                    //*/
                }
            /*
            (getsblock(getmaplayoutid(x, y)).vdelta ==
            getsblock(getmaplayoutid(x+1, y)).vdelta &&
            getsblock(getmaplayoutid(x, y)).vdelta ==
            getsblock(getmaplayoutid(x, y+1)).vdelta &&
            getsblock(getmaplayoutid(x, y)).vdelta ==
            getsblock(getmaplayoutid(x+1, y+1)).vdelta)
            */
            }
            dist += dz;
            break;
        }
        float disttonext = 0.1f + min(dx, dy);
        v.add(vec(ray).mul(disttonext));
        dist += disttonext;
    }
    return dist;
}
