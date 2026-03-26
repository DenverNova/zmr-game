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
ConVar zm_sv_ai_zm_trap_range( "zm_sv_ai_zm_trap_range", "512", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Survivor proximity required to trigger a trap or barrel." );
ConVar zm_sv_ai_zm_trap_cooldown( "zm_sv_ai_zm_trap_cooldown", "30.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Per-entity cooldown in seconds before AI can re-use the same trap or barrel." );
ConVar zm_sv_ai_zm_rush_prevention( "zm_sv_ai_zm_rush_prevention", "15.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds at round start before AI ZM can act. Does not affect human ZMs." );
ConVar zm_sv_ai_zm_rally_interval( "zm_sv_ai_zm_rally_interval", "3.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds between idle zombie rally commands." );
ConVar zm_sv_ai_zm_rally_buffer( "zm_sv_ai_zm_rally_buffer", "256.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Distance buffer for splitting rally targets between survivors." );
ConVar zm_sv_ai_zm_cull_time( "zm_sv_ai_zm_cull_time", "45.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds a zombie must be stranded before being culled. 0=disabled." );

CZMAIZombieMaster g_ZMAIZombieMaster;

// Weighted type selection: 60% shambler, 10% each special (5 types total)
static const int g_ZombieWeights[ZMCLASS_MAX] = { 60, 10, 10, 10, 10 };

// Distance threshold for considering two spawners "nearly the same distance"
#define SPAWNER_SPREAD_THRESHOLD 512.0f

// Camera smoothing: exponential lerp factor per second (lower = smoother/slower)
#define CAMERA_SMOOTH_FACTOR  6.0f   // Exponential lerp rate per second for camera position
#define CAMERA_DESIRED_FACTOR 10.0f  // How fast the intermediate desired position tracks the raw goal
#define CAMERA_HEIGHT_MIN     200.0f
#define CAMERA_HEIGHT_MAX     450.0f
#define CAMERA_BACK_DIST      350.0f

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
    m_Phase = AIZM_PHASE_SPAWN;
    m_flNextActionTime = 0.0f;
    m_iReservedResources = 0;
    m_bTrapsUnlocked = false;
    m_bTrapFiredDuringCycle = false;
    m_EntityCooldowns.RemoveAll();
    m_flRoundStartTime = gpGlobals ? gpGlobals->curtime : 0.0f;
    m_flNextRallyTime = 0.0f;
    m_flLastUpdateTime = 0.0f;
    m_bLoggedSpawners = false;

    m_iSpawnBurstRemaining = 0;
    m_SpawnBurstClass = ZMCLASS_INVALID;
    m_hSpawnBurstSpawner = nullptr;

    m_iHiddenSpawnsRemaining = 0;
    m_flHiddenSpawnDeadline = 0.0f;

    m_vecCameraPos.Init();
    m_vecCameraDesired.Init();
    m_angCameraAng.Init();
    m_iFocusedTargetIndex = 0;
    m_flFocusedTargetExpiry = 0.0f;
    m_iLastRoundRobinSlot = -1;

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
    if ( cost < 0 || !CZMBaseZombie::HasEnoughPopToSpawn( zclass ) )
        return false;

    // Need enough to cover the zombie cost PLUS maintain the reserve.
    // If reservedResources is 0 (no traps, or first cycle), just need cost.
    return pZM->GetResources() >= ( reservedResources + cost );
}

//
// Weighted zombie class picker: 60% shambler, 10% each special.
// Does NOT check affordability — the AI will save up for whatever it rolls.
// Filters by: spawner support, pop cap, per-type limits.
// Blocked types have their weight redistributed equally among valid types
// so the total always sums to 100. e.g. if hulk and banshee are blocked,
// their 20% redistributes 6-7% each to the remaining 3 valid types.
// If only one type is valid (e.g. banshee-only spawner), it gets 100%.
//
ZombieClass_t CZMAIZombieMaster::PickWeightedClass( CZMEntZombieSpawn* pSpawner, CZMPlayer* pZM, int holdBack ) const
{
    if ( !pSpawner || !pZM ) return ZMCLASS_INVALID;

    int totalWeight = 0;
    int weights[ZMCLASS_MAX];
    bool valid[ZMCLASS_MAX];

    // Determine which classes are valid (no affordability check — we save up)
    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zc = (ZombieClass_t)i;
        valid[i] = CZMBaseZombie::IsValidClass( zc )
                && SpawnerSupportsClass( pSpawner, zc )
                && CZMBaseZombie::HasEnoughPopToSpawn( zc )
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

    // Redistribute weight from blocked classes equally among valid ones.
    // This ensures the total always sums to 100 and a spawner that only
    // allows certain types gives them proportionally more weight.
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

    if ( zm_sv_ai_zm_debug.GetBool() )
    {
        char buf[256] = "";
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            ZombieClass_t zc = (ZombieClass_t)i;
            const char* name = CZMBaseZombie::ClassToName( zc );
            char tmp[64];
            Q_snprintf( tmp, sizeof(tmp), "%s=%i%s ", name ? name : "?", weights[i], valid[i] ? "" : "(X)" );
            Q_strncat( buf, tmp, sizeof(buf) );
        }
        Msg( "[AI ZM] PickWeightedClass: %s total=%i\n", buf, totalWeight );
    }

    int roll = random->RandomInt( 1, totalWeight );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] PickWeightedClass: rolled %i/%i\n", roll, totalWeight );

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
    if ( cost <= 0 || pZM->GetResources() < cost )
        return false;

    if ( !CZMBaseZombie::HasEnoughPopToSpawn( zclass ) )
        return false;

    if ( !pSpawner->IsActive() )
    {
        inputdata_t dummy;
        pSpawner->InputUnhide( dummy );
    }

    // Only spawn what we can actually afford right now (1 at a time)
    int toSpawn = MIN( count, 1 );
    pSpawner->QueueUnit( pZM, zclass, toSpawn );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Spawning %i x %s (cost: %i each, res left: %i)\n",
            toSpawn, CZMBaseZombie::ClassToName( zclass ), cost, pZM->GetResources() );

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
// GetFocusedTarget: returns the survivor the AI is currently "picking on".
// Round-robin through all living survivors, focusing on each for 5-20 seconds.
//
CBasePlayer* CZMAIZombieMaster::GetFocusedTarget() const
{
    if ( m_iFocusedTargetIndex <= 0 )
        return nullptr;

    CBasePlayer* pTarget = dynamic_cast<CBasePlayer*>( UTIL_EntityByIndex( m_iFocusedTargetIndex ) );
    if ( pTarget && pTarget->IsAlive() && pTarget->GetTeamNumber() == ZMTEAM_HUMAN )
        return pTarget;

    return nullptr;
}

//
// GatherNearestSpawnersToTarget: find spawners near a specific player.
//
void CZMAIZombieMaster::GatherNearestSpawnersToTarget( CBasePlayer* pTarget, CUtlVector<CZMEntZombieSpawn*>& outSpawners ) const
{
    if ( !pTarget )
        return;

    CUtlVector<CZMEntZombieSpawn*> allSpawners;
    GatherActiveSpawners( allSpawners );

    if ( allSpawners.Count() == 0 )
        return;

    Vector targetPos = pTarget->GetAbsOrigin();

    // Find the closest spawner to this target
    float bestDist = FLT_MAX;
    for ( int i = 0; i < allSpawners.Count(); i++ )
    {
        float dist = allSpawners[i]->GetAbsOrigin().DistTo( targetPos );
        if ( dist < bestDist )
            bestDist = dist;
    }

    // Gather all spawners within the spread threshold of the best one
    for ( int i = 0; i < allSpawners.Count(); i++ )
    {
        float dist = allSpawners[i]->GetAbsOrigin().DistTo( targetPos );
        if ( dist <= bestDist + SPAWNER_SPREAD_THRESHOLD )
            outSpawners.AddToTail( allSpawners[i] );
    }
}

//
// Camera system: smoothly glide around the map watching the focused survivor.
// Uses exponential smoothing for natural deceleration like a real player panning.
// The focused target is picked by round-robin (5-20s per survivor).
// Never teleports — always interpolates, even on the first frame.
// Adjusts height dynamically to avoid obstacles blocking the view.
//
void CZMAIZombieMaster::UpdateCamera( CZMPlayer* pZM )
{
    if ( !pZM )
        return;

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );

    if ( humans.Count() == 0 )
        return;

    // Round-robin target selection: cycle through all living survivors
    bool bNeedNewTarget = ( gpGlobals->curtime >= m_flFocusedTargetExpiry || m_iFocusedTargetIndex <= 0 );

    // Also switch if the current target died
    if ( !bNeedNewTarget )
    {
        CBasePlayer* pCur = dynamic_cast<CBasePlayer*>( UTIL_EntityByIndex( m_iFocusedTargetIndex ) );
        if ( !pCur || !pCur->IsAlive() || pCur->GetTeamNumber() != ZMTEAM_HUMAN )
            bNeedNewTarget = true;
    }

    if ( bNeedNewTarget )
    {
        // Advance to the next slot in round-robin order
        m_iLastRoundRobinSlot++;
        if ( m_iLastRoundRobinSlot >= humans.Count() )
            m_iLastRoundRobinSlot = 0;

        m_iFocusedTargetIndex = humans[ m_iLastRoundRobinSlot ]->entindex();
        m_flFocusedTargetExpiry = gpGlobals->curtime + random->RandomFloat( 5.0f, 20.0f );

        if ( zm_sv_ai_zm_debug.GetBool() )
        {
            CBasePlayer* pNew = humans[ m_iLastRoundRobinSlot ];
            Msg( "[AI ZM] Camera: focusing on %s for %.1fs (slot %i/%i)\n",
                pNew->GetPlayerName(), m_flFocusedTargetExpiry - gpGlobals->curtime,
                m_iLastRoundRobinSlot + 1, humans.Count() );
        }
    }

    // Find the current target
    CBasePlayer* pTarget = dynamic_cast<CBasePlayer*>( UTIL_EntityByIndex( m_iFocusedTargetIndex ) );
    if ( !pTarget || !pTarget->IsAlive() || pTarget->GetTeamNumber() != ZMTEAM_HUMAN )
    {
        m_iFocusedTargetIndex = 0;
        return;
    }

    // Calculate raw desired camera position: directly above and offset from target.
    // Uses a fixed offset direction (not player facing) to prevent constant whipping
    // when the target looks around.
    Vector targetPos = pTarget->GetAbsOrigin();
    Vector targetEye = pTarget->EyePosition();

    // Fixed offset: position camera to the south of the target (negative Y)
    // This avoids the jerkiness caused by tracking player eye angles.
    Vector vecOffset = Vector( 0, -1, 0 ) * CAMERA_BACK_DIST;
    Vector vecRawDesired = targetPos + vecOffset + Vector( 0, 0, CAMERA_HEIGHT_MIN );

    // Obstacle avoidance: raise camera height until we have a clear view of the target
    trace_t tr;
    for ( float h = CAMERA_HEIGHT_MIN; h <= CAMERA_HEIGHT_MAX; h += 50.0f )
    {
        Vector testPos = targetPos + vecOffset + Vector( 0, 0, h );
        UTIL_TraceLine( testPos, targetEye, MASK_VISIBLE, pZM, COLLISION_GROUP_NONE, &tr );
        if ( tr.fraction >= 0.95f )
        {
            vecRawDesired = testPos;
            break;
        }
        vecRawDesired = testPos;
    }

    // First frame: snap everything to avoid starting from origin
    if ( m_vecCameraPos.IsZero() )
    {
        m_vecCameraPos = vecRawDesired;
        m_vecCameraDesired = vecRawDesired;

        Vector vecToTarget = targetEye - vecRawDesired;
        QAngle angInit;
        VectorAngles( vecToTarget, angInit );
        m_angCameraAng = angInit;

        pZM->Teleport( &m_vecCameraPos, &m_angCameraAng, nullptr );
        pZM->SnapEyeAngles( m_angCameraAng );
        return;
    }

    float dt = gpGlobals->frametime;
    if ( dt <= 0.0f )
        dt = 0.016f;
    if ( dt > 0.05f )
        dt = 0.05f; // Clamp to prevent frame-hitch jumps

    // Double-smoothing: first smooth the desired position toward the raw goal,
    // then smooth the actual camera toward the smoothed desired.
    // This eliminates jitter from rapid target movement.
    float tDesired = 1.0f - expf( -CAMERA_DESIRED_FACTOR * dt );
    m_vecCameraDesired += ( vecRawDesired - m_vecCameraDesired ) * tDesired;

    float tCamera = 1.0f - expf( -CAMERA_SMOOTH_FACTOR * dt );
    m_vecCameraPos += ( m_vecCameraDesired - m_vecCameraPos ) * tCamera;

    // Look angle: smoothly track the target
    Vector vecToTarget = targetEye - m_vecCameraPos;
    QAngle angDesired;
    VectorAngles( vecToTarget, angDesired );

    for ( int i = 0; i < 3; i++ )
    {
        float diff = AngleNormalize( angDesired[i] - m_angCameraAng[i] );
        m_angCameraAng[i] += diff * tCamera;
    }

    // Apply to the ZM bot
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
// Only entered when a trap was fired during SPAWN or HIDDEN_SPAWN and the
// reserve needs replenishing. Save resources without spawning anything until
// resources >= highest trap cost. Then -> SPAWN. No traps fire during this
// phase (the AI is rebuilding its reserve).
//
void CZMAIZombieMaster::DoReservePhase( CZMPlayer* pZM )
{
    // If there are no traps on the map, skip to spawning
    if ( m_iReservedResources <= 0 )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Reserve: no traps on map, moving to SPAWN\n" );
        m_bTrapFiredDuringCycle = false;
        m_Phase = AIZM_PHASE_SPAWN;
        return;
    }

    if ( pZM->GetResources() >= m_iReservedResources )
    {
        m_bTrapsUnlocked = true;
        m_bTrapFiredDuringCycle = false;
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Reserve replenished (res=%i >= reserved=%i), moving to SPAWN\n",
                pZM->GetResources(), m_iReservedResources );
        m_Phase = AIZM_PHASE_SPAWN;
        return;
    }

    // Still accumulating — do nothing, no spawning allowed
}

//
// Cycle phase: SPAWN
// Internal queue of 1-15 zombies. For each zombie in the burst:
//   1. Find the best spawner near the focused survivor (dynamic — rechecked every tick).
//   2. If the spawner changed, re-roll the class using weighted selection for that spawner.
//   3. If we can afford reserve + cost, spawn it and roll the next one.
//   4. If we can't afford it, wait for resources to accumulate.
// If a trap fires mid-spawn and drops resources below reserve, the AI continues
// spawning with just the raw cost (no waiting to re-reserve). The RESERVE phase
// handles replenishment after the cycle completes.
//
void CZMAIZombieMaster::DoSpawnPhase( CZMPlayer* pZM )
{
    if ( gpGlobals->curtime < m_flNextActionTime )
        return;

    // Hold back the reserve amount if traps are unlocked AND we still have it.
    // If a trap fired mid-spawn and we're below reserve, holdBack drops to 0
    // so remaining resources go directly to finishing the spawn plan.
    int holdBack = 0;
    if ( m_bTrapsUnlocked && pZM->GetResources() >= m_iReservedResources )
        holdBack = m_iReservedResources;

    // === Find spawners near the focused target ===
    CUtlVector<CZMEntZombieSpawn*> nearSpawners;
    CBasePlayer* pFocused = GetFocusedTarget();
    if ( pFocused )
        GatherNearestSpawnersToTarget( pFocused, nearSpawners );
    else
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

    // Keep the current spawner if it's still in the nearby visible set.
    // Only pick a new one if the current spawner dropped out (survivor moved away).
    CZMEntZombieSpawn* pBestSpawner = nullptr;
    if ( m_hSpawnBurstSpawner.Get() )
    {
        for ( int i = 0; i < visibleSpawners.Count(); i++ )
        {
            if ( visibleSpawners[i] == m_hSpawnBurstSpawner.Get() )
            {
                pBestSpawner = m_hSpawnBurstSpawner.Get();
                break;
            }
        }
    }
    if ( !pBestSpawner )
        pBestSpawner = visibleSpawners[ random->RandomInt( 0, visibleSpawners.Count() - 1 ) ];

    // === No active burst — start a new one ===
    if ( m_iSpawnBurstRemaining <= 0 )
    {
        m_iSpawnBurstRemaining = random->RandomInt( 1, 15 );
        m_hSpawnBurstSpawner = pBestSpawner;
        m_SpawnBurstClass = ZMCLASS_INVALID; // Will be rolled below

        if ( zm_sv_ai_zm_debug.GetBool() )
        {
            Vector spos = pBestSpawner->GetAbsOrigin();
            Msg( "[AI ZM] Spawn burst started: %i zombies, spawner at (%.0f,%.0f,%.0f)\n",
                m_iSpawnBurstRemaining, spos.x, spos.y, spos.z );
        }
    }

    // === Dynamic spawner switching ===
    // If the focused target moved closer to a different spawner, switch and re-roll
    if ( pBestSpawner != m_hSpawnBurstSpawner.Get() )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Spawn burst: spawner changed, re-rolling class\n" );
        m_hSpawnBurstSpawner = pBestSpawner;
        m_SpawnBurstClass = ZMCLASS_INVALID; // Force re-roll for new spawner
    }

    // === Roll class if we don't have one yet (new burst or spawner changed) ===
    if ( m_SpawnBurstClass == ZMCLASS_INVALID )
    {
        m_SpawnBurstClass = PickWeightedClass( m_hSpawnBurstSpawner.Get(), pZM, holdBack );

        if ( m_SpawnBurstClass == ZMCLASS_INVALID )
        {
            // No valid classes at all (all at pop cap or type limit) — end burst
            if ( zm_sv_ai_zm_debug.GetBool() )
                Msg( "[AI ZM] Spawn burst: no valid classes, ending burst\n" );
            m_iSpawnBurstRemaining = 0;
            m_Phase = AIZM_PHASE_HIDDEN_SPAWN;
            m_iHiddenSpawnsRemaining = random->RandomInt( 1, 3 );
            m_flHiddenSpawnDeadline = gpGlobals->curtime + 15.0f;
            m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.5f, 1.5f );
            return;
        }

        if ( zm_sv_ai_zm_debug.GetBool() )
        {
            int cost = CZMBaseZombie::GetCost( m_SpawnBurstClass );
            Msg( "[AI ZM] Spawn burst: rolled %s (cost %i), %i remaining in burst\n",
                CZMBaseZombie::ClassToName( m_SpawnBurstClass ), cost, m_iSpawnBurstRemaining );
        }
    }

    // === Save up: can we afford the current class? ===
    if ( !CanAffordClass( m_SpawnBurstClass, pZM, holdBack ) )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
        {
            int cost = CZMBaseZombie::GetCost( m_SpawnBurstClass );
            Msg( "[AI ZM] Spawn burst: saving up for %s (need %i, have %i, holdBack %i)\n",
                CZMBaseZombie::ClassToName( m_SpawnBurstClass ), cost,
                pZM->GetResources(), holdBack );
        }
        m_flNextActionTime = gpGlobals->curtime + 0.5f;
        return;
    }

    // === Spawn it ===
    TrySpawnZombies( m_SpawnBurstClass, 1, m_hSpawnBurstSpawner.Get() );
    m_iSpawnBurstRemaining--;
    m_SpawnBurstClass = ZMCLASS_INVALID; // Roll fresh class for the next zombie

    // === Check if burst is complete ===
    if ( m_iSpawnBurstRemaining <= 0 )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Spawn burst complete, moving to HIDDEN_SPAWN\n" );
        m_Phase = AIZM_PHASE_HIDDEN_SPAWN;
        m_iHiddenSpawnsRemaining = random->RandomInt( 1, 3 );
        m_flHiddenSpawnDeadline = gpGlobals->curtime + 15.0f;
        m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.5f, 1.5f );
    }
}

//
// Cycle phase: HIDDEN SPAWN
// Place 1-3 surprise zombies near survivors. Uses the same save-up logic as
// SPAWN (reserve + cost when reserve is intact, raw cost when below reserve).
// Each spawn is placed immediately when affordable. Retries positions for up
// to 15 seconds before giving up. After completion:
//   - If a trap was fired during SPAWN or HIDDEN_SPAWN -> RESERVE (replenish)
//   - Otherwise -> SPAWN (continue cycle)
//
void CZMAIZombieMaster::DoHiddenSpawnPhase( CZMPlayer* pZM )
{
    if ( gpGlobals->curtime < m_flNextActionTime )
        return;

    // Timeout or done: move to next phase
    if ( gpGlobals->curtime > m_flHiddenSpawnDeadline || m_iHiddenSpawnsRemaining <= 0 )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
        {
            if ( m_iHiddenSpawnsRemaining <= 0 )
                Msg( "[AI ZM] Hidden spawn: all spawns placed\n" );
            else
                Msg( "[AI ZM] Hidden spawn: 15s timeout, %i remaining unplaced\n",
                    m_iHiddenSpawnsRemaining );
        }
        m_iHiddenSpawnsRemaining = 0;

        // Go to RESERVE only if a trap was fired and reserve needs replenishing
        if ( m_bTrapFiredDuringCycle && m_iReservedResources > 0 &&
             pZM->GetResources() < m_iReservedResources )
        {
            if ( zm_sv_ai_zm_debug.GetBool() )
                Msg( "[AI ZM] Trap was fired during cycle, moving to RESERVE to replenish\n" );
            m_Phase = AIZM_PHASE_RESERVE;
        }
        else
        {
            if ( zm_sv_ai_zm_debug.GetBool() )
                Msg( "[AI ZM] Reserve intact, moving to SPAWN\n" );
            m_bTrapFiredDuringCycle = false;
            m_Phase = AIZM_PHASE_SPAWN;
        }
        m_flNextActionTime = gpGlobals->curtime + random->RandomFloat( 0.3f, 1.0f );
        return;
    }

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );
    if ( humans.Count() == 0 )
    {
        m_Phase = AIZM_PHASE_SPAWN;
        return;
    }

    // Same holdBack logic as DoSpawnPhase
    int holdBack = 0;
    if ( m_bTrapsUnlocked && pZM->GetResources() >= m_iReservedResources )
        holdBack = m_iReservedResources;

    CBasePlayer* pTarget = humans[ random->RandomInt( 0, humans.Count() - 1 ) ];
    Vector targetPos = pTarget->GetAbsOrigin();

    // Pick class using the weighted system (respects type limits, pop caps).
    // When hidden_allclasses is off, only shamblers can be hidden-spawned.
    ZombieClass_t zclass = ZMCLASS_SHAMBLER;
    if ( zm_sv_hidden_allclasses.GetBool() )
    {
        int totalWeight = 0;
        int weights[ZMCLASS_MAX];
        bool valid[ZMCLASS_MAX];

        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            ZombieClass_t zc = (ZombieClass_t)i;
            valid[i] = CZMBaseZombie::IsValidClass( zc )
                    && CZMBaseZombie::HasEnoughPopToSpawn( zc )
                    && !IsClassAtTypeLimit( zc );
            weights[i] = 0;
        }

        int validCount = 0;
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
            if ( valid[i] ) validCount++;

        if ( validCount == 0 )
        {
            if ( zm_sv_ai_zm_debug.GetBool() )
                Msg( "[AI ZM] Hidden spawn: no valid classes, ending phase\n" );
            m_iHiddenSpawnsRemaining = 0;
            m_flNextActionTime = gpGlobals->curtime + 0.1f;
            return;
        }

        // Redistribute weight from blocked classes equally among valid ones.
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
                if ( remainder > 0 ) { weights[i]++; remainder--; }
                totalWeight += weights[i];
            }
        }

        int roll = random->RandomInt( 1, totalWeight );
        int cumulative = 0;
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            if ( !valid[i] ) continue;
            cumulative += weights[i];
            if ( roll <= cumulative ) { zclass = (ZombieClass_t)i; break; }
        }
    }
    else
    {
        if ( IsClassAtTypeLimit( ZMCLASS_SHAMBLER ) ||
             !CZMBaseZombie::HasEnoughPopToSpawn( ZMCLASS_SHAMBLER ) )
        {
            m_iHiddenSpawnsRemaining = 0;
            m_flNextActionTime = gpGlobals->curtime + 0.1f;
            return;
        }
    }

    // Check affordability with reserve holdBack
    if ( !CanAffordClass( zclass, pZM, holdBack ) )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
        {
            int cost = CZMBaseZombie::GetCost( zclass );
            Msg( "[AI ZM] Hidden spawn: saving up for %s (need %i, have %i, holdBack %i)\n",
                CZMBaseZombie::ClassToName( zclass ), cost, pZM->GetResources(), holdBack );
        }
        m_flNextActionTime = gpGlobals->curtime + 0.5f;
        return;
    }

    // Try multiple positions this tick
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
                Msg( "[AI ZM] Hidden spawn %i: %s near '%s' at (%.0f,%.0f,%.0f) cost=%i\n",
                    m_iHiddenSpawnsRemaining, CZMBaseZombie::ClassToName( zclass ),
                    pTarget->GetPlayerName(),
                    spawnPos.x, spawnPos.y, spawnPos.z, resCost );
            m_iHiddenSpawnsRemaining--;
            bSuccess = true;
            break;
        }
    }

    if ( !bSuccess && zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Hidden spawn: attempts failed this tick, retrying (%.0fs remaining)\n",
            m_flHiddenSpawnDeadline - gpGlobals->curtime );

    // Retry next tick (or finish if all spawns placed)
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

    m_bTrapFiredDuringCycle = true;

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
        Msg( "[AI ZM] Update: phase=%i res=%i reserved=%i trapsUnlocked=%i curtime=%.1f\n",
            (int)m_Phase, pZM->GetResources(), m_iReservedResources,
            m_bTrapsUnlocked ? 1 : 0, gpGlobals->curtime );

    // Run the current cycle phase: SPAWN -> HIDDEN_SPAWN -> RESERVE (if needed) -> SPAWN
    switch ( m_Phase )
    {
    case AIZM_PHASE_SPAWN:
        // Traps fire opportunistically during spawning phases
        if ( m_bTrapsUnlocked )
        {
            TryFireTrap( pZM );
            TryDetonateBarrel( pZM );
        }
        DoSpawnPhase( pZM );
        break;
    case AIZM_PHASE_HIDDEN_SPAWN:
        // Traps fire opportunistically during spawning phases
        if ( m_bTrapsUnlocked )
        {
            TryFireTrap( pZM );
            TryDetonateBarrel( pZM );
        }
        DoHiddenSpawnPhase( pZM );
        break;
    case AIZM_PHASE_RESERVE:
        // No traps during RESERVE — the AI is rebuilding its reserve
        DoReservePhase( pZM );
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
