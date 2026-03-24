#include "cbase.h"

#include "zmr_gamerules.h"
#include "zmr_player.h"
#include "zmr_entities.h"
#include "zmr_resource_system.h"
#include "zmr_util.h"
#include "npcs/zmr_zombiebase_shared.h"
#include "npcs/zmr_zombiebase.h"

#include "zmr_hiddenspawn.h"
#include "zmr_ai_zm.h"
#include "props.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


extern ConVar zm_sv_ai_zm;
extern ConVar zm_sv_hidden_allclasses;
extern ConVar zm_sv_zombiemax;
extern ConVar zm_sv_zombie_type_limits;
extern ConVar zm_sv_zombie_max_banshee;
extern ConVar zm_sv_zombie_max_hulk;
extern ConVar zm_sv_zombie_max_drifter;
extern ConVar zm_sv_zombie_max_immolator;

ConVar zm_sv_ai_zm_debug( "zm_sv_ai_zm_debug", "1", FCVAR_CHEAT, "Enable AI ZM debug logging. 0=off, 1=on." );
ConVar zm_sv_ai_zm_difficulty( "zm_sv_ai_zm_difficulty", "1.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "AI ZM difficulty multiplier. Scales AI resource income. 0.1-5.0" );
ConVar zm_sv_ai_zm_view_mode( "zm_sv_ai_zm_view_mode", "0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "AI ZM spawner access. 0=Global, 1=Within View only." );
ConVar zm_sv_ai_zm_trap_range( "zm_sv_ai_zm_trap_range", "1024", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Survivor proximity required to trigger a trap or barrel." );
ConVar zm_sv_ai_zm_trap_cooldown( "zm_sv_ai_zm_trap_cooldown", "30.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Per-entity cooldown in seconds before AI can re-use the same trap or barrel." );
ConVar zm_sv_ai_zm_rush_prevention( "zm_sv_ai_zm_rush_prevention", "15.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds at round start before AI ZM can act. Does not affect human ZMs." );
ConVar zm_sv_ai_zm_rally_interval( "zm_sv_ai_zm_rally_interval", "6.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds between idle zombie rally commands." );
ConVar zm_sv_ai_zm_rally_buffer( "zm_sv_ai_zm_rally_buffer", "256.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Distance buffer for splitting rally targets between survivors." );
ConVar zm_sv_ai_zm_cull_time( "zm_sv_ai_zm_cull_time", "45.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds a zombie must be stranded before being culled. 0=disabled." );

CZMAIZombieMaster g_ZMAIZombieMaster;

// Weighted type selection: 60% shambler, 10% each special
static const int g_ZombieWeights[ZMCLASS_MAX] = { 60, 10, 10, 10, 10 };

// Distance threshold for considering two spawners "nearly the same distance"
#define SPAWNER_SPREAD_THRESHOLD 512.0f

// Camera smoothing speed (units per second for position, degrees per second for angles)
#define CAMERA_MOVE_SPEED   2000.0f
#define CAMERA_TURN_SPEED   180.0f
#define CAMERA_SWITCH_MIN   4.0f
#define CAMERA_SWITCH_MAX   8.0f
#define CAMERA_HEIGHT       200.0f
#define CAMERA_BACK_DIST    300.0f

// Culling constants
#define CULL_CHECK_INTERVAL     10.0f
#define CULL_POP_THRESHOLD      0.8f
#define CULL_DISTANCE_THRESHOLD 6144.0f


CZMAIZombieMaster::CZMAIZombieMaster()
{
    m_EntityCooldowns.SetLessFunc( DefLessFunc( int ) );
    Reset();
}

void CZMAIZombieMaster::Reset()
{
    m_Phase = AIZM_PHASE_RESERVE;
    m_flNextActionTime = 0.0f;
    m_iReservedResources = 0;
    m_EntityCooldowns.RemoveAll();
    m_flRoundStartTime = gpGlobals ? gpGlobals->curtime : 0.0f;
    m_flNextRallyTime = 0.0f;
    m_flLastUpdateTime = 0.0f;
    m_bLoggedSpawners = false;

    m_iSpawnBurstRemaining = 0;
    m_SpawnBurstClass = ZMCLASS_INVALID;
    m_hSpawnBurstSpawner = nullptr;

    m_vecCameraPos.Init();
    m_angCameraAng.Init();
    m_iCameraTargetIndex = 0;
    m_flCameraNextSwitch = 0.0f;
    m_flCameraSwitchLerp = 0.0f;

    m_flNextCullTime = 0.0f;
}

bool CZMAIZombieMaster::IsActive() const
{
    if ( !zm_sv_ai_zm.GetBool() )
        return false;

    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] IsActive: no ZM player found\n" );
        return false;
    }

    if ( !pZM->IsBot() )
        return false;

    return true;
}

CZMPlayer* CZMAIZombieMaster::GetZMPlayer() const
{
    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CZMPlayer* pPlayer = ToZMPlayer( UTIL_PlayerByIndex( i ) );
        if ( pPlayer && pPlayer->IsZM() )
            return pPlayer;
    }
    return nullptr;
}

void CZMAIZombieMaster::GatherHumans( CUtlVector<CBasePlayer*>& humans ) const
{
    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CBasePlayer* pPlayer = UTIL_PlayerByIndex( i );
        if ( pPlayer && pPlayer->IsAlive() && pPlayer->GetTeamNumber() == ZMTEAM_HUMAN )
            humans.AddToTail( pPlayer );
    }
}

CBasePlayer* CZMAIZombieMaster::FindNearestHuman( const Vector& pos, float* outDist ) const
{
    CBasePlayer* pClosest = nullptr;
    float flClosestDist = FLT_MAX;

    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CBasePlayer* pPlayer = UTIL_PlayerByIndex( i );
        if ( !pPlayer || !pPlayer->IsAlive() )
            continue;
        if ( pPlayer->GetTeamNumber() != ZMTEAM_HUMAN )
            continue;

        float dist = pPlayer->GetAbsOrigin().DistTo( pos );
        if ( dist < flClosestDist )
        {
            flClosestDist = dist;
            pClosest = pPlayer;
        }
    }

    if ( outDist )
        *outDist = flClosestDist;

    return pClosest;
}

void CZMAIZombieMaster::GatherActiveSpawners( CUtlVector<CZMEntZombieSpawn*>& spawners ) const
{
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_zombiespawn" )) != nullptr )
    {
        CZMEntZombieSpawn* pSpawn = dynamic_cast<CZMEntZombieSpawn*>( pEnt );
        if ( pSpawn && pSpawn->IsActive() )
            spawners.AddToTail( pSpawn );
    }
}

//
// View mode filtering: returns true if the entity is accessible to the AI.
// Global mode (0) = everything is accessible.
// Within View mode (1) = only entities visible from the AI's camera position.
//
bool CZMAIZombieMaster::IsEntityInView( CZMPlayer* pZM, CBaseEntity* pEnt ) const
{
    if ( zm_sv_ai_zm_view_mode.GetInt() == 0 )
        return true;

    if ( !pZM || !pEnt )
        return false;

    Vector eyePos = m_vecCameraPos;
    if ( eyePos.IsZero() )
        eyePos = pZM->EyePosition();

    trace_t tr;
    UTIL_TraceLine( eyePos, pEnt->WorldSpaceCenter(), MASK_VISIBLE, pZM, COLLISION_GROUP_NONE, &tr );
    return ( tr.fraction >= 0.95f || tr.m_pEnt == pEnt );
}

//
// Gather spawners near survivors, spreading across multiple if distances are similar.
//
void CZMAIZombieMaster::GatherNearestSpawners( CUtlVector<CZMEntZombieSpawn*>& outSpawners ) const
{
    CUtlVector<CZMEntZombieSpawn*> allSpawners;
    GatherActiveSpawners( allSpawners );

    if ( allSpawners.Count() == 0 )
        return;

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );
    if ( humans.Count() == 0 )
        return;

    // Find the centroid of all survivors
    Vector centroid( 0, 0, 0 );
    for ( int i = 0; i < humans.Count(); i++ )
        centroid += humans[i]->GetAbsOrigin();
    centroid /= (float)humans.Count();

    // Sort spawners by distance to centroid
    float bestDist = FLT_MAX;
    for ( int i = 0; i < allSpawners.Count(); i++ )
    {
        float dist = allSpawners[i]->GetAbsOrigin().DistTo( centroid );
        if ( dist < bestDist )
            bestDist = dist;
    }

    // Gather all spawners within the spread threshold of the best one
    for ( int i = 0; i < allSpawners.Count(); i++ )
    {
        float dist = allSpawners[i]->GetAbsOrigin().DistTo( centroid );
        if ( dist <= bestDist + SPAWNER_SPREAD_THRESHOLD )
            outSpawners.AddToTail( allSpawners[i] );
    }
}

void CZMAIZombieMaster::LogAllSpawners() const
{
    Msg( "[AI ZM] === Map Spawner Report ===\n" );
    int total = 0;
    int active = 0;
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_zombiespawn" )) != nullptr )
    {
        CZMEntZombieSpawn* pSpawn = dynamic_cast<CZMEntZombieSpawn*>( pEnt );
        if ( !pSpawn )
            continue;
        total++;
        int flags = pSpawn->GetZombieFlags();
        Vector pos = pSpawn->GetAbsOrigin();
        bool bActive = pSpawn->IsActive();
        if ( bActive ) active++;

        char classStr[256] = "";
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            ZombieClass_t zc = (ZombieClass_t)i;
            if ( !CZMBaseZombie::IsValidClass( zc ) ) continue;
            if ( flags & (1 << i) )
            {
                if ( classStr[0] != '\0' )
                    Q_strncat( classStr, ", ", sizeof(classStr) );
                Q_strncat( classStr, CZMBaseZombie::ClassToName( zc ), sizeof(classStr) );
            }
        }
        if ( flags == 0 )
            Q_strncpy( classStr, "all (default)", sizeof(classStr) );

        Msg( "  Spawner %i: pos=(%.0f,%.0f,%.0f) active=%s flags=0x%X classes=[%s]\n",
            total - 1, pos.x, pos.y, pos.z, bActive ? "YES" : "NO", flags, classStr );
    }
    Msg( "[AI ZM] Total spawners: %i, Active: %i\n", total, active );
    Msg( "[AI ZM] === End Spawner Report ===\n" );
}

bool CZMAIZombieMaster::SpawnerSupportsClass( CZMEntZombieSpawn* pSpawner, ZombieClass_t zclass ) const
{
    if ( !pSpawner || zclass == ZMCLASS_INVALID ) return false;
    int flags = pSpawner->GetZombieFlags();
    return ( flags == 0 || (flags & (1 << (int)zclass)) != 0 );
}

//
// Check if a zombie class is at its per-type population limit.
//
bool CZMAIZombieMaster::IsClassAtTypeLimit( ZombieClass_t zclass ) const
{
    if ( !zm_sv_zombie_type_limits.GetBool() )
        return false;

    int limit = -1;
    switch ( zclass )
    {
    case ZMCLASS_BANSHEE:   limit = zm_sv_zombie_max_banshee.GetInt(); break;
    case ZMCLASS_HULK:      limit = zm_sv_zombie_max_hulk.GetInt(); break;
    case ZMCLASS_DRIFTER:   limit = zm_sv_zombie_max_drifter.GetInt(); break;
    case ZMCLASS_IMMOLATOR: limit = zm_sv_zombie_max_immolator.GetInt(); break;
    default: return false; // Shamblers have no per-type limit
    }

    if ( limit < 0 )
        return false;

    // Count alive zombies of this class
    const char* szClass = CZMBaseZombie::ClassToName( zclass );
    if ( !szClass )
        return false;

    int count = 0;
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, szClass )) != nullptr )
    {
        if ( pEnt->IsAlive() )
            count++;
    }

    return ( count >= limit );
}

bool CZMAIZombieMaster::CanAffordClass( ZombieClass_t zclass, CZMPlayer* pZM, int reservedResources ) const
{
    if ( zclass == ZMCLASS_INVALID || !pZM ) return false;
    int cost = CZMBaseZombie::GetCost( zclass );
    int available = pZM->GetResources() - reservedResources;
    return cost >= 0 && available >= cost && CZMBaseZombie::HasEnoughPopToSpawn( zclass );
}

//
// Weighted zombie class picker: 60% shambler, 10% each special.
// Dynamically redistributes weight for classes not supported by the spawner
// or at their per-type population limit.
//
ZombieClass_t CZMAIZombieMaster::PickWeightedClass( CZMEntZombieSpawn* pSpawner, CZMPlayer* pZM ) const
{
    if ( !pSpawner || !pZM ) return ZMCLASS_INVALID;

    int totalWeight = 0;
    int weights[ZMCLASS_MAX];
    bool valid[ZMCLASS_MAX];

    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zc = (ZombieClass_t)i;
        valid[i] = CZMBaseZombie::IsValidClass( zc )
                && SpawnerSupportsClass( pSpawner, zc )
                && CZMBaseZombie::HasEnoughPopToSpawn( zc )
                && CanAffordClass( zc, pZM, 0 )
                && !IsClassAtTypeLimit( zc );
        weights[i] = 0;
    }

    int validCount = 0;
    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        if ( valid[i] ) validCount++;
    }

    if ( validCount == 0 )
        return ZMCLASS_INVALID;

    if ( validCount == 1 )
    {
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            if ( valid[i] ) return (ZombieClass_t)i;
        }
    }

    // Redistribute weight from invalid classes equally among valid ones
    int redistributed = 0;
    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        if ( !valid[i] )
            redistributed += g_ZombieWeights[i];
    }

    int bonusEach = redistributed / validCount;
    int remainder = redistributed % validCount;

    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        if ( valid[i] )
        {
            weights[i] = g_ZombieWeights[i] + bonusEach;
            if ( remainder > 0 )
            {
                weights[i]++;
                remainder--;
            }
            totalWeight += weights[i];
        }
    }

    int roll = random->RandomInt( 1, totalWeight );
    int cumulative = 0;
    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        if ( !valid[i] ) continue;
        cumulative += weights[i];
        if ( roll <= cumulative )
            return (ZombieClass_t)i;
    }

    return ZMCLASS_INVALID;
}

bool CZMAIZombieMaster::TrySpawnZombies( ZombieClass_t zclass, int count, CZMEntZombieSpawn* pSpawner )
{
    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM || zclass == ZMCLASS_INVALID || !pSpawner )
        return false;

    int cost = CZMBaseZombie::GetCost( zclass );
    int available = pZM->GetResources() - m_iReservedResources;
    int canAfford = available / MAX( cost, 1 );
    int toSpawn = MIN( count, canAfford );

    if ( toSpawn <= 0 )
        return false;

    if ( !CZMBaseZombie::HasEnoughPopToSpawn( zclass ) )
        return false;

    if ( !pSpawner->IsActive() )
    {
        inputdata_t dummy;
        pSpawner->InputUnhide( dummy );
    }

    pSpawner->QueueUnit( pZM, zclass, toSpawn );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Spawning %i x %s (cost: %i each, reserved: %i, res left: %i)\n",
            toSpawn, CZMBaseZombie::ClassToName( zclass ), cost, m_iReservedResources, pZM->GetResources() );

    return true;
}

//
// Find the highest-cost trap on the map (for resource reservation).
// Respects view mode filtering.
//
int CZMAIZombieMaster::GetHighestTrapCost( CZMPlayer* pZM ) const
{
    int highest = 0;
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_manipulate" )) != nullptr )
    {
        CZMEntManipulate* pTrap = dynamic_cast<CZMEntManipulate*>( pEnt );
        if ( !pTrap || !pTrap->IsActive() )
            continue;
        // Reserve is global — always account for the most expensive trap on the map
        // regardless of camera position, since the camera moves around.
        int cost = pTrap->GetCost();
        if ( cost > highest )
            highest = cost;
    }
    return highest;
}

CZMEntManipulate* CZMAIZombieMaster::FindBestTrap( CZMPlayer* pZM ) const
{
    float flTrapRange = zm_sv_ai_zm_trap_range.GetFloat();
    float flCooldown = zm_sv_ai_zm_trap_cooldown.GetFloat();

    CZMEntManipulate* pBest = nullptr;
    float flBestDist = FLT_MAX;

    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_manipulate" )) != nullptr )
    {
        CZMEntManipulate* pTrap = dynamic_cast<CZMEntManipulate*>( pEnt );
        if ( !pTrap || !pTrap->IsActive() )
            continue;

        if ( !IsEntityInView( pZM, pTrap ) )
            continue;

        int idx = m_EntityCooldowns.Find( pTrap->entindex() );
        if ( idx != m_EntityCooldowns.InvalidIndex() )
        {
            if ( gpGlobals->curtime < m_EntityCooldowns[idx] + flCooldown )
                continue;
        }

        float dist = 0.0f;
        CBasePlayer* pNearest = FindNearestHuman( pTrap->GetAbsOrigin(), &dist );
        if ( !pNearest || dist > flTrapRange )
            continue;

        if ( dist < flBestDist )
        {
            flBestDist = dist;
            pBest = pTrap;
        }
    }

    return pBest;
}

//
// Camera system: move around the map looking at survivors like a real player.
// Switches between targets every few seconds and smoothly moves to the new position.
//
void CZMAIZombieMaster::UpdateCamera( CZMPlayer* pZM )
{
    if ( !pZM )
        return;

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );

    if ( humans.Count() == 0 )
        return;

    // Pick a new camera target periodically
    if ( gpGlobals->curtime >= m_flCameraNextSwitch || m_iCameraTargetIndex <= 0 )
    {
        int newIdx = humans[ random->RandomInt( 0, humans.Count() - 1 ) ]->entindex();
        m_iCameraTargetIndex = newIdx;
        m_flCameraNextSwitch = gpGlobals->curtime + random->RandomFloat( CAMERA_SWITCH_MIN, CAMERA_SWITCH_MAX );
    }

    // Find the current target
    CBasePlayer* pTarget = dynamic_cast<CBasePlayer*>( UTIL_EntityByIndex( m_iCameraTargetIndex ) );
    if ( !pTarget || !pTarget->IsAlive() || pTarget->GetTeamNumber() != ZMTEAM_HUMAN )
    {
        m_iCameraTargetIndex = 0;
        return;
    }

    // Calculate desired camera position: behind and above the target
    Vector fwd;
    AngleVectors( pTarget->EyeAngles(), &fwd );
    fwd.z = 0.0f;
    if ( fwd.Length() > 0.01f )
        fwd.NormalizeInPlace();
    else
        fwd = Vector( 1, 0, 0 );

    Vector vecDesired = pTarget->GetAbsOrigin() - fwd * CAMERA_BACK_DIST + Vector( 0, 0, CAMERA_HEIGHT );

    // Calculate desired look angle: look at the target
    Vector vecToTarget = pTarget->EyePosition() - vecDesired;
    QAngle angDesired;
    VectorAngles( vecToTarget, angDesired );

    // If camera hasn't been initialized yet, teleport immediately
    if ( m_vecCameraPos.IsZero() )
    {
        m_vecCameraPos = vecDesired;
        m_angCameraAng = angDesired;
    }
    else
    {
        // Smooth interpolation
        float dt = gpGlobals->frametime;
        if ( dt <= 0.0f )
            dt = 0.016f;

        float moveRate = CAMERA_MOVE_SPEED * dt;
        float turnRate = CAMERA_TURN_SPEED * dt;

        // Lerp position
        Vector vecDelta = vecDesired - m_vecCameraPos;
        float flDist = vecDelta.Length();
        if ( flDist > moveRate )
        {
            vecDelta.NormalizeInPlace();
            m_vecCameraPos += vecDelta * moveRate;
        }
        else
        {
            m_vecCameraPos = vecDesired;
        }

        // Lerp angles
        for ( int i = 0; i < 3; i++ )
        {
            float diff = AngleNormalize( angDesired[i] - m_angCameraAng[i] );
            if ( fabsf( diff ) > turnRate )
                m_angCameraAng[i] += ( diff > 0 ? turnRate : -turnRate );
            else
                m_angCameraAng[i] = angDesired[i];
        }
    }

    // Apply to the ZM bot — use Teleport for reliable position updates
    pZM->Teleport( &m_vecCameraPos, &m_angCameraAng, nullptr );
    pZM->SnapEyeAngles( m_angCameraAng );
}

//
// Cull stranded zombies that are too far from survivors and will never reach them.
// Only culls when pop cap is stressed (>80% full).
//
void CZMAIZombieMaster::CullStrandedZombies( CZMPlayer* pZM )
{
    float flCullTime = zm_sv_ai_zm_cull_time.GetFloat();
    if ( flCullTime <= 0.0f )
        return;

    if ( gpGlobals->curtime < m_flNextCullTime )
        return;
    m_flNextCullTime = gpGlobals->curtime + CULL_CHECK_INTERVAL;

    // Only cull when pop cap is stressed
    CZMRules* pRules = ZMRules();
    if ( !pRules )
        return;

    int curPop = pRules->GetZombiePop();
    int maxPop = zm_sv_zombiemax.GetInt();
    if ( maxPop <= 0 || (float)curPop / (float)maxPop < CULL_POP_THRESHOLD )
        return;

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );
    if ( humans.Count() == 0 )
        return;

    static const char* s_szZombieClassnames[] = {
        "npc_zombie", "npc_fastzombie", "npc_poisonzombie",
        "npc_burnzombie", "npc_dragzombie",
    };

    int nCulled = 0;

    for ( int c = 0; c < ARRAYSIZE( s_szZombieClassnames ); c++ )
    {
        CBaseEntity* pEnt = nullptr;
        while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szZombieClassnames[c] )) != nullptr )
        {
            if ( !pEnt->IsAlive() )
                continue;

            CZMBaseZombie* pZombie = dynamic_cast<CZMBaseZombie*>( pEnt );
            if ( !pZombie )
                continue;

            // Don't cull zombies that have active commands (someone rallied them)
            if ( pZombie->GetCommandQueue()->NextCommand() != nullptr )
                continue;

            // Don't cull zombies that have an enemy (they're engaged or chasing)
            if ( pZombie->GetEnemy() != nullptr )
                continue;

            // Don't cull zombies that took damage recently (they're in a fight)
            if ( gpGlobals->curtime - pZombie->GetLastDamageTime() < flCullTime )
                continue;

            // Check distance to nearest survivor
            float flClosestDist = FLT_MAX;
            for ( int h = 0; h < humans.Count(); h++ )
            {
                float dist = pZombie->GetAbsOrigin().DistTo( humans[h]->GetAbsOrigin() );
                if ( dist < flClosestDist )
                    flClosestDist = dist;
            }

            if ( flClosestDist < CULL_DISTANCE_THRESHOLD )
                continue;

            // Kill the zombie to free pop cap
            CTakeDamageInfo dmgInfo( pZM, pZM, 99999.0f, DMG_GENERIC );
            pZombie->TakeDamage( dmgInfo );
            nCulled++;
        }
    }

    if ( nCulled > 0 && zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Culled %i stranded zombie(s) to free pop cap (pop: %i/%i)\n",
            nCulled, curPop, maxPop );
}

//
// Cycle phase: RESERVE
// Accumulate resources until we have enough for the most expensive trap.
// No zombie spawning and no trap firing during this phase — the AI saves up.
// When reserves are met (or no traps exist), transition to SPAWN.
//
void CZMAIZombieMaster::DoReservePhase( CZMPlayer* pZM )
{
    // If there are no traps, skip straight to spawning
    if ( m_iReservedResources <= 0 )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Reserve: no traps on map, moving to SPAWN\n" );
        m_Phase = AIZM_PHASE_SPAWN;
        return;
    }

    if ( pZM->GetResources() >= m_iReservedResources )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Reserve met (res=%i >= reserved=%i), moving to SPAWN\n",
                pZM->GetResources(), m_iReservedResources );
        m_Phase = AIZM_PHASE_SPAWN;
        return;
    }

    // Still accumulating — do nothing (traps fire from Update loop)
}

//
// Cycle phase: SPAWN
// Picks a batch size (1-10) and spawner, then spawns one zombie per tick
// using ALL available resources (no reserve held back). After the burst
// completes (or resources run dry), moves to hidden spawn phase.
// Traps can still fire mid-wave from the Update loop if affordable.
//
void CZMAIZombieMaster::DoSpawnPhase( CZMPlayer* pZM )
{
    if ( gpGlobals->curtime < m_flNextActionTime )
        return;

    // Continue an active spawn burst — spawn one zombie per tick
    if ( m_iSpawnBurstRemaining > 0 && m_hSpawnBurstSpawner.Get() )
    {
        if ( !CanAffordClass( m_SpawnBurstClass, pZM, 0 ) ||
             IsClassAtTypeLimit( m_SpawnBurstClass ) ||
             !CZMBaseZombie::HasEnoughPopToSpawn( m_SpawnBurstClass ) )
        {
            // Can't spawn this class anymore — end burst
            m_iSpawnBurstRemaining = 0;
        }
        else
        {
            TrySpawnZombies( m_SpawnBurstClass, 1, m_hSpawnBurstSpawner.Get() );
            m_iSpawnBurstRemaining--;
        }

        if ( m_iSpawnBurstRemaining <= 0 )
        {
            // Burst finished — move to hidden spawn
            m_Phase = AIZM_PHASE_HIDDEN_SPAWN;
            m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.5f, 2.0f );
        }
        return;
    }

    // No active burst — start a new one

    // Gather spawners near survivors (spreads across multiple if similar distance)
    CUtlVector<CZMEntZombieSpawn*> nearSpawners;
    GatherNearestSpawners( nearSpawners );

    // Filter by view mode
    CUtlVector<CZMEntZombieSpawn*> visibleSpawners;
    for ( int i = 0; i < nearSpawners.Count(); i++ )
    {
        if ( IsEntityInView( pZM, nearSpawners[i] ) )
            visibleSpawners.AddToTail( nearSpawners[i] );
    }

    if ( visibleSpawners.Count() == 0 )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Spawn: no active/visible spawners, retrying\n" );
        m_flNextActionTime = gpGlobals->curtime + 1.0f;
        return;
    }

    // Pick a random spawner from the nearby set to spread spawns across
    CZMEntZombieSpawn* pSpawner = visibleSpawners[ random->RandomInt( 0, visibleSpawners.Count() - 1 ) ];

    ZombieClass_t zclass = PickWeightedClass( pSpawner, pZM );
    if ( zclass == ZMCLASS_INVALID )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Spawn: can't afford any class (res=%i), moving to HIDDEN_SPAWN\n",
                pZM->GetResources() );
        m_Phase = AIZM_PHASE_HIDDEN_SPAWN;
        m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.5f, 2.0f );
        return;
    }

    // Start a burst: pick batch size, spawn first one immediately
    m_iSpawnBurstRemaining = random->RandomInt( 1, 10 );
    m_SpawnBurstClass = zclass;
    m_hSpawnBurstSpawner = pSpawner;

    if ( zm_sv_ai_zm_debug.GetBool() )
    {
        Vector spos = pSpawner->GetAbsOrigin();
        Msg( "[AI ZM] Spawn burst: %i x %s at (%.0f,%.0f,%.0f) [%i spawners nearby]\n",
            m_iSpawnBurstRemaining, CZMBaseZombie::ClassToName( zclass ),
            spos.x, spos.y, spos.z, visibleSpawners.Count() );
    }

    TrySpawnZombies( zclass, 1, pSpawner );
    m_iSpawnBurstRemaining--;

    if ( m_iSpawnBurstRemaining <= 0 )
    {
        m_Phase = AIZM_PHASE_HIDDEN_SPAWN;
        m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.5f, 2.0f );
    }
}

//
// Cycle phase: HIDDEN SPAWN
// Place one surprise zombie behind survivors. Spends freely (no reserve gating).
// Respects per-type limits. Cycles back to RESERVE afterwards.
//
void CZMAIZombieMaster::DoHiddenSpawnPhase( CZMPlayer* pZM )
{
    if ( gpGlobals->curtime < m_flNextActionTime )
        return;

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );
    if ( humans.Count() == 0 )
    {
        m_Phase = AIZM_PHASE_RESERVE;
        return;
    }

    int minCost = 30;
    if ( pZM->GetResources() < minCost )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Hidden spawn: not enough resources (res=%i), skipping to RESERVE\n",
                pZM->GetResources() );
        m_Phase = AIZM_PHASE_RESERVE;
        m_flNextActionTime = gpGlobals->curtime + 0.5f;
        return;
    }

    CBasePlayer* pTarget = humans[ random->RandomInt( 0, humans.Count() - 1 ) ];
    Vector targetPos = pTarget->GetAbsOrigin();

    // Pick class: if hidden_allclasses is on, pick randomly from non-limit-capped types
    ZombieClass_t zclass = ZMCLASS_SHAMBLER;
    if ( zm_sv_hidden_allclasses.GetBool() )
    {
        CUtlVector<ZombieClass_t> validClasses;
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            ZombieClass_t zc = (ZombieClass_t)i;
            if ( CZMBaseZombie::IsValidClass( zc )
                && CZMBaseZombie::HasEnoughPopToSpawn( zc )
                && !IsClassAtTypeLimit( zc ) )
            {
                validClasses.AddToTail( zc );
            }
        }
        if ( validClasses.Count() > 0 )
            zclass = validClasses[ random->RandomInt( 0, validClasses.Count() - 1 ) ];
    }
    else
    {
        if ( IsClassAtTypeLimit( ZMCLASS_SHAMBLER ) )
        {
            m_Phase = AIZM_PHASE_RESERVE;
            m_flNextActionTime = gpGlobals->curtime + 0.5f;
            return;
        }
    }

    bool bSuccess = false;
    for ( int attempt = 0; attempt < 8; attempt++ )
    {
        float dist = random->RandomFloat( 256.0f, 768.0f );
        float angle = random->RandomFloat( 0.0f, 360.0f );
        Vector spawnPos = targetPos;
        spawnPos.x += cos( DEG2RAD( angle ) ) * dist;
        spawnPos.y += sin( DEG2RAD( angle ) ) * dist;

        trace_t tr;
        UTIL_TraceLine( spawnPos + Vector( 0, 0, 128 ), spawnPos - Vector( 0, 0, 256 ), MASK_NPCSOLID, nullptr, COLLISION_GROUP_NONE, &tr );
        if ( tr.fraction == 1.0f || tr.startsolid )
            continue;

        spawnPos = tr.endpos + Vector( 0, 0, 1 );

        int resCost = 0;
        HiddenSpawnError_t result = g_ZMHiddenSpawn.Spawn( zclass, pZM, spawnPos, &resCost );

        if ( result == HSERROR_OK )
        {
            if ( zm_sv_ai_zm_debug.GetBool() )
                Msg( "[AI ZM] Hidden spawn: %s near '%s' at (%.0f,%.0f,%.0f) cost=%i\n",
                    CZMBaseZombie::ClassToName( zclass ), pTarget->GetPlayerName(),
                    spawnPos.x, spawnPos.y, spawnPos.z, resCost );
            bSuccess = true;
            break;
        }
    }

    if ( !bSuccess && zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Hidden spawn: all attempts failed\n" );

    // Cycle back to RESERVE to save up for the next trap
    m_Phase = AIZM_PHASE_RESERVE;
    m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.3f, 1.0f );
}

//
// Try to fire a trap opportunistically when a survivor is within range.
// Fires if the AI can afford the trap cost. Resource management is handled
// by the cycle phases (RESERVE saves up, SPAWN/HIDDEN_SPAWN spend freely).
// Respects view mode filtering and per-trap cooldown.
//
bool CZMAIZombieMaster::TryFireTrap( CZMPlayer* pZM )
{
    CZMEntManipulate* pTrap = FindBestTrap( pZM );
    if ( !pTrap )
        return false;

    int trapCost = pTrap->GetCost();
    if ( trapCost > 0 && pZM->GetResources() < trapCost )
        return false;

    if ( trapCost > 0 )
        pZM->IncResources( -trapCost, false );

    pTrap->Trigger( pZM );

    int idx = m_EntityCooldowns.Find( pTrap->entindex() );
    if ( idx != m_EntityCooldowns.InvalidIndex() )
        m_EntityCooldowns[idx] = gpGlobals->curtime;
    else
        m_EntityCooldowns.Insert( pTrap->entindex(), gpGlobals->curtime );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Fired trap (cost: %i, res left: %i, cooldown: %.1fs)\n",
            trapCost, pZM->GetResources(), zm_sv_ai_zm_trap_cooldown.GetFloat() );

    return true;
}

//
// Try to detonate an explosive barrel. Shares cooldown with traps.
// Respects view mode filtering.
//
bool CZMAIZombieMaster::TryDetonateBarrel( CZMPlayer* pZM )
{
    float flTriggerRange = zm_sv_ai_zm_trap_range.GetFloat();
    float flCooldown = zm_sv_ai_zm_trap_cooldown.GetFloat();

    CBreakableProp* pBestBarrel = nullptr;
    float flBestDist = FLT_MAX;

    static const char* s_szPropClasses[] = { "prop_physics", "prop_physics_multiplayer" };
    for ( int pc = 0; pc < ARRAYSIZE( s_szPropClasses ); pc++ )
    {
        CBaseEntity* pEnt = nullptr;
        while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szPropClasses[pc] )) != nullptr )
        {
            CBreakableProp* pProp = dynamic_cast<CBreakableProp*>( pEnt );
            if ( !pProp || !pProp->IsAlive() )
                continue;

            if ( pProp->GetExplosiveDamage() <= 0.0f )
                continue;

            if ( !IsEntityInView( pZM, pProp ) )
                continue;

            int idx = m_EntityCooldowns.Find( pProp->entindex() );
            if ( idx != m_EntityCooldowns.InvalidIndex() )
            {
                if ( gpGlobals->curtime < m_EntityCooldowns[idx] + flCooldown )
                    continue;
            }

            float dist = 0.0f;
            CBasePlayer* pNearest = FindNearestHuman( pProp->GetAbsOrigin(), &dist );
            if ( !pNearest || dist > flTriggerRange )
                continue;

            if ( dist < flBestDist )
            {
                flBestDist = dist;
                pBestBarrel = pProp;
            }
        }
    }

    if ( !pBestBarrel )
        return false;

    CTakeDamageInfo info( pZM, pZM, 1000.0f, DMG_BLAST );
    info.SetDamagePosition( pBestBarrel->GetAbsOrigin() );
    pBestBarrel->TakeDamage( info );

    int idx = m_EntityCooldowns.Find( pBestBarrel->entindex() );
    if ( idx != m_EntityCooldowns.InvalidIndex() )
        m_EntityCooldowns[idx] = gpGlobals->curtime;
    else
        m_EntityCooldowns.Insert( pBestBarrel->entindex(), gpGlobals->curtime );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Detonated barrel at (%.0f,%.0f,%.0f), survivor %.0f units away, cooldown: %.1fs\n",
            pBestBarrel->GetAbsOrigin().x, pBestBarrel->GetAbsOrigin().y, pBestBarrel->GetAbsOrigin().z,
            flBestDist, flCooldown );

    return true;
}

//
// Main update loop
//
void CZMAIZombieMaster::Update()
{
    if ( !IsActive() )
        return;

    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM )
        return;

    // Camera always updates (even during rush prevention) for smooth movement
    UpdateCamera( pZM );

    if ( gpGlobals->curtime - m_flLastUpdateTime < 0.1f )
        return;
    m_flLastUpdateTime = gpGlobals->curtime;

    // Rush prevention: only look around (camera runs above), no actions
    float flRushTime = zm_sv_ai_zm_rush_prevention.GetFloat();
    float flTimeSinceRound = gpGlobals->curtime - m_flRoundStartTime;
    if ( flRushTime > 0.0f && flTimeSinceRound < flRushTime )
    {
        if ( zm_sv_ai_zm_debug.GetBool() && (int)(gpGlobals->curtime * 2) % 10 == 0 )
            Msg( "[AI ZM] Rush prevention active (%.1f/%.1f)\n", flTimeSinceRound, flRushTime );
        return;
    }

    // Log spawners on first real update
    if ( !m_bLoggedSpawners )
    {
        LogAllSpawners();
        m_bLoggedSpawners = true;
    }

    // Always keep reserves up to date: hold back the cost of the most expensive trap
    m_iReservedResources = GetHighestTrapCost( pZM );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Update: phase=%i res=%i reserved=%i curtime=%.1f\n",
            (int)m_Phase, pZM->GetResources(), m_iReservedResources, gpGlobals->curtime );

    // Run the current cycle phase: RESERVE -> SPAWN -> HIDDEN_SPAWN -> RESERVE
    // Traps/barrels only fire during SPAWN and HIDDEN_SPAWN so the RESERVE
    // phase can accumulate resources undisturbed.
    switch ( m_Phase )
    {
    case AIZM_PHASE_RESERVE:
        DoReservePhase( pZM );
        break;
    case AIZM_PHASE_SPAWN:
        TryFireTrap( pZM );
        TryDetonateBarrel( pZM );
        DoSpawnPhase( pZM );
        break;
    case AIZM_PHASE_HIDDEN_SPAWN:
        TryFireTrap( pZM );
        TryDetonateBarrel( pZM );
        DoHiddenSpawnPhase( pZM );
        break;
    }

    // Rally idle zombies toward survivors continuously
    RallyZombiesToSurvivors();

    // Cull stranded zombies when pop cap is stressed
    CullStrandedZombies( pZM );
}

//
// Rally zombies toward survivors with target splitting
//
CBasePlayer* CZMAIZombieMaster::PickRallyTarget( const Vector& zombiePos ) const
{
    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );

    if ( humans.Count() == 0 )
        return nullptr;

    if ( humans.Count() == 1 )
        return humans[0];

    float flBuffer = zm_sv_ai_zm_rally_buffer.GetFloat();

    float flBestDist = FLT_MAX;
    float flSecondDist = FLT_MAX;
    int iBest = -1;
    int iSecond = -1;

    for ( int i = 0; i < humans.Count(); i++ )
    {
        float dist = humans[i]->GetAbsOrigin().DistTo( zombiePos );
        if ( dist < flBestDist )
        {
            flSecondDist = flBestDist;
            iSecond = iBest;
            flBestDist = dist;
            iBest = i;
        }
        else if ( dist < flSecondDist )
        {
            flSecondDist = dist;
            iSecond = i;
        }
    }

    if ( iSecond >= 0 && (flSecondDist - flBestDist) < flBuffer )
    {
        if ( random->RandomInt( 0, 1 ) == 0 )
            return humans[iSecond];
    }

    return ( iBest >= 0 ) ? humans[iBest] : nullptr;
}

void CZMAIZombieMaster::RallyZombiesToSurvivors()
{
    if ( gpGlobals->curtime < m_flNextRallyTime )
        return;

    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM ) return;

    float flRallyRange = 4096.0f;

    static const char* s_szZombieClassnames[] = {
        "npc_zombie", "npc_fastzombie", "npc_poisonzombie",
        "npc_burnzombie", "npc_dragzombie",
    };

    for ( int c = 0; c < ARRAYSIZE( s_szZombieClassnames ); c++ )
    {
        CBaseEntity* pEnt = nullptr;
        while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szZombieClassnames[c] )) != nullptr )
        {
            if ( !pEnt->IsAlive() )
                continue;

            CZMBaseZombie* pZombie = dynamic_cast<CZMBaseZombie*>( pEnt );
            if ( !pZombie )
                continue;

            if ( pZombie->GetCommandQueue()->NextCommand() != nullptr )
                continue;

            CBasePlayer* pTarget = PickRallyTarget( pZombie->GetAbsOrigin() );
            if ( !pTarget )
                continue;

            float dist = pZombie->GetAbsOrigin().DistTo( pTarget->GetAbsOrigin() );
            if ( dist > flRallyRange )
                continue;

            pZombie->Command( pZM, pTarget->GetAbsOrigin() );
        }
    }

    m_flNextRallyTime = gpGlobals->curtime + zm_sv_ai_zm_rally_interval.GetFloat();
}
