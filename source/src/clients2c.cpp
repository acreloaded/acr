// client processing of the incoming network stream

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

VARP(networkdebug, 0, 0, 1);
#define DEBUGCOND (networkdebug==1)

extern bool watchingdemo;
extern string clientpassword;

packetqueue pktlogger;

void neterr(const char *s, int info)
{
	conoutf("\f3%s (%s %d)", _("neterr"), s, info);

	// might indicate a client/server communication bug, create error report
	pktlogger.flushtolog("packetlog.txt");
	hudoutf("\f3%s", _("neterr_logged"));

	disconnect();
}

VARP(autogetmap, 0, 1, 1);

void changemapserv(char *name, int mode, int muts, int download)		// forced map change from the server
{
	modecheck(gamemode = mode, mutators = muts);
	if(m_demo(gamemode)) return;
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
			if(!loaded || download < 10) conoutf("%s", _("getmap"));
			else conoutf("%s", _("getmap_diff"));
		}
	}
}

// update the position of other clients in the game in our world
// don't care if he's in the scenery or other players,
// just don't overlap with our client

void updatepos(playerent *d)
{
	/* // not needed
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
	*/
}

void updatelagtime(playerent *d)
{
	int lagtime = totalmillis-d->lastrecieve;
	if(lagtime)
	{
		if(d->lastrecieve) d->plag = (d->plag*5+lagtime)/6;
		d->lastrecieve = totalmillis;
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
		case N_POS:						// position of another client
		{
			int cn = getint(p);
			vec o, vel;
			float yaw, pitch, roll, pitchvel;
			o.x   = getfloat(p);
			o.y   = getfloat(p);
			o.z   = getfloat(p);
			yaw   = getfloat(p);
			pitch = getfloat(p);
			roll  = getfloat(p);
			vel.x = getfloat(p);
			vel.y = getfloat(p);
			vel.z = getfloat(p);
			pitchvel = getfloat(p);
			int f = getuint(p);
			playerent *d = getclient(cn);
			if(intermission || !d || d == player1 || isowned(d) || ((f>>4)&1)!=(d->lifesequence&1)) continue;
			vec oldpos(d->o);
			float oldyaw = d->yaw, oldpitch = d->pitch;
			d->o = o;
			d->yaw = yaw;
			d->pitch = pitch;
			d->roll = roll;
			d->vel = vel;
			d->pitchvel = pitchvel;
			d->strafe = (f&3)==3 ? -1 : f&3;
			f >>= 2;
			d->move = (f&3)==3 ? -1 : f&3;
			f >>= 3; // 2 + lifeseq
			updatecrouch(d, (f & 1) == 1);
			d->scoping = (f & 2) == 2 /*&& ads_gun(d->weaponsel->type)*/;
			d->onfloor = (f & 4) == 4;
			d->onladder = (f & 8) == 8;

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
			if(d->state==CS_WAITING) d->state = CS_ALIVE;
			// when playing a demo spectate first player we know about
			if(player1->isspectating() && player1->spectatemode==SM_NONE) togglespect();
			extern void clamproll(physent *pl);
			if(maxrollremote) clamproll((physent *) d);
			break;
		}

		default:
			neterr("type in position packet", type);
			return;
	}
}

SVARP(authname, "nobody");
SVARP(authkey, "none");

extern votedisplayinfo *curvote;

void parsemessages(int cn, playerent *d, ucharbuf &p)
{
	static char text[MAXTRANS];
	int type, joining = 0;

	while(p.remaining())
	{
		type = getint(p);

		#ifdef _DEBUG
		if(type!=N_POS && type!=N_PINGTIME && type!=N_PINGPONG && type!=N_CLIENT)
		{
			DEBUGVAR(d);
			DEBUGVAR(messagenames(type));
			protocoldebug(true);
		}
		else protocoldebug(false);
		#endif

		switch(type)
		{
			case N_SERVINFO:					// welcome messsage from the server
			{
				int mycn = getint(p), prot = getint(p), salt = getint(p);
				if(prot != PROTOCOL_VERSION)
				{
					conoutf("\f3you are using a different game protocol (you: %d, server: %d)", PROTOCOL_VERSION, prot);
					//neterr("different protocol", prot);
					disconnect();
					return;
				}
				sessionid = salt;
				player1->clientnum = mycn;
				sendintro();
				break;
			}

			case N_WELCOME:
				joining = getint(p);
				getstring(text, p);
				if(text[0]){
					conoutf("MOTD:");
					conoutf("\f4%s", text);
				}
				player1->resetspec();
				resetcamera();
				break;

			case N_CLIENT:
			{
				int cn = getint(p);//, len = getuint(p);
				ucharbuf q = p.subbuf(p.remaining());
				parsemessages(cn, getclient(cn), q);
				break;
			}

			case N_SOUND:
			{
				playerent *d = getclient(getint(p));
				const int snd = getint(p);
				switch(snd){
					case S_NOAMMO:
					case S_JUMP:
					case S_SOFTLAND:
					case S_HARDLAND:
						if(d && d != player1 && !isowned(d))
							playsound(snd, d);
						break;
				}
				break;
			}

			case N_TEXT:
			{
				int cn = getint(p), voice = getint(p) + S_MAINEND, flags = getint(p);
				getstring(text, p);
				filtertext(text, text);
				playerent *d = getclient(cn);
				if(d) saytext(d, text, flags, voice);
				else if(cn == -1) chatoutf("\f3Server Admin: \f5%s", text);
				break;
			}

			case N_MAPCHANGE:
			{
				// get map info
				int mode = getint(p), muts = getint(p), downloadable = getint(p);
				getstring(text, p);
				changemapserv(text, mode, muts, downloadable);

				// get items
				int n = getint(p);
				resetspawns();
				loopi(n) setspawn(getint(p));

				// get knives
				n = getint(p); // reuse
				knives.setsize(0);
				loopi(n){
					cknife &k = knives.add();
					k.id = getint(p);
					k.millis = totalmillis + getint(p);
					loopi(3) k.o[i] = getfloat(p);
				}
				// get confirms
				n = getint(p); // more reuse
				confirms.setsize(0);
				loopi(n){
					cconfirm &c = confirms.add();
					c.id = getint(p);
					c.team = getint(p);
					loopi(3) c.o[i] = getfloat(p);
				}
				break;
			}

			case N_KNIFEADD:
			{
				cknife &k = knives.add();
				k.id = getint(p);
				k.millis = totalmillis + KNIFETTL;
				loopi(3) k.o[i] = getfloat(p);
				break;
			}
			
			case N_KNIFEREMOVE:
			{
				int id = getint(p);
				loopv(knives) if(knives[i].id == id) knives.remove(i--);
				break;
			}

			case N_CONFIRMADD:
			{
				cconfirm &k = confirms.add();
				k.id = getint(p);
				k.team = getint(p);
				loopi(3) k.o[i] = getfloat(p);
				break;
			}
			
			case N_CONFIRMREMOVE:
			{
				int id = getint(p);
				loopv(confirms) if(confirms[i].id == id) confirms.remove(i--);
				break;
			}

			case N_BLEED:
			{
				playerent *d = getclient(getint(p));
				if(d) d->addicon(eventicon::BLEED);
				break;
			}

			case N_HEADSHOT:
			{
				// make bloody stain
				vec from, to;
				loopi(3) from[i] = getfloat(p);
				loopi(3) to[i] = getfloat(p);
				addheadshot(from, to, getint(p));
				break;
			}

			case N_NEXTMAP: // server requests next map
			{
				getint(p);
				defformatstring(nextmapalias)("nextmap_%s", getclientmap());
				const char *map = getalias(nextmapalias);	 // look up map in the cycle
				changemap(map ? map : getclientmap());
				break;
			}

			case N_SWITCHTEAM:
			{
				int t = getint(p);
                if(m_team(gamemode, mutators))
					conoutf(t & 0x20 ? "\f3You may not switch teams in zombies!" : t & 0x10 ? "\f3You may not unspectate, the server is locked" : "\f3Team %s is full", team_string(t & 0xF));
				break;
			}

			case N_INITCLIENT: // cn team skin level name
			case N_INITAI: // cn team skin skill name owner
			{
				const int cn = getint(p),
						team = getint(p),
						skin = getint(p),
						level = getint(p);
				getstring(text, p);
				const int owner = type == N_INITAI ? getint(p) : -1;

				playerent *d = newclient(cn);
				if(!d || d == player1) break;
				d->team = team;
				setskin(d, skin);
				d->level = level; // skill for bots
				// filter incoming name
				filtername(text, text);
				if(!*text) copystring(text, "unarmed");
				copystring(d->name, text);
				if(type == N_INITCLIENT){ // human
					conoutf("connected: %s", colorname(d));
					if(!joining) chatoutf("%s \f0joined \f2the \f1game", colorname(d));
				}
				else // AI
					d->ownernum = owner;

				updateclientname(d);

				if(m_affinity(gamemode)) loopi(2){
					flaginfo &f = flaginfos[i];
					if(!f.actor) f.actor = getclient(f.actor_cn);
				}
				break;
			}

			case N_REASSIGNAI:
			{
				playerent *d = getclient(getint(p));
				const int newowner = getint(p);
				getstring(text, p);
				if(!d || d == player1) break;
				d->ownernum = newowner;
				filtername(text, text);
				if(!*text) copystring(text, "unarmed");
				copystring(d->name, text);
				// state bugs
				if(d->state == CS_WAITING) d->state = CS_ALIVE;
				d->plag = 0;
				break;
			}
			
			case N_DISC:
			case N_DELAI:
			{
				int cn = getint(p), reason = type == N_DISC ? getint(p) : 0;
				playerent *d = getclient(cn);
				if(!d || d == player1) break;
				loopv(players) if(players[i]) players[i]->damagelog.removeobj(cn);
				if(type == N_DISC && *d->name){
					conoutf("player %s disconnected (%s)", colorname(d), reason >= 0 ? disc_reason(reason) : "normally");
					chatoutf("%s \f3left \f2the \f1game", colorname(d));
				}
				zapplayer(players[cn]);
				break;
			}

			case N_NEWNAME:
			{
				playerent *d = getclient(getint(p));
				getstring(text, p);
				filtertext(text, text, 1, MAXNAMELEN);
				if(!text[0]) copystring(text, "unarmed");
				if(!d || !strcmp(d->name, text)) break;
				if(d->name[0]) conoutf("%s \f6(%d) \f5is now known as \f1%s", d->name, d->clientnum, text);
				filtername(d->name, text);
				if(d == player1) alias("curname", player1->name);
				updateclientname(d);
				break;
			}

			case N_SKIN:
			{
				playerent *d = getclient(getint(p));
				int s = getint(p);
				if(d) setskin(d, s);
				break;
			}

			case N_LEVELUP:
			{
				playerent *d = getclient(getint(p));
				int lvl = clamp(getint(p), 1, MAXLEVEL);
				if(d){
					d->level = lvl;
					if(d->pBot) d->pBot->MakeSkill(lvl);
				}
				break;
			}

			case N_EDITMODE:
			{
				const int cn = getint(p), val = getint(p);
				playerent *d = getclient(cn);
				if(d) d->state = val ? CS_EDITING : CS_ALIVE;
				break;
			}

			case N_TRYSPAWN:
			{
				const int enqueued = getint(p);
				extern bool spawnenqueued;
				spawnenqueued = (enqueued != 0);
				break;
			}

			case N_SPAWN:
			{
				playerent *s = getclient(getint(p));
				if(!s || s == player1 || isowned(s)) { static playerent dummy; s = &dummy; }
				s->respawn();
				s->lifesequence = getint(p);
				s->health = getint(p);
				s->armor = getint(p);
				s->perk1 = getint(p);
				s->perk2 = getint(p);
				int gunselect = getint(p);
				s->setprimary(gunselect);
				s->selectweapon(gunselect);
				s->secondary = getint(p);
				loopi(WEAP_MAX) s->ammo[i] = getint(p);
				loopi(WEAP_MAX) s->mag[i] = getint(p);
				//s->state = CS_SPAWNING;
				s->state = CS_ALIVE;
				s->spawnmillis = lastmillis;
				loopi(3) s->o[i] = getfloat(p);
				updatepos(s);
				break;
			}

			case N_SPAWNSTATE:
			{
				playerent *d = getclient(getint(p));
				if(!d || (d != player1 && !isowned(d))) break;
				d->respawn();
				d->lifesequence = getint(p);
				d->health = getint(p);
				d->armor = getint(p);
				d->perk1 = getint(p);
				d->perk2 = getint(p);
				d->setprimary(getint(p));
				d->secondary = getint(p);
				d->selectweapon(getint(p));
				int arenaspawn = getint(p);
				loopi(WEAP_MAX) d->ammo[i] = getint(p);
				loopi(WEAP_MAX) d->mag[i] = getint(p);
				d->state = CS_ALIVE;
				d->spawnmillis = lastmillis;
				findplayerstart(d, false, arenaspawn);
				if(d == player1){
					if(editmode) toggleedit(true);
					showscores(false);
					setscope(false);
					extern int nextskin;
					if(player1->skin!=nextskin) setskin(player1, nextskin);
					arenaintermission = 0;
					if(m_duke(gamemode, mutators)){
						closemenu(NULL);
						conoutf("%s", _("spawn_newround"));
						hudeditf(HUDMSG_TIMER, _("spawn_fight"));
					}
				}
				addmsg(N_SPAWN, "ri3f3", d->clientnum, d->lifesequence, d->weaponsel->type, d->o.x, d->o.y, d->o.z);
				d->weaponswitch(d->weapons[d->primary]);
				d->weaponchanging -= weapon::weaponchangetime/(d->perk1 == PERK_TIME ? 4 : 2);
				break;
			}

			case N_SG: // someone's shotgun rays
			{	
				extern vec sg[SGRAYS];
				loopi(SGRAYS) loopj(3) sg[i][j] = getfloat(p);
				break;
			}

			case N_PROJ:
			{
				int cn = getint(p), weap = getint(p), dmg = getint(p);
				playerent *d = getclient(cn);
				vec o;
				loopi(3) o[i] = getfloat(p);
				if(!d) break;
				// hit effect
				if(d->weapons[weap]){
					if(explosive_weap(weap) && dmg);
					else if(melee_weap(weap) && dmg < 20 * HEALTHSCALE);
					else d->weapons[weap]->attackhit(o);
				}
				// blood
				if(dmg) damageeffect(dmg, o);
				break;
			}

			case N_SHOOT: case N_RICOCHET:
			case N_SHOOTC:
			{
				int scn = getint(p), gun = getint(p);
				vec from, to;
				if(type == N_SHOOTC) from = to = vec(0, 0, 0);
				else{
					loopk(3) from[k] = getfloat(p);
					loopk(3) to[k] = getfloat(p);
				}
				playerent *s = getclient(scn);
				if(!s || !weapon::valid(gun) || !s->weapons[gun]) break;
				if(s == player1 && (type == N_SHOOTC || gun == WEAP_GRENADE)) break;
				// if it's somebody else's players, remove a bit of ammo
				if(type != N_RICOCHET && s != player1 && !isowned(s)){
					--s->mag[gun];
					updatelastaction(s);
					s->lastattackweapon = s->weapons[gun];
					s->weapons[gun]->gunwait = s->weapons[gun]->info.attackdelay;
				}
				// all have to do attackfx
				s->weapons[gun]->attackfx(from, to, type == N_RICOCHET ? -2 : -1);
				break;
			}

			case N_AKIMBO:
			{
				int cn = getint(p);
				playerent *d = getclient(cn);
				if(!d) break;
				playsound(S_AKIMBOOUT, d, SP_HIGH);
				if(d == player1 || isowned(d)){
					weapon &a = *d->weapons[WEAP_AKIMBO], &p = *d->weapons[WEAP_PISTOL];
					d->akimbo = false;
					a.reset();
					// transfer ammo to pistol
					p.mag = min((int)p.info.magsize, max(a.mag, p.mag));
					p.ammo = max(p.ammo, p.ammo);
					// fix akimbo magcontent
					a.mag = 0;
					a.ammo = 0;
					if(d->weaponsel->type==WEAP_AKIMBO){
						if(d->weapons[d->primary]) d->weaponswitch(d->weapons[d->primary]);
						else d->weaponswitch(&p);
					}
				}
				break;
			}

			case N_THROWKNIFE:
			{
				playerent *d = getclient(getint(p));
				vec from, to;
				loopk(3) from[k] = getfloat(p);
				loopk(3) to[k] = getfloat(p);
				if(!d) break;
				updatelastaction(d);
				d->lastattackweapon = d->weapons[WEAP_KNIFE];
				if(d->weapons[WEAP_KNIFE]) d->weapons[WEAP_KNIFE]->attackfx(from, to, 1);
				break;
			}

			case N_THROWNADE:
			{
				playerent *d = getclient(getint(p));
				vec from, to;
				loopk(3) from[k] = getfloat(p);
				loopk(3) to[k] = getfloat(p);
				int cooked = getint(p);
				if(!d) break;
				updatelastaction(d);
				d->weaponchanging = 0;
				d->lastattackweapon = d->weapons[WEAP_GRENADE];
				if(d->weapons[WEAP_GRENADE]){
					d->weapons[WEAP_GRENADE]->attackfx(from, to, cooked);
					d->weapons[WEAP_GRENADE]->reloading = 0;
				}
				break;
			}

			case N_STREAKREADY:
			{
				playerent *d = getclient(getint(p));
				const int streak = getint(p);
				if(!d) break;
				switch(streak){
					case STREAK_AIRSTRIKE:
						d->addicon(eventicon::AIRSTRIKE);
						++d->airstrikes;
						break;
					case STREAK_DROPNADE:
						d->addicon(eventicon::DROPNADE);
						break;
					case STREAK_REVENGE:
						d->addicon(eventicon::SUICIDEBOMB);
						break;
				}
				break;
			}

			case N_STREAKUSE:
			{
				playerent *d = getclient(getint(p));
				const int streak = getint(p);
				int info = -1; vec o;
				if(streak == STREAK_AIRSTRIKE) loopi(3) o[i] = getfloat(p);
				else info = getint(p);
				if(!d) break;
				switch(streak){
					case STREAK_AIRSTRIKE:
						// may be delayed? in the future?
						--d->airstrikes;
						break;
					case STREAK_RADAR:
						d->radarearned = lastmillis + info;
						d->addicon(eventicon::RADAR);
						break;
					case STREAK_NUKE:
						if(info > 0){ // deploy nuke
							d->nukemillis = lastmillis + info;
							d->addicon(eventicon::NUKE);
							playsound(S_CALLVOTE, SP_HIGHEST);
							// add voice?
							chatoutf("\f2%s is deploying a nuke! \f%s!", colorname(d), d == player1 ? "0Stay alive" : isteam(d, player1) ? "1Defend him" : "3Stop him");
						}
						else if(!info){ // nuke deployed
							// gg...
							loopv(players) if(players[i]) { players[i]->state = CS_ALIVE; }
							chatoutf("\f3%s deployed a nuke!", colorname(d));
							playsound(S_VOTEPASS, SP_HIGHEST);
						}
						else if(info == -2){
							// nuke cancelled
							d->nukemillis = 0;
							chatoutf("\f2%s lost the nuke!", colorname(d));
							// add icon?
							playsound(S_VOTEFAIL, SP_HIGHEST);
						}
						break;
					case STREAK_DROPNADE:
					case STREAK_REVENGE:
						{
							grenadeent *g = new grenadeent(d, NADETTL - MARTYRDOMTTL);
							bounceents.add(g);
							g->id = info;

							g->nadestate = 1;//NS_THROWED;
							g->o = d->o;
							g->moveoutsidebbox((g->vel = vec(0, 0, 0)), d);
							g->resetinterp();
							g->inwater = hdr.waterlevel > g->o.z;
							break;
						}
				}
				break;
			}

			case N_RELOAD:
			{
				int cn = getint(p), gun = getint(p), mag = getint(p), ammo = getint(p);
				playerent *p = getclient(cn);
				if(!p || gun < 0 || gun >= WEAP_MAX) break;
				if(p != player1 && !isowned(p) && p->weapons[gun]) p->weapons[gun]->reload();
				p->ammo[gun] = ammo;
				p->mag[gun] = mag;
				if(gun == WEAP_KNIFE) p->addicon(eventicon::PICKUP);
				break;
			}

			case N_TEAMSCORE:
			{
				const int team = getint(p),
					points = getint(p),
					flags = getint(p),
					frags = getint(p),
					assist = getint(p),
					death = getint(p);
				if(!team_valid(team) || team == TEAM_SPECT) break;
				teamscore &t = teamscores[team];
				t.points = points;
				t.flagscore = flags;
				t.frags = frags;
				t.assists = assist;
				t.deaths = death;
				break;
			}

			case N_POINTS:
			{
				int cn = getint(p), points = getint(p);
				playerent *d = getclient(cn);
				if(d == player1) addexp(points - player1->points);
				if(d) d->points = points;
				break;
			}

			case N_POINTR:
			{
				int reason = getint(p);
				const char* pointreasons_templates[PR_MAX] = {
					"", // N/A
					"%s", // Assist
					"%s", // Splat
					"%s", // Headshot
					"%s", // Kill Confirmed
					"%s", // Kill Denied
					"%s", // Healed Self
					"%s", // Healed Teammate
					"\f3%s", // Healed Enemy
					"%s", // Prevented Bleedout
					"%s", // Bleedout prevented by teammate
					"\f0%s", // You won
					"\f1%s", // Your team wins
					"\f3%s", // You lost
				};
				defformatstring(pr_lookup_str)("point_reason_%d", reason);
				defformatstring(pr_str)(pointreasons_templates[reason%PR_MAX], _(pr_lookup_str));
				expreason(pr_str);
				break;
			}

			case N_REGEN:
			case N_HEAL:
			{
				playerent *healer = type == N_HEAL ? getclient(getint(p)) : NULL;
				int cn = getint(p), health = getint(p);
				playerent *d = getclient(cn);
				if(!d) break;
				d->health = health;
				d->lastregen = lastmillis;
				if(healer) addobit(healer, OBIT_REVIVE, FRAG_NONE, false, d);
				break;
			}

			case N_DAMAGE:
			{
				int tcn = getint(p),
					acn = getint(p),
					damage = getint(p),
					armor = getint(p),
					health = getint(p),
					weap = getint(p),
					style = getint(p);
				vec src; loopi(3) src[i] = getfloat(p);
				playerent *target = getclient(tcn), *actor = getclient(acn);
				if(!target || !actor) break;
				target->armor = armor;
				target->health = health;
				dodamage(damage, target, actor, weap, style, src);
				break;
			}

			case N_KILL:
			{
				const int vcn = getint(p),
					acn = getint(p),
					frags = getint(p),
					weap = getint(p),
					style = getint(p) & FRAG_VALID,
					damage = getint(p),
					combo = getint(p),
					streak = getint(p);

				float killdist = getfloat(p);
				vec src; loopi(3) src[i] = getfloat(p);

				const int assists = getint(p);
				// assists consumed later

				playerent *victim = getclient(vcn), *actor = getclient(acn);
				if(actor){
					actor->frags = frags;
					actor->pointstreak = streak;
				}
				if(victim){
					victim->damagelog.setsize(0);
					loopi(assists) victim->damagelog.add(getint(p));
				} else loopi(assists) getint(p);
				// assists consumed

				if(!actor || !victim) break;
				if((victim->health -= damage) > 0) victim->health = 0;
				dodamage(damage, victim, actor, weap, style, src);
				dokill(victim, actor, weap, damage, style, combo, killdist);
				break;
			}

			case N_RESUME:
			{
				loopi(MAXCLIENTS)
				{
					int cn = getint(p);
					if(p.overread() || cn<0) break;
					const int state = getint(p), lifesequence = getint(p), gunselect = getint(p),
						primary = getint(p), secondary = getint(p),
						points = getint(p), flagscore = getint(p),
						frags = getint(p), assists = getint(p),
						pointstreak = getint(p), deathstreak = getint(p),
						deaths = getint(p), health = getint(p), armor = getint(p),
						radar = getint(p), airstrikes = getint(p), nuke = getint(p), spawnmillis = getint(p);
					int ammo[WEAP_MAX], mag[WEAP_MAX];
					loopi(WEAP_MAX) ammo[i] = getint(p);
					loopi(WEAP_MAX) mag[i] = getint(p);
					playerent *d = getclient(cn);
					if(!d) continue;
					//if(d!=player1 && !isowned(d)) d->state = state;
					d->lifesequence = lifesequence;
					d->points = points;
					d->flagscore = flagscore;
					d->frags = frags;
					d->assists = assists;
					d->pointstreak = pointstreak;
					d->deathstreak = deathstreak;
					d->deaths = deaths;
					d->radarearned = lastmillis + radar;
					d->airstrikes = airstrikes;
					d->nukemillis = lastmillis + nuke;
					if(d!=player1 && !isowned(d))
					{
						d->state = state == CS_WAITING ? CS_DEAD : state;
						d->spawnmillis = lastmillis + spawnmillis;

						d->setprimary(primary);
						d->secondary = secondary;
						d->selectweapon(gunselect);
						d->health = health;
						d->armor = armor;
						memcpy(d->ammo, ammo, sizeof(ammo));
						memcpy(d->mag, mag, sizeof(mag));
					}
				}
				break;
			}

			case N_ITEMSPAWN:
			{
				int i = getint(p);
				setspawn(i);
				break;
			}

			case N_ITEMACC:
			{
				int i = getint(p), cn = getint(p), spawntime = getint(p);
				playerent *d = getclient(cn);
				pickupeffects(i, d, spawntime);
				break;
			}

			case N_EDITH:  // coop editing messages
			case N_EDITT:
			case N_EDITS:
			case N_EDITD:
			case N_EDITE:
			{
				int x  = getint(p);
				int y  = getint(p);
				int xs = getint(p);
				int ys = getint(p);
				int v  = getint(p);
				block b = { x, y, xs, ys };
				switch(type)
				{
					case N_EDITH: editheightxy(v!=0, getint(p), b); break;
					case N_EDITT: edittexxy(v, getint(p), b); break;
					case N_EDITS: edittypexy(v, b); break;
					case N_EDITD: setvdeltaxy(v, b); break;
					case N_EDITE: editequalisexy(v!=0, b); break;
				}
				break;
			}

			case N_EDITW: // coop editing water
			{
				const int newwaterlevel = getint(p);
				loopi(4) hdr.watercolor[i] = getint(p);
				if(newwaterlevel == hdr.waterlevel) break;
				hdr.waterlevel = newwaterlevel;
				conoutf("%s changed the water-level to %d", colorname(d), hdr.waterlevel);
				break;
			}

			case N_NEWMAP:
			{
				int size = getint(p);
				if(size>=0) empty_world(size, true);
				else empty_world(-1, true);
				if(d && d!=player1)
					conoutf(size>=0 ? "%s started a new map of size %d" : "%s enlarged the map to size %d", colorname(d), sfactor);
				break;
			}

			case N_EDITENT:			// coop edit of ent
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

			case N_PINGPONG:
				addmsg(N_PINGTIME, "i", totalmillis - getint(p));
				break;

			case N_PINGTIME:
			{
				int cn = getint(p), ping = getint(p);
				if(cn == getclientnum()) player1->ping = ping;
				loopv(players) if(players[i] && (i == cn || players[i]->ownernum == cn)) players[i]->ping = ping;
				break;
			}

			case N_MAPIDENT:
				conoutf("\f3please \f1get the map \f2by typing \f0/getmap");
				break;

			case N_MAPC2S:
			{
				int cn = getint(p); playerent *d = getclient(cn);
				getstring(text, p);
				conoutf("%s \f0sent the map %s to the server; \f1it can now be voted", d ? colorname(d) : "someone", text);
				break;
			}

			case N_ACCURACY:
			{
				int hit = getint(p), shot = getint(p);
				conoutf("\f1%.2f%% \f5accuracy \f4(\f1%d \f0hit, \f1%d \f2shot, \f1%d \f3wasted\f4)", hit * 100.f / max(shot, 1), hit, shot, shot - hit);
				break;
			}

			case N_TIMEUP:
			{
				int curgamemillis = getint(p), curgamelimit = getint(p), curgameseed = getint(p);
				timeupdate(curgamemillis, curgamelimit, curgameseed);
				break;
			}

			case N_QUICKSWITCH:
			{
				int cn = getint(p);
				playerent *d = getclient(cn);
				if(!d) break;
				d->weaponchanging = lastmillis-1-(weapon::weaponchangetime/(d->perk1 == PERK_TIME ? 4 : 2));
				d->nextweaponsel = d->weaponsel = d->weapons[d->primary];
				break;
			}

			case N_SWITCHWEAP:
			{
				int cn = getint(p), gun = getint(p);
				playerent *d = getclient(cn);
				if(!d || gun < 0 || gun >= WEAP_MAX) break;
				d->ads = 0;
				d->weaponswitch(d->weapons[gun]);
				//if(!d->weaponchanging) d->selectweapon(gun);
				break;
			}

			case N_SERVMSG:
				getstring(text, p);
				conoutf("%s", text);
				break;

			case N_FLAGINFO:
			{
				int flag = getint(p);
				if(!(flag == 0 || flag == 1)) break;
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

			case N_FLAGMSG:
			{
				int flag = getint(p);
				int message = getint(p);
				int actor = getint(p);
				int flagtime = message == FA_KTFSCORE ? getint(p) : -1;
				flagmsg(flag, message, actor, flagtime);
				break;
			}

			case N_FLAGCNT:
			{
				int fcn = getint(p);
				int flags = getint(p);
				playerent *p = getclient(fcn);
				if(p) p->flagscore = flags;
				break;
			}

			case N_ARENAWIN:
			{
				int acn = getint(p); playerent *alive = getclient(acn);
				// check for multiple survivors
				bool multi = false;
				if(m_team(gamemode, mutators) && alive){
					#define teammate(p) (p != alive && p->state == CS_ALIVE && isteam(p, alive))
					if(teammate(player1)) multi = true;
					else loopv(players) if(players[i] && teammate(players[i])){ multi = true; break; }
				}
				conoutf("%s", _("arenawin_over"));
				// no survivors
				if(acn == -1) hudoutf("\f3%s", _("arenawin_fail"));
				// zombies
				else if(m_zombie(gamemode)){
					if(!alive || alive->team == TEAM_RED) hudoutf("\f3%s", _("arenawin_zombies_zombie"));
					else hudoutf("\f0%s", _("arenawin_zombies_humans"));
				}
				// instead of waiting for bots to battle it out...
				else if(acn == -2) hudoutf("%s", _("arenawin_bots"));
				// should not happen? better safe than sorry
				else if(!alive) hudoutf("unknown winner...?");
				// Teams
				else if(m_team(gamemode, mutators) && multi) hudoutf("%s", _(alive->team == player1->team ? "arenawin_teamwin" : "arenawin_teamlose"));
				// FFA or one team member
				else if(alive==player1) hudoutf("%s", _("arenawin_youwin"));
				else hudoutf("%s %s", colorname(alive), _("arenawin_ffa"));
				arenaintermission = lastmillis;
				break;
			}

			case N_FORCEGIB:
			case N_FORCEDEATH:
			{
				int cn = getint(p);
				playerent *d = newclient(cn);
				if(!d) break;
				if(type == N_FORCEGIB) addgib(d);
				deathstate(d);
				break;
			}

			case N_SETPRIV:
			{
				int cl = getint(p), r = getint(p);
				playerent *p = newclient(cl);
				if(!p) break;
				p->priv = r;
				break;
			}

			case N_CLAIMPRIV:
			{
				int cl = getint(p), r = getint(p), t = getint(p);
				playerent *d = getclient(cl);
				const char *n = (d == player1) ? "\f1you" : d ? colorname(d) : "\f2(a connecting admin)";
				switch(t){
					case 0:
					case 1:
						chatoutf("%s \f2%s \f%d%s \f5access", n, t ? "relinquished" : "claimed", privcolor(r), privname(r));
						break;
					case 2:
						if(d == player1) hudoutf("\f1you \f3already \f2have \f%d%s \f5access", privcolor(r), privname(r));
						else hudoutf("\f3there is already another \f1%s \f2(\f%d%s\f2)", privname(r), privcolor(r), n);
						break;
				}
				break;
			}

			case N_AUTHREQ:
			{
				int nonce = getint(p), sauthtoken = getint(p);
				extern int authtoken;
				if(nonce < 0 || sauthtoken != authtoken){
					conoutf("server challenged incorrectly");
					break;
				}
				authtoken = -1;
				conoutf("server is challenging authentication details");
				unsigned hash[5] = {0};
				defformatstring(buf)("%s:%s!%d", authname, authkey, nonce);
				if(!gensha1(buf, hash)){
					conoutf("could not compute message digest");
					break;
				}
				addmsg(N_AUTHCHAL, "ri5", hash[0], hash[1], hash[2], hash[3], hash[4]);
				break;
			}

			case N_AUTHCHAL:
			{
				switch(getint(p)){
					case 0:
						conoutf("please wait, requesting credential match");
						break;
					case 1:
						conoutf("waiting for previous attempt...");
						break;
					case 2:
						conoutf("not connected to authentication server");
						break;
					case 3:
						conoutf("authority request failed, please check your credentials");
						break;
					case 4:
						conoutf("please wait, requesting authentication");
						break;
					case 5:
					{
						int cn = getint(p); getstring(text, p);
						playerent *d = getclient(cn);
						if(!d) break;
						filtertext(text, text, 1, MAXNAMELEN);
						chatoutf("%s \f1identified as \f2'\f9%s\f2'", colorname(d), text);
						break;
					}
					case 6:
						conoutf("please wait %.3f seconds to request another challenge", getint(p) / 1000.f);
						break;
					default:
						conoutf("server sent undefined authority message");
						break;
				}
				break;
			}

			case N_SETTEAM:
			{
				int cn = getint(p), fnt = getint(p), ftr = fnt >> 4; fnt &= 0xF;
				playerent *p = newclient(cn);
				if(!p) break;
				defformatstring(nts)("team \f%d%s", team_color(fnt), team_string(fnt));
				const bool own = (p == player1 && !watchingdemo);
				if(p->team == fnt){ // no change
                    switch(ftr){
                        case FTR_PLAYERWISH:
							if(own) hudoutf("%s", "\f1you \f2did \f3not \f2switch teams");
                            else conoutf("\f2%s did not switch teams", colorname(p));
                            break;
                        case FTR_AUTOTEAM:
							if(own) hudoutf("\f1you stay in \f2%s", nts);
                            else conoutf("\f2%s stays on %s", colorname(p), nts);
                            break;
                    }
                }
				else{ // changed
                    switch(ftr){
                        case FTR_PLAYERWISH:
							if(own) hudoutf("\f1you're \f2now in %s", nts);
                            else conoutf("\f2%s switched to %s", colorname(p), nts);
                            break;
                        case FTR_AUTOTEAM:
							if(own) hudoutf("\f2the server \f1forced you \f2to %s", nts);
                            else conoutf("\f2the server forced %s to %s", colorname(p), nts);
                            break;
                    }
					p->team = fnt;
					// client version of removeexplosives()
					removebounceents(p);
                }
				break;
			}

			case N_CALLVOTE:
			{
				int cn = getint(p), type = getint(p), voteremain = getint(p);
				playerent *d = getclient(cn);
				if(type < 0 || type >= SA_NUM || !d) break;
				votedisplayinfo *v = NULL;
				// vote data storage
				static votedata vote = votedata(text);
				vote = votedata(text); // reset it
				// vote parsing
				switch(type)
				{
					case SA_SERVERDESC:
						getstring(text, p);
						filtertext(text, text);
						break;
					case SA_BAN:
						getstring(text, p);
						filtertext(text, text);
						// fallthrough
					case SA_GIVEROLE:
						vote.int2 = getint(p);
						vote.int1 = getint(p);
						break;

					case SA_MAP:
						vote.int2 = getint(p);
						// fallthrough
					case SA_KICK:
						getstring(text, p);
						filtertext(text, text);
						// more fallthrough
					default:
						vote.int1 = getint(p);
					case SA_STOPDEMO:
					case SA_REMBANS:
					case SA_SHUFFLETEAMS:
						break;
				}
				v = newvotedisplayinfo(d, type, vote);
				if(v) v->expiremillis = totalmillis + voteremain;
				displayvote(v);
				break;
			}

			case N_CALLVOTEERR:
			{
				int e = getint(p);
				if(e < 0 || e >= VOTEE_NUM) break;
				conoutf("\f3could not vote: %s", voteerrorstr(e));
				break;
			}

			case N_VOTE:
			{
				int cn = getint(p), vote = getint(p);
				playerent *d = getclient(cn);
				if(!curvote || !d || vote < VOTE_NEUTRAL || vote > VOTE_NO) break;
				d->vote = vote;
				if(vote == VOTE_NEUTRAL) break;
				d->voternum = curvote->nextvote++;
				if((voteid || d == player1) && (d != curvote->owner || curvote->millis + 100 < lastmillis))
					conoutf("%s \f6(%d) \f2voted \f%s", (d == player1) ? "\f1you" : d->name, cn, vote == VOTE_NO ? "3no" : "0yes");
				break;
			}

			case N_VOTERESULT:
			{
				int vr = getint(p);
				playerent *d = getclient(getint(p));
				veto = d != NULL;
				if(curvote && vr >= 0 && vr < VOTE_NUM)
				{
					curvote->result = vr;
					curvote->millis = totalmillis + 5000;
					if(d) conoutf("\f1%s vetoed the vote to %s", colorname(d), vr == VOTE_YES ? "\f0pass" : "\f3fail");
					conoutf("vote %s", vr == VOTE_YES ? "\f0passed" : "\f3failed");
					playsound(vr == VOTE_YES ? S_VOTEPASS : S_VOTEFAIL, SP_HIGH);
				}
				break;
			}

			case N_WHOIS:
			{
				int cn = getint(p), wants = getint(p);
				playerent *pl = getclient(cn), *owner = getclient(wants);
				string targetname;
				copystring(targetname, pl ? colorname(pl) : "unknown");
				conoutf("\fs%s \f1requests \f2who\f0is \f3on \fr%s", owner ? colorname(owner) : "someone", targetname);
				break;
			}

			case N_WHOISINFO:
			{
				int cn = getint(p), ip = getint(p), mask = getint(p), port = getint(p);
				playerent *pl = getclient(cn);

				defformatstring(cip)("%d", ip & 0xFF);
				if(mask > 8 || (ip >> 8) & 0xFF){
					formatstring(cip)("%s.%d", cip, (ip >> 8) & 0xFF);
					if(mask > 16 || (ip >> 16) & 0xFF){
						formatstring(cip)("%s.%d", cip, (ip >> 16) & 0xFF);
						if(mask > 24 || (ip >> 24) & 0xFF) formatstring(cip)("%s.%d", cip, (ip >> 24) & 0xFF);
					}
				}
				if(mask < 32) formatstring(cip)("%s\f7/\f4%d", cip, mask);
				conoutf("\f2who\f0is \f1on \f3%s \f4returned \f5%s\f6:\f5%d", pl ? colorname(pl) : "unknown", cip, port);
				break;
			}

			case N_LISTDEMOS:
			{
				int demos = getint(p);
				if(!demos) conoutf("%s", _("demo_none"));
				else loopi(demos)
				{
					getstring(text, p);
					conoutf("%d. %s", i+1, text);
				}
				break;
			}

			case N_DEMOPLAYBACK:
			{
				watchingdemo = getint(p)!=0;
				if(watchingdemo)
				{
					getstring(text, p);
					conoutf("playing demo \"%s\"", text);
					player1->resetspec();
					player1->team = TEAM_SPECT;
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

			case N_EXT:
			{
				getstring(text, p, 64);
				int len = getint(p);
				if(len < 1) { neterr("Extension too short,", len); break; }
				if(len > 50) { neterr("Extension too long,", len); break; }
				//if(!strcmp(text, ""));
				else{ // ignore unknown extensions
					conoutf("server sent unknown extension %s, length %d", text, len);
					while(len-- > 0) getint(p);
				}
				break;
			}

			default:
				if(cn < 0) neterr("type in game packet", type);
				else conoutf("\f3illegal network message type (%d)", type);
				return;
		}
	}

	// check if joining here so as not to interrupt welcomepacket
	if(joining<0 && *getclientmap()){ // we are the first client on this server, set map
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
		case N_DEMO:
		{
			defformatstring(fname)("demos/%s.dmo", timestring());
			path(fname);
			FILE *demo = openfile(fname, "wb");
			if(!demo)
			{
				conoutf("failed writing to \"%s\"", fname);
				break;
			}
			conoutf("received demo \"%s\"", fname);
			fwrite(data, 1, len, demo);
			fclose(demo);
			break;
		}

		case N_MAPS2C:
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
