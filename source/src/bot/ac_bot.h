//
// C++ Implementation: bot
//
// Description:
//
// Header specific for AC_CUBE
//
// Author:  Rick <rickhelmus@gmail.com>
//
//
//

#ifndef AC_BOT_H
#define AC_BOT_H

#define AC_BOT_DEBUG 0

#ifdef AC_CUBE

class CACBot: public CBot
{
public:
     friend class CBotManager;
     friend class CWaypointClass;

     virtual void CheckItemPickup(void);

     // AI Functions
     virtual bool ChoosePreferredWeapon(void);
     void Reload(int Gun);
     virtual entity *SearchForEnts(bool bUseWPs, float flRange=9999.0f,
                                   float flMaxHeight=JUMP_HEIGHT);
     virtual entity *SearchForFlags(bool bUseWPs, float flRange=9999.0f,
                                   float flMaxHeight=JUMP_HEIGHT);
     virtual bool HeadToTargetEnt(void);
     virtual bool CanTakeFlag(const entity &e);
     virtual bool HeadToTargetFlag(void);
     virtual bool DoSPStuff(void);

     virtual void Spawn(void);
};

inline void AddScreenText(const char *t, ...) {} // UNDONE
inline void AddDebugText(const char *t, ...)
{
#if AC_BOT_DEBUG && defined _DEBUG
    va_list v;
    va_start(v, t);
    conoutf(t,v);
    va_end(v);
#endif
}




#endif

#endif
