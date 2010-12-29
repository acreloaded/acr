// client processing of the incoming network stream

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

VARP(networkdebug, 0, 0, 1);
#define DEBUGCOND (networkdebug==1)

extern bool c2sinit, watchingdemo;
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

VARP(autogetmap, 0, 1, 1);

void changemapserv(char *name, int mode, int download)		// forced map change from the server
{
	gamemode = mode;
	if(m_demo) return;
	bool loaded = load_world(name);
	if(download > 0)
	{
		if(securemapcheck(name, false)) return;
		bool sizematch = maploaded == download || download < 10;
		if(loaded && sizematch) return;
		if(autogetmap)
		{
			if(!loaded) getmap(); // no need to ask
			else showmenu("getmap");
		}
		else
		{
			if(!loaded || download < 10) conoutf("\"getmap\" to download the current map from the server");
			else conoutf("\"getmap\" to download a different version of the current map from the server");
		}
	}
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
		else	  d->o.x += dx<0 ? r-fx : -(r-fx);
	}
}

void updatelagtime(playerent *d)
{
	int lagtime = totalmillis-d->lastupdate;
	if(lagtime)
	{
		if(d->state!=CS_SPAWNING && d->lastupdate) d->plag = (d->plag*5+lagtime)/6;
		d->lastupdate = totalmillis;
	}
}

extern void trydisconnect();

VARP(maxrollremote, 0, 1, 1); // bound remote "roll" values by our maxroll?!
VARP(voteid, 0, 1, 1); // display votes

void parsepositions(ucharbuf &p)
{
	int type;
	while(p.remaining()) switch(type = getint(p))
	{
		case SV_POS:						// position of another client
		{
			int cn = getint(p);
			vec o, vel;
			float yaw, pitch, roll;
			o.x   = getfloat(p);
			o.y   = getfloat(p);
			o.z   = getfloat(p);
			yaw   = getfloat(p);
			pitch = getfloat(p);
			roll  = getfloat(p);
			vel.x = getfloat(p);
			vel.y = getfloat(p);
			vel.z = getfloat(p);
			int f = getuint(p), seqcolor = (f>>6)&1;
			playerent *d = getclient(cn);
			if(!d || seqcolor!=(d->lifesequence&1)) continue;
			vec oldpos(d->o);
			float oldyaw = d->yaw, oldpitch = d->pitch;
			d->o = o;
			d->o.z += d->eyeheight;
			d->yaw = yaw;
			d->pitch = pitch;
			d->roll = roll;
			d->vel = vel;
			d->strafe = (f&3)==3 ? -1 : f&3;
			f >>= 2;
			d->move = (f&3)==3 ? -1 : f&3;
			f >>= 2;
			d->onfloor = f&1;
			f >>= 1;
			d->onladder = f&1;
			f >>= 2;
			updatecrouch(d, f&1);
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
			if(d->state==CS_LAGGED || d->state==CS_SPAWNING) d->state = CS_ALIVE;
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

extern votedisplayinfo *curvote;

void parsemessages(int cn, playerent *d, ucharbuf &p)
{
	static char text[MAXTRANS];
	int type, joining = 0;
	bool demoplayback = false;

	while(p.remaining())
	{
		type = getint(p);

		#ifdef _DEBUG
		if(type!=SV_POS && type!=SV_PINGTIME && type!=SV_PINGPONG && type!=SV_CLIENT)
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
			case SV_SERVINFO:					// welcome messsage from the server
			{
				int mycn = getint(p), prot = getint(p);
				if(prot != PROTOCOL_VERSION)
				{
					conoutf("\f3you are using a different game protocol (you: %d, server: %d)", PROTOCOL_VERSION, prot);
					disconnect();
					return;
				}
				sessionid = getint(p);
				player1->clientnum = mycn;
				sendintro();
				break;
			}

			case SV_WELCOME:
				joining = getint(p);
				getstring(text, p);
				if(text[0]){
					conoutf("MOTD:");
					conoutf("\f4%s", text);
				}
				player1->resetspec();
				resetcamera();
				break;

			case SV_CLIENT:
			{
				int cn = getint(p), len = getuint(p);
				ucharbuf q = p.subbuf(len);
				parsemessages(cn, getclient(cn), q);
				break;
			}

			case SV_SOUND:
			{
				int cn = getint(p);
				playerent *d = getclient(cn);
				if(!d) d = player1;
				playsound(getint(p), d);
				break;
			}

			case SV_TEXT:
			{
				int cn = getint(p), voice = getint(p), flags = (voice >> 5) & 7;
				voice = (voice & 0x1F) + S_MAINEND;
				getstring(text, p);
				filtertext(text, text);
				playerent *d = getclient(cn);
				if(!d) break;
				saytext(d, text, flags, voice);
				break;
			}

			case SV_MAPCHANGE:
			{
				getstring(text, p);
				int mode = getint(p);
				int downloadable = getint(p);
				changemapserv(text, mode, downloadable);
				if(m_arena && joining>2) deathstate(player1);
				break;
			}

			case SV_ITEMLIST:
			{
				int n;
				resetspawns();
				while((n = getint(p))!=-1) setspawn(n, true);
				break;
			}

			case SV_MAPRELOAD:		  // server requests next map
			{
				getint(p);
				s_sprintfd(nextmapalias)("nextmap_%s", getclientmap());
				const char *map = getalias(nextmapalias);	 // look up map in the cycle
				changemap(map ? map : getclientmap());
				break;
			}

			case SV_SWITCHTEAM:
			{
				int t = getint(p);
                if(m_teammode) conoutf("\f3Team %s is full", team_string(t));
				break;
			}

			case SV_INITCLIENT:
			{
				playerent *d = newclient(getint(p));
				d->team = getint(p);
				setskin(d, getint(p));
				getstring(text, p);
				filtertext(text, text, 1, MAXNAMELEN);
				if(!text[0]) s_strcpy(text, "unnamed");
				conoutf("connected: %s", colorname(d, text));
				updateclientname(d);
				filtername(d->name, text);
				if(m_flags) loopi(2)
				{
					flaginfo &f = flaginfos[i];
					if(!f.actor) f.actor = getclient(f.actor_cn);
				}
				break;
			}

			case SV_NEWNAME:
			{
				playerent *d = getclient(getint(p));
				getstring(text, p);
				filtertext(text, text, 1, MAXNAMELEN);
				if(!text[0]) s_strcpy(text, "unnamed");
				if(!d || !strcmp(d->name, text)) break;
				if(d->name[0]) conoutf("%s \f6(%d) \f5is now known as \f1%s", d->name, d->clientnum, text);
				filtername(d->name, text);
				if(d == player1) alias("curname", player1->name);
				updateclientname(d);
				break;
			}

			case SV_SKIN:
				setskin(d, getint(p));
				break;

			case SV_CDIS:
			{
				int cn = getint(p);
				playerent *d = getclient(cn);
				if(!d) break;
				if(d->name[0]) conoutf("player %s disconnected", colorname(d));
				zapplayer(players[cn]);
				break;
			}

			case SV_EDITMODE:
			{
				int val = getint(p);
				if(!d) break;
				if(val) d->state = CS_EDITING;
				else d->state = CS_ALIVE;
				break;
			}

			case SV_SPAWN:
			{
				playerent *s = d;
				if(!s) { static playerent dummy; s = &dummy; }
				s->respawn();
				s->lifesequence = getint(p);
				s->health = getint(p);
				s->armour = getint(p);
				int gunselect = getint(p);
				s->setprimary(gunselect);
				s->selectweapon(gunselect);
				loopi(NUMGUNS) s->ammo[i] = getint(p);
				loopi(NUMGUNS) s->mag[i] = getint(p);
				s->state = CS_SPAWNING;
				break;
			}

			case SV_SPAWNSTATE:
			{
				if(editmode) toggleedit(true);
				showscores(false);
				setscope(false);
				player1->respawn();
				player1->lifesequence = getint(p);
				player1->health = getint(p);
				player1->armour = getint(p);
				player1->setprimary(getint(p));
				player1->selectweapon(getint(p));
				int arenaspawn = getint(p);
				loopi(NUMGUNS) player1->ammo[i] = getint(p);
				loopi(NUMGUNS) player1->mag[i] = getint(p);
				player1->state = CS_ALIVE;
				findplayerstart(player1, false, arenaspawn);
				extern int nextskin;
				if(player1->skin!=nextskin) setskin(player1, nextskin);
				arenaintermission = 0;
				if(m_arena)
				{
					closemenu(NULL);
					conoutf("new round starting... fight!");
					hudeditf(HUDMSG_TIMER, "FIGHT!");
					if(m_botmode) BotManager.RespawnBots();
				}
				addmsg(SV_SPAWN, "rii", player1->lifesequence, player1->weaponsel->type);
				player1->weaponswitch(player1->primweap);
				player1->weaponchanging -= weapon::weaponchangetime/2;
				break;
			}

			case SV_SHOTFX:
			{
				int scn = getint(p), gun = getint(p);
				vec from, to;
				loopk(3) from[k] = getfloat(p);
				loopk(3) to[k] = getfloat(p);
				playerent *s = getclient(scn);
				if(!s || !weapon::valid(gun)) break;
				if(gun==GUN_SHOTGUN) createrays(from, to);
				s->lastaction = lastmillis;
				if(s != player1) s->mag[gun]--;
				if(s->weapons[gun])
				{
					s->lastattackweapon = s->weapons[gun];
					s->weapons[gun]->attackfx(from, to, -1);
				}
				break;
			}

			case SV_THROWNADE:
			{
				vec from, to;
				loopk(3) from[k] = getfloat(p);
				loopk(3) to[k] = getfloat(p);
				int nademillis = getint(p);
				if(!d) break;
				d->lastaction = lastmillis;
				d->lastattackweapon = d->weapons[GUN_GRENADE];
				if(d->weapons[GUN_GRENADE]) d->weapons[GUN_GRENADE]->attackfx(from, to, nademillis);
				break;
			}

			case SV_RELOAD:
			{
				int cn = getint(p), gun = getint(p);
				playerent *p = getclient(cn);
				playsound(guns[gun].reload, p ? p : player1);
				if(!p) break;
				int bullets = min(p->ammo[gun], magsize(gun) - p->mag[gun]);
				p->ammo[gun] -= bullets;
				p->mag[gun] += bullets;
				break;
			}

			case SV_DAMAGE:
			{
				int tcn = getint(p),
					acn = getint(p),
					damage = getint(p),
					armour = getint(p),
					health = getint(p),
					weap = getint(p);
				playerent *target = getclient(tcn), *actor = getclient(acn);
				if(!target || !actor) break;
				target->armour = armour;
				target->health = health;
				dodamage(damage, target, actor, weap & 0x7F, (weap & 0x80) > 0, false);
				break;
			}

			case SV_HITPUSH:
			{
				int gun = getint(p), damage = getint(p);
				vec dir;
				loopk(3) dir[k] = getfloat(p);
				player1->hitpush(damage, dir, NULL, gun);
				break;
			}

			case SV_DIED:
			{
				int vcn = getint(p), acn = getint(p), frags = getint(p), weap = getint(p);
				playerent *victim = getclient(vcn), *actor = getclient(acn);
				if(!actor) break;
				actor->frags = frags;
				if(!victim) break;
				dokill(victim, actor, weap & 0x7F, (weap & 0x80) > 0);
				break;
			}

			case SV_RESUME:
			{
				loopi(MAXCLIENTS)
				{
					int cn = getint(p);
					if(p.overread() || cn<0) break;
					int state = getint(p), lifesequence = getint(p), gunselect = getint(p), flagscore = getint(p), frags = getint(p), deaths = getint(p), health = getint(p), armour = getint(p);
					int ammo[NUMGUNS], mag[NUMGUNS];
					loopi(NUMGUNS) ammo[i] = getint(p);
					loopi(NUMGUNS) mag[i] = getint(p);
					playerent *d = (cn == getclientnum() ? player1 : newclient(cn));
					if(!d) continue;
					if(d!=player1) d->state = state;
					d->lifesequence = lifesequence;
					d->flagscore = flagscore;
					d->frags = frags;
					d->deaths = deaths;
					if(d!=player1)
					{
						int primary = GUN_KNIFE;
						if(m_osok) primary = GUN_SNIPER;
						else if(m_pistol) primary = GUN_PISTOL;
						else if(!m_lss)
						{
							if(gunselect < GUN_GRENADE) primary = gunselect;
							loopi(GUN_GRENADE) if(ammo[i] || mag[i]) primary = max(primary, i);
							if(primary <= GUN_PISTOL) primary = GUN_ASSAULT;
						}
						d->setprimary(primary);
						d->selectweapon(gunselect);
						d->health = health;
						d->armour = armour;
						memcpy(d->ammo, ammo, sizeof(ammo));
						memcpy(d->mag, mag, sizeof(mag));
					}
				}
				break;
			}

			case SV_ITEMSPAWN:
			{
				int i = getint(p);
				setspawn(i, true);
				break;
			}

			case SV_ITEMACC:
			{
				int i = getint(p), cn = getint(p);
				playerent *d = cn==getclientnum() ? player1 : getclient(cn);
				pickupeffects(i, d);
				break;
			}

			case SV_EDITH:			  // coop editing messages, should be extended to include all possible editing ops
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

			case SV_NEWMAP:
			{
				int size = getint(p);
				if(size>=0) empty_world(size, true);
				else empty_world(-1, true);
				if(d && d!=player1)
					conoutf(size>=0 ? "%s started a new map of size %d" : "%s enlarged the map to size %d", colorname(d), sfactor);
				break;
			}

			case SV_EDITENT:			// coop edit of ent
			{
				uint i = getint(p);
				while((uint)ents.length()<=i) ents.add().type = NOTUSED;
				int to = ents[i].type;
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
				if(ents[i].type==SOUND) preloadmapsound(ents[i]);
				break;
			}

			case SV_PINGPONG:
				addmsg(SV_PINGTIME, "i", totalmillis - getint(p));
				break;

			case SV_PINGTIME:
			{
				playerent *pl = getclient(getint(p));
				int pp = getint(p);
				if(pl) pl->ping = pp;
				break;
			}

			case SV_TIMEUP:
				timeupdate(getint(p));
				break;

			case SV_WEAPCHANGE:
			{
				int gun = getint(p);
				if(d) d->selectweapon(gun);
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
						float x = getfloat(p), y = getfloat(p), z = getfloat(p);
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

			case SV_FLAGCNT:
			{
				int fcn = getint(p);
				int flags = getint(p);
				playerent *p = (fcn == getclientnum() ? player1 : getclient(fcn));
				if(p) p->flagscore = flags;
				break;
			}

			case SV_ARENAWIN:
			{
				int acn = getint(p);
				playerent *alive = acn<0 ? NULL : (acn==getclientnum() ? player1 : getclient(acn));
				conoutf("the round is over! next round in 5 seconds...");
				if(m_botmode && acn==-2) hudoutf("the bots have won the round!");
				else if(!alive) hudoutf("everyone died!");
				else if(m_teammode) hudoutf("team %s has won the round!", alive->team);
				else if(alive==player1) hudoutf("you are the survivor!");
				else hudoutf("%s is the survivor!", colorname(alive));
				arenaintermission = lastmillis;
				break;
			}

			case SV_FORCEDEATH:
			{
				int cn = getint(p);
				playerent *d = cn==getclientnum() ? player1 : newclient(cn);
				if(!d) break;
				deathstate(d);
				break;
			}

			case SV_CURRENTSOP:
			{
				int cl = getint(p), r = getint(p);
				playerent *p = getclient(cl);
				p->priv = r;
				break;
			}

			case SV_SOPCHANGE:
			{
				int cl = getint(p), r = getint(p);
				bool drop = (r >> 7) & 1, err = (r >> 6) & 1; r &= 0x3F;
				playerent *d = getclient(cl);
				const char *n = (d == player1) ? "\f1you" : colorname(d);
				if(!d) break;
				if(err){
					if(d == player1) hudoutf("\f1you \f3already \f2have \f%d%s \f5status", privcolor(r), privname(r));
					else hudoutf("\f3there is already a \f1master \f2(\f0%s\f2)", n);
					break;
				}
				hudoutf("%s \f2%s \f%d%s \f5status", n, drop ? "relinquished" : "claimed", privcolor(r), privname(r));
				break;
			}

			case SV_SETTEAM:
			{
				int cn = getint(p), fnt = getint(p), ftr = fnt >> 4; fnt &= 0xF;
				playerent *p = getclient(cn);
				if(!p || !m_teammode) break;
				const char* nts = team_string(fnt);
				if(p->team == fnt){
					if(p == player1 && ftr == FTR_AUTOTEAM) hudoutf("\f1you stay in team %s", nts);
					break;
				}
				p->team = fnt;
				if(p == player1 && !watchingdemo){
					switch(ftr){
						case FTR_PLAYERWISH:
							conoutf("\f2you're now in team %s", nts);
							break;
						case FTR_AUTOTEAM:
							hudoutf("\f1the server forced you to team %s", nts);
							break;
					}
				}
				else{
                    switch(ftr)
                    {
                        case FTR_PLAYERWISH:
                            conoutf("\f2%s switched to team %s", colorname(p), nts); // new message
                            break;
                        case FTR_AUTOTEAM:
                            conoutf("\f1the server forced %s to team \f%s", colorname(p), fnt ? 1 : 3, nts);
                            break;
                    }
                }
				break;
			}

			case SV_CALLVOTE:
			{
				int cn = getint(p), type = getint(p);
				playerent *d = getclient(cn);
				if(type < 0 || type >= SA_NUM || !d) return;
				votedisplayinfo *v = NULL;
				string a;
				switch(type)
				{
					case SA_MAP:
						getstring(text, p);
						filtertext(text, text);
						itoa(a, getint(p));
						v = newvotedisplayinfo(d, type, text, a);
						break;
					case SA_SERVERDESC:
						getstring(text, p);
						filtertext(text, text);
						v = newvotedisplayinfo(d, type, text, NULL);
						break;
					case SA_STOPDEMO:
					case SA_REMBANS:
					case SA_SHUFFLETEAMS:
						v = newvotedisplayinfo(d, type, NULL, NULL);
						break;
					case SA_GIVEADMIN:
						break;
						itoa(a, getint(p));
						itoa(text, getint(p));
						v = newvotedisplayinfo(d, type, a, text);
					default:
						itoa(a, getint(p));
						v = newvotedisplayinfo(d, type, a, NULL);
						break;
				}
				if(v) v->expiremillis = totalmillis;
				if(type == SA_KICK) v->expiremillis += 35000;
				else if(type == SA_BAN) v->expiremillis += 25000;
				else v->expiremillis += 40000;
				displayvote(v);
				break;
			}

			case SV_CALLVOTEERR:
			{
				int e = getint(p);
				if(e < 0 || e >= VOTEE_NUM) break;
				conoutf("\f3could not vote: %s", voteerrorstr(e));
				break;
			}

			case SV_VOTE:
			{
				int cn = getint(p), vote = getint(p);
				playerent *d = getclient(cn);
				if(!curvote || !d) break;
				d->vote = vote;
				d->voternum = curvote->nextvote++;
				if(voteid) conoutf("%s \f6(%d) \f2voted \f%s", d->name, cn, vote == VOTE_NO ? "3no" : "0yes");
				break;
			}

			case SV_VOTERESULT:
			{
				int v = getint(p);
				veto = ((v >> 7) & 1) > 0;
				v &= 0x7F;
				if(curvote && v >= 0 && v < VOTE_NUM)
				{
					curvote->result = v;
					curvote->millis = totalmillis + 5000;
					conoutf("vote %s", v == VOTE_YES ? "\f0passed" : "\f3failed");
					playsound(v == VOTE_YES ? S_VOTEPASS : S_VOTEFAIL, SP_HIGH);
				}
				break;
			}

			case SV_WHOIS:
			{
				int cn = getint(p), ip = getint(p), mask = getint(p);
				playerent *pl = getclient(cn);
				s_sprintfd(cip)("%d", ip & 0xFF);
				if(mask >= 8 || (ip >> 8) & 0xFF){
					s_sprintf(cip)("%s.%d", cip, (ip >> 8) & 0xFF);
					if(mask >= 16 || (ip >> 16) & 0xFF){
						s_sprintf(cip)("%s.%d", cip, (ip >> 16) & 0xFF);
						if(mask >= 24|| (ip >> 24) & 0xFF) s_sprintf(cip)("%s.%d", cip, (ip >> 24) & 0xFF);
					}
				}
				if(mask < 32) s_sprintf(cip)("%s/%d", cip, mask);
				conoutf("WHOIS client %d:\n\f5name\t%s\n\f5IP\t%s", cn, pl ? colorname(pl) : "", cip);
				break;
			}

			case SV_LISTDEMOS:
			{
				int demos = getint(p);
				if(!demos) conoutf("no demos available");
				else loopi(demos)
				{
					getstring(text, p);
					conoutf("%d. %s", i+1, text);
				}
				break;
			}

			case SV_DEMOPLAYBACK:
			{
				watchingdemo = demoplayback = getint(p)!=0;
				if(demoplayback)
				{
					player1->resetspec();
					player1->state = CS_SPECTATE;
				}
				else
				{
					// cleanups
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

	// check if joining here so as not to interrupt welcomepacket
	if(joining<0 && *getclientmap()) // we are the first client on this server, set map
	{
		nextmode = gamemode;
		changemap(getclientmap());
	}

	#ifdef _DEBUG
	protocoldebug(false);
	#endif
}

void receivefile(uchar *data, int len)
{
	ucharbuf p(data, len);
	int type = getint(p);
	data += p.length();
	len -= p.length();
	switch(type)
	{
		case SV_DEMO:
		{
			s_sprintfd(fname)("demos/%s.dmo", timestring());
			path(fname);
			FILE *demo = openfile(fname, "wb");
			if(!demo)
			{
				conoutf("failed writing to \"%s\"", fname);
				return;
			}
			conoutf("received demo \"%s\"", fname);
			fwrite(data, 1, len, demo);
			fclose(demo);
			break;
		}

		case SV_RECVMAP:
		{
			static char text[MAXTRANS];
			getstring(text, p);
			conoutf("received map \"%s\" from server, reloading..", text);
			int mapsize = getint(p);
			int cfgsize = getint(p);
			int cfgsizegz = getint(p);
			int size = mapsize + cfgsizegz;
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
			break;
		}

		default:
			p.len = 0;
			parsemessages(-1, NULL, p);
			break;
	}
}

void servertoclient(int chan, uchar *buf, int len)   // processes any updates from the server
{
	ucharbuf p(buf, len);
	switch(chan)
	{
		case 0: parsepositions(p); break;
		case 1: parsemessages(-1, NULL, p); break;
		case 2: receivefile(p.buf, p.maxlen); break;
	}
}

void localservertoclient(int chan, uchar *buf, int len)   // processes any updates from the server
{
	servertoclient(chan, buf, len);
}
