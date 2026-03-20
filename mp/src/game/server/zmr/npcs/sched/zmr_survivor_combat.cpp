#include "cbase.h"

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



    if ( !m_NextEnemyScan.HasStarted() || m_NextEnemyScan.IsElapsed() )
    {
        m_NextEnemyScan.Start( 0.1f );


        CBaseEntity* pClosestEnemy = pOuter->GetSenses()->GetClosestEntity();

        // Taunt on kill: if our last target died, 25% chance to taunt (30s cooldown)
        CBaseEntity* pLastTarget = m_hLastCombatTarget.Get();
        if ( pLastTarget && !pLastTarget->IsAlive() )
        {
            if ( !m_NextTauntVoice.HasStarted() || m_NextTauntVoice.IsElapsed() )
            {
                m_NextTauntVoice.Start( 30.0f );
                if ( random->RandomFloat( 0.0f, 1.0f ) < 0.25f )
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

            // Melee fallback: engage with fists/melee if no ranged weapons or enemy is close
            if ( !IsIntercepted() && flDist < 512.0f )
            {
                m_pAttackCloseRangeSched->SetAttackTarget( pClosestEnemy );
                m_pAttackCloseRangeSched->SetMeleeing( true );
                Intercept( m_pAttackCloseRangeSched, "Trying to attack with melee!" );
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
    }
    else
    {
        m_Path.Invalidate();

        m_bMovingOutOfRange = false;
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

    Vector vecTarget = vecMyPos + dir * 128.0f;
    Vector vecOriginalTarget = vecTarget;

    CNavArea* pStart = pOuter->GetLastKnownArea();
    CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 200.0f, false );

    if ( pGoal )
    {
        pGoal->GetClosestPointOnArea( vecTarget, &vecTarget );

        // We don't want to go forward!
        if ( (vecTarget - vecMyPos).Normalized().Dot( dir ) < 0.0f )
        {
            vecTarget = vecOriginalTarget;
        }
    }

    m_PathCost.SetStartPos( vecMyPos, pStart );
    m_PathCost.SetGoalPos( vecTarget, pGoal );

    m_Path.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost );


    m_bMovingOutOfRange = m_Path.IsValid();

    m_vecLookAt = pEnemy->WorldSpaceCenter();
}
