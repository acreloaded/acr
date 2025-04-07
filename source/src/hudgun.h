VARP(nosway, 0, 0, 1);
VARP(swayspeeddiv, 1, 105, 1000);
VARP(swaymovediv, 1, 200, 1000);
VARP(swayupspeeddiv, 1, 105, 1000);
VARP(swayupmovediv, 1, 200, 1000);

vec swaytest;

struct weaponmove
{
    static vec swaydir;
    static int swaymillis, lastsway;

    float k_rot, kick;
    vec pos;
    int anim, basetime;

    weaponmove() : k_rot(0), kick(0), anim(0), basetime(0) { pos.x = pos.y = pos.z = 0.0f; }

    void calcmove(vec aimdir, int lastaction, playerent *p)
    {
        kick = k_rot = 0.0f;
        pos = p->o;

        if(!nosway)
        {
            float k = pow(0.7f, (lastmillis-lastsway)/10.0f);
            swaydir.mul(k);
            vec dv(p->vel);
            dv.mul((1-k)/max(p->vel.magnitude(), p->maxspeed));
            dv.x *= 1.5f;
            dv.y *= 1.5f;
            dv.z *= 0.4f;
            swaydir.add(dv);
        }

        if(p->onfloor || p->onladder || p->inwater) swaymillis += lastmillis-lastsway;
        lastsway = lastmillis;

        swaytest.x = swaytest.y = swaytest.z = 0;

        if(p->weaponsel->reloading)
        {
            anim = ANIM_GUN_RELOAD;
            basetime = p->weaponsel->reloading;
            float reloadtime = (float)p->weaponsel->info.reloadtime,
                  progress = clamp((lastmillis - p->weaponsel->reloading)/reloadtime, 0.0f, clamp(1.0f - (p->lastaction + p->weaponsel->gunwait - lastmillis)/reloadtime, 0.5f, 1.0f));
            // only use the cheap "aim down" hack for akimbo
            if (p->weaponsel->type == GUN_AKIMBO)
            {
                progress -= .4f;
                if (progress > 0)
                {
                    progress /= .6f;
                    k_rot = -90 * sinf(progress*M_PI);
                }
            }
        }
        else
        {
            anim = ANIM_GUN_IDLE;
            basetime = lastaction;

            if (p->weaponchanging)
            {
                float progress = clamp((lastmillis - p->weaponchanging) / (float)SWITCHTIME(p->perk1 == PERK_TIME), 0.0f, 1.0f);
                k_rot = -90 * sinf(progress * M_PI);
            }

            if (p->weaponsel == p->lastattackweapon || p->weaponsel->type == GUN_RPG || p->weaponsel->type == GUN_HEAL)
            {
                int timediff = lastmillis - lastaction,
                    animtime = min(p->weaponsel->gunwait, (int)p->weaponsel->info.attackdelay);
                float progress = max(0.0f, min(1.0f, timediff / (float)animtime));
                // f(x) = -sin(x-1.5)^3
                kick = -sinf(pow((1.5f * progress) - 1.5f, 3));
                kick *= p->eyeheight / p->maxeyeheight;
                if (p->weaponsel->type == GUN_HEAL)
                    basetime = lastmillis - (int)(p->zoomed * 1000.f);
                else if (p->lastaction || p->weaponsel->type == GUN_RPG)
                    anim = p->weaponsel->modelanim();
            }
        }

        vec sway = aimdir;
        float k_back = 0.0f;

        if(p->weaponsel->info.mdl_kick_rot || p->weaponsel->info.mdl_kick_back)
        {
            k_rot += p->weaponsel->info.mdl_kick_rot*kick;
            k_back = p->weaponsel->info.mdl_kick_back*kick/10;
        }

        if(nosway) sway.x = sway.y = sway.z = 0;
        else
        {
            float swayspeed = sinf((float)swaymillis/swayspeeddiv)/(swaymovediv/10.0f);
            float swayupspeed = cosf((float)swaymillis/swayupspeeddiv)/(swayupmovediv/10.0f);

            float plspeed = min(1.0f, sqrtf(p->vel.x*p->vel.x + p->vel.y*p->vel.y));

            swayspeed *= plspeed/2;
            swayupspeed *= plspeed/2;

            swap(sway.x, sway.y);
            sway.y = -sway.y;

            swayupspeed = fabs(swayupspeed); // sway a semicirle only
            sway.z = 1.0f;

            sway.x *= swayspeed;
            sway.y *= swayspeed;
            sway.z *= swayupspeed;

            sway.mul(p->eyeheight / p->maxeyeheight);

            if (ads_gun(p->weaponsel->type))
            {
                const float zoom_eased = sqrtf(p->zoomed);
                k_rot *= 1 - zoom_eased / 2.f;
                k_back *= 1 - zoom_eased / 1.1f;
                sway.mul(1 - zoom_eased / 1.2f);
                swaydir.mul(1 - zoom_eased / 1.3f);
            }
        }

        pos.add(swaydir);
        pos.x -= aimdir.x*k_back+sway.x;
        pos.y -= aimdir.y*k_back+sway.y;
        pos.z -= aimdir.z*k_back+sway.z;

        (swaytest = aimdir).mul(-k_back).add(sway);
    }
};

vec weaponmove::swaydir(0, 0, 0);
int weaponmove::lastsway = 0, weaponmove::swaymillis = 0;

void preload_hudguns()
{
    loopi(NUMGUNS)
    {
        defformatstring(widn)("modmdlweap%d", i);
        defformatstring(path)("weapons/%s", identexists(widn)?getalias(widn):guns[i].modelname);
        loadmodel(path);
    }
    loadmodel(identexists("modmdlweapshell1") ? getalias("modmdlweapshell1") : "shells/pistol");
    loadmodel(identexists("modmdlweapshell2") ? getalias("modmdlweapshell2") : "shells/rifle");
    loadmodel(identexists("modmdlweapshell3") ? getalias("modmdlweapshell3") : "shells/shotgun");
    loadmodel(identexists("modmdlweapshell4") ? getalias("modmdlweapshell4") : "shells/sniper");
}

