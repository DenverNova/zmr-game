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

ConVar zm_sv_ai_zm_debug( "zm_sv_ai_zm_debug", "0", FCVAR_CHEAT, "Enable AI ZM debug logging (server console only). 0=off, 1=on." );

ConVar zm_sv_ai_zm_spawn_interval( "zm_sv_ai_zm_spawn_interval", "8.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Base seconds between AI ZM spawn waves." );
ConVar zm_sv_ai_zm_aggression( "zm_sv_ai_zm_aggression", "1.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "AI ZM aggression. Higher = faster spawns, larger groups. (0.1 - 3.0)" );
ConVar zm_sv_ai_zm_spawn_batch( "zm_sv_ai_zm_spawn_batch", "3", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Base max zombies per wave (scaled by plan type)." );
ConVar zm_sv_ai_zm_trap_range( "zm_sv_ai_zm_trap_range", "1024", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Distance a survivor must be to a trap for the AI ZM to trigger it." );
ConVar zm_sv_ai_zm_spawn_range( "zm_sv_ai_zm_spawn_range", "2048", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Max distance from a survivor for AI ZM to use a spawner." );
ConVar zm_sv_ai_zm_tactic_min_time( "zm_sv_ai_zm_tactic_min_time", "15.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Min seconds before AI ZM builds a new plan." );
ConVar zm_sv_ai_zm_tactic_max_time( "zm_sv_ai_zm_tactic_max_time", "40.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Max seconds before AI ZM builds a new plan." );
ConVar zm_sv_ai_zm_stall_timeout( "zm_sv_ai_zm_stall_timeout", "12.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds before AI ZM abandons a plan it can't execute." );
ConVar zm_sv_ai_zm_rally_interval( "zm_sv_ai_zm_rally_interval", "6.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds between AI ZM zombie rally commands." );
ConVar zm_sv_ai_zm_rally_buffer( "zm_sv_ai_zm_rally_buffer", "256.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Distance buffer for target splitting when rallying zombies." );
ConVar zm_sv_ai_zm_trap_cooldown( "zm_sv_ai_zm_trap_cooldown", "30.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Per-trap cooldown in seconds before the AI ZM can re-use the same trap." );
ConVar zm_sv_ai_zm_hidden_max( "zm_sv_ai_zm_hidden_max", "5", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Max hidden spawns per 2-minute window." );
ConVar zm_sv_ai_zm_plan_pause( "zm_sv_ai_zm_plan_pause", "8.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds to pause between completing one plan and starting the next." );
ConVar zm_sv_ai_zm_rush_prevention( "zm_sv_ai_zm_rush_prevention", "15.0", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Seconds at round start before the AI ZM can spawn, trigger traps, or use abilities. Does not affect human ZMs." );

CZMAIZombieMaster g_ZMAIZombieMaster;


CZMAIZombieMaster::CZMAIZombieMaster()
{
    m_TrapCooldowns.SetLessFunc( DefLessFunc( int ) );
    Reset();
}

void CZMAIZombieMaster::Reset()
{
    m_Plan.Purge();
    m_PlanSpawners.Purge();
    m_iPlanStep = 0;
    m_flPlanStepReadyTime = 0.0f;
    m_flPlanExpireTime = 0.0f;
    m_iSaveTarget = -1;
    m_iLastSpawnerUsed = -1;
    m_flNextTrapTime = 0.0f;
    m_flNextRallyTime = 0.0f;
    m_flNextHiddenSpawnTime = 0.0f;
    m_flLastUpdateTime = 0.0f;
    m_bLoggedSpawners = false;
    m_flPlanCooldownUntil = 0.0f;
    m_TrapCooldowns.RemoveAll();
    m_nHiddenSpawnsThisWindow = 0;
    m_flHiddenSpawnWindowStart = 0.0f;
    m_flNextBarrelDetonateTime = 0.0f;
}

bool CZMAIZombieMaster::IsActive() const
{
    if ( !zm_sv_ai_zm.GetBool() )
    {
        return false;
    }

    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] IsActive: no ZM player found\n" );
        return false;
    }

    // Only run AI when the ZM is a bot, not a human player
    if ( !pZM->IsBot() )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] IsActive: ZM is human player '%s', AI disabled\n", pZM->GetPlayerName() );
        return false;
    }

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

bool CZMAIZombieMaster::CanAffordClass( ZombieClass_t zclass, CZMPlayer* pZM ) const
{
    if ( zclass == ZMCLASS_INVALID || !pZM ) return false;
    int cost = CZMBaseZombie::GetCost( zclass );
    return cost >= 0 && pZM->GetResources() >= cost && CZMBaseZombie::HasEnoughPopToSpawn( zclass );
}

bool CZMAIZombieMaster::HasSurvivorNearTrap() const
{
    float flRange = zm_sv_ai_zm_trap_range.GetFloat();
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_manipulate" )) != nullptr )
    {
        CZMEntManipulate* pTrap = dynamic_cast<CZMEntManipulate*>( pEnt );
        if ( !pTrap || !pTrap->IsActive() )
            continue;
        float dist = 0.0f;
        FindNearestHuman( pTrap->GetAbsOrigin(), &dist );
        if ( dist <= flRange )
            return true;
    }
    return false;
}

void CZMAIZombieMaster::GatherActiveSpawners( CUtlVector<CZMEntZombieSpawn*>& spawners ) const
{
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_zombiespawn" )) != nullptr )
    {
        CZMEntZombieSpawn* pSpawn = dynamic_cast<CZMEntZombieSpawn*>( pEnt );
        if ( !pSpawn )
            continue;
        // IsActive() respects the same gating as human ZMs - spawners hidden
        // until map triggers reveal them should stay hidden for the AI too.
        if ( !pSpawn->IsActive() )
            continue;
        spawners.AddToTail( pSpawn );
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

        // Build class list string
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

bool CZMAIZombieMaster::IsClassCheap( ZombieClass_t zclass ) const
{
    if ( zclass == ZMCLASS_INVALID ) return false;
    return CZMBaseZombie::GetCost( zclass ) <= 15;
}

ZombieClass_t CZMAIZombieMaster::PickClassForSpawner( CZMEntZombieSpawn* pSpawner, ZombieClass_t avoid ) const
{
    if ( !pSpawner ) return ZMCLASS_INVALID;

    CUtlVector<ZombieClass_t> candidates;
    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zc = (ZombieClass_t)i;
        if ( !CZMBaseZombie::IsValidClass( zc ) ) continue;
        if ( !SpawnerSupportsClass( pSpawner, zc ) ) continue;
        if ( zc != avoid )
            candidates.AddToTail( zc );
    }

    // If we filtered out everything by avoiding, allow the avoided class as fallback
    if ( candidates.Count() == 0 )
    {
        for ( int i = 0; i < ZMCLASS_MAX; i++ )
        {
            ZombieClass_t zc = (ZombieClass_t)i;
            if ( !CZMBaseZombie::IsValidClass( zc ) ) continue;
            if ( !SpawnerSupportsClass( pSpawner, zc ) ) continue;
            candidates.AddToTail( zc );
        }
    }

    if ( candidates.Count() == 0 ) return ZMCLASS_INVALID;
    return candidates[ random->RandomInt( 0, candidates.Count() - 1 ) ];
}

CZMEntManipulate* CZMAIZombieMaster::FindBestTrap() const
{
    float flTrapRange = zm_sv_ai_zm_trap_range.GetFloat();
    float flTrapCooldown = zm_sv_ai_zm_trap_cooldown.GetFloat();

    CZMEntManipulate* pBest = nullptr;
    float flBestDist = FLT_MAX;

    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "info_manipulate" )) != nullptr )
    {
        CZMEntManipulate* pTrap = dynamic_cast<CZMEntManipulate*>( pEnt );
        if ( !pTrap || !pTrap->IsActive() )
            continue;

        // Per-trap cooldown: skip if this trap was used recently
        int idx = m_TrapCooldowns.Find( pTrap->entindex() );
        if ( idx != m_TrapCooldowns.InvalidIndex() )
        {
            if ( gpGlobals->curtime < m_TrapCooldowns[idx] + flTrapCooldown )
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
// Class picking helpers
//
ZombieClass_t CZMAIZombieMaster::PickCheapestClass( CZMEntZombieSpawn* pSpawner ) const
{
    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM ) return ZMCLASS_INVALID;

    ZombieClass_t best = ZMCLASS_INVALID;
    int bestCost = INT_MAX;

    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zclass = (ZombieClass_t)i;
        if ( !CZMBaseZombie::IsValidClass( zclass ) )
            continue;
        if ( !(pSpawner->GetZombieFlags() & (1 << i)) )
            continue;
        if ( !CZMBaseZombie::HasEnoughPopToSpawn( zclass ) )
            continue;

        int cost = CZMBaseZombie::GetCost( zclass );
        if ( cost >= 0 && cost < bestCost && pZM->GetResources() >= cost )
        {
            bestCost = cost;
            best = zclass;
        }
    }

    return best;
}

ZombieClass_t CZMAIZombieMaster::PickExpensiveClass( CZMEntZombieSpawn* pSpawner ) const
{
    ZombieClass_t best = ZMCLASS_INVALID;
    int bestCost = 0;

    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zclass = (ZombieClass_t)i;
        if ( !CZMBaseZombie::IsValidClass( zclass ) )
            continue;
        if ( !(pSpawner->GetZombieFlags() & (1 << i)) )
            continue;

        int cost = CZMBaseZombie::GetCost( zclass );
        if ( cost > bestCost )
        {
            bestCost = cost;
            best = zclass;
        }
    }

    return best;
}

ZombieClass_t CZMAIZombieMaster::PickRandomAffordableClass( CZMEntZombieSpawn* pSpawner ) const
{
    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM ) return ZMCLASS_INVALID;

    CUtlVector<ZombieClass_t> candidates;

    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zclass = (ZombieClass_t)i;
        if ( !CZMBaseZombie::IsValidClass( zclass ) )
            continue;
        if ( !(pSpawner->GetZombieFlags() & (1 << i)) )
            continue;
        if ( !CZMBaseZombie::HasEnoughPopToSpawn( zclass ) )
            continue;

        int cost = CZMBaseZombie::GetCost( zclass );
        if ( cost >= 0 && pZM->GetResources() >= cost )
            candidates.AddToTail( zclass );
    }

    if ( candidates.Count() == 0 )
        return ZMCLASS_INVALID;

    return candidates[ random->RandomInt( 0, candidates.Count() - 1 ) ];
}

bool CZMAIZombieMaster::TrySpawnZombies( ZombieClass_t zclass, int count, CZMEntZombieSpawn* pSpawner )
{
    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM || zclass == ZMCLASS_INVALID || !pSpawner )
        return false;

    int cost = CZMBaseZombie::GetCost( zclass );
    int canAfford = pZM->GetResources() / MAX( cost, 1 );
    int toSpawn = MIN( count, canAfford );

    if ( toSpawn <= 0 )
        return false;

    if ( !CZMBaseZombie::HasEnoughPopToSpawn( zclass ) )
        return false;

    // Ensure spawner is active so SpawnThink won't abort on IsActive() check
    if ( !pSpawner->IsActive() )
    {
        inputdata_t dummy;
        pSpawner->InputUnhide( dummy );
    }

    pSpawner->QueueUnit( pZM, zclass, toSpawn );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Spawning %i x %s (cost: %i each, res left: %i)\n",
            toSpawn, CZMBaseZombie::ClassToName( zclass ), cost, pZM->GetResources() );

    return true;
}

//
// Dynamic plan system - builds concrete multi-step spawn plans distributed across all spawners
//
void CZMAIZombieMaster::BuildNewPlan()
{
    m_Plan.Purge();
    m_PlanSpawners.Purge();
    m_iPlanStep = 0;
    m_iSaveTarget = -1;

    float flAggression = clamp( zm_sv_ai_zm_aggression.GetFloat(), 0.1f, 3.0f );
    float minTime = zm_sv_ai_zm_tactic_min_time.GetFloat();
    float maxTime = zm_sv_ai_zm_tactic_max_time.GetFloat();
    m_flPlanExpireTime = gpGlobals->curtime + random->RandomFloat( minTime, maxTime );

    // Snapshot all active spawners globally (no range limit)
    GatherActiveSpawners( m_PlanSpawners );
    if ( m_PlanSpawners.Count() == 0 )
    {
        m_flPlanExpireTime = gpGlobals->curtime + 5.0f;
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] BuildNewPlan: no active spawners, will retry\n" );
        return;
    }

    if ( zm_sv_ai_zm_debug.GetBool() )
    {
        Msg( "[AI ZM] BuildNewPlan: %i active spawners found globally\n", m_PlanSpawners.Count() );
        for ( int s = 0; s < m_PlanSpawners.Count(); s++ )
        {
            // Log each spawner's position and what classes it supports
            int flags = m_PlanSpawners[s]->GetZombieFlags();
            Vector pos = m_PlanSpawners[s]->GetAbsOrigin();
            float dist = 0.0f;
            FindNearestHuman( pos, &dist );
            Msg( "  [%i] pos=(%.0f,%.0f,%.0f) flags=0x%X nearestSurvivor=%.0f\n",
                s, pos.x, pos.y, pos.z, flags, dist );
        }
    }

    float baseInterval = zm_sv_ai_zm_spawn_interval.GetFloat() / flAggression;

    // Decide number of waves: 3-6 steps per plan
    int numSteps = random->RandomInt( 3, 6 );

    // Distribute steps across spawners round-robin, starting from a different one each plan
    int startSpawnerIdx;
    if ( m_iLastSpawnerUsed >= 0 && m_PlanSpawners.Count() > 1 )
        startSpawnerIdx = ( m_iLastSpawnerUsed + 1 ) % m_PlanSpawners.Count();
    else
        startSpawnerIdx = random->RandomInt( 0, m_PlanSpawners.Count() - 1 );

    ZombieClass_t lastClass = ZMCLASS_INVALID;

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Plan: building %i steps, starting at spawner %i\n", numSteps, startSpawnerIdx );

    for ( int w = 0; w < numSteps; w++ )
    {
        // Round-robin through spawners
        int spawnerIdx = ( startSpawnerIdx + w ) % m_PlanSpawners.Count();
        CZMEntZombieSpawn* pSpawner = m_PlanSpawners[ spawnerIdx ];

        // Pick a class for this spawner, avoiding the last class used
        ZombieClass_t zc = PickClassForSpawner( pSpawner, lastClass );
        if ( zc == ZMCLASS_INVALID )
            continue;

        // Cap counts: cheap/shambler types up to 1-25, expensive/special up to 1-5
        int maxCount = IsClassCheap( zc ) ? 25 : 5;
        int minCount = 1;
        int count = random->RandomInt( minCount, maxCount );

        // Scale by aggression but never exceed caps
        count = (int)( count * flAggression );
        count = clamp( count, minCount, maxCount );

        AIZMSpawnStep_t step;
        step.zclass = zc;
        step.count = count;
        step.flDelay = ( w == 0 ) ? 0.0f : baseInterval * random->RandomFloat( 0.5f, 1.2f );
        step.iSpawnerIndex = spawnerIdx;
        m_Plan.AddToTail( step );

        lastClass = zc;

        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "  Step %i: %i x %s at spawner[%i]\n", w, count,
                CZMBaseZombie::ClassToName( zc ), spawnerIdx );
    }

    // Set the first step ready time
    m_flPlanStepReadyTime = gpGlobals->curtime + ( m_Plan.Count() > 0 ? m_Plan[0].flDelay : 0.0f );
}

void CZMAIZombieMaster::ExecutePlanStep( CZMPlayer* pZM )
{
    if ( m_Plan.Count() == 0 || m_iPlanStep >= m_Plan.Count() )
    {
        // Respect post-plan cooldown before building a new plan
        if ( gpGlobals->curtime < m_flPlanCooldownUntil )
            return;
        BuildNewPlan();
        return;
    }

    // Expired plan - build a new one
    if ( gpGlobals->curtime > m_flPlanExpireTime )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Plan expired at step %i/%i, building new one\n", m_iPlanStep, m_Plan.Count() );
        BuildNewPlan();
        return;
    }

    // Not ready yet
    if ( gpGlobals->curtime < m_flPlanStepReadyTime )
        return;

    const AIZMSpawnStep_t& step = m_Plan[ m_iPlanStep ];

    // Resolve the spawner for this step
    CZMEntZombieSpawn* pSpawner = nullptr;
    if ( step.iSpawnerIndex >= 0 && step.iSpawnerIndex < m_PlanSpawners.Count() )
    {
        pSpawner = m_PlanSpawners[ step.iSpawnerIndex ];
        // Validate it's still active
        if ( pSpawner && !pSpawner->IsActive() )
            pSpawner = nullptr;
    }

    // If the planned spawner is gone or invalid, try to find any spawner that supports this class
    if ( !pSpawner )
    {
        CUtlVector<CZMEntZombieSpawn*> fallback;
        GatherActiveSpawners( fallback );
        for ( int s = 0; s < fallback.Count(); s++ )
        {
            if ( SpawnerSupportsClass( fallback[s], step.zclass ) )
            {
                pSpawner = fallback[s];
                break;
            }
        }
    }

    if ( !pSpawner )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Step %i: no valid spawner for %s, skipping\n",
                m_iPlanStep, CZMBaseZombie::ClassToName( step.zclass ) );
        m_iPlanStep++;
        if ( m_iPlanStep < m_Plan.Count() )
            m_flPlanStepReadyTime = gpGlobals->curtime + m_Plan[m_iPlanStep].flDelay;
        return;
    }

    if ( !CanAffordClass( step.zclass, pZM ) )
    {
        // Can't afford yet - wait
        m_flPlanStepReadyTime = gpGlobals->curtime + 2.0f;
        return;
    }

    if ( zm_sv_ai_zm_debug.GetBool() )
    {
        Vector spos = pSpawner->GetAbsOrigin();
        Msg( "[AI ZM] Executing step %i/%i: %i x %s at spawner[%i] pos=(%.0f,%.0f,%.0f)\n",
            m_iPlanStep, m_Plan.Count(), step.count,
            CZMBaseZombie::ClassToName( step.zclass ),
            step.iSpawnerIndex, spos.x, spos.y, spos.z );
    }

    if ( TrySpawnZombies( step.zclass, step.count, pSpawner ) )
    {
        m_iLastSpawnerUsed = step.iSpawnerIndex;
        m_iPlanStep++;
        if ( m_iPlanStep < m_Plan.Count() )
        {
            m_flPlanStepReadyTime = gpGlobals->curtime + m_Plan[m_iPlanStep].flDelay;
        }
        else
        {
            if ( zm_sv_ai_zm_debug.GetBool() )
                Msg( "[AI ZM] Plan complete! Pausing %.1f seconds before next plan.\n", zm_sv_ai_zm_plan_pause.GetFloat() );
            m_flPlanCooldownUntil = gpGlobals->curtime + zm_sv_ai_zm_plan_pause.GetFloat();
            m_Plan.Purge();
        }
    }
    else
    {
        m_flPlanStepReadyTime = gpGlobals->curtime + 2.0f;
    }
}

void CZMAIZombieMaster::UpdateTrapOpportunism( CZMPlayer* pZM )
{
    float flTrapRange = zm_sv_ai_zm_trap_range.GetFloat();

    // Find the best trap candidate (handles per-trap cooldown internally)
    CZMEntManipulate* pTrap = FindBestTrap();
    if ( !pTrap )
    {
        // No valid traps right now - check again soon
        if ( gpGlobals->curtime >= m_flNextTrapTime )
            m_flNextTrapTime = gpGlobals->curtime + 2.0f;
        return;
    }

    // How close is the nearest survivor to this trap?
    float flSurvivorDist = 0.0f;
    FindNearestHuman( pTrap->GetAbsOrigin(), &flSurvivorDist );

    int trapCost = pTrap->GetCost();

    // Reserve some resources for spawns unless a survivor is very close
    bool bUrgent = ( flSurvivorDist < flTrapRange * 0.5f );
    int reserveForSpawn = bUrgent ? 0 : 15;
    int totalNeeded = trapCost + reserveForSpawn;
    if ( trapCost > 0 && pZM->GetResources() < totalNeeded )
    {
        if ( zm_sv_ai_zm_debug.GetBool() )
            Msg( "[AI ZM] Trap available (cost %i) but need %i (res=%i), skipping\n",
                trapCost, totalNeeded, pZM->GetResources() );
        m_flNextTrapTime = gpGlobals->curtime + 3.0f;
        return;
    }

    if ( trapCost > 0 )
        pZM->IncResources( -trapCost, false );

    pTrap->Trigger( pZM );

    // Record per-trap cooldown by entity index
    int trapIdx = m_TrapCooldowns.Find( pTrap->entindex() );
    if ( trapIdx != m_TrapCooldowns.InvalidIndex() )
        m_TrapCooldowns[trapIdx] = gpGlobals->curtime;
    else
        m_TrapCooldowns.Insert( pTrap->entindex(), gpGlobals->curtime );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Triggered trap (cost: %i, res left: %i, survivor dist: %.0f, per-trap cd: %.1fs)\n",
            trapCost, pZM->GetResources(), flSurvivorDist, zm_sv_ai_zm_trap_cooldown.GetFloat() );

    // Short re-check delay to avoid triggering multiple traps in the same frame
    m_flNextTrapTime = gpGlobals->curtime + 1.0f;
}

void CZMAIZombieMaster::TryHiddenSpawn( CZMPlayer* pZM )
{
    if ( gpGlobals->curtime < m_flNextHiddenSpawnTime )
        return;

    // Rate limit: max N hidden spawns per 2-minute window
    int maxPerWindow = zm_sv_ai_zm_hidden_max.GetInt();
    float flWindowLen = 120.0f;
    if ( m_flHiddenSpawnWindowStart == 0.0f || gpGlobals->curtime - m_flHiddenSpawnWindowStart >= flWindowLen )
    {
        m_flHiddenSpawnWindowStart = gpGlobals->curtime;
        m_nHiddenSpawnsThisWindow = 0;
    }
    if ( m_nHiddenSpawnsThisWindow >= maxPerWindow )
    {
        m_flNextHiddenSpawnTime = m_flHiddenSpawnWindowStart + flWindowLen;
        return;
    }

    // Need at least some resources to attempt a hidden spawn
    int minCost = 30;
    if ( pZM->GetResources() < minCost )
        return;

    CUtlVector<CBasePlayer*> humans;
    GatherHumans( humans );
    if ( humans.Count() == 0 )
        return;

    // Pick a random survivor to spawn near
    CBasePlayer* pTarget = humans[ random->RandomInt( 0, humans.Count() - 1 ) ];
    Vector targetPos = pTarget->GetAbsOrigin();

    // Pick a random zombie class (AI always gets all classes for hidden spawn)
    ZombieClass_t zclass = ZMCLASS_SHAMBLER;
    CUtlVector<ZombieClass_t> validClasses;
    for ( int i = 0; i < ZMCLASS_MAX; i++ )
    {
        ZombieClass_t zc = (ZombieClass_t)i;
        if ( CZMBaseZombie::IsValidClass( zc ) && CZMBaseZombie::HasEnoughPopToSpawn( zc ) )
            validClasses.AddToTail( zc );
    }
    if ( validClasses.Count() > 0 )
        zclass = validClasses[ random->RandomInt( 0, validClasses.Count() - 1 ) ];

    // Try several random positions behind/around the target survivor
    for ( int attempt = 0; attempt < 8; attempt++ )
    {
        // Random offset 256-768 units away from the survivor
        float dist = random->RandomFloat( 256.0f, 768.0f );
        float angle = random->RandomFloat( 0.0f, 360.0f );
        Vector spawnPos = targetPos;
        spawnPos.x += cos( DEG2RAD( angle ) ) * dist;
        spawnPos.y += sin( DEG2RAD( angle ) ) * dist;

        // Trace down to find ground
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
                Msg( "[AI ZM] Hidden spawn: %s near survivor '%s' at (%.0f,%.0f,%.0f) cost=%i\n",
                    CZMBaseZombie::ClassToName( zclass ), pTarget->GetPlayerName(),
                    spawnPos.x, spawnPos.y, spawnPos.z, resCost );

            m_nHiddenSpawnsThisWindow++;

            // Longer cooldown after success
            float flAggression = clamp( zm_sv_ai_zm_aggression.GetFloat(), 0.1f, 3.0f );
            m_flNextHiddenSpawnTime = gpGlobals->curtime + random->RandomFloat( 15.0f, 30.0f ) / flAggression;
            return;
        }
    }

    // All attempts failed, retry sooner
    m_flNextHiddenSpawnTime = gpGlobals->curtime + 5.0f;
}

void CZMAIZombieMaster::TryDetonateBarrel( CZMPlayer* pZM )
{
    if ( gpGlobals->curtime < m_flNextBarrelDetonateTime )
        return;

    float flTriggerRange = 350.0f; // Survivor must be this close to a barrel

    CBreakableProp* pBestBarrel = nullptr;
    float flBestDist = FLT_MAX;

    // Scan both prop_physics and prop_physics_multiplayer
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
    {
        m_flNextBarrelDetonateTime = gpGlobals->curtime + 3.0f;
        return;
    }

    // 70% chance to detonate for more natural, unpredictable behavior
    if ( random->RandomFloat( 0.0f, 1.0f ) > 0.7f )
    {
        m_flNextBarrelDetonateTime = gpGlobals->curtime + 2.0f;
        return;
    }

    // Detonate the barrel by applying blast damage
    CTakeDamageInfo info( pZM, pZM, 1000.0f, DMG_BLAST );
    info.SetDamagePosition( pBestBarrel->GetAbsOrigin() );
    pBestBarrel->TakeDamage( info );

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Detonated explosive barrel at (%.0f,%.0f,%.0f), survivor %.0f units away\n",
            pBestBarrel->GetAbsOrigin().x, pBestBarrel->GetAbsOrigin().y, pBestBarrel->GetAbsOrigin().z, flBestDist );

    // Cooldown before trying another barrel
    m_flNextBarrelDetonateTime = gpGlobals->curtime + 20.0f;
}

void CZMAIZombieMaster::Update()
{
    if ( !IsActive() )
        return;

    if ( gpGlobals->curtime - m_flLastUpdateTime < 0.5f )
        return;
    m_flLastUpdateTime = gpGlobals->curtime;

    CZMPlayer* pZM = GetZMPlayer();
    if ( !pZM )
        return;

    if ( zm_sv_ai_zm_debug.GetBool() )
        Msg( "[AI ZM] Update: ZM='%s' res=%i planStep=%i/%i trapReady=%.1f curtime=%.1f\n",
            pZM->GetPlayerName(), pZM->GetResources(),
            m_iPlanStep, m_Plan.Count(), m_flNextTrapTime, gpGlobals->curtime );

    // Rush prevention: don't do anything for the first N seconds of a round
    float flRushTime = zm_sv_ai_zm_rush_prevention.GetFloat();
    if ( flRushTime > 0.0f && gpGlobals->curtime < flRushTime )
        return;

    // Build first plan if we don't have one
    if ( m_Plan.Count() == 0 )
    {
        // Log all spawners on first plan build so we have a full map report
        if ( !m_bLoggedSpawners )
        {
            LogAllSpawners();
            m_bLoggedSpawners = true;
        }
        BuildNewPlan();
    }

    // Traps always run opportunistically
    UpdateTrapOpportunism( pZM );

    // Execute current plan step
    ExecutePlanStep( pZM );

    // Occasionally try a hidden spawn to surprise survivors
    TryHiddenSpawn( pZM );

    // Detonate explosive barrels when survivors are close
    TryDetonateBarrel( pZM );

    // Rally idle zombies toward survivors
    RallyZombiesToSurvivors();
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

    // Find the closest and second closest
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

    // If two targets are close enough (within buffer), randomly pick between them
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

    float flRallyRange = 4096.0f; // Rally all zombies within a generous range

    static const char* s_szZombieClassnames[] = {
        "npc_zombie",
        "npc_fastzombie",
        "npc_poisonzombie",
        "npc_burnzombie",
        "npc_dragzombie",
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

            // Only command idle zombies (no current command)
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
