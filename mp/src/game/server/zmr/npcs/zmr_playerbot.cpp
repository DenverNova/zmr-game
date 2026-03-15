#include "cbase.h"
#include "gameinterface.h"
#include "in_buttons.h"
#include "soundent.h"


#include "npcr/npcr_schedule.h"
#include "npcr/npcr_senses.h"

#include "zmr_playerbot.h"
#include "zmr_playermodels.h"

#include "npcs/sched/zmr_bot_main.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar zm_sv_debug_bot_lookat( "zm_sv_debug_bot_lookat", "0" );


class CZMPlayerSenses : public NPCR::CBaseSenses
{
public:
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

    virtual int GetSoundMask() const OVERRIDE { return SOUND_COMBAT | SOUND_DANGER; }
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
    m_bStayPut = false;
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
    // Use PostThink-deferred call since weapons may not be fully initialized yet
    SetContextThink( &CZMPlayerBot::ThinkEquipBestWeapon, gpGlobals->curtime + 0.1f, "EquipBestWeaponThink" );

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

ConVar zm_sv_bot_default_behavior( "zm_sv_bot_default_behavior", "0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "AI Survivor default behavior. 0=Follow Random, 1=Explore, 2=Defend Spawn, 3=Complete Objectives" );
ConVar zm_sv_bot_help_range( "zm_sv_bot_help_range", "1024", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Range for Help voice command to call AI survivors." );
ConVar zm_sv_bot_taunt_chance( "zm_sv_bot_taunt_chance", "8", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Percent chance for AI survivor to taunt after a kill (0=never)." );

static int g_nBotNameIndexMale = 0;
static int g_nBotNameIndexFemale = 0;

static const char* PickBotName( bool bFemale )
{
    if ( bFemale )
    {
        int count = ARRAYSIZE( g_szBotNamesFemale );
        // Shuffle starting point
        if ( g_nBotNameIndexFemale == 0 )
            g_nBotNameIndexFemale = RandomInt( 0, count - 1 );
        const char* name = g_szBotNamesFemale[ g_nBotNameIndexFemale % count ];
        g_nBotNameIndexFemale++;
        return name;
    }
    else
    {
        int count = ARRAYSIZE( g_szBotNamesMale );
        if ( g_nBotNameIndexMale == 0 )
            g_nBotNameIndexMale = RandomInt( 0, count - 1 );
        const char* name = g_szBotNamesMale[ g_nBotNameIndexMale % count ];
        g_nBotNameIndexMale++;
        return name;
    }
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
    // Priority: main gun (with ammo) > sidearm (with ammo) > melee > fists
    static const ZMBotWeaponTypeRange_t s_Priority[] = {
        BOTWEPRANGE_MAINGUN,
        BOTWEPRANGE_SECONDARYWEAPON,
        BOTWEPRANGE_MELEE,
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
    if ( IsAlive() )
        EquipBestWeapon();
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

    // Trace forward at knee height to detect obstacles
    Vector fwd;
    AngleVectors( EyeAngles(), &fwd );
    fwd.z = 0.0f;
    fwd.NormalizeInPlace();

    Vector origin = GetAbsOrigin();
    Vector kneePos = origin + Vector( 0, 0, 18.0f );
    Vector kneeEnd = kneePos + fwd * 32.0f;

    trace_t trKnee;
    CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_NONE );
    UTIL_TraceLine( kneePos, kneeEnd, MASK_PLAYERSOLID, &filter, &trKnee );

    if ( trKnee.fraction >= 1.0f )
        return;

    // Something at knee height - check if we can clear it by jumping
    Vector headPos = origin + Vector( 0, 0, 64.0f );
    Vector headEnd = headPos + fwd * 32.0f;

    trace_t trHead;
    UTIL_TraceLine( headPos, headEnd, MASK_PLAYERSOLID, &filter, &trHead );

    if ( trHead.fraction < 1.0f )
        return;

    // Obstacle at knee but clear at head = jumpable
    PressJump( 0.15f );
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
