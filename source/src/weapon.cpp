// weapon.cpp: all shooting and effects code

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"
#include "hudgun.h"

VARP(autoreload, 0, 1, 1);

vec sg[SGRAYS];

void updatelastaction(playerent *d){
	loopi(WEAP_MAX) d->weapons[i]->updatetimers();
	d->lastaction = lastmillis;
}

inline void _checkweaponswitch(playerent *p){
	if(!p->weaponchanging) return;
	int timeprogress = lastmillis - p->weaponchanging;
	if(timeprogress > weapon::weaponchangetime) p->weaponchanging = 0;
	else if(timeprogress > weapon::weaponchangetime / 2) p->weaponsel = p->nextweaponsel;
}

void checkweaponswitch(){
	_checkweaponswitch(player1);
	loopv(players) if(players[i]) _checkweaponswitch(players[i]);
}

void selectweapon(weapon *w){
	if(!w || !player1->weaponsel->deselectable()) return;
	if(w->selectable())
	{
		// substitute akimbo
		weapon *akimbo = player1->weapons[WEAP_AKIMBO];
		if(w->type==WEAP_PISTOL && akimbo->selectable()) w = akimbo;

		player1->weaponswitch(w);
	}
}

void selectweaponi(int w){
	if(player1->state == CS_ALIVE && w >= 0 && w < WEAP_MAX)
	{
		selectweapon(player1->weapons[w]);
	}
}

void shiftweapon(int s){
	if(player1->state == CS_ALIVE)
	{
		if(!player1->weaponsel->deselectable()) return;

		weapon *curweapon = player1->weaponsel;
		weapon *akimbo = player1->weapons[WEAP_AKIMBO];

		// collect available weapons
		vector<weapon *> availweapons;
		loopi(WEAP_MAX)
		{
			weapon *w = player1->weapons[i];
			if(!w) continue;
			if(w->selectable() || w==curweapon || (w->type==WEAP_PISTOL && player1->akimbo))
			{
				availweapons.add(w);
			}
		}

		// replace pistol by akimbo
		if(player1->akimbo)
		{
			availweapons.removeobj(akimbo); // and remove initial akimbo
			int pistolidx = availweapons.find(player1->weapons[WEAP_PISTOL]);
			if(pistolidx>=0) availweapons[pistolidx] = akimbo; // insert at pistols position
			if(curweapon->type==WEAP_PISTOL) curweapon = akimbo; // fix selection
		}

		// detect the next weapon
		int num = availweapons.length();
		int curidx = availweapons.find(curweapon);
		if(!num || curidx<0) return;
		int idx = (curidx+s) % num;
		if(idx<0) idx += num;
		weapon *next = availweapons[idx];
		if(next->type!=player1->weaponsel->type) // different weapon
		{
			selectweapon(next);
		}
	}
	else if(player1->isspectating()) updatefollowplayer(s);
}

int currentprimary() { return player1->primweap->type; }
int prevweapon() { return player1->prevweaponsel->type; }
int curweapon() { return player1->weaponsel->type; }

int magcontent(int w) { if(w >= 0 && w < WEAP_MAX) return player1->weapons[w]->mag; else return -1;}
int magreserve(int w) { if(w >= 0 && w < WEAP_MAX) return player1->weapons[w]->ammo; else return -1;}

COMMANDN(weapon, selectweaponi, ARG_1INT);
COMMAND(shiftweapon, ARG_1INT);
COMMAND(currentprimary, ARG_IVAL);
COMMAND(prevweapon, ARG_IVAL);
COMMAND(curweapon, ARG_IVAL);
COMMAND(magcontent, ARG_1EXP);
COMMAND(magreserve, ARG_1EXP);

void tryreload(playerent *p){
	if(!p || p->state!=CS_ALIVE || p->weaponsel->reloading || p->weaponchanging) return;
	if(p->ads == 0)	p->weaponsel->reload();
	else{
		p->wantsreload = true;
		setscope(false);
	}
}

void selfreload() { tryreload(player1); }
COMMANDN(reload, selfreload, ARG_NONE);

void selfuse(){
	// for now we're using it for airstrikes
	addmsg(N_STREAKUSE, "rf3", worldpos.x, worldpos.y, worldpos.z);
}
COMMANDN(use, selfuse, ARG_NONE);

#include "ballistics.h"

bool intersecthead(playerent *d, const vec &from, const vec &to, vec *end = NULL, float tolerance = 1){
	float dist;
	if(intersectsphere(from, to, d->head, HEADSIZE*tolerance, dist)){
		if(end) (*end = to).sub(from).mul(dist).add(from);
		return true;
	}
	return false;
}

int intersect(playerent *d, const vec &from, const vec &to, vec *end){
	float dist;
	// share the head function for other uses
	if(d->head.x >= 0 && intersecthead(d, from, to, end)) return HIT_HEAD;
	float y = d->yaw*RAD, p = (d->pitch/4+90)*RAD, c = cosf(p);
	vec bottom(d->o), top(sinf(y)*c, -cosf(y)*c, sinf(p));
	bottom.z -= d->eyeheight;
	top.mul(d->eyeheight/* + d->aboveeye*/).add(bottom); // space above shoulders
	// torso
	bottom.sub(top).mul(TORSOPART).add(top);
	if(intersectcylinder(from, to, bottom, top, d->radius, dist))
	{
		if(end) (*end = to).sub(from).mul(dist).add(from);
		return HIT_TORSO;
	}
	// restore to body
	bottom.sub(top).div(TORSOPART).add(top);
	// legs
	top.sub(bottom).mul(LEGPART).add(bottom);
	if(intersectcylinder(from, to, bottom, top, d->radius, dist)){
		if(end) (*end = to).sub(from).mul(dist).add(from);
		return HIT_LEG;
	}
	return HIT_NONE;
}

bool intersect(entity *e, const vec &from, const vec &to, vec *end){
	mapmodelinfo &mmi = getmminfo(e->attr2);
	if(!&mmi || !mmi.h) return false;

	float lo = float(S(e->x, e->y)->floor+mmi.zoff+e->attr3);
	return intersectbox(vec(e->x, e->y, lo+mmi.h/2.0f), vec(mmi.rad, mmi.rad, mmi.h/2.0f), from, to, end);
}

playerent *playerincrosshairhit(int &hitzone){
	const vec &from = camera1->o, &to = worldpos;
	const physent *at = camera1;

	playerent *best = NULL;
	float bestdist = 1e16f;
	int zone;
	loopv(players){
		playerent *o = players[i];
		if(!o || o==at || (o->state!=CS_ALIVE && o->state!=CS_EDITING)) continue;
		float dist = at->o.dist(o->o);
		if(dist < bestdist && (zone = intersect(o, from, to))){
			best = o;
			bestdist = dist;
			hitzone = zone;
		}
	}
	return best;
}

void damageeffect(int damage, playerent *d){
	particle_splash(3, damage/10, 1000, d->o);
}


vector<bounceent *> bounceents;

void removebounceents(playerent *owner){
	loopv(bounceents) if(bounceents[i]->owner==owner) { delete bounceents[i]; bounceents.remove(i--); }
}

void movebounceents(){
	loopv(bounceents) if(bounceents[i])
	{
		bounceent *p = bounceents[i];
		if(p->bouncetype && p->applyphysics()) movebounceent(p, 1, false);
		if(!p->isalive(lastmillis))
		{
			p->destroy();
			delete p;
			bounceents.remove(i--);
		}
	}
}

void clearbounceents(){
	if(gamespeed==100);
	else if(multiplayer(false)) bounceents.add((bounceent *)player1);
	loopv(bounceents) if(bounceents[i]) { delete bounceents[i]; bounceents.remove(i--); }
}

VARP(shellsize, 1, 4, 10);

void renderbounceents(){
	loopv(bounceents)
	{
		bounceent *p = bounceents[i];
		if(!p) continue;
		string model;
		vec o(p->o);

		float scale = 1.f;
		int anim = ANIM_MAPMODEL, basetime = 0;
		switch(p->bouncetype)
		{
			case BT_KNIFE:
				copystring(model, "weapons/knife/static");
				break;
			case BT_NADE:
				copystring(model, "weapons/grenade/static");
				break;
			case BT_SHELL:
			{
				copystring(model, "weapons/shell");
				scale = shellsize / 24.f;
				int t = lastmillis-p->millis;
				if(t>p->timetolive-2000)
				{
					anim = ANIM_DECAY;
					basetime = p->millis+p->timetolive-2000;
					t -= p->timetolive-2000;
					o.z -= t*t/4000000000.0f*t;
				}
				break;
			}
			case BT_GIB:
			default:
			{
				uint n = (((4*(uint)(size_t)p)+(uint)p->timetolive)%3)+1;
				formatstring(model)("misc/gib0%u", n);
				int t = lastmillis-p->millis;
				if(t>p->timetolive-2000)
				{
					anim = ANIM_DECAY;
					basetime = p->millis+p->timetolive-2000;
					t -= p->timetolive-2000;
					o.z -= t*t/4000000000.0f*t;
				}
				break;
			}
		}
		path(model);
		if(p->bouncetype == BT_SHELL) sethudgunperspective(true);
		rendermodel(model, anim|ANIM_LOOP|ANIM_DYNALLOC, 0, PLAYERRADIUS, o, p->yaw+90, p->pitch, 0, basetime, NULL, NULL, scale);
		if(p->bouncetype == BT_SHELL) sethudgunperspective(false);
	}
}

VARP(gib, 0, 1, 1);
VARP(gibnum, 0, 6, 1000);
VARP(gibttl, 0, 7000, 60000);
VARP(gibspeed, 1, 30, 100);

void addgib(playerent *d){
	if(!d || !gib || !gibttl) return;
	playsound(S_GIB, d);

	loopi(gibnum)
	{
		bounceent *p = bounceents.add(new bounceent);
		p->owner = d;
		p->millis = lastmillis;
		p->timetolive = gibttl+rnd(10)*100;
		p->bouncetype = BT_GIB;

		p->o = d->o;
		p->o.z -= d->aboveeye;
		p->inwater = hdr.waterlevel>p->o.z;

		p->yaw = (float)rnd(360);
		p->pitch = (float)rnd(360);

		p->maxspeed = 30.0f;
		p->rotspeed = 3.0f;

		const float angle = (float)rnd(360);
		const float speed = (float)gibspeed;

		p->vel.x = sinf(RAD*angle)*rnd(1000)/1000.0f;
		p->vel.y = cosf(RAD*angle)*rnd(1000)/1000.0f;
		p->vel.z = rnd(1000)/1000.0f;
		p->vel.mul(speed/100.0f);

		p->resetinterp();
	}
}

void shorten(vec &from, vec &to, vec &target){
	target.sub(from).normalize().mul(from.dist(to)).add(from);
}

// weapon

weapon::weapon(struct playerent *owner, int type) : type(type), owner(owner), info(guns[type]),
	ammo(owner->ammo[type]), mag(owner->mag[type]), gunwait(owner->gunwait[type]), reloading(0){
}

const int weapon::scopetime = ADSTIME;

int weapon::flashtime() const { return min(max((int)info.attackdelay, 180)/3, 150); }

void weapon::sendshoot(vec &from, vec &to){
	if(owner!=player1 && owner->ownernum!=getclientnum()) return;
	to.sub(owner->o);
	addmsg(N_SHOOT, "ri3f3", owner->clientnum, lastmillis, owner->weaponsel->type, to.x, to.y, to.z);
}

bool weapon::modelattacking(){
	int animtime = min(owner->gunwait[owner->weaponsel->type], (int)owner->weaponsel->info.attackdelay);
	if(lastmillis - owner->lastaction < animtime) return true;
	else return false;
}

void weapon::attacksound(){
	if(info.sound == S_NULL) return;
	bool local = (owner == player1);
	if(!suppressed_gun(type)){
		owner->radarmillis = lastmillis;
		owner->lastloudpos[0] = owner->o.x;
		owner->lastloudpos[1] = owner->o.y;
		owner->lastloudpos[2] = owner->yaw;
	}
	playsound(info.sound, owner, local ? SP_HIGH : SP_NORMAL);
}

bool weapon::reload(){
	if(mag >= magsize(type) || ammo < reloadsize(type)) return false;
	updatelastaction(owner);
	reloading = lastmillis;
	gunwait += info.reloadtime;

	owner->ammo[type] -= reloadsize(type);
	owner->mag[type] = min<int>(magsize(type), owner->mag[type] + reloadsize(type));

	if(player1 == owner || owner->ownernum == getclientnum()) addmsg(N_RELOAD, "ri3", owner->clientnum, lastmillis, type);
	return true;
}

VARP(oldfashionedgunstats, 0, 0, 1);

void weapon::renderstats(){
	string gunstats, ammostr;
	const int clipsize = reloadsize(type);
	itoa(ammostr, (int)floor((float)ammo / clipsize));
	if(ammo % clipsize) formatstring(ammostr)("%s/%i", ammostr, ammo % clipsize);
	formatstring(gunstats)(oldfashionedgunstats ? "%i/%s" : "%i", mag, ammostr);
	draw_text(gunstats, 590, 823);
	if(!oldfashionedgunstats){
		int offset = text_width(gunstats);
		glScalef(0.5f, 0.5f, 1.0f);
		draw_textf("%s", (590 + offset)*2, 826*2, ammostr);
	}
}

void weapon::attackphysics(vec &from, vec &to) // physical fx to the owner
{
	vec unitv;
	const float dist = to.dist(from, unitv);
	const float kick = dynrecoil() * -0.01f / dist;

	// kickback
	owner->vel.add(vec(unitv).mul(kick * owner->eyeheight / owner->maxeyeheight));
	// recoil
	const float recoilshift = (rnd(info.recoilangle * 20 + 1) / 10.f - info.recoilangle) * RAD, recoilval = info.recoil * sqrtf(rnd(50) + 51) / 100.f;
	owner->pitchvel += cosf(recoilshift) * recoilval;
	owner->yawvel += sinf(recoilshift) * recoilval;
	const float maxmagnitude = sqrtf(owner->pitchvel * owner->pitchvel + owner->yawvel + owner->yawvel) / info.maxrecoil * (owner->perk == PERK_HAND ? 7 : 10);
	if(maxmagnitude > 1){
		owner->pitchvel /= maxmagnitude;
		owner->yawvel /= maxmagnitude;
	}
}

void weapon::attackhit(const vec &o){
	particle_splash(0, 5, 250, o);
}

VARP(lefthand, 0, 0, 1);

void weapon::renderhudmodel(int lastaction, bool akimboflip){
	vec unitv;
	float dist = worldpos.dist(owner->o, unitv);
	unitv.div(dist);

	const bool flip = akimboflip ^ (lefthand > 0);
	weaponmove wm;
	if(!intermission) wm.calcmove(unitv, lastaction);
	defformatstring(path)("weapons/%s", info.modelname);
	const bool emit = ((wm.anim&ANIM_INDEX)==ANIM_WEAP_SHOOT) && (lastmillis - lastaction) < flashtime();
	if(ads_gun(type) && (wm.anim&ANIM_INDEX)==ANIM_WEAP_SHOOT){ wm.anim = ANIM_WEAP_IDLE; }
	rendermodel(path, wm.anim|ANIM_DYNALLOC|(flip ? ANIM_MIRROR : 0)|(emit ? ANIM_PARTICLE : 0), 0, -1, wm.pos, owner->yaw+90, owner->pitch+wm.k_rot, 40.0f, wm.basetime, NULL, NULL, 1.28f);
}

void weapon::updatetimers(){
	if(gunwait) gunwait = max(gunwait - (lastmillis-owner->lastaction), 0);
}

void weapon::onselecting(){
	updatelastaction(owner);
	playsound(S_GUNCHANGE, owner, owner == player1 ? SP_HIGH : SP_NORMAL);
}

void weapon::renderhudmodel() { renderhudmodel(owner->lastaction); }
void weapon::renderaimhelp(int teamtype) { if(owner->ads != 1000) drawcrosshair(owner, CROSSHAIR_DEFAULT, teamtype); }
int weapon::dynspread() {
	if(info.spread <= 1) return 1;
	return (int)(info.spread * (owner->vel.magnitude() / 3.f + owner->pitchvel / 5.f + 0.4f) * 2.4f * owner->eyeheight / owner->maxeyeheight * (1 - sqrtf(owner->ads / 1000.f)));
}
float weapon::dynrecoil() { return info.kick * (1 - owner->ads / 2000.f); }
bool weapon::selectable() { return this != owner->weaponsel && owner->state == CS_ALIVE && !owner->weaponchanging; }
bool weapon::deselectable() { return !reloading; }

void weapon::equipplayer(playerent *pl){
	if(!pl) return;
	pl->weapons[WEAP_ASSAULT] = new assaultrifle(pl);
	pl->weapons[WEAP_GRENADE] = new grenades(pl);
	pl->weapons[WEAP_KNIFE] = new knife(pl);
	pl->weapons[WEAP_PISTOL] = new pistol(pl);
	pl->weapons[WEAP_SHOTGUN] = new shotgun(pl);
	pl->weapons[WEAP_BOLT] = new boltrifle(pl);
	pl->weapons[WEAP_SNIPER] = new sniperrifle(pl);
	pl->weapons[WEAP_SUBGUN] = new subgun(pl);
	pl->weapons[WEAP_AKIMBO] = new akimbo(pl);
	pl->weapons[WEAP_HEAL] = new heal(pl);
	pl->weapons[WEAP_SWORD] = new wavegun(pl);
	pl->weapons[WEAP_BOW] = new crossbow(pl);
	pl->selectweapon(WEAP_ASSAULT);
	pl->setprimary(WEAP_ASSAULT);
	pl->setnextprimary(WEAP_ASSAULT);
}

bool weapon::valid(int id) { return id>=0 && id<WEAP_MAX; }

// grenadeent

enum { NS_NONE, NS_ACTIVATED = 0, NS_THROWED, NS_EXPLODED };

grenadeent::grenadeent(playerent *owner, int millis){
	ASSERT(owner);
	nadestate = NS_NONE;
	local = owner==player1 || owner->ownernum == getclientnum();
	bounceent::owner = owner;
	bounceent::millis = id = lastmillis;
	timetolive = NADETTL-millis;
	bouncetype = BT_NADE;
	maxspeed = 30.0f;
	rotspeed = 6.0f;
	distsincebounce = 0.0f;
}

grenadeent::~grenadeent(){
	if(owner && owner->weapons[WEAP_GRENADE]) owner->weapons[WEAP_GRENADE]->removebounceent(this);
}

void grenadeent::explode(){
	if(nadestate!=NS_ACTIVATED && nadestate!=NS_THROWED ) return;
	nadestate = NS_EXPLODED;
	if(local) addmsg(N_PROJ, "ri4f3", owner->clientnum, lastmillis, WEAP_GRENADE, id, o.x, o.y, o.z);
}

void grenadeent::activate(){
	if(nadestate!=NS_NONE) return;
	nadestate = NS_ACTIVATED;

	if(local){
		addmsg(N_SHOOTC, "ri3", owner->clientnum, millis, WEAP_GRENADE);
		playsound(S_GRENADEPULL, SP_HIGH);
	}
}

void grenadeent::_throw(const vec &from, const vec &vel){
	if(nadestate!=NS_ACTIVATED) return;
	nadestate = NS_THROWED;
	this->vel = vel;
	o = from;
	resetinterp();
	inwater = hdr.waterlevel>o.z;

	if(local){
		addmsg(N_THROWNADE, "rif6i", owner->clientnum, o.x, o.y, o.z, vel.x, vel.y, vel.z, lastmillis-millis);
		playsound(S_GRENADETHROW, SP_HIGH);
	}
	else playsound(S_GRENADETHROW, owner);
}

void grenadeent::moveoutsidebbox(const vec &direction, playerent *boundingbox){
	vel = direction;
	o = boundingbox->o;
	inwater = hdr.waterlevel>o.z;

	boundingbox->cancollide = false;
	loopi(10) moveplayer(this, 10, true, 10);
	boundingbox->cancollide = true;
}

void grenadeent::destroy() { explode(); }
bool grenadeent::applyphysics() { return nadestate==NS_THROWED; }

void grenadeent::oncollision(){
	if(distsincebounce>=1.5f) playsound(S_GRENADEBOUNCE1+rnd(2), &o);
	distsincebounce = 0.0f;
}

void grenadeent::onmoved(const vec &dist){
	distsincebounce += dist.magnitude();
}

// grenades

grenades::grenades(playerent *owner) : weapon(owner, WEAP_GRENADE), inhandnade(NULL), throwwait(325), state(GST_NONE) {}

int grenades::flashtime() const { return 0; }

bool grenades::busy() { return state!=GST_NONE; }

bool grenades::attack(vec &targ){
	int attackmillis = lastmillis-owner->lastaction;
	//vec &to = targ;

	bool waitdone = attackmillis>=gunwait;
	if(waitdone) gunwait = reloading = 0;

	switch(state)
	{
		case GST_NONE:
			if(waitdone && owner->attacking && this==owner->weaponsel) activatenade(); // activate
			break;

		case GST_INHAND:
			if(waitdone)
			{
				if(!owner->attacking || this!=owner->weaponsel) thrownade(); // throw
				else if(!inhandnade->isalive(lastmillis)) dropnade(); // drop & have fun
			}
			break;

		case GST_THROWING:
			if(attackmillis >= throwwait) // throw done
			{
				reset();
				if(!mag && this==owner->weaponsel) // switch to primary immediately
				{
					addmsg(N_QUICKSWITCH, "ri", owner->clientnum);
					owner->weaponchanging = lastmillis-1-(weaponchangetime/2);
					owner->nextweaponsel = owner->weaponsel = owner->primweap;
				}
				return false;
			}
			break;
	}
	return true;
}

void grenades::attackfx(const vec &from, const vec &to, int millis) // other player's grenades
{
	if(millis < 0){ // activate
		state = GST_INHAND;
		playsound(S_GRENADEPULL, owner, SP_HIGH);
	}
	else /*if(millis > 0)*/ { // throw
		grenadeent *g = new grenadeent(owner, millis);
		state = GST_THROWING;
		bounceents.add(g);
		g->_throw(from, to);
	}
}

GLuint flashtex;
void flashme(float dist){
	gamefocus->flashmillis = lastmillis + 3000 - sqrtf(dist) * 300;
	// store last render
	SDL_Surface *image = SDL_CreateRGBSurface(SDL_SWSURFACE, screen->w, screen->h, 24, 0x0000FF, 0x00FF00, 0xFF0000, 0);
	if(!image){
		if(flashtex) glDeleteTextures(1, &flashtex);
		flashtex = 0;
	}
	else{
		uchar *tmp = new uchar[screen->w*screen->h*3];
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, screen->w, screen->h, GL_RGB, GL_UNSIGNED_BYTE, tmp);
		uchar *dst = (uchar *)image->pixels;
		loopi(screen->h){
			loopj(screen->w*3) dst[j] = ~tmp[3*screen->w*(screen->h-i-1) + j];
			endianswap(dst, 3, screen->w);
			dst += image->pitch;
		}
		delete[] tmp;
		// make tex
		if(!flashtex) glGenTextures(1, &flashtex);
		if(flashtex){
			extern GLenum texformat(int bpp);
			createtexture(flashtex, image->w, image->h, image->pixels, 0, false, texformat(image->format->BitsPerPixel), 0);
			SDL_FreeSurface(image);
		}
	}
}
COMMAND(flashme, ARG_NONE);

void explosioneffect(const vec &o){
	particle_splash(0, 50, 300, o);
	adddynlight(NULL, o, 16, 200, 100, 255, 255, 224);
	adddynlight(NULL, o, 16, 600, 600, 192, 160, 128);
	playsound(S_FEXPLODE, &o);
}

void grenades::attackhit(const vec &o){
	particle_fireball(5, o, owner);
	addscorchmark(o);
	explosioneffect(o);
	extern int shotline, shotlinettl;
	extern void newparticle(const vec &o, const vec &d, int fade, int type);
	if(shotline && shotlinettl) loopi(9) loopj(9) loopk(9){
		vec t(i/4.5f-1, j/4.5f-1, k/4.5f-1);
		t.add(o);
		traceShot(o, t);
		//addshotline(owner, o, t);
		newparticle(o, t, shotlinettl, 6);
		particle_splash(0, 8, 250, t);
	}
	//if(gamefocus->state == CS_ALIVE && gamefocus->o.dist(o) < 30.f) flashme(gamefocus->o.dist(o));
}

int grenades::modelanim(){
	if(state == GST_THROWING) return ANIM_WEAP_THROW;
	else
	{
		int animtime = min(gunwait, (int)info.attackdelay);
		if(state == GST_INHAND || lastmillis - owner->lastaction < animtime) return ANIM_WEAP_SHOOT;
	}
	return ANIM_WEAP_IDLE;
}

void grenades::activatenade(){
	if(!mag) return;

	inhandnade = new grenadeent(owner);
	bounceents.add(inhandnade);

	updatelastaction(owner);
	mag--;
	gunwait = info.attackdelay;
	owner->lastattackweapon = this;
	state = GST_INHAND;
	inhandnade->activate();
}

void grenades::thrownade(){
	if(!inhandnade) return;
	vec vel(sinf(RAD*owner->yaw) * cosf(RAD*owner->pitch), -cosf(RAD*owner->yaw)* cosf(RAD*owner->pitch), sinf(RAD*owner->pitch));
	vel.mul(NADEPOWER);
	thrownade(vel);
}

void grenades::thrownade(const vec &vel){
	inhandnade->moveoutsidebbox(vel, owner);
	inhandnade->_throw(inhandnade->o, vel);
	inhandnade = NULL;

	updatelastaction(owner);
	state = GST_THROWING;
	if(this==owner->weaponsel) owner->attacking = false;
}

void grenades::dropnade(){
	vec n(0,0,0);
	thrownade(n);
}

void grenades::renderstats(){ draw_textf("%i", 830, 823, mag); }

bool grenades::selectable() { return weapon::selectable() && state != GST_INHAND && mag; }
void grenades::reset() { state = GST_NONE; }

void grenades::onselecting() { reset(); weapon::onselecting(); }
void grenades::onownerdies(){
	reset();
	if(owner==player1 && inhandnade) dropnade();
}

void grenades::removebounceent(bounceent *b){
	if(b == inhandnade) { inhandnade = NULL; reset(); }
}

VARP(burst, 0, 0, 10);
VARP(burstfull, 0, 1, 1); // full burst before stopping

// gun base class

gun::gun(playerent *owner, int type) : weapon(owner, type) {}

bool gun::attack(vec &targ){
	if(type == WEAP_SHOTGUN && owner == player1 && player1->attacking) ((shotgun *)this)->autoreloading = false;

	const int attackmillis = lastmillis-owner->lastaction;
	if(attackmillis<gunwait) return false;
	gunwait = reloading = 0;

	if(!owner->attacking)
	{
		shots = 0;
		checkautoreload();
		return false;
	}

	updatelastaction(owner);
	if(!mag)
	{
		gunwait += 250;
		owner->lastattackweapon = NULL;
		shots = 0;
		if(!checkautoreload()){
			if(!m_nopistol && owner->weapons[WEAP_PISTOL]->mag || owner->weapons[WEAP_PISTOL]->ammo) selectweapon(owner->weapons[WEAP_PISTOL]);
			else{
				playsound(S_NOAMMO, owner);
				addmsg(N_PHYS, "ri2", owner->clientnum, PHYS_NOAMMO);
			};
		}
		return false;
	}

	shots++;
	owner->lastattackweapon = this;
	if(!info.isauto || ((type == WEAP_ASSAULT || type == WEAP_SUBGUN) && burst && shots >= burst)) owner->attacking = false;

	vec from = owner->o;
	vec to = targ;
	from.z -= WEAPONBELOWEYE;

	attackphysics(from, to);

	gunwait = info.attackdelay;
	mag--;

	sendshoot(from, to);
	return true;
}

VARP(shellttl, 0, 4000, 20000);

void gun::attackshell(const vec &to){
	if(!shellttl) return;
	bounceent *s = bounceents.add(new bounceent);
	s->owner = owner;
	s->millis = lastmillis;
	s->timetolive = gibttl;
	s->bouncetype = BT_SHELL;
	
	const bool akimboflip = (type == WEAP_AKIMBO && ((akimbo *)this)->akimboside) ^ (lefthand > 0);
	s->vel = vec(1, rnd(101) / 800.f - .1f, (rnd(51) + 50) / 100.f);
	s->vel.rotate_around_z(owner->yaw*RAD);
	s->o = owner->o;
	vec *ejecttrans = hudEject(owner, akimboflip);
	if(ejecttrans) s->o.add(*ejecttrans);
	else s->o.add(vec(s->vel.x * owner->radius, s->vel.y * owner->radius, -WEAPONBELOWEYE));
	s->vel.mul(.02f * (rnd(3) + 5));
	if(akimboflip) s->vel.rotate_around_z(180*RAD);
	s->inwater = hdr.waterlevel > owner->o.z;
	s->cancollide = false;

	s->yaw = owner->yaw+180;
	s->pitch = -owner->pitch;

	s->maxspeed = 30.f;
	s->rotspeed = 3.f;

	s->resetinterp();
}

void gun::attackfx(const vec &from2, const vec &too, int millis){
	// trace shot
	vec from(from2), to(too);
	traceShot(from, to);
	if(millis & 1){
		from.z -= WEAPONBELOWEYE;
		attackshell(to);
	}
	addbullethole(owner, from, to);
	addshotline(owner, from, to, millis & 1);
	particle_splash(0, 5, 250, to);
	adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
	if(millis & 1) attacksound();
}

int gun::modelanim() { return modelattacking() ? ANIM_WEAP_SHOOT|ANIM_LOOP : ANIM_WEAP_IDLE; }
bool gun::checkautoreload() { if(autoreload && owner==player1 && !mag && ammo) { tryreload(owner); return true; } return false; }


// shotgun

shotgun::shotgun(playerent *owner) : gun(owner, WEAP_SHOTGUN), autoreloading(false) {}

void shotgun::attackfx(const vec &from2, const vec &to, int millis){
	vec from(from2);
	from.z -= WEAPONBELOWEYE;
	loopi(SGRAYS) particle_splash(0, 5, 200, sg[i]);
	uchar filter = 0;
	if(addbullethole(owner, from, to)) loopi(SGRAYS){
		if(filter++ % 4){
			addshotline(owner, from, sg[i], 2);
		}
		if(filter >= 4) filter = 0;
		traceShot(from, sg[i]);
		addbullethole(owner, from, sg[i], 0, false);
	}
	if(millis & 1) attackshell(to);
	adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
	attacksound();
}

bool shotgun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }

void shotgun::renderaimhelp(int teamtype){ drawcrosshair(owner, CROSSHAIR_SHOTGUN, teamtype); }

bool shotgun::reload(){
	if(owner == player1) autoreloading = mag < magsize(type) && ammo;
	if(!gun::reload()) return false;
	return true;
}

bool shotgun::checkautoreload() {
	if(owner != player1 || !autoreload) return false;
	if(!mag && ammo) autoreloading = true;
	if(autoreloading) tryreload(owner);
	else return gun::checkautoreload();
	return true;
}

// subgun

subgun::subgun(playerent *owner) : gun(owner, WEAP_SUBGUN) {}
bool subgun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }

// wavegun

wavegun::wavegun(playerent *owner) : gun(owner, WEAP_SWORD) {}
bool wavegun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }

void wavegun::attackfx(const vec &from2, const vec &to, int millis){
	vec from(from2);
	if(millis & 1) from.z -= WEAPONBELOWEYE;
	addbullethole(owner, from, to);
	particle_splash(0, 50, 200, to);
	//attacksound();
}

// crossbow

vector<cstick> sticks;

crossbow::crossbow(playerent *owner) : gun(owner, WEAP_BOW) {}
bool crossbow::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }

void crossbow::attackfx(const vec &from2, const vec &too, int millis){
	vec from(from2), to(too);
	traceShot(from, to);
	from.z -= WEAPONBELOWEYE;

	addshotline(owner, from, to, 2);
	particle_trail(15, 400, from, to);
	particle_splash(0, 5, 250, to);
	attacksound();
}

void crossbow::attackhit(const vec &o){
	particle_fireball(13, o, owner);
	explosioneffect(o);
}

// scopedprimary
scopedprimary::scopedprimary(playerent *owner, int type) : gun(owner, type) {}

bool scopedprimary::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }
void scopedprimary::attackfx(const vec &from2, const vec &to, int millis){
	vec from(from2);
	if(millis & 1){
		from.z -= WEAPONBELOWEYE;
		attackshell(to);
	}
	addbullethole(owner, from, to);
	addshotline(owner, from, to, 2);
	particle_splash(0, 50, 200, to);
	particle_trail(1, 500, from, to);
	adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
	attacksound();
}

float scopedprimary::dynrecoil() { return weapon::dynrecoil() * 1 - owner->ads / 1500; } // 1/3 spread when ADS
void scopedprimary::renderhudmodel() { if(owner->ads < adsscope) weapon::renderhudmodel(); }

void scopedprimary::renderaimhelp(int teamtype){
	if(owner->ads >= adsscope){ drawscope(); drawcrosshair(owner, CROSSHAIR_SCOPE, teamtype, NULL, 24.0f); }
	else weapon::renderaimhelp(teamtype);
}

// sniperrifle
sniperrifle::sniperrifle(playerent *owner) : scopedprimary(owner, WEAP_SNIPER) {}

// boltrifle
boltrifle::boltrifle(playerent* owner) : scopedprimary(owner, WEAP_BOLT) {}

// assaultrifle

assaultrifle::assaultrifle(playerent *owner) : gun(owner, WEAP_ASSAULT) {}

float assaultrifle::dynrecoil() { return weapon::dynrecoil() + (rnd(8)*-0.01f); }
bool assaultrifle::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }


// pistol

pistol::pistol(playerent *owner) : gun(owner, WEAP_PISTOL) {}
bool pistol::selectable() { return weapon::selectable() && !m_nopistol; }


// akimbo

akimbo::akimbo(playerent *owner) : gun(owner, WEAP_AKIMBO), akimbomillis(0){
	akimbolastaction[0] = akimbolastaction[1] = 0;
}

bool akimbo::attack(vec &targ){
	if(gun::attack(targ))
	{
		akimbolastaction[akimboside?1:0] = lastmillis;
		akimboside = !akimboside;
		return true;
	}
	return false;
}

void akimbo::onammopicked(){
	akimbomillis = lastmillis + 30000;
	if(owner==player1 || owner->ownernum == getclientnum())
	{
		if(owner->weaponsel->type!=WEAP_SNIPER && owner->weaponsel->type!=WEAP_GRENADE) owner->weaponswitch(this);
		addmsg(N_AKIMBO, "ri2", owner->clientnum, lastmillis);
	}
}

void akimbo::onselecting(){
	gun::onselecting();
	akimbolastaction[0] = akimbolastaction[1] = lastmillis;
}

bool akimbo::selectable() { return weapon::selectable() && !m_nopistol && owner->akimbo; }
void akimbo::updatetimers() { weapon::updatetimers(); /*loopi(2) akimbolastaction[i] = lastmillis;*/ }
void akimbo::reset() { akimbolastaction[0] = akimbolastaction[1] = akimbomillis = 0; akimboside = false; }

void akimbo::renderhudmodel(){
	weapon::renderhudmodel(*akimbolastaction, false);
	weapon::renderhudmodel(akimbolastaction[1], true);
}

bool akimbo::timerout() { return akimbomillis && akimbomillis <= lastmillis; }

// heal

heal::heal(playerent *owner) : gun(owner, WEAP_HEAL) {}

int heal::flashtime() const { return 0; }

bool heal::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }

//void heal::renderaimhelp(int teamtype){ if(state) weapon::renderaimhelp(teamtype); }
void heal::attackfx(const vec &from2, const vec &too, int millis){
	vec from(from2), to(too);
	traceShot(from, to);
	from.z -= WEAPONBELOWEYE;

	addshotline(owner, from, to, 2);
	particle_trail(14, 400, from, to);
	particle_splash(0, 3, 200, to);
	attacksound();
}

void heal::attackhit(const vec &o){
	particle_splash(0, 50, 300, o);
}

vector<cknife> knives;

// knifeent
knifeent::knifeent(playerent *owner, int millis){
	ASSERT(owner);
	knifestate = NS_NONE;
	local = owner==player1 || owner->ownernum == getclientnum();
	bounceent::owner = owner;
	bounceent::millis = lastmillis;
	timetolive = KNIFETTL-millis;
	bouncetype = BT_KNIFE;
	maxspeed = 25.0f;
	radius = .1f;
	aboveeye = .7f;
	eyeheight = .3f;
	yaw = owner->yaw+180;
	pitch = 75-owner->pitch;
	roll = owner->roll;
	rotspeed = 0;
}

knifeent::~knifeent(){
	//if(owner && owner->weapons[WEAP_KNIFE]) owner->weapons[WEAP_KNIFE]->removebounceent(this);
}

void knifeent::explode(){
	if(knifestate!=NS_ACTIVATED && knifestate!=NS_THROWED ) return;
	knifestate = NS_EXPLODED;
	static vec n(0,0,0);
	if(local){
		extern playerent *tkhit;
		addmsg(N_PROJ, "ri4f3", owner->clientnum, lastmillis, WEAP_KNIFE, tkhit ? tkhit->clientnum : -1, o.x, o.y, o.z);
	}
	playsound(S_GRENADEBOUNCE1+rnd(2), &o);
	destroy();
}

void knifeent::activate(){
	if(knifestate!=NS_NONE) return;
	knifestate = NS_ACTIVATED;

	if(local){
		addmsg(N_SHOOTC, "ri3", owner->clientnum, millis, WEAP_KNIFE);
		//playsound(S_KNIFEPULL, SP_HIGH);
	}
}

void knifeent::_throw(const vec &from, const vec &vel){
	if(knifestate!=NS_ACTIVATED) return;
	knifestate = NS_THROWED;
	this->vel = vel;
	o = from;
	resetinterp();
	inwater = hdr.waterlevel>o.z;

	if(local) addmsg(N_THROWKNIFE, "rif6", owner->clientnum, o.x, o.y, o.z, vel.x, vel.y, vel.z);
	playsound(S_GRENADETHROW, SP_HIGH);
}

void knifeent::moveoutsidebbox(const vec &direction, playerent *boundingbox){
	vel = direction;
	o = boundingbox->o;
	inwater = hdr.waterlevel>o.z;

	boundingbox->cancollide = false;
	loopi(10) moveplayer(this, 10, true, 10);
	boundingbox->cancollide = true;
}

void knifeent::destroy() { timetolive = 0; }
bool knifeent::applyphysics() { return knifestate==NS_THROWED; }

void knifeent::oncollision(){ explode(); }

// knife

knife::knife(playerent *owner) : weapon(owner, WEAP_KNIFE), inhandknife(NULL), state(GST_NONE) {}

int knife::flashtime() const { return 0; }

//bool knife::busy(){}

bool knife::attack(vec &targ){
	int attackmillis = lastmillis-owner->lastaction;
	if(owner->scoping || state){
		const bool waitdone = attackmillis >= 500;
		switch(state){
			case GST_NONE:
				if(waitdone && owner->scoping && this==owner->weaponsel) activateknife(); // activate
				break;
			case GST_INHAND:
				if(waitdone){
					if(!owner->scoping || this!=owner->weaponsel) throwknife(); // throw
					else if(!inhandknife->isalive(lastmillis)) throwknife(true);
				}
				break;
			case GST_THROWING:
				if(attackmillis >= 250){
					reset();
					if(!ammo){
						addmsg(N_QUICKSWITCH, "ri", owner->clientnum);
						owner->weaponchanging = lastmillis-1-(weaponchangetime/2);
						owner->nextweaponsel = owner->weaponsel = owner->primweap;
					}
					return false;
				}
				break;
		}
		return true;
	}
	if(attackmillis<gunwait) return false;
	gunwait = reloading = 0;

	if(!owner->attacking) return false;
	updatelastaction(owner);

	owner->lastattackweapon = this;
	owner->attacking = info.isauto;

	vec from = owner->o;
	vec to = targ;
	from.z -= WEAPONBELOWEYE;

	vec unitv;
	float dist = to.dist(from, unitv);
	unitv.div(dist);
	unitv.mul(guns[WEAP_KNIFE].endrange);
	to = from;
	to.add(unitv);

	sendshoot(from, to);
	gunwait = info.attackdelay;
	return true;
}

void knife::reset() { state = GST_NONE; }
bool knife::selectable() { return weapon::selectable() && mag; }
int knife::modelanim() { 
	if(state == GST_THROWING) return ANIM_WEAP_THROW;
	else{
		//int animtime = min(gunwait, (int)info.attackdelay);
		if(state == GST_INHAND /*|| lastmillis - owner->lastaction < animtime*/) return ANIM_WEAP_RELOAD;
	}
	return modelattacking() ? ANIM_WEAP_SHOOT : ANIM_WEAP_IDLE;
}

void knife::onownerdies(){
	reset();
	if(owner == player1 && inhandknife) throwknife(true); // muscle spasm
}

void knife::activateknife(){
	if(!ammo) return;

	inhandknife = new knifeent(owner);
	bounceents.add(inhandknife);

	updatelastaction(owner);
	ammo--;
	gunwait = info.attackdelay;
	owner->lastattackweapon = this;
	state = GST_INHAND;
	inhandknife->activate();
}

void knife::throwknife(bool weak){
	if(!inhandknife) return;
	vec vel(sinf(RAD*owner->yaw) * cosf(RAD*owner->pitch), -cosf(RAD*owner->yaw) * cosf(RAD*owner->pitch), sinf(RAD*owner->pitch));
	vel.mul(weak ? NADEPOWER : KNIFEPOWER);
	throwknife(vel);
}

void knife::throwknife(const vec &vel){
	inhandknife->moveoutsidebbox(vel, owner);
	inhandknife->_throw(inhandknife->o, vel);
	inhandknife = NULL;

	updatelastaction(owner);
	state = GST_THROWING;
	if(this==owner->weaponsel) owner->attacking = false;
}

//void knife::renderaimhelp(int teamtype){ if(state) weapon::renderaimhelp(teamtype); }
void knife::attackfx(const vec &from, const vec &to, int millis) {
	if(from.iszero() && to.iszero() && millis < 0){
		state = GST_INHAND;
		//playsound(S_GRENADEPULL, owner, SP_HIGH);
	}
	else if(millis == 1){
		knifeent *g = new knifeent(owner);
		state = GST_THROWING;
		bounceents.add(g);
		g->_throw(from, to);
	}
	else attacksound();
}
void knife::attackhit(const vec &o){
	particle_splash(0, 50, 300, o);
}
void knife::renderstats() { draw_textf("%i", 590, 823, ammo); }

// setscope for snipers and iron sights
void setscope(bool enable){
	if(!player1->state == CS_ALIVE) return;
	if(player1->weaponsel->type == WEAP_KNIFE){
		player1->scoping = enable;
	}
	else if(ads_gun(player1->weaponsel->type)){
		player1->scoping = enable;
		addmsg(N_SCOPE, "ri", enable ? 1 : 0);
	}
}

COMMAND(setscope, ARG_1INT);


void shoot(playerent *p, vec &targ){
	if(p->state==CS_DEAD || p->weaponchanging) return;
	weapon *weap = p->weaponsel;
	if(weap){
		weap->attack(targ);
		loopi(WEAP_MAX){
			weapon *bweap = p->weapons[i];
			if(bweap != weap && bweap->busy()) bweap->attack(targ);
		}
	}
}

void checkakimbo(){
	if(player1->akimbo)
	{
		akimbo &a = *((akimbo *)player1->weapons[WEAP_AKIMBO]);
		if(a.timerout())
		{
			weapon &p = *player1->weapons[WEAP_PISTOL];
			player1->akimbo = false;
			a.reset();
			// transfer ammo to pistol
			p.mag = min((int)p.info.magsize, max(a.mag, p.mag));
			p.ammo = max(p.ammo, p.ammo);
			// fix akimbo magcontent
			a.mag = 0;
			a.ammo = 0;
			if(player1->weaponsel->type==WEAP_AKIMBO){
				if(player1->primweap) player1->weaponswitch(player1->primweap);
				else player1->weaponswitch(&p);
			}
			playsound(S_AKIMBOOUT, player1);
			addmsg(N_PHYS, "ri2", player1->clientnum, PHYS_AKIMBOOUT);
		}
	}
}