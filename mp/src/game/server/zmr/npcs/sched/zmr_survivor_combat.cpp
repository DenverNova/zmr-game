#include "cbase.h"

#include "props.h"
#include "zmr_survivor_follow.h"
#include "zmr_survivor_combat.h"
#include "zmr_voicelines.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


extern ConVar zm_sv_debug_bot_lookat;


CSurvivorCombatSchedule::CSurvivorCombatSchedule()
{
    m_pAttackCloseRangeSched = new CSurvivorAttackCloseRangeSchedule;
    m_pAttackLongRangeSched = new CSurvivorAttackLongRangeSchedule;

    m_vecLookAt = vec3_origin;
    m_pLastLookArea = nullptr;
    m_flIdleSince = 0.0f;
    m_bWasMoving = false;
    m_RetreatPosition = vec3_origin;
    m_RetreatTime = 0.0f;
    m_nRetreatAttempts = 0;
}

CSurvivorCombatSchedule::~CSurvivorCombatSchedule()
{
    delete m_pAttackCloseRangeSched;
    delete m_pAttackLongRangeSched;
}


NPCR::CSchedule<CZMPlayerBot>* CSurvivorCombatSchedule::CreateFriendSchedule()
{
    return new CSurvivorFollowSchedule;
}

void CSurvivorCombatSchedule::OnStart()
{
    m_NextEnemyScan.Invalidate();
    m_NextRangeCheck.Invalidate();
    m_NextHeardLook.Invalidate();
    m_NextLookAround.Invalidate();
    m_hLastCombatTarget.Set( nullptr );
    m_RetreatPosition = vec3_origin;
    m_RetreatTime = 0.0f;
    m_nRetreatAttempts = 0;
}

void CSurvivorCombatSchedule::OnContinue()
{
    m_Path.Invalidate();
    m_NextLookAround.Start( 4.0f );
}

void CSurvivorCombatSchedule::OnIntercept()
{
    m_Path.Invalidate();
}

void CSurvivorCombatSchedule::OnUpdate()
{
    CZMPlayerBot* pOuter = GetOuter();

    if ( !pOuter->IsHuman() )
    {
        return;
    }



    // Explosive barrel targeting: if we have a ranged weapon, check for barrels
    // with 5+ zombies clustered nearby and shoot them for massive damage
    if ( pOuter->HasAnyEffectiveRangeWeapons() && (!m_NextBarrelScan.HasStarted() || m_NextBarrelScan.IsElapsed()) )
    {
        m_NextBarrelScan.Start( 1.0f );

        CBaseEntity* pBestBarrel = nullptr;
        int nBestZombies = 0;

        CBaseEntity* pProp = nullptr;
        while ( (pProp = gEntList.FindEntityByClassname( pProp, "prop_physics*" )) != nullptr )
        {
            if ( !pProp->IsAlive() )
                continue;

            CBreakableProp* pBreakable = dynamic_cast<CBreakableProp*>( pProp );
            if ( !pBreakable || pBreakable->GetExplosiveDamage() <= 0.0f )
                continue;

            // Must be within reasonable shooting distance and visible
            float flBarrelDist = pProp->GetAbsOrigin().DistTo( pOuter->GetPosition() );
            if ( flBarrelDist > 1024.0f || flBarrelDist < 200.0f )
                continue;

            if ( !pOuter->GetSenses()->HasLOS( pProp->WorldSpaceCenter() ) )
                continue;

            // Count zombies near this barrel
            int nNearby = 0;
            float flBlastRadius = 350.0f;
            static const char* s_szZombies[] = {
                "npc_zombie", "npc_fastzombie", "npc_poisonzombie",
                "npc_burnzombie", "npc_dragzombie",
            };
            for ( int z = 0; z < ARRAYSIZE( s_szZombies ); z++ )
            {
                CBaseEntity* pZombie = nullptr;
                while ( (pZombie = gEntList.FindEntityByClassname( pZombie, s_szZombies[z] )) != nullptr )
                {
                    if ( !pZombie->IsAlive() ) continue;
                    if ( pZombie->GetAbsOrigin().DistTo( pProp->GetAbsOrigin() ) < flBlastRadius )
                        nNearby++;
                }
            }

            if ( nNearby >= 5 && nNearby > nBestZombies )
            {
                nBestZombies = nNearby;
                pBestBarrel = pProp;
            }
        }

        if ( pBestBarrel )
        {
            // Equip a ranged weapon and shoot the barrel
            if ( !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_MAINGUN ) )
                pOuter->EquipWeaponOfType( BOTWEPRANGE_MAINGUN );
            if ( !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_MAINGUN ) && !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_SECONDARYWEAPON ) )
                pOuter->EquipWeaponOfType( BOTWEPRANGE_SECONDARYWEAPON );

            Vector vecBarrel = pBestBarrel->WorldSpaceCenter();
            pOuter->GetMotor()->FaceTowards( vecBarrel );
            if ( pOuter->GetMotor()->IsFacing( vecBarrel, 15.0f ) )
                pOuter->PressFire1( 0.15f );
        }
    }

    if ( !m_NextEnemyScan.HasStarted() || m_NextEnemyScan.IsElapsed() )
    {
        m_NextEnemyScan.Start( 0.1f );


        CBaseEntity* pClosestEnemy = pOuter->GetSenses()->GetClosestEntity();

        // Taunt on kill: if our last target died, 45% chance to taunt (30s cooldown)
        CBaseEntity* pLastTarget = m_hLastCombatTarget.Get();
        if ( pLastTarget && !pLastTarget->IsAlive() )
        {
            if ( !m_NextTauntVoice.HasStarted() || m_NextTauntVoice.IsElapsed() )
            {
                m_NextTauntVoice.Start( 30.0f );
                if ( random->RandomFloat( 0.0f, 1.0f ) < 0.45f )
                    ZMGetVoiceLines()->OnVoiceLine( pOuter, 4 ); // Taunt
            }
            m_hLastCombatTarget.Set( nullptr );
        }

        if ( pClosestEnemy && pOuter->ShouldChase( pClosestEnemy ) != NPCR::RES_NO )
        {
            m_hLastCombatTarget.Set( pClosestEnemy );

            float flDist = pClosestEnemy->GetAbsOrigin().DistTo( pOuter->GetPosition() );

            if ( pOuter->HasAnyEffectiveRangeWeapons() )
            {
                if ( flDist > 400.0f )
                {
                    m_pAttackLongRangeSched->SetAttackTarget( pClosestEnemy );
                    Intercept( m_pAttackLongRangeSched, "Trying to attack long range!" );
                }

                if ( !IsIntercepted() )
                {
                    m_pAttackCloseRangeSched->SetAttackTarget( pClosestEnemy );
                    m_pAttackCloseRangeSched->SetMeleeing( false );
                    Intercept( m_pAttackCloseRangeSched, "Trying to attack close range!" );
                }
            }

            // Melee fallback: only fight with fists/melee when cornered (enemy very close).
            // If the enemy is further away, the bot should try to flee and find guns/ammo.
            if ( !IsIntercepted() )
            {
                // Cornered threshold: if zombie is within 200u, we must fight
                // Otherwise try to kite/flee and let the follow schedule find weapons
                if ( flDist < 200.0f )
                {
                    m_pAttackCloseRangeSched->SetAttackTarget( pClosestEnemy );
                    m_pAttackCloseRangeSched->SetMeleeing( true );
                    Intercept( m_pAttackCloseRangeSched, "Cornered - fighting with melee!" );
                }
                else
                {
                    // Flee from the zombie - move back and let weapon scavenging happen
                    MoveBackFromThreat( pClosestEnemy );
                }
            }

            if ( IsIntercepted() )
                return;
        }
    }


    SetSelfCall( true ); // Ignore our own IsBusy listener.
    bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
    SetSelfCall( false );

    if ( !bBusy && (!m_NextRangeCheck.HasStarted() || m_NextRangeCheck.IsElapsed()) )
    {
        m_NextRangeCheck.Start( 0.5f );

        CBaseEntity* pEnemy = m_hLastCombatTarget.Get();
        if ( pEnemy )
        {
            if ( ShouldMoveBack( pEnemy ) )
                MoveBackFromThreat( pEnemy );
        }
            
    }


    if ( m_Path.IsValid() && !bBusy )
    {
        m_Path.Update( pOuter );

        // Wall-running stuck detection: if retreating but barely moved, try a new direction
        if ( m_bMovingOutOfRange && m_RetreatPosition != vec3_origin )
        {
            float flDistMoved = (pOuter->GetPosition() - m_RetreatPosition).Length2D();
            if ( flDistMoved < 10.0f && gpGlobals->curtime - m_RetreatTime > 1.0f )
            {
                m_Path.Invalidate();
                CBaseEntity* pEnemy = m_hLastCombatTarget.Get();
                if ( pEnemy )
                    MoveBackFromThreat( pEnemy );
            }
        }
    }
    else
    {
        m_Path.Invalidate();

        m_bMovingOutOfRange = false;
        m_nRetreatAttempts = 0;
    }


    // Look-around idle behavior: only trigger after the bot has been stationary for a random delay.
    // Track moving->stopped transitions per bot so they don't all sync up.
    bool bMovingNow = pOuter->GetMotor()->IsMoving();
    if ( bMovingNow )
    {
        // Bot is moving - reset idle timer and suppress look-around
        m_bWasMoving = true;
        m_flIdleSince = 0.0f;
        m_NextLookAround.Invalidate();
    }
    else
    {
        // Bot just stopped - record the time
        if ( m_bWasMoving )
        {
            m_bWasMoving = false;
            // Short per-bot delay before starting to look around, offset by entindex
            float flDelay = random->RandomFloat( 0.5f, 1.5f ) + ( (pOuter->entindex() * 3) % 7 ) * 0.1f;
            m_flIdleSince = gpGlobals->curtime + flDelay;
            m_NextLookAround.Invalidate();
        }

        // Only look around once we've been idle long enough
        bool bIdleReady = ( m_flIdleSince > 0.0f && gpGlobals->curtime >= m_flIdleSince );

        if ( bIdleReady )
        {
            if ( !m_NextLookAround.HasStarted() || m_NextLookAround.IsElapsed() )
            {
                // Pick a new independent random look direction for this bot
                // Use entindex as part of the seed offset so bots don't sync
                float flInterval = random->RandomFloat( 3.0f, 6.0f ) + ( pOuter->entindex() % 5 ) * 0.3f;
                m_NextLookAround.Start( flInterval );

                CNavArea* pMyArea = pOuter->GetLastKnownArea();
                if ( pMyArea )
                {
                    // Pick a random direction offset by entindex so bots look different ways
                    int startDir = pOuter->entindex() % NUM_DIRECTIONS;
                    for ( int i = 0; i < NUM_DIRECTIONS; i++ )
                    {
                        NavDirType dir = (NavDirType)( (startDir + i) % NUM_DIRECTIONS );
                        CNavArea* pArea = pMyArea->GetRandomAdjacentArea( dir );
                        if ( pArea && pArea != m_pLastLookArea )
                        {
                            m_pLastLookArea = pArea;
                            // Use eye-height offset so bots look forward, not at the floor
                            Vector vecEyeOfs = pOuter->EyePosition() - pOuter->GetAbsOrigin();
                            m_vecLookAt = pArea->GetRandomPoint() + vecEyeOfs;
                            break;
                        }
                    }
                }
            }

            bool bIsFacing = pOuter->GetMotor()->IsFacing( m_vecLookAt );

            if ( zm_sv_debug_bot_lookat.GetBool() )
            {
                Vector box( 4.0f, 4.0f, 4.0f );
                NDebugOverlay::Box( m_vecLookAt, -box, box, (!bIsFacing) ? 255 : 0, bIsFacing ? 255 : 0, 0, 0, 0.1f );
            }

            if ( !bIsFacing )
            {
                pOuter->GetMotor()->FaceTowards( m_vecLookAt );
            }
        }
    }
}

void CSurvivorCombatSchedule::OnSpawn()
{
    m_Path.Invalidate();
}

void CSurvivorCombatSchedule::OnHeardSound( CSound* pSound )
{
    if ( m_bMovingOutOfRange )
        return;

    int soundType = pSound->SoundType();

    // Priority: danger/combat > gunfire > player/world
    int nPriority = 0;
    if ( soundType & SOUND_DANGER )
        nPriority = 4;
    else if ( soundType & SOUND_COMBAT )
        nPriority = 3;
    else if ( soundType & SOUND_BULLET_IMPACT )
        nPriority = 2;
    else if ( soundType & (SOUND_PLAYER | SOUND_WORLD) )
        nPriority = 1;

    if ( nPriority == 0 )
        return;

    // High-priority sounds can interrupt the cooldown
    if ( m_NextHeardLook.HasStarted() && !m_NextHeardLook.IsElapsed() )
    {
        if ( nPriority < 3 )
            return;
    }

    float flCooldown = ( nPriority >= 3 ) ? 1.0f : 2.0f;
    m_NextHeardLook.Start( flCooldown );
    m_NextLookAround.Start( flCooldown );

    auto* pOwner = pSound->m_hOwner.Get();

    if ( pOwner && pOwner->IsPlayer() && pOwner->GetTeamNumber() == ZMTEAM_HUMAN )
    {
        Vector fwd;
        AngleVectors( pOwner->EyeAngles(), &fwd );
        m_vecLookAt = pOwner->EyePosition() + fwd * 1024.0f;
    }
    else
    {
        m_vecLookAt = pSound->GetSoundOrigin();
    }
}

void CSurvivorCombatSchedule::OnForcedMove( CNavArea* pArea )
{
    m_Path.Invalidate();

    auto vecMyPos = GetOuter()->GetAbsOrigin();
    auto vecTarget = pArea->GetRandomPoint();

    auto* pStart = GetOuter()->GetLastKnownArea();

    m_PathCost.SetStartPos( vecMyPos, pStart );
    m_PathCost.SetGoalPos( vecTarget, pArea );

    m_Path.Compute( vecMyPos, vecTarget, pStart, pArea, m_PathCost );
}

NPCR::QueryResult_t CSurvivorCombatSchedule::IsBusy() const
{
    return (m_Path.IsValid() &&
            !IsSelfCall()) // Ignore if we're the one asking.
        ? NPCR::RES_YES : NPCR::RES_NONE;
}

bool CSurvivorCombatSchedule::ShouldMoveBack( CBaseEntity* pEnemy ) const
{
    float flDistSqr = pEnemy->GetAbsOrigin().DistToSqr( GetOuter()->GetPosition() );

    // ZMRTODO: Take into account velocity.
    if ( flDistSqr > (128.0f*128.0f) )
        return false;


    return true;
}

void CSurvivorCombatSchedule::MoveBackFromThreat( CBaseEntity* pEnemy )
{
    CZMPlayerBot* pOuter = GetOuter();

    const Vector vecEnemyPos = pEnemy->GetAbsOrigin();
    const Vector vecMyPos = pOuter->GetPosition();

    Vector dir = (vecMyPos - vecEnemyPos);
    dir.z = 0.0f;
    dir.NormalizeInPlace();
    if ( dir.IsLengthLessThan( 0.1f ) )
        dir = Vector( 1.0f, 0.0f, 0.0f );

    // If stuck retreating, try perpendicular directions
    if ( m_nRetreatAttempts > 0 )
    {
        float flRotate = ( m_nRetreatAttempts % 2 == 1 ) ? 90.0f : -90.0f;
        if ( m_nRetreatAttempts >= 3 )
            flRotate = ( m_nRetreatAttempts % 2 == 1 ) ? 135.0f : -135.0f;
        float rad = DEG2RAD( flRotate );
        float c = cos( rad );
        float s = sin( rad );
        Vector rotated;
        rotated.x = dir.x * c - dir.y * s;
        rotated.y = dir.x * s + dir.y * c;
        rotated.z = 0.0f;
        dir = rotated;
    }

    Vector vecTarget = vecMyPos + dir * 128.0f;

    CNavArea* pStart = pOuter->GetLastKnownArea();
    CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 200.0f, false );

    if ( pGoal )
    {
        pGoal->GetClosestPointOnArea( vecTarget, &vecTarget );

        // Reject paths toward the enemy
        Vector toTarget = (vecTarget - vecMyPos);
        toTarget.z = 0.0f;
        Vector toEnemy = (vecEnemyPos - vecMyPos);
        toEnemy.z = 0.0f;
        if ( toTarget.LengthSqr() > 1.0f && toEnemy.LengthSqr() > 1.0f )
        {
            if ( toTarget.Normalized().Dot( toEnemy.Normalized() ) > 0.5f )
            {
                m_nRetreatAttempts++;
                if ( m_nRetreatAttempts > 5 )
                    m_nRetreatAttempts = 0;
                return;
            }
        }
    }

    m_PathCost.SetStartPos( vecMyPos, pStart );
    m_PathCost.SetGoalPos( vecTarget, pGoal );

    if ( m_Path.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost ) )
    {
        m_RetreatPosition = vecMyPos;
        m_RetreatTime = gpGlobals->curtime;
        m_nRetreatAttempts++;
    }
    else
    {
        m_nRetreatAttempts++;
    }

    m_bMovingOutOfRange = m_Path.IsValid();

    if ( m_nRetreatAttempts > 5 )
        m_nRetreatAttempts = 0;

    m_vecLookAt = pEnemy->WorldSpaceCenter();
}
