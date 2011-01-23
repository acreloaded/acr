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
		pos = player1->o;
		
		if(!nosway)
		{
			float k = pow(0.7f, (lastmillis-lastsway)/10.0f);
			swaydir.mul(k);
			vec dv(player1->vel);
			dv.mul((1-k)/max(player1->vel.magnitude(), player1->maxspeed));
			dv.x *= 1.5f;
			dv.y *= 1.5f;
			dv.z *= 0.4f;
			swaydir.add(dv);
		}

		if(player1->onfloor || player1->onladder || player1->inwater) swaymillis += lastmillis-lastsway;
		lastsway = lastmillis;

		anim = ANIM_GUN_IDLE;
		if(player1->weaponchanging){
			basetime = ads_gun(player1->weaponsel->type) ? lastmillis : player1->weaponchanging;
			float progress = clamp((lastmillis - player1->weaponchanging)/(float)weapon::weaponchangetime, 0.0f, 1.0f);
			k_rot = -90*sinf(progress*M_PI);
		}
		else if(player1->weaponsel->reloading){
			anim = ANIM_GUN_RELOAD;
			basetime = player1->weaponsel->reloading;
			/*
			float reloadtime = (float)player1->weaponsel->info.reloadtime,
				  progress = clamp((lastmillis - player1->weaponsel->reloading)/reloadtime, 0.0f, clamp(1.0f - (player1->lastaction + player1->weaponsel->gunwait - lastmillis)/reloadtime, 0.5f, 1.0f));
			k_rot = -90*sinf(progress*M_PI);*/
		}
		else{
			basetime = lastaction;

			int timediff = lastmillis-lastaction, 
				animtime = min(player1->weaponsel->gunwait, (int)player1->weaponsel->info.attackdelay);
			vec sway = aimdir;
			float progress = 0.0f;
			float k_back = 0.0f;
			
			if(player1->weaponsel==player1->lastattackweapon){
				progress = max(0.0f, min(1.0f, timediff/(float)animtime));
				// f(x) = -sin(x-1.5)^3
				kick = -sinf(pow((1.5f*progress)-1.5f,3));
				kick *= player1->eyeheight / player1->maxeyeheight;
				if(player1->lastaction) anim = player1->weaponsel->modelanim();
			}
			
			if(player1->weaponsel->info.mdl_kick_rot || player1->weaponsel->info.mdl_kick_back){
				k_rot = player1->weaponsel->info.mdl_kick_rot*kick;
				k_back = player1->weaponsel->info.mdl_kick_back*kick/10;
			}
			
			if(nosway) sway.x = sway.y = sway.z = 0;
			else{
				float swayspeed = sinf((float)swaymillis/swayspeeddiv)/(swaymovediv/10.0f);
				float swayupspeed = cosf((float)swaymillis/swayupspeeddiv)/(swayupmovediv/10.0f);

				float plspeed = min(1.0f, sqrtf(player1->vel.x*player1->vel.x + player1->vel.y*player1->vel.y));
				
				swayspeed *= plspeed/2;
				swayupspeed *= plspeed/2;

				swap(sway.x, sway.y);
				sway.y = -sway.y;
				
				swayupspeed = fabs(swayupspeed); // sway a semicirle only
				sway.z = 1.0f;
				
				sway.x *= swayspeed;
				sway.y *= swayspeed;
				sway.z *= swayupspeed;

				sway.mul(player1->eyeheight / player1->maxeyeheight);
			}

			if(ads_gun(player1->weaponsel->type)){
				if((anim&ANIM_INDEX) == ANIM_GUN_IDLE) basetime = lastmillis-player1->ads;
				else if((anim&ANIM_INDEX) == ANIM_GUN_SHOOT && player1->ads){ anim &= ~ANIM_GUN_SHOOT; anim |= ANIM_GUN_SHOOT2; }
				k_rot *= 1 - player1->ads / 2000.f;
				k_back *= 1 - player1->ads / 1000.f;
				sway.mul(1 - player1->ads / 1200.f);
				swaydir.mul(1 - player1->ads / 1300.f);
			} else if(player1->weaponsel->type == GUN_AKIMBO && (anim&ANIM_INDEX) == ANIM_GUN_IDLE) basetime = lastmillis;

			pos.add(swaydir);
			pos.x -= aimdir.x*k_back+sway.x;
			pos.y -= aimdir.y*k_back+sway.y;
			pos.z -= aimdir.z*k_back+sway.z;
		}
	}
};

vec weaponmove::swaydir(0, 0, 0);
int weaponmove::lastsway = 0, weaponmove::swaymillis = 0;

void preload_hudguns()
{
	loopi(NUMGUNS)
	{
		s_sprintfd(path)("weapons/%s", guns[i].modelname);
		loadmodel(path);
	}
}

