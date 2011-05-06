VARP(nosway, 0, 0, 1);
VARP(swayspeeddiv, 1, 105, 1000);
VARP(swaymovediv, 1, 200, 1000); 
VARP(swayupspeeddiv, 1, 105, 1000);
VARP(swayupmovediv, 1, 200, 1000); 

struct weaponmove
{
	static vec swaydir;
	static int swaymillis, lastsway;

	float k_rot, kick;
	vec pos;
	int anim, basetime;
	
	weaponmove() : k_rot(0), kick(0), anim(0), basetime(0) { pos.x = pos.y = pos.z = 0.0f; }

	void calcmove(vec aimdir, int lastaction)
	{
		kick = k_rot = 0.0f;
		pos = gamefocus->o;
		
		if(!nosway)
		{
			float k = pow(0.7f, (lastmillis-lastsway)/10.0f);
			swaydir.mul(k);
			vec dv(gamefocus->vel);
			dv.mul((1-k)/max(gamefocus->vel.magnitude(), gamefocus->maxspeed));
			dv.x *= 1.5f;
			dv.y *= 1.5f;
			dv.z *= 0.4f;
			swaydir.add(dv);
		}

		if(gamefocus->onfloor || gamefocus->onladder || gamefocus->inwater) swaymillis += lastmillis-lastsway;
		lastsway = lastmillis;

		anim = ANIM_GUN_IDLE;

		float k_back = 0.0f; vec sway = aimdir;
		if(nosway) sway.x = sway.y = sway.z = 0;
		else{
			float swayspeed = sinf((float)swaymillis/swayspeeddiv)/(swaymovediv/10.0f);
			float swayupspeed = cosf((float)swaymillis/swayupspeeddiv)/(swayupmovediv/10.0f);

			float plspeed = min(1.0f, sqrtf(gamefocus->vel.x*gamefocus->vel.x + gamefocus->vel.y*gamefocus->vel.y));
				
			swayspeed *= plspeed/2;
			swayupspeed *= plspeed/2;

			swap(sway.x, sway.y);
			sway.y = -sway.y;
				
			swayupspeed = fabs(swayupspeed); // sway a semicirle only
			sway.z = 1.0f;
				
			sway.x *= swayspeed;
			sway.y *= swayspeed;
			sway.z *= swayupspeed;

			sway.mul(gamefocus->eyeheight / gamefocus->maxeyeheight);
		}

		if(gamefocus->weaponchanging){
			basetime = ads_gun(gamefocus->weaponsel->type) ? lastmillis : gamefocus->weaponchanging;
			float progress = clamp((lastmillis - gamefocus->weaponchanging)/(float)weapon::weaponchangetime, 0.0f, 1.0f);
			k_rot = -90*sinf(progress*M_PI);
		}
		else if(gamefocus->weaponsel->reloading){
			anim = ANIM_GUN_RELOAD;
			basetime = gamefocus->weaponsel->reloading;
			if(gamefocus->weaponsel->type == GUN_AKIMBO){
				float reloadtime = (float)gamefocus->weaponsel->info.reloadtime,
					progress = clamp((lastmillis - gamefocus->weaponsel->reloading)/reloadtime, 0.0f,
						clamp(1.0f - (gamefocus->lastaction + gamefocus->weaponsel->gunwait - lastmillis)/reloadtime, 0.5f, 1.0f));
				if((progress -= .4f) > 0){
					progress /= .6f;
					k_rot = -90 * sinf(progress*M_PI);
				}
			}
		}
		else{
			basetime = lastaction;

			int timediff = lastmillis-lastaction, 
				animtime = min(gamefocus->weaponsel->gunwait, (int)gamefocus->weaponsel->info.attackdelay);
			
			float progress = 0.0f;
			
			if(gamefocus->weaponsel==gamefocus->lastattackweapon){
				progress = max(0.0f, min(1.0f, timediff/(float)animtime));
				// f(x) = -sin(x-1.5)^3
				kick = -sinf(pow((1.5f*progress)-1.5f,3));
				kick *= gamefocus->eyeheight / gamefocus->maxeyeheight;
				if(gamefocus->lastaction) anim = gamefocus->weaponsel->modelanim();
			}
			
			if(gamefocus->weaponsel->info.mdl_kick_rot || gamefocus->weaponsel->info.mdl_kick_back){
				k_rot = gamefocus->weaponsel->info.mdl_kick_rot*kick;
				k_back = gamefocus->weaponsel->info.mdl_kick_back*kick/10;
			}
			
			if((anim&ANIM_INDEX) == ANIM_GUN_IDLE || (anim&ANIM_INDEX) == ANIM_GUN_SHOOT)
				basetime = lastmillis-gamefocus->ads;
			if(ads_gun(gamefocus->weaponsel->type)){
				k_rot *= 1 - gamefocus->ads / 2000.f;
				k_back *= 1 - gamefocus->ads / 1100.f;
				sway.mul(1 - gamefocus->ads / 1200.f);
				swaydir.mul(1 - gamefocus->ads / 1300.f);
			}
		}

		pos.add(swaydir);
		pos.x -= aimdir.x*k_back+sway.x;
		pos.y -= aimdir.y*k_back+sway.y;
		pos.z -= aimdir.z*k_back+sway.z;
	}
};

vec weaponmove::swaydir(0, 0, 0);
int weaponmove::lastsway = 0, weaponmove::swaymillis = 0;

void preload_hudguns()
{
	loopi(NUMGUNS){
		s_sprintfd(path)("weapons/%s", guns[i].modelname);
		loadmodel(path);
	}
	loadmodel("weapons/shell");
}

