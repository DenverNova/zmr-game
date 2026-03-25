#include "cbase.h"
#include "gameinterface.h"
#include "in_buttons.h"
#include "soundent.h"
#include "doors.h"


#include "npcr/npcr_motor.h"
#include "npcr/npcr_schedule.h"
#include "npcr/npcr_senses.h"
#include "nav_mesh.h"
#include "nav_ladder.h"

#include "zmr_playerbot.h"
#include "zmr_playermodels.h"

#include "npcs/sched/zmr_bot_main.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar zm_sv_debug_bot_lookat( "zm_sv_debug_bot_lookat", "1" );


class CZMPlayerSenses : public NPCR::CBaseSenses
{
public:
    typedef NPCR::CBaseSenses BaseClass;

    CZMPlayerSenses( CZMPlayerBot* pNPC ) : NPCR::CBaseSenses( pNPC )
    {
    }

    virtual void FindNewEntities( CUtlVector<CBaseEntity*>& vListEntities ) OVERRIDE
    {
        static const char* s_szZombieClasses[] = {
            "npc_zombie", "npc_fastzombie", "npc_poisonzombie",
            "npc_burnzombie", "npc_dragzombie",
        };

        for ( int c = 0; c < ARRAYSIZE( s_szZombieClasses ); c++ )
        {
            CBaseEntity* pEnt = nullptr;
            while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szZombieClasses[c] )) != nullptr )
            {
                if ( pEnt->IsAlive() )
                    vListEntities.AddToTail( pEnt );
            }
        }
    }

    // Proximity sense: zombies within 500 units are always detected
    // regardless of FOV - only require line of sight.
    // This ensures ranged zombies (acid/poison) that attack from a distance
    // are detected even if the bot isn't looking directly at them.
    virtual bool CanSeeCharacter( CBaseEntity* pEnt ) const OVERRIDE
    {
        float flDistSqr = pEnt->GetAbsOrigin().DistToSqr( GetOuter()->GetAbsOrigin() );
        if ( flDistSqr < (500.0f * 500.0f) )
        {
            return HasLOS( pEnt->WorldSpaceCenter() );
        }
        return BaseClass::CanSeeCharacter( pEnt );
    }

    virtual int GetSoundMask() const OVERRIDE { return SOUND_COMBAT | SOUND_DANGER | SOUND_BULLET_IMPACT | SOUND_PLAYER | SOUND_WORLD; }
};


extern ConVar bot_mimic;
extern ConVar bot_mimic_pitch_offset;
extern ConVar bot_mimic_yaw_offset;
extern ConVar bot_mimic_target;
extern ConVar bot_attack;


LINK_ENTITY_TO_CLASS( npcr_player_zm, CZMPlayerBot );
PRECACHE_REGISTER( npcr_player_zm );


CZMPlayerBot::CZMPlayerBot()
{
    m_hFollowTarget.Set( nullptr );
    m_flNextObstacleCheck = 0.0f;
    m_flNextLadderCheck = 0.0f;
    m_flNextCliffCheck = 0.0f;
    m_hLastAttacker.Set( nullptr );
    m_flLastAttackerTime = 0.0f;
    m_bStayPut = false;
    m_iBehaviorOverride = -1;
    m_iMixedBehavior = -1;
    m_vecCommandedDefendPos = vec3_origin;
    m_bHasCommandedDefendPos = false;
    m_hCommandedGrabTarget.Set( nullptr );
    m_vecCommandedDropPos = vec3_origin;
    m_bHasCommandedDropPos = false;
}

CZMPlayerBot::~CZMPlayerBot()
{
}

NPCR::CScheduleInterface* CZMPlayerBot::CreateScheduleInterface()
{
    return new NPCR::CScheduleInterface( this, new CPlayerBotMainSchedule );
}

NPCR::CBaseSenses* CZMPlayerBot::CreateSenses()
{
    return new CZMPlayerSenses( this );
}

bool CZMPlayerBot::ShouldUpdate() const
{
    if ( m_lifeState != LIFE_ALIVE )
        return false;

    return CBaseNPC::ShouldUpdate();
}

bool CZMPlayerBot::IsEnemy( CBaseEntity* pEnt ) const
{
    if ( !pEnt || !pEnt->IsAlive() )
        return false;

    // All zombies (NPCs) are enemies to survivor bots
    if ( pEnt->IsNPC() )
        return true;

    return false;
}

static const char* PickBotName( bool bFemale );

void CZMPlayerBot::Spawn()
{
    BaseClass::Spawn();

    NPCR::CEventDispatcher::OnSpawn();

    // ZM bots keep their assigned name ("Zombie Master")
    // Check both team and name since team may not be set on first spawn
    if ( IsZM() || FStrEq( GetPlayerName(), "Zombie Master" ) )
        return;

    // Equip the best weapon we have (e.g. pistol from GiveDefaultItems over fists)
    // Use deferred calls since weapons may not be fully initialized yet
    SetContextThink( &CZMPlayerBot::ThinkEquipBestWeapon, gpGlobals->curtime + 0.5f, "EquipBestWeaponThink" );
    // Second attempt as safety net in case first one was too early
    SetContextThink( &CZMPlayerBot::ThinkEquipBestWeapon, gpGlobals->curtime + 1.5f, "EquipBestWeaponRetry" );

    // Assign a gender-matched name based on the actual model
    const char* pszModel = STRING( GetModelName() );
    bool bFemale = ( pszModel && Q_strstr( pszModel, "female" ) != nullptr );

    CZMPlayerModelData* pModelData = ZMGetPlayerModels()->GetPlayerModelData( pszModel );
    if ( pModelData )
        bFemale = ( pModelData->GetGender() == 1 );

    const char* pName = PickBotName( bFemale );
    engine->SetFakeClientConVarValue( edict(), "name", pName );
    SetPlayerName( pName );
}

static const char* g_szBotNamesMale[] =
{
    "Marcus", "Jake", "Derek", "Travis", "Cole",
    "Nathan", "Victor", "Leon", "Hank", "Ellis",
    "Rick", "Glenn", "Shane", "Daryl", "Morgan",
    "Frank", "Chuck", "Nick", "Coach", "Bill",
    "Louis", "Francis", "Jim", "Ray", "Duane"
};

static const char* g_szBotNamesFemale[] =
{
    "Zoey", "Rochelle", "Sarah", "Elena", "Maria",
    "Claire", "Jill", "Ada", "Rebecca", "Sheva",
    "Maggie", "Carol", "Beth", "Rosita", "Sasha",
    "Tess", "Ellie", "Abby", "Dina", "Riley",
    "Kate", "Lena", "Mia", "Eva", "Nina"
};

ConVar zm_sv_bot_default_behavior( "zm_sv_bot_default_behavior", "3", FCVAR_NOTIFY | FCVAR_ARCHIVE, "AI Survivor default behavior. 0=Follow Random, 1=Explore, 2=Defend Spawn, 3=Mixed Mode" );
ConVar zm_sv_bot_weapon_search_range( "zm_sv_bot_weapon_search_range", "1024", FCVAR_NOTIFY | FCVAR_ARCHIVE, "How far AI survivors search for weapons and ammo (units)." );
ConVar zm_sv_bot_command_range( "zm_sv_bot_command_range", "1024", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Range for voice commands to affect AI survivors." );
// Hardcoded taunt chance (percent) for AI survivors after a kill
#define BOT_TAUNT_CHANCE 8

static int g_nBotNameIndexMale = 0;
static int g_nBotNameIndexFemale = 0;

static char g_szBotNameBuf[128];

static const char* PickBotName( bool bFemale )
{
    const char* baseName;
    if ( bFemale )
    {
        int count = ARRAYSIZE( g_szBotNamesFemale );
        if ( g_nBotNameIndexFemale == 0 )
            g_nBotNameIndexFemale = RandomInt( 0, count - 1 );
        baseName = g_szBotNamesFemale[ g_nBotNameIndexFemale % count ];
        g_nBotNameIndexFemale++;
    }
    else
    {
        int count = ARRAYSIZE( g_szBotNamesMale );
        if ( g_nBotNameIndexMale == 0 )
            g_nBotNameIndexMale = RandomInt( 0, count - 1 );
        baseName = g_szBotNamesMale[ g_nBotNameIndexMale % count ];
        g_nBotNameIndexMale++;
    }

    Q_snprintf( g_szBotNameBuf, sizeof( g_szBotNameBuf ), "%s (Bot)", baseName );
    return g_szBotNameBuf;
}

CZMPlayer* CZMPlayerBot::CreateZMBot( const char* playername )
{
    char name[128];
    if ( playername && (*playername) )
        Q_strncpy( name, playername, sizeof( name ) );
    else
        Q_strncpy( name, "Survivor", sizeof( name ) );

    CZMPlayer* pPlayer = NPCR::CPlayer<CZMPlayer>::CreateBot<CZMPlayerBot>( name );

    return pPlayer;
}

CBasePlayer* CZMPlayerBot::BotPutInServer( edict_t* pEdict, const char* playername )
{
    CZMPlayer* pPlayer = CreatePlayer( "npcr_player_zm", pEdict );

    if ( pPlayer )
    {
        pPlayer->SetPlayerName( playername );
        pPlayer->SetPickPriority( 0 );

        pPlayer->ClearFlags();
        pPlayer->AddFlag( FL_CLIENT | FL_FAKECLIENT );
    }
    
    return pPlayer;
}

bool CZMPlayerBot::OverrideUserCmd( CUserCmd& cmd )
{
    if ( bot_attack.GetBool() )
        cmd.buttons |= IN_ATTACK;


    if ( bot_mimic.GetInt() <= 0 )
        return false;

    // A specific target?
    if ( bot_mimic_target.GetInt() > 0 && bot_mimic_target.GetInt() == entindex() )
    {
        return false;
    }

    CBasePlayer* pPlayer = UTIL_PlayerByIndex( bot_mimic.GetInt() );
    if ( !pPlayer )
        return false;

    const CUserCmd* pPlyCmd = pPlayer->GetLastUserCommand();
    if ( !pPlyCmd )
        return false;


    cmd = *pPlyCmd;
    cmd.viewangles[PITCH] += bot_mimic_pitch_offset.GetFloat();
    cmd.viewangles[YAW] += bot_mimic_yaw_offset.GetFloat();
    cmd.viewangles[ROLL] = 0.0f;

    // HACK
    SetEyeAngles( cmd.viewangles );

    return true;
}

ZMBotWeaponTypeRange_t CZMPlayerBot::GetWeaponType( CZMBaseWeapon* pWeapon )
{
    if ( !pWeapon )
        return BOTWEPRANGE_INVALID;

    return GetWeaponType( pWeapon->GetClassname() );
}

bool CZMPlayerBot::MustStopToShoot() const
{
    auto type = GetWeaponType( GetActiveWeapon() );

    if ( type == BOTWEPRANGE_MAINGUN )
    {
        const char* wep = GetActiveWeapon()->GetClassname() + 10;
        if ( FStrEq( wep, "rifle" ) || FStrEq( wep, "r700" ) )
            return true;
    }
    if ( type == BOTWEPRANGE_SECONDARYWEAPON )
    {
        const char* wep = GetActiveWeapon()->GetClassname() + 10;
        if ( FStrEq( wep, "revolver" ) )
            return true;
    }

    return false;
}

ZMBotWeaponTypeRange_t CZMPlayerBot::GetWeaponType( const char* classname )
{
    if ( !classname || !(*classname) )
        return BOTWEPRANGE_INVALID;

    // Skip 'weapon_zm_'
    const char* wep = classname + sizeof( "weapon_zm_" ) - 1;

    // Main guns (shotguns, rifles, SMGs, snipers)
    if ( FStrEq( wep, "mac10" ) )
        return BOTWEPRANGE_MAINGUN;
    if ( Q_strncmp( wep, "shotgun", sizeof( "shotgun" ) - 1 ) == 0 )
        return BOTWEPRANGE_MAINGUN;
    if ( FStrEq( wep, "rifle" ) )
        return BOTWEPRANGE_MAINGUN;
    if ( FStrEq( wep, "r700" ) )
        return BOTWEPRANGE_MAINGUN;

    // Sidearms (pistol, revolver)
    if ( FStrEq( wep, "pistol" ) )
        return BOTWEPRANGE_SECONDARYWEAPON;
    if ( FStrEq( wep, "revolver" ) )
        return BOTWEPRANGE_SECONDARYWEAPON;

    // Melee weapons
    if ( FStrEq( wep, "improvised" ) )
        return BOTWEPRANGE_MELEE;
    if ( FStrEq( wep, "sledge" ) )
        return BOTWEPRANGE_MELEE;
    if ( FStrEq( wep, "fireaxe" ) )
        return BOTWEPRANGE_MELEE;

    // Fists (lowest priority)
    if ( FStrEq( wep, "fistscarry" ) )
        return BOTWEPRANGE_FISTS;

    // Throwables (grenades)
    if ( FStrEq( wep, "molotov" ) )
        return BOTWEPRANGE_THROWABLE;
    if ( FStrEq( wep, "pipebomb" ) )
        return BOTWEPRANGE_THROWABLE;

    return BOTWEPRANGE_INVALID;
}

bool CZMPlayerBot::HasWeaponOfType( ZMBotWeaponTypeRange_t wepType ) const
{
    for ( int i = 0; i < MAX_WEAPONS; i++ )
    {
        CBaseCombatWeapon* pWep = m_hMyWeapons.Get( i );
        if ( pWep && GetWeaponType( ToZMBaseWeapon( pWep ) ) == wepType )
        {
            return true;
        }
    }
    
    return false;
}

bool CZMPlayerBot::HasEquippedWeaponOfType( ZMBotWeaponTypeRange_t wepType ) const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();

    return ( pWep && GetWeaponType( pWep ) == wepType );
}

bool CZMPlayerBot::EquipWeaponOfType( ZMBotWeaponTypeRange_t wepType )
{
    CZMBaseWeapon* pWep = FindWeaponOfType( wepType );
    if ( !pWep )
        return false;

    if ( pWep == m_hActiveWeapon.Get() )
        return true;


    Weapon_Switch( pWep );
    return pWep == m_hActiveWeapon.Get();
}

bool CZMPlayerBot::EquipBestWeapon()
{
    // Priority: main gun (with ammo) > sidearm (with ammo) > melee > throwable > fists
    static const ZMBotWeaponTypeRange_t s_Priority[] = {
        BOTWEPRANGE_MAINGUN,
        BOTWEPRANGE_SECONDARYWEAPON,
        BOTWEPRANGE_MELEE,
        BOTWEPRANGE_THROWABLE,
        BOTWEPRANGE_FISTS,
    };

    for ( int i = 0; i < ARRAYSIZE( s_Priority ); i++ )
    {
        CZMBaseWeapon* pWep = FindWeaponOfType( s_Priority[i] );
        if ( !pWep )
            continue;

        // For ranged weapons, check ammo
        if ( s_Priority[i] == BOTWEPRANGE_MAINGUN || s_Priority[i] == BOTWEPRANGE_SECONDARYWEAPON )
        {
            if ( !WeaponHasAmmo( pWep ) )
                continue;
        }

        if ( pWep == m_hActiveWeapon.Get() )
            return true;

        Weapon_Switch( pWep );
        return ( pWep == m_hActiveWeapon.Get() );
    }

    return false;
}

void CZMPlayerBot::ThinkEquipBestWeapon()
{
    if ( !IsAlive() )
        return;

    extern ConVar zm_sv_bot_debug;
    if ( zm_sv_bot_debug.GetBool() )
    {
        CZMBaseWeapon* pBefore = GetActiveWeapon();
        Msg( "[Bot %s] ThinkEquipBestWeapon: before='%s'\n",
            GetPlayerName(), pBefore ? pBefore->GetClassname() : "(none)" );
        for ( int i = 0; i < MAX_WEAPONS; i++ )
        {
            CZMBaseWeapon* pWep = ToZMBaseWeapon( GetWeapon( i ) );
            if ( !pWep ) continue;
            Msg( "  [%i] %s type=%i\n", i, pWep->GetClassname(), GetWeaponType( pWep ) );
        }
    }

    EquipBestWeapon();

    if ( zm_sv_bot_debug.GetBool() )
    {
        CZMBaseWeapon* pAfter = GetActiveWeapon();
        Msg( "[Bot %s] ThinkEquipBestWeapon: after='%s'\n",
            GetPlayerName(), pAfter ? pAfter->GetClassname() : "(none)" );
    }
}

ZMBotWeaponTypeRange_t CZMPlayerBot::GetCurrentWeaponType() const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();
    if ( !pWep )
        return BOTWEPRANGE_INVALID;

    return GetWeaponType( pWep );
}

bool CZMPlayerBot::WeaponHasAmmo( CZMBaseWeapon* pWeapon ) const
{
    if ( !pWeapon )
        return false;

    if ( !pWeapon->UsesPrimaryAmmo() )
        return true;

    return pWeapon->Clip1() > 0 || GetAmmoCount( pWeapon->GetPrimaryAmmoType() ) > 0;
}

bool CZMPlayerBot::ShouldReload() const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();
    if ( !pWep )
        return false;

    return ( pWep->UsesClipsForAmmo1() && pWep->UsesPrimaryAmmo() && pWep->Clip1() == 0 );
}

bool CZMPlayerBot::HasAnyEffectiveRangeWeapons() const
{
    for ( int i = 0; i < MAX_WEAPONS; i++ )
    {
        CZMBaseWeapon* pWep = ToZMBaseWeapon( m_hMyWeapons.Get( i ) );
        if ( !pWep )
            continue;


        ZMBotWeaponTypeRange_t wepType = GetWeaponType( pWep );
        if ( wepType != BOTWEPRANGE_MAINGUN && wepType != BOTWEPRANGE_SECONDARYWEAPON )
            continue;

        if ( WeaponHasAmmo( pWep ) )
            return true;
    }

    return false;
}

bool CZMPlayerBot::CanReload() const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();
    if ( !pWep ) return false;

    if ( pWep->Clip1() < pWep->GetMaxClip1() )
        return true;

    return false;
}

bool CZMPlayerBot::CanAttack() const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();
    if ( !pWep ) return false;

    if ( pWep->UsesPrimaryAmmo() && pWep->Clip1() <= 0 )
        return false;

    return pWep->m_flNextPrimaryAttack <= gpGlobals->curtime;
}

float CZMPlayerBot::GetOptimalAttackDistance() const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();
    if ( !pWep ) return FLT_MAX;

    // ZMRTODO: Get rid of this hardcoded stuff.

    // HACK: Melee
    if ( !pWep->UsesPrimaryAmmo() )
        return 32.0f;


    // Skip 'weapon_zm_'
    const char* wep = pWep->GetClassname() + sizeof( "weapon_zm_" ) - 1;

    if ( FStrEq( wep, "rifle" ) || FStrEq( wep, "revolver" ) )
        return 768.0f;
    if ( FStrEq( wep, "shotgun_sporting" ) )
        return 150.0f;
    if ( FStrEq( wep, "shotgun" ) )
        return 100.0f;
    if ( FStrEq( wep, "mac10" ) )
        return 200.0f;

    return 400.0f;
}

float CZMPlayerBot::GetMaxAttackDistance() const
{
    CZMBaseWeapon* pWep = GetActiveWeapon();
    if ( !pWep ) return FLT_MAX;

    // ZMRTODO: Get rid of this hardcoded stuff.

    // HACK: Melee
    if ( !pWep->UsesPrimaryAmmo() )
        return 50.0f;


    // Skip 'weapon_zm_'
    const char* wep = pWep->GetClassname() + sizeof( "weapon_zm_" ) - 1;

    if ( FStrEq( wep, "rifle" ) || FStrEq( wep, "revolver" ) )
        return 1024.0f;
    if ( FStrEq( wep, "shotgun_sporting" ) )
        return 400.0f;
    if ( FStrEq( wep, "shotgun" ) )
        return 256.0f;
    if ( FStrEq( wep, "mac10" ) )
        return 512.0f;

    return 768.0f;
}

CZMBaseWeapon* CZMPlayerBot::FindWeaponOfType( ZMBotWeaponTypeRange_t wepType ) const
{
    for ( int i = 0; i < MAX_WEAPONS; i++ )
    {
        CZMBaseWeapon* pWep = ToZMBaseWeapon( m_hMyWeapons.Get( i ) );
        if ( pWep && GetWeaponType( pWep ) == wepType )
        {
            return pWep;
        }
    }

    return nullptr;
}

void CZMPlayerBot::CheckObstacleJump()
{
    if ( !IsAlive() || !(GetFlags() & FL_ONGROUND) )
        return;

    if ( gpGlobals->curtime < m_flNextObstacleCheck )
        return;

    m_flNextObstacleCheck = gpGlobals->curtime + 0.25f;

    // Trace forward at waist height to detect obstacles and doors
    Vector fwd;
    AngleVectors( EyeAngles(), &fwd );
    fwd.z = 0.0f;
    fwd.NormalizeInPlace();

    Vector origin = GetAbsOrigin();
    Vector waistPos = origin + Vector( 0, 0, 36.0f );
    Vector waistEnd = waistPos + fwd * 48.0f;

    trace_t trWaist;
    UTIL_TraceLine( waistPos, waistEnd, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &trWaist );

    if ( trWaist.fraction >= 1.0f )
        return;

    CBaseEntity* pHit = trWaist.m_pEnt;

    // Check if the obstacle is a USE-activatable door
    if ( pHit && (FClassnameIs( pHit, "func_door" ) || FClassnameIs( pHit, "func_door_rotating" ) || FClassnameIs( pHit, "prop_door_rotating" )) )
    {
        if ( pHit->HasSpawnFlags( SF_DOOR_PUSE ) && !pHit->HasSpawnFlags( SF_DOOR_LOCKED ) )
        {
            GetMotor()->FaceTowards( pHit->WorldSpaceCenter() );
            PressUse( 0.3f );
            return;
        }
    }

    // Not a door - check if we can jump over it
    Vector kneePos = origin + Vector( 0, 0, 18.0f );
    Vector kneeEnd = kneePos + fwd * 32.0f;

    trace_t trKnee;
    CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_NONE );
    UTIL_TraceLine( kneePos, kneeEnd, MASK_PLAYERSOLID, &filter, &trKnee );

    if ( trKnee.fraction >= 1.0f )
        return;

    Vector headPos = origin + Vector( 0, 0, 64.0f );
    Vector headEnd = headPos + fwd * 32.0f;

    trace_t trHead;
    UTIL_TraceLine( headPos, headEnd, MASK_PLAYERSOLID, &filter, &trHead );

    if ( trHead.fraction < 1.0f )
        return;

    // Obstacle at knee but clear at head = jumpable
    PressJump( 0.15f );
}

void CZMPlayerBot::CheckLadderClimb()
{
    if ( !IsAlive() )
        return;

    if ( gpGlobals->curtime < m_flNextLadderCheck )
        return;

    m_flNextLadderCheck = gpGlobals->curtime + 0.2f;

    // If already on a ladder, press forward to climb up (or back to go down)
    if ( GetMoveType() == MOVETYPE_LADDER )
    {
        // Look up slightly and press forward to climb
        QAngle ang = EyeAngles();
        if ( ang.x > -60.0f )
            ang.x = -45.0f; // Look up to climb
        SetEyeAngles( ang );
        return;
    }

    // Not on a ladder — check for nearby nav ladders we should use
    CNavArea* pMyArea = GetLastKnownArea();
    if ( !pMyArea )
        return;

    // Check all nav ladders connected to our current area
    for ( int dir = 0; dir < CNavLadder::NUM_LADDER_DIRECTIONS; dir++ )
    {
        const NavLadderConnectVector* pLadders = pMyArea->GetLadders( (CNavLadder::LadderDirectionType)dir );
        if ( !pLadders )
            continue;

        for ( int i = 0; i < pLadders->Count(); i++ )
        {
            CNavLadder* pLadder = (*pLadders)[i].ladder;
            if ( !pLadder )
                continue;

            // Only use ladders that are reasonably close
            Vector ladderBottom = pLadder->m_bottom;
            float flDist = GetAbsOrigin().DistTo( ladderBottom );
            if ( flDist > 128.0f )
                continue;

            // Walk toward the ladder bottom and press USE to attach
            Vector toLadder = ladderBottom - GetAbsOrigin();
            toLadder.z = 0.0f;
            if ( toLadder.Length() > 16.0f )
            {
                GetMotor()->FaceTowards( ladderBottom );
                GetMotor()->Approach( ladderBottom );
            }

            // Press USE to attach to the ladder entity
            CBaseEntity* pLadderEnt = pLadder->GetLadderEntity();
            if ( pLadderEnt )
            {
                GetMotor()->FaceTowards( pLadderEnt->WorldSpaceCenter() );
                PressUse( 0.3f );
            }
            return;
        }
    }
}

void CZMPlayerBot::CheckCliffAhead()
{
    if ( !IsAlive() || !(GetFlags() & FL_ONGROUND) )
        return;

    if ( gpGlobals->curtime < m_flNextCliffCheck )
        return;

    m_flNextCliffCheck = gpGlobals->curtime + 0.3f;

    // Only check while moving
    if ( !GetMotor()->IsMoving() )
        return;

    // Trace forward at ground level to find where the floor is ahead of us
    Vector fwd;
    AngleVectors( EyeAngles(), &fwd );
    fwd.z = 0.0f;
    fwd.NormalizeInPlace();

    Vector origin = GetAbsOrigin();
    Vector aheadPos = origin + fwd * 64.0f;

    // Trace down from the ahead position to see if there's ground
    trace_t tr;
    UTIL_TraceLine( aheadPos + Vector( 0, 0, 16.0f ), aheadPos - Vector( 0, 0, 200.0f ),
        MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &tr );

    if ( tr.fraction >= 1.0f )
    {
        // No ground within 200 units below — dangerous drop ahead, stop and jump back
        PressJump( 0.15f );
        return;
    }

    // Check drop height — anything over ~192 units (about 3x player height) would hurt
    float flDropHeight = (aheadPos.z + 16.0f) - tr.endpos.z;
    if ( flDropHeight > 192.0f )
    {
        // Dangerous drop — press jump to try to stop momentum
        PressJump( 0.15f );
    }
}

CON_COMMAND( bot, "" )
{
    if ( !UTIL_IsCommandIssuedByServerAdmin() )
    {
        return;
    }


    CZMPlayerBot::CreateZMBot();
}

CON_COMMAND( zm_bot, "" )
{
    if ( !UTIL_IsCommandIssuedByServerAdmin() )
    {
        return;
    }


    const char* name = args.Arg( 1 );

    CZMPlayerBot::CreateZMBot( name );
}
