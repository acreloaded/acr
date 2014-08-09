// client processing of the incoming network stream

#include "cube.h"
#include "bot/bot.h"

VARP(networkdebug, 0, 0, 1);
#define DEBUGCOND (networkdebug==1)

extern bool watchingdemo;
extern string clientpassword;

packetqueue pktlogger;

void neterr(const char *s)
{
    conoutf("\f3illegal network message (%s)", s);

    // might indicate a client/server communication bug, create error report
    pktlogger.flushtolog("packetlog.txt");
    conoutf("\f3wrote a network error report to packetlog.txt, please post this file to the bugtracker now!");

    disconnect();
}

VARP(autogetmap, 0, 1, 1); // only if the client doesn't have that map
VARP(autogetnewmaprevisions, 0, 1, 1);

bool localwrongmap = false;
int MA = 0, Hhits = 0; // flowtron: moved here
bool changemapserv(char *name, int mode, int muts, int download, int revision)        // forced map change from the server
{
    MA = Hhits = 0; // reset for checkarea()
    modecheck(gamemode = mode, mutators = muts);
    if(m_demo(gamemode)) return true;
    if(m_edit(gamemode))
    {
        if(!name[0] || !load_world(name)) empty_world(0, true);
        return true;
    }
    else if(player1->state==CS_EDITING) { /*conoutf("SANITY drop from EDITING");*/ toggleedit(true); } // fix stuck-in-editmode bug
    bool loaded = load_world(name);
    if(download > 0)
    {
        bool revmatch = hdr.maprevision == revision || revision == 0;
        if(watchingdemo)
        {
            if(!revmatch) conoutf(_("%c3demo was recorded on map revision %d, you have map revision %d"), CC, revision, hdr.maprevision);
        }
        else
        {
            if(securemapcheck(name, false)) return true;
            bool sizematch = maploaded == download || download < 10;
            if(loaded && sizematch && revmatch) return true;
            bool getnewrev = autogetnewmaprevisions && revision > hdr.maprevision;
            if(autogetmap || getnewrev)
            {
                if(!loaded || getnewrev) getmap(); // no need to ask
                else
                {
                    defformatstring(msg)("map '%s' revision: local %d, provided by server %d", name, hdr.maprevision, revision);
                    alias("__getmaprevisions", msg);
                    showmenu("getmap");
                }
            }
            else
            {
                if(!loaded || download < 10) conoutf(_("\"getmap\" to download the current map from the server"));
                else conoutf(_("\"getmap\" to download a %s version of the current map from the server"),
                         revision == 0 ? _("different") : (revision > hdr.maprevision ? _("newer") : _("older")));
            }
        }
    }
    else return true;
    return false;
}

// update the position of other clients in the game in our world
// don't care if he's in the scenery or other players,
// just don't overlap with our client

void updatepos(playerent *d)
{
    const float r = player1->radius+d->radius;
    const float dx = player1->o.x-d->o.x;
    const float dy = player1->o.y-d->o.y;
    const float dz = player1->o.z-d->o.z;
    const float rz = player1->aboveeye+d->eyeheight;
    const float fx = (float)fabs(dx), fy = (float)fabs(dy), fz = (float)fabs(dz);
    if(fx<r && fy<r && fz<rz && d->state!=CS_DEAD)
    {
        if(fx<fy) d->o.y += dy<0 ? r-fy : -(r-fy);  // push aside
        else      d->o.x += dx<0 ? r-fx : -(r-fx);
    }
}

void updatelagtime(playerent *d)
{
    int lagtime = totalmillis-d->lastupdate;
    if(lagtime)
    {
        if(d->lastupdate) d->plag = (d->plag*5+lagtime)/6;
        d->lastupdate = totalmillis;
    }
}

extern void trydisconnect();

VARP(maxrollremote, 0, 0, 20); // bound remote "roll" values by our maxroll?!

void parsepositions(ucharbuf &p)
{
    int type;
    while(p.remaining()) switch(type = getint(p))
    {
        case SV_POS:                        // position of another client
        case SV_POSC:
        {
            int cn, f, g;
            vec o, vel;
            float yaw, pitch, roll = 0;
            bool scoping, sprinting;//, shoot;
            if(type == SV_POSC)
            {
                bitbuf<ucharbuf> q(p);
                cn = q.getbits(5);
                int usefactor = q.getbits(2) + 7;
                o.x = q.getbits(usefactor + 4) / DMF;
                o.y = q.getbits(usefactor + 4) / DMF;
                yaw = q.getbits(9) * 360.0f / 512;
                pitch = (q.getbits(8) - 128) * 90.0f / 127;
                roll = !q.getbits(1) ? (q.getbits(6) - 32) * 20.0f / 31 : 0.0f;
                if(!q.getbits(1))
                {
                    vel.x = (q.getbits(4) - 8) / DVELF;
                    vel.y = (q.getbits(4) - 8) / DVELF;
                    vel.z = (q.getbits(4) - 8) / DVELF;
                }
                else vel.x = vel.y = vel.z = 0.0f;
                f = q.getbits(8);
                int negz = q.getbits(1);
                int full = q.getbits(1);
                int s = q.rembits();
                if(s < 3) s += 8;
                if(full) s = 11;
                int z = q.getbits(s);
                if(negz) z = -z;
                o.z = z / DMF;
                scoping = ( q.getbits(1) ? true : false );
                q.getbits(1);//shoot = ( q.getbits(1) ? true : false );
                sprinting = q.getbits(1) ? true : false;
            }
            else
            {
                cn = getint(p);
                o.x   = getuint(p)/DMF;
                o.y   = getuint(p)/DMF;
                o.z   = getuint(p)/DMF;
                yaw   = (float)getuint(p);
                pitch = (float)getint(p);
                g = getuint(p);
                if ((g>>3) & 1) roll  = (float)(getint(p)*20.0f/125.0f);
                if (g & 1) vel.x = getint(p)/DVELF; else vel.x = 0;
                if ((g>>1) & 1) vel.y = getint(p)/DVELF; else vel.y = 0;
                if ((g>>2) & 1) vel.z = getint(p)/DVELF; else vel.z = 0;
                scoping = ( (g>>4) & 1 ? true : false );
                //shoot = ( (g>>5) & 1 ? true : false ); // we are not using this yet
                sprinting = ((g >> 6) & 1 ? true : false);
                f = getuint(p);
            }
            int seqcolor = (f>>6)&1;
            playerent *d = getclient(cn);
            if(!d || seqcolor!=(d->lifesequence&1)) continue;
            vec oldpos(d->o);
            float oldyaw = d->yaw, oldpitch = d->pitch;
            loopi(3)
            {
                float dr = o.v[i] - d->o.v[i] + ( i == 2 ? d->eyeheight : 0);
                if ( !dr ) d->vel.v[i] = 0.0f;
                else if ( d->vel.v[i] ) d->vel.v[i] = dr * 0.05f + d->vel.v[i] * 0.95f;
                d->vel.v[i] += vel.v[i];
                if ( i==2 && d->onfloor && d->vel.v[i] < 0.0f ) d->vel.v[i] = 0.0f;
            }
            d->o = o;
            d->o.z += d->eyeheight;
            d->yaw = yaw;
            d->pitch = pitch;
            d->scoping = scoping;
            d->roll = roll;
            d->strafe = (f&3)==3 ? -1 : f&3;
            f >>= 2;
            d->move = (f&3)==3 ? -1 : f&3;
            f >>= 2;
            d->onfloor = f&1;
            f >>= 1;
            d->onladder = f&1;
            f >>= 2;
            d->last_pos = totalmillis;
            updatecrouch(d, f&1);
            d->sprinting = sprinting;
            updatepos(d);
            updatelagtime(d);
            extern int smoothmove, smoothdist;
            if(d->state==CS_DEAD)
            {
                d->resetinterp();
                d->smoothmillis = 0;
            }
            else if(smoothmove && d->smoothmillis>=0 && oldpos.dist(d->o) < smoothdist)
            {
                d->newpos = d->o;
                d->newpos.z -= d->eyeheight;
                d->newyaw = d->yaw;
                d->newpitch = d->pitch;
                d->o = oldpos;
                d->yaw = oldyaw;
                d->pitch = oldpitch;
                oldpos.z -= d->eyeheight;
                (d->deltapos = oldpos).sub(d->newpos);
                d->deltayaw = oldyaw - d->newyaw;
                if(d->deltayaw > 180) d->deltayaw -= 360;
                else if(d->deltayaw < -180) d->deltayaw += 360;
                d->deltapitch = oldpitch - d->newpitch;
                d->smoothmillis = lastmillis;
            }
            else d->smoothmillis = 0;
            if (d->state == CS_WAITING) d->state = CS_ALIVE;
            // when playing a demo spectate first player we know about
            if(player1->isspectating() && player1->spectatemode==SM_NONE) togglespect();
            extern void clamproll(physent *pl);
            if(maxrollremote) clamproll((physent *) d);
            break;
        }

        default:
            neterr("type");
            return;
    }
}

extern int checkarea(int maplayout_factor, char *maplayout);
char *mlayout = NULL;
int Mv = 0, Ma = 0, F2F = 1000 * MINFF; // moved up:, MA = 0;
float Mh = 0;
extern int connected;
extern int lastpm;
extern bool noflags;
bool item_fail = false;
int map_quality = MAP_IS_EDITABLE;

/// TODO: many functions and variables are redundant between client and server... someone should redo the entire server code and unify client and server.
bool good_map() // call this function only at startmap
{
    if (mlayout) MA = checkarea(sfactor, mlayout);

    F2F = 1000 * MINFF;
    if(m_flags(gamemode))
    {
        flaginfo &f0 = flaginfos[0];
        flaginfo &f1 = flaginfos[1];
#define DIST(x) (f0.pos.x - f1.pos.x)
        F2F = (!numflagspawn[0] || !numflagspawn[1]) ? 1000 * MINFF : DIST(x)*DIST(x)+DIST(y)*DIST(y);
#undef DIST
    }

    item_fail = false;
    loopv(ents)
    {
        entity &e1 = ents[i];
        if (e1.type < I_CLIPS || e1.type > I_AKIMBO) continue;
        float density = 0, hdensity = 0;
        loopvj(ents)
        {
            entity &e2 = ents[j];
            if (e2.type < I_CLIPS || e2.type > I_AKIMBO || i == j) continue;
#define DIST(x) (e1.x - e2.x)
#define DIST_ATT ((e1.z + e1.attr1) - (e2.z + e2.attr1))
            float r2 = DIST(x)*DIST(x) + DIST(y)*DIST(y) + DIST_ATT*DIST_ATT;
#undef DIST_ATT
#undef DIST
            if ( r2 == 0.0f ) { conoutf("\f3MAP CHECK FAIL: Items too close %s %s (%hd,%hd)", entnames[e1.type], entnames[e2.type],e1.x,e1.y); item_fail = true; break; }
            r2 = 1/r2;
            if (r2 < 0.0025f) continue;
            if (e1.type != e2.type)
            {
                hdensity += r2;
                continue;
            }
            density += r2;
        }
        if ( hdensity > 0.5f ) { conoutf("\f3MAP CHECK FAIL: Items too close %s %.2f (%hd,%hd)", entnames[e1.type],hdensity,e1.x,e1.y); item_fail = true; break; }
        switch(e1.type)
        {
#define LOGTHISSWITCH(X) if( density > X ) { conoutf("\f3MAP CHECK FAIL: Items too close %s %.2f (%hd,%hd)", entnames[e1.type],density,e1.x,e1.y); item_fail = true; break; }
            case I_CLIPS:
            case I_HEALTH: LOGTHISSWITCH(0.24f); break;
            case I_AMMO: LOGTHISSWITCH(0.04f); break;
            case I_HELMET: LOGTHISSWITCH(0.02f); break;
            case I_ARMOUR:
            case I_GRENADE:
            case I_AKIMBO: LOGTHISSWITCH(0.005f); break;
            default: break;
#undef LOGTHISSWITCH
        }
    }

    map_quality = (!item_fail && F2F > MINFF && MA < MAXMAREA && Mh < MAXMHEIGHT && Hhits < MAXHHITS) ? MAP_IS_GOOD : MAP_IS_BAD;
    if ( (!connected || m_edit(gamemode)) && map_quality == MAP_IS_BAD ) map_quality = MAP_IS_EDITABLE;
    return map_quality > 0;
}

void onCallVote(int type, int vcn, const votedata &vote)
{
    if(identexists("onCallVote"))
    {
        defformatstring(runas)("onCallVote %d %d %d %d [%s]", type, vcn, vote.int1, vote.int2, vote.str1);
        execute(runas);
    }
}

void onChangeVote(int mod, int id, int cn)
{
    if(identexists("onChangeVote"))
    {
        defformatstring(runas)("onChangeVote %d %d %d", mod, id, cn);
        execute(runas);
    }
}

extern votedisplayinfo *curvote;

void parsemessages(int cn, playerent *d, ucharbuf &p, bool demo = false)
{
    static char text[MAXTRANS];
    int type, joining = 0;
    bool demoplayback = false;

    while(p.remaining())
    {
        type = getint(p);

        #ifdef _DEBUG
        if(type!=SV_POS && type!=SV_CLIENTPING && type!=SV_PINGPONG && type!=SV_CLIENT)
        {
            DEBUGVAR(d);
            ASSERT(type>=0 && type<SV_NUM);
            DEBUGVAR(messagenames[type]);
            protocoldebug(true);
        }
        else protocoldebug(false);
        #endif

        switch(type)
        {
            case SV_SERVINFO:  // welcome message from the server
            {
                int mycn = getint(p), prot = getint(p);
                if (prot != PROTOCOL_VERSION && !(watchingdemo && prot == -PROTOCOL_VERSION))
                {
                    conoutf(_("%c3incompatible game protocol (local protocol: %d :: server protocol: %d)"), CC, PROTOCOL_VERSION, prot);
                    conoutf("\f3if this occurs a lot, obtain an upgrade from \f1http://acr.victorz.ca");
                    if(watchingdemo) conoutf("breaking loop : \f3this demo is using a different protocol\f5 : end it now!"); // SVN-WiP-bug: causes endless retry loop else!
                    else disconnect();
                    return;
                }
                sessionid = getint(p);
                player1->clientnum = mycn;
                if(getint(p) > 0) conoutf(_("INFO: this server is password protected"));
                sendintro();
                break;
            }

            case SV_WELCOME:
                joining = getint(p);
                player1->resetspec();
                resetcamera();
                break;

            case SV_CLIENT:
            {
                int cn = getint(p);//, len = getuint(p);
                d = getclient(cn);
                /*
                ucharbuf q = p.subbuf(p.remaining());
                parsemessages(cn, getclient(cn), q, demo);
                */
                break;
            }

            case SV_SOUND:
            {
                playerent *d = getclient(getint(p));
                const int snd = getint(p);
                if(!d || d == player1 || isowned(d)) break;
                switch(snd)
                {
                    case S_NOAMMO:
                    case S_JUMP:
                    case S_SOFTLAND:
                    case S_HARDLAND:
                        audiomgr.playsound(snd, d);
                        break;
                }
                break;
            }

            case SV_TEXT:
            {
                int cn = getint(p), voice = getint(p), flags = getint(p);
                getstring(text, p);
                filtertext(text, text);
                playerent *d = getclient(cn);
                if(d) saytext(d, text, flags, voice);
                else if(cn == -1) chatonlyf("\f4MOTD:\n%s", text);
                else chatoutf("\f5[\f1CONSOLE\f5] \f2%s", text);
                break;
            }

            case SV_TEXTPRIVATE:
            {
                int cn = getint(p);
                getstring(text, p);
                filtertext(text, text);
                playerent *d = getclient(cn);
                if(!d) break;
                if(d->ignored) clientlogf("ignored: pm %s %s", colorname(d), text);
                else
                {
                    chatoutf("%s (PM):\f9 %s", colorname(d), highlight(text));
                    lastpm = d->clientnum;
                    if(identexists("onPM"))
                    {
                        defformatstring(onpm)("onPM %d [%s]", d->clientnum, text);
                        execute(onpm);
                    }
                }
                break;
            }

            case SV_MAPCHANGE:
            {
                // get map info
                getstring(text, p);
                int mode = getint(p), muts = getint(p);
                int downloadable = getint(p);
                int revision = getint(p);
                localwrongmap = !changemapserv(text, mode, muts, downloadable, revision);
                if(m_duke(gamemode, mutators) && joining>2) deathstate(player1);

                // get item spawns
                int n;
                resetspawns();
                while (!p.overread())
                {
                    n = getint(p);
                    if (n == -1) break;
                    setspawn(n);
                }

                // get knives
                n = getint(p); // reuse
                knives.setsize(0);
                loopi(n)
                {
                    cknife &k = knives.add();
                    k.id = getint(p);
                    k.millis = totalmillis + getint(p);
                    loopi(3) k.o[i] = getint(p)/DMF;
                }
                // get confirms
                n = getint(p); // more reuse
                confirms.setsize(0);
                loopi(n)
                {
                    cconfirm &c = confirms.add();
                    c.id = getint(p);
                    c.team = getint(p);
                    loopi(3) c.o[i] = getint(p)/DMF;
                }
                break;
            }

            case SV_KNIFEADD:
            {
                cknife &k = knives.add();
                k.id = getint(p);
                k.millis = totalmillis + KNIFETTL;
                loopi(3) k.o[i] = getint(p)/DMF;
                break;
            }

            case SV_KNIFEREMOVE:
            {
                int id = getint(p);
                loopv(knives) if (knives[i].id == id) knives.remove(i--);
                break;
            }

            case SV_CONFIRMADD:
            {
                cconfirm &k = confirms.add();
                k.id = getint(p);
                k.team = getint(p);
                loopi(3) k.o[i] = getint(p)/DMF;
                break;
            }

            case SV_CONFIRMREMOVE:
            {
                int id = getint(p);
                loopv(confirms) if (confirms[i].id == id) confirms.remove(i--);
                break;
            }

            case SV_MAPIDENT:
            {
                conoutf(_("%c3please %c1get the map %c2by typing %c0/getmap"), CC, CC, CC, CC);
                break;
            }

            case SV_SWITCHNAME:
                getstring(text, p);
                filtername(text, text);
                if(!text[0]) copystring(text, "unarmed");
                if(d)
                {
                    if(strcmp(d->name, text))
                        conoutf(_("%s is now known as %s"), colorname(d), text);
                    if(identexists("onNameChange"))
                    {
                        defformatstring(onnamechange)("onNameChange %d \"%s\"", d->clientnum, text);
                        execute(onnamechange);
                    }
                    copystring(d->name, text, MAXNAMELEN+1);
                    updateclientname(d);
                }
                break;

            case SV_SWITCHSKIN:
                loopi(2)
                {
                    int skin = getint(p);
                    if(d) d->setskin(i, skin);
                }
                break;

            case SV_THIRDPERSON:
            case SV_LEVEL:
            {
                playerent *d = getclient(getint(p));
                int info = getint(p);
                if (!d || d == player1) break;
                switch (type)
                {
                    case SV_THIRDPERSON:
                        d->thirdperson = info;
                        break;
                    case SV_LEVEL:
                        info = clamp(info, 1, MAXLEVEL);
                        d->level = info;
                        if (d->pBot) d->pBot->MakeSkill(info);
                        break;
                }
                break;
            }

            case SV_INITCLIENT:            // another client either connected or changed name/team
            {
                int cn = getint(p);
                playerent *d = newclient(cn);
                if(!d || d == player1)
                {
                    getstring(text, p);
                    loopi(4) getint(p);
                    break;
                }
                getstring(text, p);
                filtername(text, text);
                if(!text[0]) copystring(text, "unarmed");
                copystring(d->name, text, MAXNAMELEN+1);
                conoutf(_("connected: %s"), colorname(d));
                chatonlyf(_("%s %c0joined %c2the %c1game"), colorname(d), CC, CC, CC);
                if(identexists("onConnect"))
                {
                    defformatstring(onconnect)("onConnect %d", d->clientnum);
                    execute(onconnect);
                }
                loopi(2) d->setskin(i, getint(p));
                d->level = getint(p);
                d->team = getint(p);
                d->build = getint(p);
                d->thirdperson = getint(p);

                if(m_flags(gamemode)) loopi(2)
                {
                    flaginfo &f = flaginfos[i];
                    if(!f.actor) f.actor = getclient(f.actor_cn);
                }
                updateclientname(d);
                break;
            }

            case SV_INITAI:
            {
                const int cn = getint(p);
                playerent *d = newclient(cn);
                if(!d || d == player1) break; // should NOT happen!
                d->ownernum = getint(p);
                BotManager.GetBotName(getint(p), d);
                loopi(2) d->setskin(i, getint(p));
                d->team = getint(p);
                d->level = getint(p); // skill for bots

                if(m_flags(gamemode)) loopi(2)
                {
                    flaginfo &f = flaginfos[i];
                    if(!f.actor) f.actor = getclient(f.actor_cn);
                }
                updateclientname(d);
                break;
            }

            case SV_REASSIGNAI:
            {
                playerent *d = getclient(getint(p));
                const int newowner = getint(p);
                getstring(text, p);
                if(!d || d == player1) break;
                if(isowned(d) && newowner != getclientnum())
                    d->removeai();
                d->ownernum = newowner;
                //if(d->state == CS_WAITING) d->state = CS_ALIVE; // the server will now force death before reassigning
                d->plag = 0;
                break;
            }

            case SV_CDIS:
            {
                const int cn = getint(p);
                const int reason = getint(p);
                playerent *d = getclient(cn);
                if(!d || d == player1) break;
                if(d->name[0])
                {
                    extern const char *disc_reason(int reason);
                    conoutf(_("player %s disconnected (%s)"), colorname(d), disc_reason(reason));
                    chatonlyf(_("%s %c3left %c2the %c1game"), colorname(d), CC, CC, CC);
                }
                zapplayer(players[cn]);
                if(identexists("onDisconnect"))
                {
                    defformatstring(ondisconnect)("onDisconnect %d", d->clientnum);
                    execute(ondisconnect);
                }
                break;
            }
            case SV_DELAI:
            {
                int cn = getint(p);
                playerent *d = getclient(cn);
                if(!d || d == player1) break;
                zapplayer(players[cn]);
                break;
            }

            case SV_EDITMODE:
            {
                int val = getint(p);
                if(!d) break;
                d->state = val ? CS_EDITING : CS_ALIVE;
                break;
            }

            case SV_TRYSPAWN:
            {
                const int enqueued = getint(p);
                extern bool spawnenqueued;
                spawnenqueued = (enqueued > 0);
                if (enqueued) player1->respawnoffset = lastmillis - SPAWNDELAY + enqueued;
                break;
            }

            case SV_SPAWN:
            {
                playerent *d = getclient(getint(p));
                if(!d || d == player1 || isowned(d)) { static playerent dummy; d = &dummy; }
                d->respawn(gamemode, mutators);
                d->lifesequence = getint(p);
                d->health = getint(p);
                d->armour = getint(p);
                d->perk1 = getint(p);
                d->perk2 = getint(p);
                d->primary = getint(p);
                d->selectweapon(d->primary);
                d->secondary = getint(p);
                loopi(NUMGUNS) d->ammo[i] = getint(p);
                loopi(NUMGUNS) d->mag[i] = getint(p);
                loopi(3) d->o[i] = getint(p) / DMF;
                d->yaw = getint(p);
                d->state = CS_ALIVE;
                d->lastspawn = lastmillis;
                if (identexists("onSpawn"))
                {
                    defformatstring(onspawn)("onSpawn %d", d->clientnum);
                    execute(onspawn);
                }
                if(d->lifesequence==0) d->resetstats(); //NEW
                break;
            }

            case SV_SPAWNSTATE:
            {
                playerent *d = getclient(getint(p));
                if(!d || (d != player1 && !isowned(d))) { static playerent dummy; d = &dummy; }
                if ( map_quality == MAP_IS_BAD )
                {
                    loopi(7+2*NUMGUNS) getint(p);
                    conoutf(_("map deemed unplayable - fix it before you can spawn"));
                    break;
                }

                if(d == player1)
                {
                    if(editmode) toggleedit(true);
                    showscores(false);
                    setscope(false);
                    setburst(false);
                }
                d->respawn(gamemode, mutators);
                d->lifesequence = getint(p);
                d->health = getint(p);
                d->armour = getint(p);
                d->perk1 = getint(p);
                d->perk2 = getint(p);
                d->primary = getint(p);
                d->selectweapon(d->primary);
                d->secondary = getint(p);
                loopi(NUMGUNS) d->ammo[i] = getint(p);
                loopi(NUMGUNS) d->mag[i] = getint(p);
                d->state = CS_ALIVE;
                d->lastspawn = lastmillis;
                loopi(3) d->o[i] = getint(p) / DMF;
                d->yaw = getint(p);
                d->pitch = d->roll = 0;
                entinmap(d); // client may adjust spawn position a little
                if(d == player1 && m_duke(gamemode, mutators) && !localwrongmap)
                {
                    if (!m_zombie(gamemode) && !m_convert(gamemode, mutators)) arenaintermission = 0;
                    //closemenu(NULL);
                    conoutf(_("new round starting... fight!"));
                    hudeditf(HUDMSG_TIMER, "FIGHT!");
                }
                addmsg(SV_SPAWN, "ri5", d->clientnum, d->lifesequence, (int)(d->o.x*DMF), (int)(d->o.y*DMF), (int)(d->o.z*DMF));
                d->weaponswitch(d->weapons[d->primary]);
                d->weaponchanging -= SWITCHTIME(d->perk1 == PERK_TIME) / 2;
                if (identexists("onSpawn"))
                {
                    defformatstring(onspawn)("onSpawn %d", d->clientnum);
                    execute(onspawn);
                }
                if(d->lifesequence==0) d->resetstats(); //NEW
                break;
            }

            case SV_BLEED:
            {
                playerent *d = getclient(getint(p));
                if (d) d->addicon(eventicon::BLEED);
                break;
            }

            case SV_HEADSHOT:
            {
                // make bloody stain
                vec from, to;
                loopi(3) from[i] = getint(p) / DMF;
                loopi(3) to[i] = getint(p) / DMF;
                addheadshot(from, to, getint(p));
                break;
            }

            case SV_EXPLODE:
            {
                int cn = getint(p), weap = getint(p), dmg = getint(p);
                playerent *d = getclient(cn);
                vec o;
                loopi(3) o[i] = getint(p) / DMF;
                if (!d) break;
                // hit effect
                if (d->weapons[weap])
                {
                    if (explosive_weap(weap) && dmg);
                    else if (melee_weap(weap) && dmg < 20 * HEALTHSCALE);
                    else d->weapons[weap]->attackhit(o);
                }
                // blood
                if (dmg) damageeffect(dmg, o);
                break;
            }

            case SV_SG:
            {
                extern vec sg[SGRAYS];
                loopi(SGRAYS)
                {
                    sg[i].x = getint(p) / DMF;
                    sg[i].y = getint(p) / DMF;
                    sg[i].z = getint(p) / DMF;
                }
                break;
            }

            case SV_SHOOT:
            case SV_SHOOTC:
            case SV_RICOCHET:
            {
                int scn = getint(p), gun = getint(p);
                vec from, to;
                if (type == SV_SHOOTC)
                    from = to = vec(0, 0, 0);
                else
                {
                    loopk(3) from[k] = getint(p) / DMF;
                    loopk(3) to[k] = getint(p) / DMF;
                }
                playerent *s = getclient(scn);
                if (!s || !weapon::valid(gun) || !s->weapons[gun]) break;
                if (s == player1 && (type == SV_SHOOTC || gun == GUN_GRENADE)) break;
                // if it's somebody else's players, remove a bit of ammo
                if (type != SV_RICOCHET && s != player1 && !isowned(s))
                {
                    s->lastaction = lastmillis;
                    s->weaponchanging = 0;
                    s->mag[gun]--;

                    s->lastattackweapon = s->weapons[gun];
                    s->weapons[gun]->gunwait = s->weapons[gun]->info.attackdelay;
                    s->weapons[gun]->reloading = 0;
                }
                // all have to do is attackfx
                s->weapons[gun]->attackfx(from, to, type == SV_RICOCHET ? -2 : -1);
                s->pstatshots[gun]++; //NEW
                break;
            }

            case SV_THROWNADE:
            {
                playerent *d = getclient(getint(p));
                vec from, to;
                loopk(3) from[k] = getint(p)/DMF;
                loopk(3) to[k] = getint(p)/DNF;
                int nademillis = getint(p);
                if(!d) break;
                d->lastaction = lastmillis;
                d->weaponchanging = 0;
                d->lastattackweapon = d->weapons[GUN_GRENADE];
                if(d->weapons[GUN_GRENADE])
                {
                    d->weapons[GUN_GRENADE]->attackfx(from, to, nademillis);
                    d->weapons[GUN_GRENADE]->reloading = 0;
                }
                if(d!=player1) d->pstatshots[GUN_GRENADE]++; //NEW
                break;
            }

            case SV_RELOAD:
            {
                int cn = getint(p), gun = getint(p), mag = getint(p), ammo = getint(p);
                playerent *p = getclient(cn);
                if (!p || gun < 0 || gun >= NUMGUNS) break;
                if (p != player1 && !isowned(p) && p->weapons[gun])
                    p->weapons[gun]->reload(false);
                p->ammo[gun] = ammo;
                p->mag[gun] = mag;
                //if (gun == GUN_KNIFE) p->addicon(eventicon::PICKUP);
                break;
            }

            // for AUTH: WIP

            case SV_AUTHREQ:
            {
                extern int autoauth;
                getstring(text, p);
                if(autoauth && text[0] && tryauth(text)) conoutf("server requested authkey \"%s\"", text);
                break;
            }

            case SV_AUTHCHAL:
            {
                getstring(text, p);
                authkey *a = findauthkey(text);
                uint id = (uint)getint(p);
                getstring(text, p);
                if(a && a->lastauth && lastmillis - a->lastauth < 60*1000)
                {
                    vector<char> buf;
                    answerchallenge(a->key, text, buf);
                    //conoutf("answering %u, challenge %s with %s", id, text, buf.getbuf());
                    addmsg(SV_AUTHANS, "rsis", a->desc, id, buf.getbuf());
                }
                break;
            }

            // :for AUTH

            case SV_POINTS:
            {
                const int reason = getint(p), points = getint(p);
                addexp(points);
                if (reason < 0 || reason >= PR_MAX) break;
                const char *pointreason_names[PR_MAX] =
                {
                    "",
                    _("Assist"),
                    _("SPLAT!"),
                    _("HEADSHOT!"),
                    _("Kill Confirmed"),
                    _("Kill Denied"),
                    _("Healed Self"),
                    _("Healed Teammate"),
                    _("%c3Healed Enemy"),
                    _("Prevented Bleedout!"),
                    _("Teammate saved you!"), // Bleedout prevented by teammate
                    _("%c0You won!"),
                    _("%c1Your team wins!"),
                    _("%c3You lost!"),
                    _("%c1Domination bonus"),
                    _("%c0Flag secured!"),
                    _("%c3Flag overthrown!"),
                    _("%c0Buzzkill!"),
                    _("%c3Buzzkilled!"),
                    _("%c1Got own tags!"),
                    _("%c3Kill Denied"),
                };
                formatstring(text)(pointreason_names[reason], CC);
                expreason(text);
                break;
            }

            case SV_REGEN:
            case SV_HEAL:
            {
                playerent *healer = type == SV_HEAL ? getclient(getint(p)) : NULL;
                const int cn = getint(p), health = getint(p);
                playerent *d = getclient(cn);
                if (!d) break;
                d->health = health;
                d->lastregen = lastmillis;
                if (!healer) break;
                addobit(healer, OBIT_REVIVE, FRAG_NONE, false, d);
                if (d == player1) hudoutf("\fs\f1REVIVED \f2by \fr%s", colorname(healer));
                break;
            }

            case SV_KILL:
            {
                int vcn = getint(p),
                    acn = getint(p),
                    gun = getint(p),
                    style = getint(p),
                    damage = getint(p),
                    combo = getint(p),
                    assist = getint(p);
                float killdist = getint(p)/DMF;
                vec src; loopi(3) src[i] = getint(p)/DMF;
                playerent *victim = getclient(vcn), *actor = getclient(acn);
                //if (actor) actor->pointstreak = pointstreak;

                if (!victim) break;
                victim->health -= damage;
                if (!actor) break;
                dodamage(damage, victim, actor, gun, style, src);
                //victim->deathcamsrc = src;
                dokill(victim, actor, gun, style, damage, combo, assist, killdist);
                break;
            }

            case SV_DAMAGE:
            {
                int tcn = getint(p),
                    acn = getint(p),
                    damage = getint(p),
                    armour = getint(p),
                    health = getint(p),
                    gun = getint(p),
                    style = getint(p);
                vec src; loopi(3) src[i] = getint(p)/DMF;
                playerent *target = getclient(tcn), *actor = getclient(acn);
                if (!target || !actor) break;
                target->armour = armour;
                target->health = health;
                dodamage(damage, target, actor, gun, style, src);
                actor->pstatdamage[gun] += damage; //NEW
                break;
            }

            case SV_RESUME:
            {
                loopi(MAXCLIENTS)
                {
                    int cn = getint(p);
                    if(p.overread() || cn<0) break;
                    int state = getint(p), lifesequence = getint(p), primary = getint(p), secondary = getint(p), perk1 = getint(p), perk2 = getint(p), gunselect = getint(p), flagscore = getint(p), frags = getint(p), deaths = getint(p), health = getint(p), armour = getint(p), points = getint(p);
                    int ammo[NUMGUNS], mag[NUMGUNS];
                    loopi(NUMGUNS) ammo[i] = getint(p);
                    loopi(NUMGUNS) mag[i] = getint(p);
                    playerent *d = newclient(cn);
                    if(!d) continue;
                    if(d!=player1) d->state = state;
                    d->lifesequence = lifesequence;
                    d->flagscore = flagscore;
                    d->frags = frags;
                    d->deaths = deaths;
                    d->points = points;
                    if(d!=player1)
                    {
                        d->primary = primary;
                        d->secondary = secondary;
                        d->selectweapon(gunselect);
                        d->health = health;
                        d->armour = armour;
                        d->perk1 = perk1;
                        d->perk2 = perk2;
                        memcpy(d->ammo, ammo, sizeof(ammo));
                        memcpy(d->mag, mag, sizeof(mag));
                        if(d->lifesequence==0) d->resetstats(); //NEW
                    }
                }
                break;
            }

            case SV_DISCSCORES:
            {
                discscores.shrink(0);
                int team;
                while((team = getint(p)) >= 0)
                {
                    discscore &ds = discscores.add();
                    ds.team = team;
                    getstring(text, p);
                    filtername(ds.name, text);
                    ds.flags = getint(p);
                    ds.frags = getint(p);
                    ds.deaths = getint(p);
                    ds.points = getint(p);
                }
                break;
            }
            case SV_ITEMSPAWN:
                setspawn(getint(p));
                break;

            case SV_ITEMACC:
            {
                int i = getint(p), cn = getint(p), spawntime = getint(p);
                playerent *d = getclient(cn);
                pickupeffects(i, d, spawntime);
                break;
            }

            case SV_EDITH:              // coop editing messages, should be extended to include all possible editing ops
            case SV_EDITT:
            case SV_EDITS:
            case SV_EDITD:
            case SV_EDITE:
            {
                int x  = getint(p);
                int y  = getint(p);
                int xs = getint(p);
                int ys = getint(p);
                int v  = getint(p);
                block b = { x, y, xs, ys };
                switch(type)
                {
                    case SV_EDITH: editheightxy(v!=0, getint(p), b); break;
                    case SV_EDITT: edittexxy(v, getint(p), b); break;
                    case SV_EDITS: edittypexy(v, b); break;
                    case SV_EDITD: setvdeltaxy(v, b); break;
                    case SV_EDITE: editequalisexy(v!=0, b); break;
                }
                break;
            }

            case SV_EDITW:
            {
                const int newwaterlevel = getint(p);
                loopi(4) hdr.watercolor[i] = getint(p);
                if (newwaterlevel == hdr.waterlevel) break;
                hdr.waterlevel = newwaterlevel;
                conoutf(_("%s changed the water-level to %d"), colorname(d), hdr.waterlevel);
                break;
            }

            case SV_NEWMAP:
            {
                int size = getint(p);
                if(size>=0) empty_world(size, true);
                else empty_world(-1, true);
                if(d && d!=player1)
                    conoutf(size>=0 ? _("%s started a new map of size %d") : _("%s enlarged the map to size %d"), colorname(d), sfactor);
                break;
            }

            case SV_EDITENT:            // coop edit of ent
            {
                uint i = getint(p);
                while((uint)ents.length()<=i) ents.add().type = NOTUSED;
                int to = ents[i].type;
                if(ents[i].type==SOUND)
                {
                    entity &e = ents[i];

                    entityreference entref(&e);
                    location *loc = audiomgr.locations.find(e.attr1, &entref, mapsounds);

                    if(loc)
                        loc->drop();
                }

                ents[i].type = getint(p);
                ents[i].x = getint(p);
                ents[i].y = getint(p);
                ents[i].z = getint(p);
                ents[i].attr1 = getint(p);
                ents[i].attr2 = getint(p);
                ents[i].attr3 = getint(p);
                ents[i].attr4 = getint(p);
                ents[i].spawned = false;
                if(ents[i].type==LIGHT || to==LIGHT) calclight();
                if(ents[i].type==SOUND) audiomgr.preloadmapsound(ents[i]);
                break;
            }

            case SV_PINGPONG:
            {
                int millis = getint(p);
                addmsg(SV_CLIENTPING, "i", totalmillis - millis);
                break;
            }

            case SV_CLIENTPING:
            {
                int cn = getint(p), ping = getint(p);
                if(cn == getclientnum())
                    player1->ping = ping;
                loopv(players)
                    if(players[i] && (i == cn || players[i]->ownernum == cn))
                        players[i]->ping = ping;
                break;
            }

            case SV_TIMEUP:
            {
                int curgamemillis = getint(p);
                int curgamelimit = getint(p);
                timeupdate(curgamemillis, curgamelimit);
                break;
            }

            case SV_WEAPCHANGE:
            {
                int cn = getint(p), gun = getint(p);
                playerent *d = getclient(cn);
                if (!d || gun < 0 || gun >= NUMGUNS) break;
                d->zoomed = 0;
                d->weaponswitch(d->weapons[gun]);
                //if(!d->weaponchanging) d->selectweapon(gun);
                break;
            }

            case SV_QUICKSWITCH:
            {
                int cn = getint(p);
                playerent *d = getclient(cn);
                if (!d) break;
                d->weaponchanging = lastmillis - 1 - (SWITCHTIME(d->perk1 == PERK_TIME) / 2);
                d->nextweaponsel = d->weaponsel = d->weapons[d->primary];
                break;
            }

            case SV_SERVMSG:
                getstring(text, p);
                conoutf("%s", text);
                break;

            case SV_FLAGINFO:
            {
                int flag = getint(p);
                if(flag<0 || flag>1) return;
                flaginfo &f = flaginfos[flag];
                f.state = getint(p);
                switch(f.state)
                {
                    case CTFF_STOLEN:
                        flagstolen(flag, getint(p));
                        break;
                    case CTFF_DROPPED:
                    {
                        float x = getuint(p)/DMF;
                        float y = getuint(p)/DMF;
                        float z = getuint(p)/DMF;
                        flagdropped(flag, x, y, z);
                        break;
                    }
                    case CTFF_INBASE:
                        flaginbase(flag);
                        break;
                    case CTFF_IDLE:
                        flagidle(flag);
                        break;
                }
                break;
            }

            case SV_FLAGMSG:
            {
                int flag = getint(p);
                int message = getint(p);
                int actor = getint(p);
                int flagtime = message == FA_KTFSCORE ? getint(p) : -1;
                flagmsg(flag, message, actor, flagtime);
                break;
            }

            case SV_FLAGSECURE:
            {
                const int ent = getint(p), team = getint(p), enemy = getint(p), overthrown = getint(p);
                if (!ents.inrange(ent) || ents[ent].type != CTF_FLAG || (!team_isactive(team) && team != TEAM_SPECT) || ents[ent].attr2 < 2) break;
                ents[ent].attr2 = 2 + team;
                ents[ent].attr3 = enemy;
                ents[ent].attr4 = overthrown;
                break;
            }

            case SV_ARENAWIN:
            {
                int acn = getint(p);
                playerent *alive = getclient(acn);
                // check for multiple survivors
                bool multi = false;
                if (m_team(gamemode, mutators) && alive)
                {
#define teammate(p) (p != alive && p->state == CS_ALIVE && isteam(p, alive))
                    if (teammate(player1)) multi = true;
                    else loopv(players) if (players[i] && teammate(players[i])){ multi = true; break; }
#undef teammate
                }
                conoutf(_("the round is over! next round in 5 seconds..."));

                // no survivors
                if (acn == -1) hudoutf(_("%c3everyone died; epic fail!"), CC);
                // instead of waiting for bots to battle it out...
                else if (acn == -2) hudoutf(_("the bots have won the round!"));
                // should not happen? better safe than sorry
                else if (!alive) hudoutf("unknown winner...?");
                // Teams
                else if (m_team(gamemode, mutators) && multi)
                {
                    if (alive->team == player1->team)
                        hudoutf(_("your team is the victor!"));
                    else
                        hudoutf(_("your team was dominated!"));
                }
                // FFA or one team member
                else if (alive == player1) hudoutf(_("you are the victor!"));
                else hudoutf(_("%s is the victor!"), colorname(alive));

                // set intermission time
                arenaintermission = lastmillis;
                break;
            }

            case SV_ZOMBIESWIN:
            {
                const int info = getint(p), round = (info >> 1) & 0x7F;
                if (round > MAXZOMBIEROUND) hudoutf(_("%c0the humans have prevailed!"), CC);
                else if (info & 1) hudoutf(_("%c2Get ready for wave %c1%d%c4; %c0the humans held off the zombies!"), CC, CC, round, CC, CC);
                else hudoutf(_("%c2Get ready for wave %c1%d%c4; %c3the zombies have overrun the humans!"), CC, CC, round, CC, CC);
                arenaintermission = lastmillis;
                break;
            }

            case SV_CONVERTWIN:
            {
                hudoutf(_("%c1%cbeveryone has been converted!"), CC, CC);
                arenaintermission = lastmillis;
                break;
            }

            case SV_FORCEDEATH:
            case SV_FORCEGIB:
            {
                int cn = getint(p);
                playerent *d = newclient(cn);
                if(!d) break;
                if (type == SV_FORCEGIB) addgib(d);
                deathstate(d);
                break;
            }

            case SV_CLAIMPRIV:
            {
                int cl = getint(p), r = getint(p), t = getint(p);
                playerent *d = getclient(cl);
                const char *n = (d == player1) ? "\f1you" : d ? colorname(d) : "\f2[a connecting admin]";
                switch (t){
                    case 0:
                    case 1:
                        chatoutf(_("%s %s %s access"), n, t ? _("relinquished") : _("claimed"), privname(r));
                        break;
                    case 2:
                        if (d == player1) hudoutf(_("you already have %s access"), privname(r));
                        else hudoutf(_("there is already another %s (%s)"), privname(r), n);
                        break;
                    case 3:
                        hudoutf(_("%c2this password is not privileged; it is a deban password!"), CC);
                        break;
                }
                break;
            }

            case SV_SETPRIV:
            {
                int c = getint(p), priv = getint(p);
                playerent *pl = newclient(c);
                if(!pl) break;
                pl->clientrole = priv;
                break;
            }

            case SV_TEAMDENY:
            {
                int t = getint(p);
                if (t == 0x10) conoutf(_("%c3you were forced into this team by a vote and may not switch"), CC);
                else if (t == 0x11) conoutf(_("%c3you may not switch teams in this mode!"), CC);
                else if (t == 0x12) conoutf(_("%c3match team size is set -- cannot switch sides"), CC);
                else conoutf(_("%cteam %s is full!"), CC, team_string(t & 0xF));
                break;
            }

            case SV_SETTEAM:
            {
                int cn = getint(p), fnt = getint(p), ftr = fnt >> 4; fnt &= 0xf;
                playerent *d = newclient(cn);
                if (!d) break;
                if (d->team == fnt)
                {
                    // no change
                    switch (ftr){
                        case FTR_PLAYERWISH:
                            if (d == player1) hudoutf(_("%c1you %c2did not switch teams"), CC, CC);
                            else conoutf(_("%c2%s did not switch teams"), CC, colorname(d));
                            break;
                        case FTR_AUTO:
                            if (d == player1) hudoutf(_("%c1you %c2stay in team %s"), CC, CC, team_string(fnt));
                            else if (d->ownernum < 0) conoutf(_("%c2%s stays on team %s"), CC, colorname(d), team_string(fnt));
                            break;
                    }
                }
                else
                {
                    switch (ftr)
                    {
                        case FTR_PLAYERWISH:
                            if (d == player1) hudoutf(_("%c1you %c2are now in team %s"), CC, CC, team_string(fnt));
                            else conoutf(_("%c2%s switched to team %s"), CC, colorname(d), team_string(fnt));
                            break;
                        case FTR_AUTO:
                            if (d == player1) hudoutf(_("%c2the server %c1forced you %c2to team %s"), CC, CC, CC, team_string(fnt));
                            else if(d->ownernum < 0) conoutf(_("%c2the server forced %s to team %s"), CC, colorname(d), team_string(fnt));
                            break;
                    }
                    d->team = fnt;
                    // client version of removeexplosives()
                    removebounceents(d);
                }
                break;
            }

            case SV_SERVERMODE:
            {
                int sm = getint(p);
                servstate.autoteam = sm & 1;
                servstate.mastermode = (sm >> 2) & MM_MASK;
                servstate.matchteamsize = sm >> 4;
                break;
            }

            case SV_CALLVOTE:
            {
                int cn = getint(p), type = getint(p), voteremain = getint(p);
                playerent *d = getclient(cn);
                if( type < 0 || type >= SA_NUM ) break;
                votedisplayinfo *v = NULL;
                // vote data storage
                static votedata vote = votedata(text);
                vote = votedata(text); // reset it
                // vote parsing
                switch(type)
                {
                    case SA_BAN:
                    case SA_MAP:
                        vote.int2 = getint(p);
                        // fallthrough
                    case SA_KICK:
                        vote.int1 = getint(p);
                        // fallthrough
                    case SA_SERVERDESC:
                        getstring(text, p);
                        break;
                    case SA_FORCETEAM:
                    case SA_GIVEADMIN:
                        vote.int2 = getint(p);
                        // fallthrough
                    case SA_MASTERMODE:
                    case SA_AUTOTEAM:
                    case SA_RECORDDEMO:
                    case SA_CLEARDEMOS:
                    case SA_BOTBALANCE:
                    case SA_SUBDUE:
                    case SA_REVOKE:
                        vote.int1 = getint(p);
                        // fallthrough
                    case SA_STOPDEMO:
                        // compatibility
                    default:
                    case SA_REMBANS:
                    case SA_SHUFFLETEAMS:
                        break;
                }
                v = newvotedisplayinfo(d, type, vote);
                if(!v) break;
                v->expiremillis = totalmillis + voteremain;
                displayvote(v);
                onCallVote(type, v->owner->clientnum, vote);
                break;
            }

            case SV_CALLVOTEERR:
            {
                int errn = getint(p);
                callvoteerr(errn);
                onChangeVote( 1, errn, -1 );
                break;
            }

            case SV_VOTE:
            {
                const int cn = getint(p), vote = getint(p);
                if (!curvote) break;
                playerent *d = getclient(cn);
                if (!d || vote < VOTE_NEUTRAL || vote > VOTE_NO) break;
                d->vote = vote;
                if (vote == VOTE_NEUTRAL) break;
                d->voternum = curvote->nextvote++;
                if ((/*voteid*/ true || d == player1) && (d != curvote->owner || curvote->millis + 100 < lastmillis))
                    conoutf("%s \f6(%d) \f2voted \f%s", (d == player1) ? "\f1you" : d->name, cn, vote == VOTE_NO ? "3no" : "0yes");
                onChangeVote( 2, vote, cn );
                break;
            }

            case SV_VOTEREMAIN:
            {
                const int projection = getint(p), yes_remain = getint(p), no_remain = getint(p);
                if (!curvote) break;
                curvote->expiryresult = projection;
                curvote->yes_remain = yes_remain;
                curvote->no_remain = no_remain;
                break;
            }

            case SV_VOTERESULT:
            {
                int vres = getint(p), vetocn = getint(p);
                playerent *d = getclient(vetocn);
                curvote->veto = (d != NULL);
                if (curvote && vres >= 0 && vres < VOTE_NUM)
                {
                    curvote->result = vres;
                    curvote->millis = totalmillis + 5000;
                    if (d) conoutf("\f1%s vetoed the vote to %s", colorname(d), vres == VOTE_YES ? "\f0pass" : "\f3fail");
                    conoutf(vres == VOTE_YES ? _("vote %c0passed") : _("vote %c3failed"), CC);
                    audiomgr.playsound(vres == VOTE_YES ? S_VOTEPASS : S_VOTEFAIL, SP_HIGH);
                    if (identexists("onVoteEnd")) execute("onVoteEnd");
                    extern int votepending;
                    votepending = 0;
                }
                onChangeVote( 3, vres, vetocn );
                break;
            }

            case SV_WHOIS:
            {
                const int cn = getint(p);
                playerent *pl = getclient(cn);
                if (cn == -1)
                {
                    const int owner = getint(p), wants = getint(p);
                    pl = getclient(owner);
                    playerent *wanted = getclient(wants);
                    conoutf(_("%s requests whois on %s"), pl ? colorname(pl) : "someone", wanted ? colorname(wanted) : "someone");
                }
                else
                {
                    const int ip = getint(p), mask = getint(p), port = getint(p);
                    getstring(text, p);
                    filtertext(text, text);
                    defformatstring(cip)("%d", ip & 0xFF);
                    if (mask > 8 || (ip >> 8) & 0xFF)
                    {
                        concatformatstring(cip, ".%d", (ip >> 8) & 0xFF);
                        if (mask > 16 || (ip >> 16) & 0xFF)
                        {
                            concatformatstring(cip, ".%d", (ip >> 16) & 0xFF);
                            if (mask > 24 || (ip >> 24) & 0xFF)
                                concatformatstring(cip, ".%d", (ip >> 24) & 0xFF);
                        }
                    }
                    if (mask < 32) concatformatstring(cip, "/%d", mask);
                    conoutf(_("whois on %s returned %s:%d"), pl ? colorname(pl) : "unknown", cip, port);
                    if (text[0])
                        conoutf(_("this user is authed as '%s'"), text);
                    else
                        conoutf(_("this user is not authed"));
                }
                break;
            }

            case SV_SENDDEMOLIST:
            {
                int demos = getint(p);
                if(!demos) conoutf(_("no demos available"));
                else loopi(demos)
                {
                    getstring(text, p);
                    conoutf("%d. %s", i+1, text);
                }
                break;
            }

            case SV_DEMOPLAYBACK:
            {
                string demofile;
                extern char *curdemofile;
                getstring(demofile, p, MAXSTRLEN);
                watchingdemo = demoplayback = demofile[0] != '\0';
                DELETEA(curdemofile);
                if(demoplayback)
                {
                    curdemofile = newstring(demofile);
                    player1->resetspec();
                    player1->state = CS_DEAD;
                    player1->team = TEAM_SPECT;
                }
                else
                {
                    // cleanups
                    curdemofile = newstring("n/a");
                    loopv(players) zapplayer(players[i]);
                    clearvote();
                    player1->state = CS_ALIVE;
                    player1->resetspec();
                }
                player1->clientnum = getint(p);
                break;
            }

            default:
                neterr("type");
                return;
        }
    }

    #ifdef _DEBUG
    protocoldebug(false);
    #endif
}

void setDemoFilenameFormat(char *fmt)
{
    extern string demofilenameformat;
    if(fmt && fmt[0]!='\0')
    {
        copystring(demofilenameformat, fmt);
    } else copystring(demofilenameformat, DEFDEMOFILEFMT); // reset to default if passed empty string - or should we output the current value in this case?
}
COMMANDN(demonameformat, setDemoFilenameFormat, "s");
void setDemoTimestampFormat(char *fmt)
{
    extern string demotimestampformat;
    if(fmt && fmt[0]!='\0')
    {
        copystring(demotimestampformat, fmt);
    } else copystring(demotimestampformat, DEFDEMOTIMEFMT); // reset to default if passed empty string - or should we output the current value in this case?
}
COMMANDN(demotimeformat, setDemoTimestampFormat, "s");
void setDemoTimeLocal(int *truth)
{
    extern int demotimelocal;
    demotimelocal = *truth == 0 ? 0 : 1;
}
COMMANDN(demotimelocal, setDemoTimeLocal, "i");
void getdemonameformat() { extern string demofilenameformat; result(demofilenameformat); } COMMAND(getdemonameformat, "");
void getdemotimeformat() { extern string demotimestampformat; result(demotimestampformat); } COMMAND(getdemotimeformat, "");
void getdemotimelocal() { extern int demotimelocal; intret(demotimelocal); } COMMAND(getdemotimelocal, "");


const char *parseDemoFilename(char *srvfinfo)
{
    int gmode = 0; //-314;
    int gmuts = G_M_NONE;
    int mplay = 0;
    int mdrop = 0;
    int stamp = 0;
    string srvmap;
    if(srvfinfo && srvfinfo[0])
    {
        int fip = 0;
        char sep[] = ":";
        char *pch;
        pch = strtok (srvfinfo,sep);
        while (pch != NULL && fip < 4)
        {
            fip++;
            switch(fip)
            {
                case 1: gmode = atoi(pch); break;
                case 5: gmuts = atoi(pch); break;
                case 2: mplay = atoi(pch); break;
                case 3: mdrop = atoi(pch); break;
                case 4: stamp = atoi(pch); break;
                default: break;
            }
            pch = strtok (NULL, sep);
        }
        copystring(srvmap, pch);
    }
    extern const char *getDemoFilename(int gmode, int gmuts, int mplay, int mdrop, int tstamp, char *srvmap);
    return getDemoFilename(gmode, gmuts, mplay, mdrop, stamp, srvmap);
}

void receivefile(uchar *data, int len)
{
    static char text[MAXTRANS];
    ucharbuf p(data, len);
    int type = getint(p);
    data += p.length();
    len -= p.length();
    switch(type)
    {
        case SV_SENDDEMO:
        {
            getstring(text, p);
            extern string demosubpath;
            defformatstring(demofn)("%s", parseDemoFilename(text));
            defformatstring(fname)("demos/%s%s.dmo", demosubpath, demofn);
            copystring(demosubpath, "");
            //data += strlen(text);
            int demosize = getint(p);
            if(p.remaining() < demosize)
            {
                p.forceoverread();
                break;
            }
            path(fname);
            stream *demo = openrawfile(fname, "wb");
            if(!demo)
            {
                conoutf(_("failed writing to \"%s\""), fname);
                return;
            }
            conoutf(_("received demo \"%s\""), fname);
            demo->write(&p.buf[p.len], demosize);
            delete demo;
            break;
        }

        case SV_RECVMAP:
        {
            getstring(text, p);
            conoutf(_("received map \"%s\" from server, reloading.."), text);
            int mapsize = getint(p);
            int cfgsize = getint(p);
            int cfgsizegz = getint(p);
            /* int revision = */ getint(p);
            int size = mapsize + cfgsizegz;
            if(MAXMAPSENDSIZE < mapsize + cfgsizegz || cfgsize > MAXCFGFILESIZE) { // sam's suggestion
                conoutf(_("map %s is too large to receive"), text);
            } else {
                if(p.remaining() < size)
                {
                    p.forceoverread();
                    break;
                }
                if(securemapcheck(text))
                {
                    p.len += size;
                    break;
                }
                writemap(path(text), mapsize, &p.buf[p.len]);
                p.len += mapsize;
                writecfggz(path(text), cfgsize, cfgsizegz, &p.buf[p.len]);
                p.len += cfgsizegz;
            }
            break;
        }

        default:
            p.len = 0;
            parsemessages(-1, NULL, p);
            break;
    }
}

void servertoclient(int chan, uchar *buf, int len, bool demo)   // processes any updates from the server
{
    ucharbuf p(buf, len);
    switch(chan)
    {
        case 0: parsepositions(p); break;
        case 1: parsemessages(-1, NULL, p, demo); break;
        case 2: receivefile(p.buf, p.maxlen); break;
    }
}

void localservertoclient(int chan, uchar *buf, int len, bool demo)   // processes any updates from the server
{
//    pktlogger.queue(enet_packet_create (buf, len, 0));  // log local & demo packets
    servertoclient(chan, buf, len, demo);
}
