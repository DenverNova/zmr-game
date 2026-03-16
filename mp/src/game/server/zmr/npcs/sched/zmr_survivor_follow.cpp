#include "cbase.h"

#include "ammodef.h"
#include "soundent.h"

#include "zmr_survivor_follow.h"
#include "zmr_entities.h"
#include "zmr_voicelines.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar zm_sv_bot_default_behavior;
extern ConVar zm_sv_bot_weapon_search_range;


CSurvivorFollowSchedule::CSurvivorFollowSchedule()
{
    m_vecFormationOffset = vec3_origin;
    m_vecDefendPos = vec3_origin;
    m_bHasDefendPos = false;
    m_vecHeardLookAt = vec3_origin;
    m_iMixedBehavior = -1;
    m_bScavenging = false;
    m_vecPreScavengePos = vec3_origin;
    m_hScavengeTarget.Set( nullptr );
    m_flExploreScanPitch = 0.0f;
    m_flExploreScanYawOffset = 0.0f;
    m_bExploreIdling = false;
    m_bFleeingExplosion = false;
}

CSurvivorFollowSchedule::~CSurvivorFollowSchedule()
{
}

void CSurvivorFollowSchedule::OnStart()
{
    m_NextFollowTarget.Start( 2.0f );
    m_NextWeaponScan.Start( 1.0f );
}

void CSurvivorFollowSchedule::OnContinue()
{
    m_Path.Invalidate();
}

void CSurvivorFollowSchedule::OnUpdate()
{
    CZMPlayerBot* pOuter = GetOuter();

    if ( !pOuter->IsHuman() )
    {
        return;
    }

    pOuter->CheckObstacleJump();

    // Dodge ZM explosions - scan for nearby buildup explosions and flee
    if ( !m_NextExplosionCheck.HasStarted() || m_NextExplosionCheck.IsElapsed() )
    {
        m_NextExplosionCheck.Start( 0.3f );

        CBaseEntity* pExp = nullptr;
        CBaseEntity* pClosestExp = nullptr;
        float flClosestDistSqr = FLT_MAX;
        float flDangerRadius = 0.0f;
        Vector myPos = pOuter->GetAbsOrigin();

        while ( (pExp = gEntList.FindEntityByClassname( pExp, "env_delayed_physexplosion" )) != nullptr )
        {
            CZMPhysExplosion* pPhysExp = dynamic_cast<CZMPhysExplosion*>( pExp );
            if ( !pPhysExp ) continue;

            float radius = pPhysExp->GetRadius();
            float distSqr = myPos.DistToSqr( pPhysExp->GetAbsOrigin() );

            // Add safety margin to radius
            float dangerRadiusSqr = (radius + 64.0f) * (radius + 64.0f);
            if ( distSqr < dangerRadiusSqr && distSqr < flClosestDistSqr )
            {
                flClosestDistSqr = distSqr;
                pClosestExp = pPhysExp;
                flDangerRadius = radius;
            }
        }

        if ( pClosestExp )
        {
            // Flee directly away from the explosion
            Vector vecAway = myPos - pClosestExp->GetAbsOrigin();
            vecAway.z = 0.0f;
            float flLen = vecAway.NormalizeInPlace();
            if ( flLen < 1.0f )
                vecAway = Vector( 1.0f, 0.0f, 0.0f );

            Vector vecFleeGoal = myPos + vecAway * (flDangerRadius + 128.0f);

            CNavArea* pStart = pOuter->GetLastKnownArea();
            CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecFleeGoal, true, 512.0f, false );
            if ( pStart && pGoal )
            {
                m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
                m_PathCost.SetStartPos( myPos, pStart );
                m_FleePath.Compute( myPos, vecFleeGoal, pStart, pGoal, m_PathCost );
                if ( !m_bFleeingExplosion )
                    ZMGetVoiceLines()->OnVoiceLine( pOuter, 7 ); // Alert
                m_bFleeingExplosion = true;
            }
        }
        else
        {
            m_bFleeingExplosion = false;
        }
    }

    // If fleeing an explosion, run the flee path and skip everything else
    if ( m_bFleeingExplosion && m_FleePath.IsValid() )
    {
        m_FleePath.Update( pOuter );
        return;
    }

    // Periodically look for weapons to pick up
    TryPickupNearbyWeapons();

    // Keep the best weapon equipped when not in combat
    pOuter->EquipBestWeapon();

    // If we heard something interesting recently, look toward it
    if ( m_NextHeardLook.HasStarted() && !m_NextHeardLook.IsElapsed() && m_vecHeardLookAt != vec3_origin )
    {
        if ( !pOuter->GetMotor()->IsFacing( m_vecHeardLookAt ) )
            pOuter->GetMotor()->FaceTowards( m_vecHeardLookAt );
    }

    // Periodically scan for threats in peripheral vision (zombie near follow target or us)
    if ( !m_NextPeripheralScan.HasStarted() || m_NextPeripheralScan.IsElapsed() )
    {
        m_NextPeripheralScan.Start( random->RandomFloat( 0.8f, 1.5f ) );

        CBaseEntity* pClosestThreat = nullptr;
        float flThreatDistSqr = 600.0f * 600.0f;
        Vector myPos = pOuter->GetAbsOrigin();

        // Check our follow target's position too, so we react to threats near them
        Vector checkPos = myPos;
        auto* pFollow = m_hFollowTarget.Get();
        if ( pFollow )
            checkPos = pFollow->GetAbsOrigin();

        static const char* s_szZombieClasses[] = {
            "npc_zombie", "npc_fastzombie", "npc_poisonzombie",
            "npc_burnzombie", "npc_dragzombie",
        };
        for ( int c = 0; c < ARRAYSIZE( s_szZombieClasses ); c++ )
        {
            CBaseEntity* pEnt = nullptr;
            while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szZombieClasses[c] )) != nullptr )
            {
                if ( !pEnt->IsAlive() ) continue;
                float dSqr = pEnt->GetAbsOrigin().DistToSqr( checkPos );
                if ( dSqr < flThreatDistSqr )
                {
                    flThreatDistSqr = dSqr;
                    pClosestThreat = pEnt;
                }
            }
        }

        if ( pClosestThreat )
        {
            // Turn to face the threat - the combat schedule will handle attacking
            m_vecHeardLookAt = pClosestThreat->WorldSpaceCenter();
            m_NextHeardLook.Start( 1.5f );
        }
    }

    // Handle commanded grab object (player held E on a physics object)
    CBaseEntity* pGrabTarget = pOuter->GetCommandedGrabTarget();
    if ( pGrabTarget )
    {
        Vector vecTarget = pGrabTarget->GetAbsOrigin();
        float flDist = pOuter->GetAbsOrigin().DistTo( vecTarget );

        if ( flDist < 80.0f )
        {
            // Close enough - switch to fists and grab it
            if ( !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_FISTS ) )
                pOuter->EquipWeaponOfType( BOTWEPRANGE_FISTS );

            pOuter->GetMotor()->FaceTowards( pGrabTarget->WorldSpaceCenter() );
            pOuter->PressUse( 0.15f );
            pOuter->ClearCommandedGrabTarget();
        }
        else
        {
            // Walk to the object
            CNavArea* pStart = pOuter->GetLastKnownArea();
            CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 256.0f, false );

            if ( pStart && pGoal )
            {
                if ( !m_ObjPath.IsValid() || !m_NextObjectiveScan.HasStarted() || m_NextObjectiveScan.IsElapsed() )
                {
                    Vector vecMyPos = pOuter->GetAbsOrigin();
                    m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
                    m_PathCost.SetStartPos( vecMyPos, pStart );
                    m_ObjPath.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost );
                    m_NextObjectiveScan.Start( 3.0f );
                }

                bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
                if ( m_ObjPath.IsValid() && !bBusy )
                    m_ObjPath.Update( pOuter );
            }
        }
        return;
    }
    else if ( pGrabTarget )
    {
        // Target was removed/destroyed
        pOuter->ClearCommandedGrabTarget();
    }

    int behavior = zm_sv_bot_default_behavior.GetInt();

    // If the bot was told to stay put, don't follow anyone
    if ( pOuter->IsStayingPut() )
    {
        m_hFollowTarget.Set( nullptr );
        m_Path.Invalidate();
        return;
    }

    // If a player explicitly told this bot to follow (via E key), always do that
    auto* pExplicitFollow = pOuter->GetFollowTarget();
    if ( pExplicitFollow && IsValidFollowTarget( pExplicitFollow, true ) )
    {
        // Follow the explicit target
        if ( m_hFollowTarget.Get() != pExplicitFollow || !m_Path.IsValid() )
            StartFollow( pExplicitFollow );

        bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
        if ( m_Path.IsValid() && !bBusy && ShouldMoveCloser( pExplicitFollow ) )
        {
            m_Path.Update( pOuter, pExplicitFollow, m_PathCost );
        }
        return;
    }

    // Voice command override takes priority over default behavior
    int effectiveBehavior = ( pOuter->GetBehaviorOverride() >= 0 ) ? pOuter->GetBehaviorOverride() : behavior;

    // No explicit follow target - use the effective behavior mode
    switch ( effectiveBehavior )
    {
    case 1: // Explore
        UpdateExploreMode();
        return;
    case 2: // Defend Spawn
        UpdateDefendMode();
        return;
    case 3: // Mixed Mode - each bot gets a random behavior
        UpdateMixedMode();
        return;
    default: // 0 = Follow nearest player
        break;
    }

    // Behavior 0: Follow nearest human player
    auto* pFollow = m_hFollowTarget.Get();

    if ( (pFollow && !IsValidFollowTarget( pFollow )) || m_NextFollowTarget.IsElapsed() )
    {
        NextFollow();
        pFollow = m_hFollowTarget.Get();
    }

    // No human survivors to follow - fall back to defending the spawn area
    if ( !pFollow )
    {
        UpdateDefendMode();
        return;
    }

    bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;

    if ( m_Path.IsValid() && pFollow && !bBusy && ShouldMoveCloser( pFollow ) )
    {
        m_Path.Update( pOuter, pFollow, m_PathCost );
    }
}

void CSurvivorFollowSchedule::OnSpawn()
{
    m_Path.Invalidate();
    m_ExplorePath.Invalidate();

    m_NextFollowTarget.Start( 0.5f );
    m_NextWeaponScan.Start( 2.0f );

    // Save spawn position for defend mode
    m_vecDefendPos = GetOuter()->GetAbsOrigin();
    m_bHasDefendPos = true;

    // Re-randomize mixed mode behavior each round
    m_iMixedBehavior = -1;
    m_bExploreIdling = false;
}

void CSurvivorFollowSchedule::OnHeardSound( CSound* pSound )
{
    if ( m_NextHeardLook.HasStarted() && !m_NextHeardLook.IsElapsed() )
        return;

    int soundType = pSound->SoundType();
    bool bInteresting = false;

    // React to combat, gunfire, danger, and any NPC/world sounds (zombie growls, attacks, etc.)
    if ( soundType & ( SOUND_COMBAT | SOUND_BULLET_IMPACT | SOUND_DANGER | SOUND_PLAYER | SOUND_WORLD ) )
        bInteresting = true;
    if ( !bInteresting )
        return;

    auto* pOwner = pSound->m_hOwner.Get();
    if ( pOwner && pOwner->IsPlayer() )
    {
        Vector fwd;
        AngleVectors( pOwner->EyeAngles(), &fwd );
        m_vecHeardLookAt = pOwner->EyePosition() + fwd * 1024.0f;
    }
    else
    {
        m_vecHeardLookAt = pSound->GetSoundOrigin();
    }

    m_NextHeardLook.Start( random->RandomFloat( 1.0f, 2.0f ) );
}

//NPCR::QueryResult_t CSurvivorFollowSchedule::IsBusy() const
//{
//    return m_Path.IsValid() ? NPCR::RES_YES : NPCR::RES_NONE;
//}

NPCR::QueryResult_t CSurvivorFollowSchedule::ShouldChase( CBaseEntity* pEnemy ) const
{
    // Always allow engaging enemies, even while following - bots should run and gun
    return NPCR::RES_NONE;
}

//void CSurvivorFollowSchedule::OnMoveSuccess( NPCR::CBaseNavPath* pPath )
//{
//        
//}

bool CSurvivorFollowSchedule::IsValidFollowTarget( CBasePlayer* pPlayer, bool bCheckLoop ) const
{
    if ( pPlayer->GetTeamNumber() != ZMTEAM_HUMAN || !pPlayer->IsAlive() )
    {
        return false;
    }

    auto* pOuter = GetOuter();

    // Make sure our following chain isn't circular.

    auto* pLoop = pPlayer;
    do
    {
        if ( pLoop->IsBot() )
        {
            auto* pBot = static_cast<CZMPlayerBot*>( pLoop );
            auto* pTheirTarget = pBot->GetFollowTarget();
            if ( pTheirTarget == pOuter ) // Don't follow a bot that is following us, lul.
            {
                return false;
            }

            if ( bCheckLoop )
                pLoop = pTheirTarget;
            else
                break;
        }
        else
        {
            break;
        }
    }
    while ( pLoop != nullptr );

    return true;
}

void CSurvivorFollowSchedule::NextFollow()
{
    float flNextCheck = 1.0f;

    auto* pLastFollow = m_hFollowTarget.Get();
    bool bWasValid = pLastFollow ? IsValidFollowTarget( pLastFollow, true ) : false;

    CBasePlayer* pFollow = FindSurvivorToFollow();

    if ( (pLastFollow != pFollow || !m_Path.IsValid()) && pFollow )
    {
        StartFollow( pFollow );
        flNextCheck = 15.0f;
    }
    else if ( !bWasValid )
    {
        m_hFollowTarget.Set( nullptr );
    }

    m_NextFollowTarget.Start( flNextCheck );
}

void CSurvivorFollowSchedule::StartFollow( CBasePlayer* pFollow )
{
    auto* pOuter = GetOuter();


    m_hFollowTarget.Set( pFollow );
    pOuter->SetFollowTarget( pFollow );

    // Spread bots in a fan behind the follow target using a golden-angle distribution.
    // Multiply by a prime (7) so consecutive entity indices produce widely separated slots.
    int slot = ( pOuter->entindex() * 7 ) % 12;
    // Alternate left/right and step back in rows of 3
    int col = slot % 3;   // 0=center, 1=left, 2=right
    int row = slot / 3;   // row depth
    float flSide = ( col == 0 ) ? 0.0f : ( col == 1 ? -1.0f : 1.0f );
    float flSideSpread = 64.0f + row * 16.0f;
    float flBackSpread = 48.0f + row * 48.0f;

    m_vecFormationOffset.x = 0.0f;
    m_vecFormationOffset.y = flSide * flSideSpread;
    m_vecFormationOffset.z = 0.0f;
    // Use z=0 and offset in follow-relative space: negative x = behind target
    // Actually store as world-space delta that gets added to follow goal
    m_vecFormationOffset.x = -flBackSpread;
    m_vecFormationOffset.y = flSide * flSideSpread;

    m_Path.SetGoalTolerance( 32.0f );
    m_Path.Compute( pOuter, pFollow, m_PathCost );
}

bool CSurvivorFollowSchedule::ShouldMoveCloser( CBasePlayer* pFollow ) const
{
    auto* pOuter = GetOuter();

    float flDistSqr = pOuter->GetAbsOrigin().DistToSqr( pFollow->GetAbsOrigin() );

    return ( flDistSqr > (160.0f * 160.0f) );
}

CBasePlayer* CSurvivorFollowSchedule::FindSurvivorToFollow( CBasePlayer* pIgnore, bool bAllowBot ) const
{
    auto* pOuter = GetOuter();

    Vector mypos = pOuter->GetAbsOrigin();

    CBasePlayer* pClosest = nullptr;
    float flClosestDist = FLT_MAX;

    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        auto* pPlayer = static_cast<CBasePlayer*>( UTIL_EntityByIndex( i ) );

        if ( !pPlayer ) continue;

        if ( pPlayer == pOuter ) continue;

        // Only follow human players, never other bots
        if ( pPlayer->IsBot() ) continue;

        if ( !IsValidFollowTarget( pPlayer, true ) ) continue;


        float dist = pPlayer->GetAbsOrigin().DistToSqr( mypos );
        if ( dist < flClosestDist )
        {
            flClosestDist = dist;
            pClosest = pPlayer;
        }
    }

    return pClosest;
}

void CSurvivorFollowSchedule::UpdateExploreMode()
{
    CZMPlayerBot* pOuter = GetOuter();

    // Scan for weapons/ammo while exploring
    TryPickupNearbyWeapons();

    // Hunt zombies: if one is nearby, face it so combat schedule can engage
    CBaseEntity* pZombie = FindNearestZombie( 800.0f );
    if ( pZombie )
    {
        m_vecHeardLookAt = pZombie->WorldSpaceCenter();
        m_NextHeardLook.Start( 1.5f );
    }

    // Idle pause: periodically stop and look around before continuing
    if ( m_bExploreIdling )
    {
        if ( !m_ExploreIdlePause.IsElapsed() )
        {
            // Still idling - look around
            UpdateExploreLookAngles();
            return;
        }
        // Done idling, resume moving
        m_bExploreIdling = false;
    }

    // Randomly trigger an idle pause when reaching a waypoint or after some travel time
    if ( m_ExplorePath.IsValid() && !m_ExploreIdlePause.HasStarted() )
    {
        m_ExploreIdlePause.Start( random->RandomFloat( 6.0f, 14.0f ) );
    }
    else if ( m_ExploreIdlePause.HasStarted() && m_ExploreIdlePause.IsElapsed() && !m_bExploreIdling )
    {
        // Time for a pause - stop and look around for 2-4 seconds
        m_bExploreIdling = true;
        m_ExploreIdlePause.Start( random->RandomFloat( 2.0f, 4.0f ) );
        m_ExplorePath.Invalidate();
        return;
    }

    // Natural look angles while moving
    UpdateExploreLookAngles();

    if ( !m_NextExplorePath.HasStarted() || m_NextExplorePath.IsElapsed() || !m_ExplorePath.IsValid() )
    {
        // Pick a random nav area and walk to it
        CNavArea* pStart = pOuter->GetLastKnownArea();
        if ( !pStart )
            return;

        int navCount = TheNavMesh->GetNavAreaCount();
        if ( navCount <= 0 )
            return;

        int n = random->RandomInt( 0, navCount - 1 );

        class CAreaPick
        {
        public:
            CAreaPick( int n ) { m_nCount = n; m_pArea = nullptr; }
            bool operator()( CNavArea* pArea )
            {
                if ( --m_nCount < 0 ) { m_pArea = pArea; return false; }
                return true;
            }
            CNavArea* GetArea() const { return m_pArea; }
        private:
            CNavArea* m_pArea;
            int m_nCount;
        };

        CAreaPick pick( n );
        TheNavMesh->ForAllAreas( pick );

        CNavArea* pGoal = pick.GetArea();
        if ( !pGoal )
            return;

        Vector vecMyPos = pOuter->GetAbsOrigin();
        Vector vecGoal = pGoal->GetCenter();

        m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
        m_PathCost.SetStartPos( vecMyPos, pStart );

        m_ExplorePath.Compute( vecMyPos, vecGoal, pStart, pGoal, m_PathCost );
        m_NextExplorePath.Start( 20.0f );
    }

    bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
    if ( m_ExplorePath.IsValid() && !bBusy )
    {
        m_ExplorePath.Update( pOuter );
    }
}

void CSurvivorFollowSchedule::UpdateExploreLookAngles()
{
    CZMPlayerBot* pOuter = GetOuter();

    // Periodically shift the scan pitch and yaw offset for natural head movement
    if ( !m_ExploreLookScan.HasStarted() || m_ExploreLookScan.IsElapsed() )
    {
        m_ExploreLookScan.Start( random->RandomFloat( 1.5f, 4.0f ) );

        if ( m_bExploreIdling )
        {
            // While idling, look around more dramatically
            m_flExploreScanPitch = random->RandomFloat( -15.0f, 10.0f );
            m_flExploreScanYawOffset = random->RandomFloat( -90.0f, 90.0f );
        }
        else
        {
            // While moving, gentle scanning - mostly forward, slight up/down variation
            m_flExploreScanPitch = random->RandomFloat( -8.0f, 5.0f );
            m_flExploreScanYawOffset = random->RandomFloat( -30.0f, 30.0f );
        }
    }

    QAngle angCur = pOuter->EyeAngles();

    // Smoothly interpolate pitch toward target scan pitch
    float flPitchDiff = m_flExploreScanPitch - angCur.x;
    float flPitchStep = gpGlobals->frametime * 30.0f;
    if ( fabsf( flPitchDiff ) > flPitchStep )
        angCur.x += ( flPitchDiff > 0 ? flPitchStep : -flPitchStep );
    else
        angCur.x = m_flExploreScanPitch;

    // Apply yaw offset while idling (while moving, Approach handles yaw)
    if ( m_bExploreIdling )
    {
        float flYawTarget = angCur.y + m_flExploreScanYawOffset;
        pOuter->GetMotor()->FaceTowards( flYawTarget );
    }

    // Clamp pitch to natural range
    angCur.x = clamp( angCur.x, -20.0f, 15.0f );
    pOuter->SetEyeAngles( angCur );
}

CBaseEntity* CSurvivorFollowSchedule::FindNearestZombie( float flMaxRange ) const
{
    CZMPlayerBot* pOuter = GetOuter();
    Vector myPos = pOuter->GetAbsOrigin();
    float flBestDistSqr = flMaxRange * flMaxRange;
    CBaseEntity* pBest = nullptr;

    static const char* s_szZombieClasses[] = {
        "npc_zombie", "npc_fastzombie", "npc_poisonzombie",
        "npc_burnzombie", "npc_dragzombie",
    };

    for ( int c = 0; c < ARRAYSIZE( s_szZombieClasses ); c++ )
    {
        CBaseEntity* pEnt = nullptr;
        while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szZombieClasses[c] )) != nullptr )
        {
            if ( !pEnt->IsAlive() ) continue;
            float dSqr = pEnt->GetAbsOrigin().DistToSqr( myPos );
            if ( dSqr < flBestDistSqr )
            {
                flBestDistSqr = dSqr;
                pBest = pEnt;
            }
        }
    }

    return pBest;
}

void CSurvivorFollowSchedule::UpdateDefendMode()
{
    CZMPlayerBot* pOuter = GetOuter();

    // Use player-commanded defend position if available
    if ( pOuter->HasCommandedDefendPos() )
    {
        m_vecDefendPos = pOuter->GetCommandedDefendPos();
        m_bHasDefendPos = true;
    }
    else if ( !m_bHasDefendPos )
    {
        m_vecDefendPos = pOuter->GetAbsOrigin();
        m_bHasDefendPos = true;
    }

    // Scan for weapons/ammo while defending
    TryPickupNearbyWeapons();

    // Hunt zombies nearby so we actively defend
    CBaseEntity* pZombie = FindNearestZombie( 600.0f );
    if ( pZombie )
    {
        m_vecHeardLookAt = pZombie->WorldSpaceCenter();
        m_NextHeardLook.Start( 1.5f );
    }

    // Stay near the defend position
    float flDist = pOuter->GetAbsOrigin().DistTo( m_vecDefendPos );
    if ( flDist > 256.0f )
    {
        // Need to walk back to the defend zone
        if ( !m_ExplorePath.IsValid() || m_NextExplorePath.IsElapsed() )
        {
            Vector vecMyPos = pOuter->GetAbsOrigin();
            CNavArea* pStart = pOuter->GetLastKnownArea();
            CNavArea* pGoal = TheNavMesh->GetNearestNavArea( m_vecDefendPos, true, 512.0f, false );

            if ( pStart && pGoal )
            {
                m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
                m_PathCost.SetStartPos( vecMyPos, pStart );
                m_ExplorePath.Compute( vecMyPos, m_vecDefendPos, pStart, pGoal, m_PathCost );
                m_NextExplorePath.Start( 10.0f );
            }
        }

        bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
        if ( m_ExplorePath.IsValid() && !bBusy )
        {
            m_ExplorePath.Update( pOuter );
        }
    }
}

void CSurvivorFollowSchedule::UpdateMixedMode()
{
    // Assign a random behavior on first call (per bot, persists for the round)
    if ( m_iMixedBehavior < 0 )
    {
        m_iMixedBehavior = random->RandomInt( 0, 2 );

        // Initialize defend position for mixed mode defend bots
        if ( m_iMixedBehavior == 2 && !m_bHasDefendPos )
        {
            m_vecDefendPos = GetOuter()->GetAbsOrigin();
            m_bHasDefendPos = true;
        }
    }

    switch ( m_iMixedBehavior )
    {
    case 1:
        UpdateExploreMode();
        return;
    case 2:
        UpdateDefendMode();
        return;
    default:
    {
        // Follow mode - scan for weapons/ammo and hunt zombies while following
        TryPickupNearbyWeapons();

        CBaseEntity* pZombie = FindNearestZombie( 600.0f );
        if ( pZombie )
        {
            m_vecHeardLookAt = pZombie->WorldSpaceCenter();
            m_NextHeardLook.Start( 1.5f );
        }

        // Ensure the follow target timer is running
        if ( !m_NextFollowTarget.HasStarted() || m_NextFollowTarget.IsElapsed() )
        {
            m_NextFollowTarget.Start( 0.1f );
            NextFollow();
        }

        auto* pFollow = m_hFollowTarget.Get();
        if ( pFollow && !IsValidFollowTarget( pFollow ) )
        {
            NextFollow();
            pFollow = m_hFollowTarget.Get();
        }

        if ( !pFollow )
        {
            // No human to follow - explore instead of just standing
            UpdateExploreMode();
            return;
        }

        // Make sure we have a path to follow target
        if ( !m_Path.IsValid() && pFollow )
            StartFollow( pFollow );

        bool bBusy = GetOuter()->IsBusy() == NPCR::RES_YES;
        if ( m_Path.IsValid() && pFollow && !bBusy && ShouldMoveCloser( pFollow ) )
        {
            m_Path.Update( GetOuter(), pFollow, m_PathCost );
        }
        return;
    }
    }
}

void CSurvivorFollowSchedule::TryPickupNearbyWeapons()
{
    CZMPlayerBot* pOuter = GetOuter();
    if ( !pOuter->IsAlive() )
        return;

    // If we're currently scavenging, handle the walk-to-item logic
    CBaseEntity* pScavTarget = m_hScavengeTarget.Get();
    if ( m_bScavenging )
    {
        if ( !pScavTarget )
        {
            // Target was picked up or removed - return to pre-scavenge position
            m_bScavenging = false;
            m_hScavengeTarget.Set( nullptr );

            // Path back to where we were before
            Vector vecMyPos = pOuter->GetAbsOrigin();
            if ( m_vecPreScavengePos.DistTo( vecMyPos ) > 64.0f )
            {
                CNavArea* pStart = pOuter->GetLastKnownArea();
                CNavArea* pGoal = TheNavMesh->GetNearestNavArea( m_vecPreScavengePos, true, 256.0f, false );
                if ( pStart && pGoal )
                {
                    m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
                    m_PathCost.SetStartPos( vecMyPos, pStart );
                    m_ObjPath.Compute( vecMyPos, m_vecPreScavengePos, pStart, pGoal, m_PathCost );
                }
            }
            return;
        }

        float flDist = pOuter->GetAbsOrigin().DistTo( pScavTarget->GetAbsOrigin() );
        if ( flDist < 64.0f )
        {
            pOuter->GetMotor()->FaceTowards( pScavTarget->GetAbsOrigin() );
            pOuter->PressUse( 0.15f );
            m_bScavenging = false;
            m_hScavengeTarget.Set( nullptr );
            m_NextWeaponScan.Start( 1.0f );
        }
        else
        {
            // Keep walking toward the item
            bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
            if ( m_ObjPath.IsValid() && !bBusy )
                m_ObjPath.Update( pOuter );
        }
        return;
    }

    // If returning from a scavenge trip, keep walking the return path
    if ( m_ObjPath.IsValid() )
    {
        bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
        if ( !bBusy )
            m_ObjPath.Update( pOuter );
        return;
    }

    if ( !m_NextWeaponScan.IsElapsed() )
        return;

    m_NextWeaponScan.Start( 2.0f );

    // Determine what loadout slots we still need
    bool bNeedMainGun = !pOuter->HasWeaponOfType( BOTWEPRANGE_MAINGUN );
    bool bNeedSidearm = !pOuter->HasWeaponOfType( BOTWEPRANGE_SECONDARYWEAPON );
    bool bNeedMelee   = !pOuter->HasWeaponOfType( BOTWEPRANGE_MELEE );
    bool bNeedGrenade = !pOuter->HasWeaponOfType( BOTWEPRANGE_THROWABLE );

    // Check if we need ammo for any ranged weapon we carry
    // Trigger when missing at least one clip's worth of reserve ammo
    bool bNeedAmmo = false;
    int iNeededAmmoType = -1;
    int iLowestAmmoMissing = 0;
    for ( int i = 0; i < MAX_WEAPONS; i++ )
    {
        CZMBaseWeapon* pWep = ToZMBaseWeapon( pOuter->GetWeapon( i ) );
        if ( !pWep || !pWep->UsesPrimaryAmmo() )
            continue;

        ZMBotWeaponTypeRange_t wtype = CZMPlayerBot::GetWeaponType( pWep );
        if ( wtype == BOTWEPRANGE_FISTS || wtype == BOTWEPRANGE_MELEE || wtype == BOTWEPRANGE_INVALID )
            continue;

        int iAmmoType = pWep->GetPrimaryAmmoType();
        if ( iAmmoType < 0 )
            continue;

        int iCurrent = pOuter->GetAmmoCount( iAmmoType );
        int iMax = GetAmmoDef()->MaxCarry( iAmmoType );
        if ( iMax <= 0 )
            continue;

        // Get the weapon's clip size as the threshold
        int iClipSize = pWep->GetMaxClip1();
        if ( iClipSize <= 0 )
            iClipSize = 10; // fallback

        int iMissing = iMax - iCurrent;
        if ( iMissing >= iClipSize && iMissing > iLowestAmmoMissing )
        {
            iLowestAmmoMissing = iMissing;
            iNeededAmmoType = iAmmoType;
            bNeedAmmo = true;
        }
    }

    // Fully loaded - nothing to pick up at all
    if ( !bNeedMainGun && !bNeedSidearm && !bNeedMelee && !bNeedGrenade && !bNeedAmmo )
        return;

    Vector myPos = pOuter->GetAbsOrigin();
    Vector eyePos = pOuter->EyePosition();
    float flSearchRange = zm_sv_bot_weapon_search_range.GetFloat();
    float flBestDist = flSearchRange;
    CBaseEntity* pBestWeapon = nullptr;
    int nBestPriority = 0;

    static const char* s_szWeaponClassnames[] = {
        "weapon_zm_pistol",
        "weapon_zm_shotgun",
        "weapon_zm_shotgun_sporting",
        "weapon_zm_mac10",
        "weapon_zm_rifle",
        "weapon_zm_revolver",
        "weapon_zm_sledge",
        "weapon_zm_improvised",
        "weapon_zm_molotov",
        "weapon_zm_fireaxe",
        "weapon_zm_r700",
        "weapon_zm_pipebomb",
    };

    for ( int w = 0; w < ARRAYSIZE( s_szWeaponClassnames ); w++ )
    {
        CBaseEntity* pEnt = nullptr;
        while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szWeaponClassnames[w] )) != nullptr )
        {
            CBaseCombatWeapon* pWep = pEnt->MyCombatWeaponPointer();
            if ( !pWep || pWep->GetOwner() )
                continue;

            ZMBotWeaponTypeRange_t wepType = CZMPlayerBot::GetWeaponType( pWep->GetClassname() );
            int nPriority = 0;
            switch ( wepType )
            {
            case BOTWEPRANGE_MAINGUN:   if ( bNeedMainGun ) nPriority = 4; break;
            case BOTWEPRANGE_SECONDARYWEAPON: if ( bNeedSidearm ) nPriority = 3; break;
            case BOTWEPRANGE_MELEE:     if ( bNeedMelee )   nPriority = 2; break;
            case BOTWEPRANGE_THROWABLE: if ( bNeedGrenade ) nPriority = 1; break;
            default: break;
            }
            if ( nPriority == 0 )
                continue;

            float dist = myPos.DistTo( pWep->GetAbsOrigin() );
            if ( dist > flSearchRange )
                continue;

            if ( nPriority < nBestPriority )
                continue;
            if ( nPriority == nBestPriority && dist >= flBestDist )
                continue;

            trace_t tr;
            UTIL_TraceLine( eyePos, pWep->WorldSpaceCenter(),
                MASK_VISIBLE, pOuter, COLLISION_GROUP_NONE, &tr );
            if ( tr.fraction < 0.9f && tr.m_pEnt != pWep )
                continue;

            flBestDist = dist;
            pBestWeapon = pWep;
            nBestPriority = nPriority;
        }
    }

    // Scan for ammo boxes if we're low on ammo for a carried weapon
    if ( bNeedAmmo && iNeededAmmoType >= 0 )
    {
        static const struct { const char* classname; const char* ammoname; } s_szAmmoClassnames[] = {
            { "item_ammo_pistol",        "Pistol"    },
            { "item_ammo_pistol_large",  "Pistol"    },
            { "item_box_buckshot",       "Buckshot"  },
            { "item_ammo_357",           "357"       },
            { "item_ammo_357_large",     "357"       },
            { "item_ammo_smg1",          "SMG1"      },
            { "item_ammo_smg1_large",    "SMG1"      },
            { "item_ammo_revolver",      "Revolver"  },
        };

        for ( int a = 0; a < ARRAYSIZE( s_szAmmoClassnames ); a++ )
        {
            int iBoxAmmoType = GetAmmoDef()->Index( s_szAmmoClassnames[a].ammoname );
            if ( iBoxAmmoType != iNeededAmmoType )
                continue;

            CBaseEntity* pAmmoEnt = nullptr;
            while ( (pAmmoEnt = gEntList.FindEntityByClassname( pAmmoEnt, s_szAmmoClassnames[a].classname )) != nullptr )
            {
                float dist = myPos.DistTo( pAmmoEnt->GetAbsOrigin() );
                if ( dist > flSearchRange )
                    continue;

                // Ammo is high priority when we need it - same as sidearm
                int nPriority = 3;
                if ( nPriority < nBestPriority )
                    continue;
                if ( nPriority == nBestPriority && dist >= flBestDist )
                    continue;

                trace_t tr;
                UTIL_TraceLine( eyePos, pAmmoEnt->WorldSpaceCenter(),
                    MASK_VISIBLE, pOuter, COLLISION_GROUP_NONE, &tr );
                if ( tr.fraction < 0.9f && tr.m_pEnt != pAmmoEnt )
                    continue;

                flBestDist = dist;
                pBestWeapon = pAmmoEnt;
                nBestPriority = nPriority;
            }
        }
    }

    if ( !pBestWeapon )
        return;

    float flPickupDist = myPos.DistTo( pBestWeapon->GetAbsOrigin() );
    if ( flPickupDist < 64.0f )
    {
        pOuter->GetMotor()->FaceTowards( pBestWeapon->GetAbsOrigin() );
        pOuter->PressUse( 0.15f );
    }
    else
    {
        // Save current position so we can return after picking up the item
        m_vecPreScavengePos = myPos;
        m_bScavenging = true;
        m_hScavengeTarget.Set( pBestWeapon );

        Vector vecTarget = pBestWeapon->GetAbsOrigin();
        CNavArea* pStart = pOuter->GetLastKnownArea();
        CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 256.0f, false );

        if ( pStart && pGoal )
        {
            m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
            m_PathCost.SetStartPos( myPos, pStart );

            if ( !m_ObjPath.Compute( myPos, vecTarget, pStart, pGoal, m_PathCost ) )
            {
                m_bScavenging = false;
                m_hScavengeTarget.Set( nullptr );
                m_NextWeaponScan.Start( 10.0f );
            }
        }
        else
        {
            m_bScavenging = false;
            m_hScavengeTarget.Set( nullptr );
        }
    }
}
