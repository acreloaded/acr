// protos.h
enum { WP_KNIFE = 0, WP_EXP, WP_KILL, WP_ESCORT, WP_DEFEND, WP_SECURE, WP_OVERTHROW, WP_GRAB, WP_ENEMY, WP_FRIENDLY, WP_STOLEN, WP_RETURN, WP_DEFUSE, WP_TARGET, WP_BOMB, WP_AIRSTRIKE, WP_NUKE, WP_NUM };
extern void renderwaypoint(int wp, const vec &o, float alpha = 1, bool thruwalls = true);
extern void renderprogress_back(const vec &o, const color &c = color(0, 0, 0));
extern void renderprogress(const vec &o, float progress, const color &c, float offset = 0);
extern void renderwaypoints();

// rendergl.cpp
inline float render_2d_as_3d_start(const vec &o, bool thruwalls = true)
{
    glPushMatrix();
    /*
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glStencilMask(0x000000);
    glDisable(GL_CULL_FACE);
    */
    if (thruwalls) glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glTranslatef(o.x, o.y, o.z);
    glRotatef(camera1->yaw - 180, 0, 0, 1);
    glRotatef(camera1->pitch, 1, 0, 0);
    glRotatef(camera1->roll, 0, -1, 0);
    extern float zoomfactor();
    return sqrtf(o.dist(camera1->o)*zoomfactor());
}

inline void render_2d_as_3d_end(bool thruwalls = true)
{
    /*
    glEnable(GL_CULL_FACE);
    glStencilMask(0xFFFFFF);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    */
    glDisable(GL_BLEND);
    glEnable(GL_FOG);
    if (thruwalls) glEnable(GL_DEPTH_TEST);
    glPopMatrix();
}

void renderwaypoint(int wp, const vec &o, float alpha, bool thruwalls)
{
    static Texture *tex = NULL;
    if (!tex)
    {
        tex = textureload("packages/misc/waypoints.png");
        if (!tex) return;
    }
    const float scale = render_2d_as_3d_start(o, thruwalls), s = waypointsize / (wp == WP_KNIFE || wp == WP_EXP ? 200.f : 100.f) * scale;
    glColor4f(1, 1, 1, alpha);
    quad(tex->id, vec(s / 2.0f, 0.0f, s), vec(s / -2.0f, 0.0f, 0.0f), (wp % 6) / 6.f, (wp / 6) / 3.f, 1.0f / 6, 1.0f / 3);
    render_2d_as_3d_end(thruwalls);
}

void renderprogress_back(const vec &o, const color &c)
{
    const float scale = render_2d_as_3d_start(o), s = waypointsize / 100.f * scale;
    glColor4f(c.r, c.g, c.b, c.alpha);
    quad(0, vec(.52f * scale, 0.0f, s + .27f * scale), vec(-.52f * scale, 0.0f, s + .08f * scale), 0.0f, 0.0f, 1.0f, 1.0f);
    render_2d_as_3d_end();
}

void renderprogress(const vec &o, float progress, const color &c, float offset)
{
    const float scale = render_2d_as_3d_start(o), s = waypointsize / 100.f * scale;
    glColor4f(c.r, c.g, c.b, c.alpha);
    quad(0, vec((.5f - 1.f * offset) * scale, 0.0f, s + .25f * scale), vec((.5f - 1.f * (offset + progress)) * scale, 0.0f, s + .1f * scale), 0.0f, 0.0f, 1.0f, 1.0f);
    render_2d_as_3d_end();
}

void renderwaypoints()
{
    if (!waypointsize) return;
    // throwing knife pickups
    loopv(knives)
    {
        vec s;
        bool ddt = focus->perk2 == PERK2_VISION; // disable depth test
        if (!ddt)
        {
            vec dir, s;
            (dir = focus->o).sub(knives[i].o).normalize();
            ddt = rayclip(knives[i].o, dir, s) + 1.f >= focus->o.dist(knives[i].o);
        }
        renderwaypoint(WP_KNIFE, knives[i].o, (float)(knives[i].millis - totalmillis) / KNIFETTL, ddt);
    }
    // vision perk
    if (focus->perk2 == PERK2_VISION)
        loopv(bounceents)
        {
            bounceent *b = bounceents[i];
            if (!b || (b->bouncetype != BT_NADE && b->bouncetype != BT_KNIFE)) continue;
            if (b->bouncetype == BT_NADE && ((grenadeent *)b)->nadestate != GST_INHAND) continue;
            if (b->bouncetype == BT_KNIFE && ((knifeent *)b)->knifestate != GST_INHAND) continue;
            renderwaypoint(b->bouncetype == BT_NADE ? WP_EXP : WP_KNIFE, b->o);
        }
    // flags
    const int teamfix = focus->team == TEAM_SPECT ? TEAM_CLA : focus->team;
    if (m_flags(gamemode))
    {
        if (m_secure(gamemode)) loopv(ents)
        {
            entity &e = ents[i];
            const int team = e.attr2 - 2;
            if (e.type == CTF_FLAG && team >= 0)
            {
                vec o(e.x, e.y, (float)S(int(e.x), int(e.y))->floor + PLAYERHEIGHT);
                renderwaypoint(team == TEAM_SPECT ? WP_SECURE : team == teamfix ? WP_DEFEND : WP_OVERTHROW, o, e.attr4 ? fabs(sinf(lastmillis / 200.f)) : 1.f, true);
                if (e.attr4)
                {
                    float progress = e.attr4 / 255.f;
                    renderprogress_back(o, color(0, 0, 0, .35f));
                    if (m_gsp1(gamemode, mutators))
                        renderprogress(o, progress, e.attr3 ? color(0, 0, 1, .28f) : color(1, 0, 0, .28f));
                    else
                    {
                        renderprogress(o, team == TEAM_SPECT ? .5f : progress / 2.f, color(1, 1, 1, .28f));
                        if (team == TEAM_SPECT)
                            renderprogress(o, progress / 2.f, e.attr3 ? color(0, 0, 1, .28f) : color(1, 0, 0, .28f), .5f);
                    }
                }
            }
        }
        else loopi(2)
        {
            float a = 1;
            int wp = -1;
            vec o;

            const flaginfo &f = flaginfos[i];
            const flaginfo &of = flaginfos[team_opposite(i)];
            const entity &e = *f.flagent;

            // flag
            switch (f.state)
            {
                case CTFF_STOLEN:
                {
                    if (f.actor == focus && !isthirdperson) break;
                    if (OUTBORD(f.actor->o.x, f.actor->o.y)) break;
                    const bool friendly = (focus == f.actor || (m_team(gamemode, mutators) && f.actor->team == teamfix));
                    if (friendly)
                    {
                        o = f.actor->o;
                        wp = (m_capture(gamemode) || m_bomber(gamemode)) ? WP_ESCORT : WP_DEFEND;
                    }
                    else
                    {
                        if (m_classic(gamemode, mutators)) break;
                        o = vec(f.actor->lastloudpos.v);
                        a = max(.15f, 1.f - (lastmillis - f.actor->radarmillis) / 5000.f);
                        wp = WP_KILL;
                    }
                    break;
                }
                case CTFF_DROPPED:
                    if (OUTBORD(f.pos.x, f.pos.y)) break;
                    o = f.pos;
                    o.z += PLAYERHEIGHT;
                    if (m_capture(gamemode)) wp = i == teamfix ? WP_RETURN : WP_ENEMY;
                    else if (m_keep(gamemode)) wp = WP_ENEMY;
                    else if (m_bomber(gamemode)) wp = i == teamfix ? WP_BOMB : WP_DEFUSE;
                    else wp = i == teamfix ? WP_FRIENDLY : WP_GRAB;
                    break;
            }
            o.z += PLAYERABOVEEYE;
            if (wp >= 0 && wp < WP_NUM) renderwaypoint(wp, o, a);

            if (OUTBORD(e.x, e.y)) continue;

            // flag base
            a = 1;
            wp = WP_STOLEN; // "wait"
            switch (f.state){
                default: // stolen or dropped
                    if (m_bomber(gamemode)) wp = of.state != CTFF_INBASE ? i == teamfix ? WP_DEFEND : WP_TARGET : -1;
                    else if (m_keep(gamemode) ? (f.actor != focus && !isteam(f.actor, focus)) : m_team(gamemode, mutators) ? (i != teamfix) : (f.actor != focus)) wp = -1; break;
                case CTFF_INBASE:
                    if (m_capture(gamemode))
                        wp = i == teamfix ? WP_FRIENDLY : WP_GRAB;
                    else if (m_bomber(gamemode))
                        wp = i == teamfix ? WP_BOMB : WP_TARGET;
                    else if (m_hunt(gamemode))
                        wp = i == teamfix ? WP_FRIENDLY : WP_ENEMY;
                    else if (m_overload(gamemode))
                        wp = i == teamfix ? WP_FRIENDLY : WP_TARGET;
                    else // if(m_keep(gamemode))
                        wp = WP_GRAB;
                    break;
                case CTFF_IDLE: // KTF only
                    // WAIT here if the opponent has the flag
                    if (of.state == CTFF_STOLEN && of.actor && focus != of.actor && !isteam(of.actor, focus))
                        break;
                    wp = WP_ENEMY;
                    break;
            }
            o.x = e.x;
            o.y = e.y;
            o.z = (float)S(int(e.x), int(e.y))->floor + PLAYERHEIGHT;
            if (wp >= 0 && wp < WP_NUM) renderwaypoint(wp, o, a);
            if (m_overload(gamemode))
            {
                renderprogress_back(o, color(0, 0, 0, .35f));
                renderprogress(o, e.attr3 / 255.f, color(1, 1, 1, .28f));
            }
        }
    }
    // players
    loopv(players)
    {
        playerent *pl = i == getclientnum() ? player1 : players[i];
        if (!pl || (pl == focus && !isthirdperson) || pl->state == CS_DEAD) continue;
        const bool has_flag = m_flags(gamemode) && ((flaginfos[0].state == CTFF_STOLEN && flaginfos[0].actor_cn == i) || (flaginfos[1].state == CTFF_STOLEN && flaginfos[1].actor_cn == i));
        if (has_flag) continue;
        const bool has_nuke = pl->nukemillis >= totalmillis;
        if (has_nuke || m_psychic(gamemode, mutators))
        {
            renderwaypoint((focus == pl || isteam(focus, pl)) ? WP_DEFEND : WP_KILL, pl->o);
            if (has_nuke) renderwaypoint(WP_NUKE, vec(pl->o.x, pl->o.y, pl->o.z + PLAYERHEIGHT));
        }
    }
}