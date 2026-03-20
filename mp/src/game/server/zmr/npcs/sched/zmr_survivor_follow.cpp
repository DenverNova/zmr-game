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

ConVar zm_sv_bot_debug( "zm_sv_bot_debug", "0", FCVAR_CHEAT, "Enable AI survivor bot debug logging. 0=off, 1=on." );


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
    m_nScavengeStuckCount = 0;
    m_flLastScavengeDist = FLT_MAX;
    m_flExploreScanPitch = 0.0f;
    m_flExploreScanYawOffset = 0.0f;
    m_bExploreIdling = false;
    m_bFleeingExplosion = false;
    m_flDefendLookYaw = 0.0f;
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

    // Determine effective behavior early so we can route explore bots correctly
    int behavior = zm_sv_bot_default_behavior.GetInt();
    int effectiveBehavior = ( pOuter->GetBehaviorOverride() >= 0 ) ? pOuter->GetBehaviorOverride() : behavior;

    // Mixed Mode: assign a permanent random sub-behavior (0/1/2) on first update
    if ( effectiveBehavior == 3 )
    {
        if ( m_iMixedBehavior < 0 )
        {
            m_iMixedBehavior = random->RandomInt( 0, 2 );

            if ( zm_sv_bot_debug.GetBool() )
            {
                const char* names[] = { "Follow", "Explore", "Defend" };
                Msg( "[Bot %s] Mixed mode assigned: %s (%i)\n",
                    GetOuter()->GetPlayerName(), names[m_iMixedBehavior], m_iMixedBehavior );
            }

            if ( m_iMixedBehavior == 2 && !m_bHasDefendPos )
            {
                m_vecDefendPos = GetOuter()->GetAbsOrigin();
                m_bHasDefendPos = true;
            }
        }
        effectiveBehavior = m_iMixedBehavior;
    }

    bool bIsExploreMode = ( effectiveBehavior == 1 );

    // Explore mode bots should not be held back by StayingPut - the convar/override behavior wins
    if ( bIsExploreMode && pOuter->IsStayingPut() )
    {
        pOuter->SetStayPut( false );
    }

    // If we heard something interesting recently, look toward it
    if ( m_NextHeardLook.HasStarted() && !m_NextHeardLook.IsElapsed() && m_vecHeardLookAt != vec3_origin )
    {
        if ( !pOuter->GetMotor()->IsFacing( m_vecHeardLookAt ) )
            pOuter->GetMotor()->FaceTowards( m_vecHeardLookAt );
    }

    // Position deconfliction: nudge away from other bots to avoid stacking
    if ( !m_NextDeconflict.HasStarted() || m_NextDeconflict.IsElapsed() )
    {
        m_NextDeconflict.Start( 0.5f );
        Vector myPos = pOuter->GetAbsOrigin();
        Vector nudge = vec3_origin;
        int nTooClose = 0;

        for ( int i = 1; i <= gpGlobals->maxClients; i++ )
        {
            CBasePlayer* pOther = static_cast<CBasePlayer*>( UTIL_EntityByIndex( i ) );
            if ( !pOther || pOther == pOuter || !pOther->IsAlive() ) continue;
            if ( pOther->GetTeamNumber() != ZMTEAM_HUMAN ) continue;

            Vector delta = myPos - pOther->GetAbsOrigin();
            delta.z = 0;
            float flDist = delta.Length();
            if ( flDist < 48.0f && flDist > 1.0f )
            {
                nudge += delta.Normalized() * (48.0f - flDist);
                nTooClose++;
            }
            else if ( flDist <= 1.0f )
            {
                nudge.x += random->RandomFloat( -24.0f, 24.0f );
                nudge.y += random->RandomFloat( -24.0f, 24.0f );
                nTooClose++;
            }
        }

        if ( nTooClose > 0 )
        {
            Vector vecGoal = myPos + nudge;
            pOuter->GetMotor()->Approach( vecGoal );
        }
    }

    // Explore mode: skip scavenging, grab targets, threat scanning etc.
    // UpdateExploreMode handles its own lightweight pickup and threat detection.
    if ( bIsExploreMode )
    {
        // Throttle debug output
        bool bDebugThisTick = false;
        if ( zm_sv_bot_debug.GetBool() && ( !m_NextDebugLog.HasStarted() || m_NextDebugLog.IsElapsed() ) )
        {
            bDebugThisTick = true;
            m_NextDebugLog.Start( 5.0f );

            CZMBaseWeapon* pActive = pOuter->GetActiveWeapon();
            Msg( "[Bot %s] --- State (Explore) ---\n", pOuter->GetPlayerName() );
            Msg( "  Active weapon: %s\n", pActive ? pActive->GetClassname() : "(none)" );
            Msg( "  ExplorePath=%i  Idling=%i\n",
                m_ExplorePath.IsValid() ? 1 : 0, m_bExploreIdling ? 1 : 0 );
        }

        // Abort any lingering scavenge state from before we entered explore mode
        if ( m_bScavenging )
        {
            m_bScavenging = false;
            m_hScavengeTarget.Set( nullptr );
            m_ObjPath.Invalidate();
            m_nScavengeStuckCount = 0;
            m_flLastScavengeDist = FLT_MAX;
        }

        UpdateExploreMode();
        return;
    }

    // --- Non-explore behaviors: Follow / Defend ---

    // If carrying a physics object and a zombie is nearby, drop it and fight
    {
        CZMBaseWeapon* pActiveWep = pOuter->GetActiveWeapon();
        if ( pActiveWep && FStrEq( pActiveWep->GetClassname() + sizeof("weapon_zm_") - 1, "fistscarry" ) )
        {
            CBaseEntity* pNearZombie = FindNearestZombie( 400.0f );
            if ( pNearZombie )
            {
                pOuter->ForceDropOfCarriedPhysObjects( nullptr );
                pOuter->EquipBestWeapon();
            }
        }
    }

    // Combat takes priority over scavenging - if a zombie is close, abort scavenging
    if ( m_bScavenging || m_ObjPath.IsValid() )
    {
        CBaseEntity* pThreat = FindNearestZombie( 400.0f );
        if ( pThreat )
        {
            m_bScavenging = false;
            m_hScavengeTarget.Set( nullptr );
            m_ObjPath.Invalidate();
            m_nScavengeStuckCount = 0;
            m_flLastScavengeDist = FLT_MAX;
            pOuter->EquipBestWeapon();
        }
        else
        {
            TryPickupNearbyWeapons();
            return;
        }
    }

    // Periodically look for weapons to pick up (Follow/Defend only)
    TryPickupNearbyWeapons();

    // Keep the best weapon equipped when not in combat
    pOuter->EquipBestWeapon();

    // Periodically scan for threats in peripheral vision (zombie near follow target or us)
    if ( !m_NextPeripheralScan.HasStarted() || m_NextPeripheralScan.IsElapsed() )
    {
        m_NextPeripheralScan.Start( random->RandomFloat( 0.8f, 1.5f ) );

        CBaseEntity* pClosestThreat = nullptr;
        float flThreatDistSqr = 600.0f * 600.0f;
        Vector myPos = pOuter->GetAbsOrigin();

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
            if ( !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_FISTS ) )
                pOuter->EquipWeaponOfType( BOTWEPRANGE_FISTS );

            pOuter->GetMotor()->FaceTowards( pGrabTarget->WorldSpaceCenter() );
            pOuter->PressUse( 0.15f );
            pOuter->ClearCommandedGrabTarget();
        }
        else
        {
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
        pOuter->ClearCommandedGrabTarget();
    }

    // Throttle debug output to once every 5 seconds per bot
    bool bDebugThisTick = false;
    if ( zm_sv_bot_debug.GetBool() && ( !m_NextDebugLog.HasStarted() || m_NextDebugLog.IsElapsed() ) )
    {
        bDebugThisTick = true;
        m_NextDebugLog.Start( 5.0f );

        CZMBaseWeapon* pActive = pOuter->GetActiveWeapon();
        Msg( "[Bot %s] --- State ---\n", pOuter->GetPlayerName() );
        Msg( "  Active weapon: %s\n", pActive ? pActive->GetClassname() : "(none)" );
        Msg( "  Inventory:\n" );
        for ( int i = 0; i < MAX_WEAPONS; i++ )
        {
            CZMBaseWeapon* pWep = ToZMBaseWeapon( pOuter->GetWeapon( i ) );
            if ( !pWep ) continue;
            int clip = pWep->Clip1();
            int ammoType = pWep->GetPrimaryAmmoType();
            int reserve = ( ammoType >= 0 ) ? pOuter->GetAmmoCount( ammoType ) : -1;
            Msg( "    [%i] %s  clip=%i reserve=%i type=%i\n",
                i, pWep->GetClassname(), clip, reserve, CZMPlayerBot::GetWeaponType( pWep ) );
        }
        Msg( "  Scavenging=%i  ObjPath=%i  FollowPath=%i  ExplorePath=%i\n",
            m_bScavenging ? 1 : 0, m_ObjPath.IsValid() ? 1 : 0,
            m_Path.IsValid() ? 1 : 0, m_ExplorePath.IsValid() ? 1 : 0 );
        Msg( "  StayingPut=%i  HasDefendPos=%i  MixedBehavior=%i\n",
            pOuter->IsStayingPut() ? 1 : 0, m_bHasDefendPos ? 1 : 0, m_iMixedBehavior );
    }

    // If the bot was told to stay put, don't follow anyone
    if ( pOuter->IsStayingPut() )
    {
        if ( bDebugThisTick )
            Msg( "[Bot %s] Staying put - skipping behavior update\n", pOuter->GetPlayerName() );
        m_hFollowTarget.Set( nullptr );
        m_Path.Invalidate();
        return;
    }

    // If a player explicitly told this bot to follow (via E key), always do that
    auto* pExplicitFollow = pOuter->GetFollowTarget();
    if ( pExplicitFollow && IsValidFollowTarget( pExplicitFollow, true ) )
    {
        if ( bDebugThisTick )
            Msg( "[Bot %s] Explicit follow target: %s\n", pOuter->GetPlayerName(), pExplicitFollow->GetPlayerName() );

        if ( !ShouldMoveCloser( pExplicitFollow ) )
        {
            m_Path.Invalidate();
            return;
        }

        if ( m_hFollowTarget.Get() != pExplicitFollow || !m_Path.IsValid() )
            StartFollow( pExplicitFollow );

        bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
        if ( m_Path.IsValid() && !bBusy )
        {
            m_Path.Update( pOuter, pExplicitFollow, m_PathCost );
        }
        else if ( !m_Path.IsValid() && !bBusy )
        {
            // No navmesh or path failed - walk directly toward the target
            pOuter->GetMotor()->Approach( pExplicitFollow->GetAbsOrigin() );
        }
        return;
    }

    if ( bDebugThisTick )
        Msg( "[Bot %s] Behavior: convar=%i override=%i effective=%i\n",
            pOuter->GetPlayerName(), behavior, pOuter->GetBehaviorOverride(), effectiveBehavior );

    // Dispatch to behavior mode
    switch ( effectiveBehavior )
    {
    case 2: // Defend Spawn
        UpdateDefendMode();
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
        if ( bDebugThisTick )
            Msg( "[Bot %s] No follow target found - falling back to defend mode\n", pOuter->GetPlayerName() );
        UpdateDefendMode();
        return;
    }

    if ( !ShouldMoveCloser( pFollow ) )
    {
        m_Path.Invalidate();
        return;
    }

    if ( !m_Path.IsValid() )
        StartFollow( pFollow );

    bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;

    if ( m_Path.IsValid() && !bBusy )
    {
        m_Path.Update( pOuter, pFollow, m_PathCost );
    }
    else if ( !m_Path.IsValid() && !bBusy )
    {
        // No navmesh or path failed - walk directly toward the target
        pOuter->GetMotor()->Approach( pFollow->GetAbsOrigin() );
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
    m_NextExplorePath.Invalidate();
    m_ExplorePath.Invalidate();

    // Reset scavenge state
    m_bScavenging = false;
    m_hScavengeTarget.Set( nullptr );
    m_nScavengeStuckCount = 0;
    m_flLastScavengeDist = FLT_MAX;
    m_BlacklistedItems.Purge();
    m_BlacklistClearTimer.Invalidate();
    m_flDefendLookYaw = 0.0f;
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

    m_Path.SetGoalTolerance( 128.0f );
    m_Path.Compute( pOuter, pFollow, m_PathCost );
}

bool CSurvivorFollowSchedule::ShouldMoveCloser( CBasePlayer* pFollow ) const
{
    auto* pOuter = GetOuter();

    float flDistSqr = pOuter->GetAbsOrigin().DistToSqr( pFollow->GetAbsOrigin() );

    // Hysteresis: start moving when far, stop when close
    // If we're already moving (path valid), keep going until 128 units
    // If we're stopped (no path), don't start until 256 units
    if ( m_Path.IsValid() )
        return ( flDistSqr > (128.0f * 128.0f) );
    else
        return ( flDistSqr > (256.0f * 256.0f) );
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

    // Initial scavenge phase: for the first few seconds after spawn, use normal
    // scavenging to pick up nearby weapons and ammo before heading out to explore.
    // m_NextWeaponScan is initialized to 2.0s on spawn, so it won't be elapsed immediately.
    if ( !m_NextExplorePath.HasStarted() )
    {
        // First time entering explore mode this round - allow scavenging for a bit
        TryPickupNearbyWeapons();
        pOuter->EquipBestWeapon();

        // If actively walking to a scavenge target, let it finish
        if ( m_bScavenging && m_ObjPath.IsValid() )
        {
            m_ObjPath.Update( pOuter );
            return;
        }

        // Done scavenging or nothing nearby - start exploring
        m_bScavenging = false;
        m_hScavengeTarget.Set( nullptr );
        m_ObjPath.Invalidate();
        m_NextExplorePath.Start( 0.1f );
    }

    // While exploring, pick up weapons and ammo we walk near (within 128 units)
    if ( m_NextWeaponScan.IsElapsed() )
    {
        m_NextWeaponScan.Start( 0.5f );
        Vector myPos = pOuter->GetAbsOrigin();

        static const char* s_szPickupClasses[] = {
            "weapon_zm_pistol", "weapon_zm_shotgun", "weapon_zm_shotgun_sporting",
            "weapon_zm_mac10", "weapon_zm_rifle", "weapon_zm_revolver",
            "weapon_zm_sledge", "weapon_zm_improvised", "weapon_zm_molotov",
            "weapon_zm_fireaxe", "weapon_zm_r700", "weapon_zm_pipebomb",
            "item_ammo_pistol", "item_ammo_pistol_large", "item_box_buckshot",
            "item_ammo_357", "item_ammo_357_large", "item_ammo_smg1",
            "item_ammo_smg1_large", "item_ammo_revolver",
        };
        for ( int w = 0; w < ARRAYSIZE( s_szPickupClasses ); w++ )
        {
            CBaseEntity* pEnt = nullptr;
            while ( (pEnt = gEntList.FindEntityByClassname( pEnt, s_szPickupClasses[w] )) != nullptr )
            {
                if ( pEnt->GetOwnerEntity() ) continue;
                CBaseCombatWeapon* pWep = pEnt->MyCombatWeaponPointer();
                if ( pWep && pWep->GetOwner() ) continue;

                if ( myPos.DistTo( pEnt->GetAbsOrigin() ) < 128.0f )
                {
                    pOuter->GetMotor()->FaceTowards( pEnt->GetAbsOrigin() );
                    pOuter->PressUse( 0.15f );
                    break;
                }
            }
        }
        pOuter->EquipBestWeapon();
    }

    // If carrying a physics object and a zombie is nearby, drop it and fight
    CZMBaseWeapon* pActiveWep = pOuter->GetActiveWeapon();
    if ( pActiveWep && FStrEq( pActiveWep->GetClassname() + sizeof("weapon_zm_") - 1, "fistscarry" ) )
    {
        CBaseEntity* pNearZombie = FindNearestZombie( 400.0f );
        if ( pNearZombie )
        {
            pOuter->ForceDropOfCarriedPhysObjects( nullptr );
            pOuter->EquipBestWeapon();
        }
    }

    // Combat engagement: when a zombie is nearby, stop exploring and let combat schedule handle it
    CBaseEntity* pZombie = FindNearestZombie( 600.0f );
    if ( pZombie )
    {
        // Stop moving so the combat schedule can properly intercept
        m_ExplorePath.Invalidate();
        m_vecHeardLookAt = pZombie->WorldSpaceCenter();
        m_NextHeardLook.Start( 1.5f );

        // Face the zombie and let combat take over
        pOuter->GetMotor()->FaceTowards( pZombie->WorldSpaceCenter() );
        pOuter->EquipBestWeapon();
        return;
    }

    // Continuously explore: when path is done or invalid, immediately pick a new destination
    if ( !m_ExplorePath.IsValid() || m_NextExplorePath.IsElapsed() )
    {
        CNavArea* pStart = pOuter->GetLastKnownArea();
        if ( !pStart )
        {
            pStart = TheNavMesh->GetNearestNavArea( pOuter->GetAbsOrigin(), true, 512.0f, false );
            if ( !pStart )
            {
                if ( zm_sv_bot_debug.GetBool() )
                    Msg( "[Bot %s] Explore: no nav area near bot!\n", pOuter->GetPlayerName() );
                return;
            }
        }

        int navCount = TheNavMesh->GetNavAreaCount();
        if ( navCount <= 0 )
            return;

        Vector vecMyPos = pOuter->GetAbsOrigin();
        bool bPathFound = false;

        // Use per-bot entropy so each bot picks different areas.
        // Entity index + current time ensures unique random sequences per bot.
        int botSeed = pOuter->entindex() * 7919 + (int)(gpGlobals->curtime * 1000.0f);

        for ( int attempt = 0; attempt < 10 && !bPathFound; attempt++ )
        {
            // Mix the attempt number into the seed for variety across retries
            int n = abs( (botSeed + attempt * 6271) % navCount );

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
            if ( !pGoal || pGoal == pStart )
                continue;

            Vector vecGoal = pGoal->GetCenter();
            if ( vecMyPos.DistToSqr( vecGoal ) < (512.0f * 512.0f) )
                continue;

            m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
            m_PathCost.SetStartPos( vecMyPos, pStart );
            m_PathCost.SetGoalPos( vecGoal, pGoal );

            if ( m_ExplorePath.Compute( vecMyPos, vecGoal, pStart, pGoal, m_PathCost ) )
            {
                bPathFound = true;
                m_NextExplorePath.Start( 30.0f );

                if ( zm_sv_bot_debug.GetBool() )
                    Msg( "[Bot %s] Explore: heading to area %i (dist=%.0f)\n",
                        pOuter->GetPlayerName(), pGoal->GetID(), vecMyPos.DistTo( vecGoal ) );
            }
        }

        if ( !bPathFound )
        {
            if ( zm_sv_bot_debug.GetBool() )
                Msg( "[Bot %s] Explore: no reachable area found, retrying...\n", pOuter->GetPlayerName() );
            m_NextExplorePath.Start( 0.5f );
        }
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
    else
    {
        // No zombies nearby - look around calmly in different directions
        if ( !m_DefendLookTimer.HasStarted() || m_DefendLookTimer.IsElapsed() )
        {
            m_DefendLookTimer.Start( random->RandomFloat( 3.0f, 6.0f ) );

            // Pick a new absolute target yaw (not an offset from current)
            QAngle angBase = pOuter->EyeAngles();
            m_flDefendLookYaw = anglemod( angBase.y + random->RandomFloat( -120.0f, 120.0f ) );
        }

        // Smoothly face the absolute target direction using a world-space point
        Vector fwd;
        QAngle lookAng( 0.0f, m_flDefendLookYaw, 0.0f );
        AngleVectors( lookAng, &fwd );
        Vector vecLookTarget = pOuter->EyePosition() + fwd * 512.0f;
        pOuter->GetMotor()->FaceTowards( vecLookTarget );
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
            if ( !pStart )
                pStart = TheNavMesh->GetNearestNavArea( vecMyPos, true, 512.0f, false );
            CNavArea* pGoal = TheNavMesh->GetNearestNavArea( m_vecDefendPos, true, 512.0f, false );

            if ( pStart && pGoal )
            {
                m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
                m_PathCost.SetStartPos( vecMyPos, pStart );
                m_PathCost.SetGoalPos( m_vecDefendPos, pGoal );
                m_ExplorePath.Compute( vecMyPos, m_vecDefendPos, pStart, pGoal, m_PathCost );
                m_NextExplorePath.Start( 10.0f );
            }
        }

        bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
        if ( m_ExplorePath.IsValid() && !bBusy )
        {
            m_ExplorePath.Update( pOuter );
        }
        else if ( !m_ExplorePath.IsValid() && !bBusy )
        {
            // No navmesh - walk directly toward defend position
            pOuter->GetMotor()->Approach( m_vecDefendPos );
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
                if ( !pStart )
                    pStart = TheNavMesh->GetNearestNavArea( vecMyPos, true, 512.0f, false );
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
            m_nScavengeStuckCount = 0;
            m_flLastScavengeDist = FLT_MAX;
            m_NextWeaponScan.Start( 1.0f );
        }
        else
        {
            // Stuck detection: if we haven't made meaningful progress, give up quickly
            if ( flDist >= m_flLastScavengeDist - 8.0f )
                m_nScavengeStuckCount++;
            else
                m_nScavengeStuckCount = 0;
            m_flLastScavengeDist = flDist;

            if ( m_nScavengeStuckCount > 15 || !m_ObjPath.IsValid() )
            {
                // Blacklist this item so we don't try again soon
                m_BlacklistedItems.AddToTail( m_hScavengeTarget );
                if ( !m_BlacklistClearTimer.HasStarted() )
                    m_BlacklistClearTimer.Start( 120.0f );

                if ( zm_sv_bot_debug.GetBool() )
                    Msg( "[Bot %s] Scavenge stuck on '%s' (dist=%.0f, stuck=%i) - blacklisting\n",
                        pOuter->GetPlayerName(), pScavTarget->GetClassname(), flDist, m_nScavengeStuckCount );

                m_bScavenging = false;
                m_hScavengeTarget.Set( nullptr );
                m_ObjPath.Invalidate();
                m_nScavengeStuckCount = 0;
                m_flLastScavengeDist = FLT_MAX;
                m_NextWeaponScan.Start( 5.0f );
                return;
            }

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

    // Periodically clear the blacklist so items become candidates again
    if ( m_BlacklistClearTimer.HasStarted() && m_BlacklistClearTimer.IsElapsed() )
    {
        m_BlacklistedItems.Purge();
        m_BlacklistClearTimer.Invalidate();
    }

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

    // Throttle debug output (shares timer with OnUpdate)
    bool bDebug = zm_sv_bot_debug.GetBool() && m_NextDebugLog.HasStarted() && m_NextDebugLog.GetRemainingTime() > 2.5f;

    // Fully loaded - nothing to pick up at all
    if ( !bNeedMainGun && !bNeedSidearm && !bNeedMelee && !bNeedGrenade && !bNeedAmmo )
        return;

    if ( bDebug )
        Msg( "[Bot %s] TryPickup: need main=%i side=%i melee=%i nade=%i ammo=%i (ammoType=%i)\n",
            pOuter->GetPlayerName(), bNeedMainGun, bNeedSidearm, bNeedMelee, bNeedGrenade, bNeedAmmo, iNeededAmmoType );

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

            // Skip blacklisted items we previously failed to reach
            bool bBlacklisted = false;
            for ( int bl = 0; bl < m_BlacklistedItems.Count(); bl++ )
            {
                if ( m_BlacklistedItems[bl].Get() == pEnt )
                { bBlacklisted = true; break; }
            }
            if ( bBlacklisted )
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
                // Skip blacklisted items
                bool bBlacklisted = false;
                for ( int bl = 0; bl < m_BlacklistedItems.Count(); bl++ )
                {
                    if ( m_BlacklistedItems[bl].Get() == pAmmoEnt )
                    { bBlacklisted = true; break; }
                }
                if ( bBlacklisted )
                    continue;

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
    {
        if ( bDebug )
            Msg( "[Bot %s] TryPickup: nothing found in range %.0f\n", pOuter->GetPlayerName(), flSearchRange );
        return;
    }

    if ( bDebug )
        Msg( "[Bot %s] TryPickup: found '%s' at dist=%.0f priority=%i\n",
            pOuter->GetPlayerName(), pBestWeapon->GetClassname(), myPos.DistTo( pBestWeapon->GetAbsOrigin() ), nBestPriority );

    float flPickupDist = myPos.DistTo( pBestWeapon->GetAbsOrigin() );
    if ( flPickupDist < 64.0f )
    {
        if ( bDebug )
            Msg( "[Bot %s] TryPickup: close enough, pressing USE on '%s'\n", pOuter->GetPlayerName(), pBestWeapon->GetClassname() );
        pOuter->GetMotor()->FaceTowards( pBestWeapon->GetAbsOrigin() );
        pOuter->PressUse( 0.15f );
    }
    else
    {
        if ( bDebug )
            Msg( "[Bot %s] TryPickup: walking to '%s' (%.0f units away)\n", pOuter->GetPlayerName(), pBestWeapon->GetClassname(), flPickupDist );
        // Save current position so we can return after picking up the item
        m_vecPreScavengePos = myPos;
        m_bScavenging = true;
        m_hScavengeTarget.Set( pBestWeapon );

        Vector vecTarget = pBestWeapon->GetAbsOrigin();
        CNavArea* pStart = pOuter->GetLastKnownArea();
        if ( !pStart )
            pStart = TheNavMesh->GetNearestNavArea( myPos, true, 512.0f, false );
        CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 256.0f, false );

        if ( pStart && pGoal )
        {
            m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
            m_PathCost.SetStartPos( myPos, pStart );

            if ( !m_ObjPath.Compute( myPos, vecTarget, pStart, pGoal, m_PathCost ) )
            {
                // Path failed - blacklist this item so we don't keep trying
                m_BlacklistedItems.AddToTail( EHANDLE( pBestWeapon ) );
                if ( !m_BlacklistClearTimer.HasStarted() )
                    m_BlacklistClearTimer.Start( 120.0f );
                m_bScavenging = false;
                m_hScavengeTarget.Set( nullptr );
                m_NextWeaponScan.Start( 2.0f );
            }
        }
        else
        {
            // No nav area found near item - blacklist it
            m_BlacklistedItems.AddToTail( EHANDLE( pBestWeapon ) );
            if ( !m_BlacklistClearTimer.HasStarted() )
                m_BlacklistClearTimer.Start( 120.0f );
            m_bScavenging = false;
            m_hScavengeTarget.Set( nullptr );
        }
    }
}
