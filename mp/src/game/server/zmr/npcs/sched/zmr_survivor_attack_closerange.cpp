#include "cbase.h"

#include "npcr_motor.h"
#include "npcr_motor_player.h"
#include "zmr_survivor_attack_closerange.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


CSurvivorAttackCloseRangeSchedule::CSurvivorAttackCloseRangeSchedule()
{
    m_bAllowMelee = false;
    m_RetreatPosition = vec3_origin;
    m_RetreatTime = 0.0f;
    m_nRetreatAttempts = 0;
}

CSurvivorAttackCloseRangeSchedule::~CSurvivorAttackCloseRangeSchedule()
{
}

void CSurvivorAttackCloseRangeSchedule::OnStart()
{
    m_bMovingToRange = false;
    m_bMovingOutOfRange = false;
    m_Path.Invalidate();
    m_NextRangeCheck.Invalidate();
    m_NextMovingToRange.Invalidate();
    m_RetreatPosition = vec3_origin;
    m_RetreatTime = 0.0f;
    m_nRetreatAttempts = 0;


    CZMPlayerBot* pOuter = GetOuter();

    if ( !IsMeleeing() )
    {
        if ( pOuter->EquipWeaponOfType( BOTWEPRANGE_MAINGUN ) && pOuter->WeaponHasAmmo( pOuter->GetActiveWeapon() ) )
        {
            return;
        }

        if ( pOuter->EquipWeaponOfType( BOTWEPRANGE_SECONDARYWEAPON ) && pOuter->WeaponHasAmmo( pOuter->GetActiveWeapon() ) )
        {
            return;
        }

        // No ranged ammo - fall back to melee weapons and mark as meleeing
        if ( pOuter->EquipWeaponOfType( BOTWEPRANGE_MELEE ) )
        {
            m_bAllowMelee = true;
            return;
        }

        // Last resort: fists
        if ( pOuter->EquipWeaponOfType( BOTWEPRANGE_FISTS ) )
        {
            m_bAllowMelee = true;
            return;
        }
    }
    else
    {
        if ( pOuter->EquipWeaponOfType( BOTWEPRANGE_MELEE ) )
        {
            return;
        }

        // Fall back to fists
        if ( pOuter->EquipWeaponOfType( BOTWEPRANGE_FISTS ) )
        {
            return;
        }
    }

        
    End( "Couldn't find any weapons with ammo!" );
}

void CSurvivorAttackCloseRangeSchedule::OnContinue()
{
    End( "Done." );
}

void CSurvivorAttackCloseRangeSchedule::OnUpdate()
{
    CZMPlayerBot* pOuter = GetOuter();

    CBaseEntity* pEnemy = GetAttackTarget();
    if ( !pEnemy || !pEnemy->IsAlive() )
    {
        static_cast<NPCR::CPlayerMotor*>( pOuter->GetMotor() )->SetSuppressYawSnap( false );
        End( "No enemy left!" );
        return;
    }


    if ( !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_MELEE ) && !pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_FISTS ) && !pOuter->WeaponHasAmmo( pOuter->GetActiveWeapon() ) )
    {
        // Try to switch to melee or fists before giving up
        if ( !pOuter->EquipBestWeapon() )
        {
            End( "No ammo left!" );
            return;
        }
        // If we switched to melee/fists, allow melee attacks
        if ( pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_MELEE ) || pOuter->HasEquippedWeaponOfType( BOTWEPRANGE_FISTS ) )
            m_bAllowMelee = true;
    }

    // Auto-reload: if clip is empty but we have reserve ammo, reload immediately
    if ( pOuter->ShouldReload() )
    {
        pOuter->PressReload( 0.15f );
    }


    // Don't restrict combat by follow target distance - let bots fight freely
    // Follow behavior will resume once combat schedule ends


    if ( !m_NextRangeCheck.HasStarted() || m_NextRangeCheck.IsElapsed() )
    {
        m_NextRangeCheck.Start( 0.15f );

        // Always try to back away from threats first (kiting)
        if ( !m_bMovingOutOfRange && ShouldMoveBack( pEnemy ) )
        {
            MoveBackFromThreat( pEnemy );
        }
        else if ( !m_bMovingToRange && !IsInRangeToAttack( pEnemy ) && ShouldMoveInRange() )
        {
            MoveToShootingRange( pEnemy );
        }
    }


    // Suppress yaw snap during ALL combat movement (melee and ranged) so bots
    // face the enemy while backing up instead of turning their back to run.
    NPCR::CPlayerMotor* pMotor = static_cast<NPCR::CPlayerMotor*>( pOuter->GetMotor() );
    bool bShouldSuppressYaw = m_Path.IsValid();
    pMotor->SetSuppressYawSnap( bShouldSuppressYaw );

    if ( m_Path.IsValid() )
    {
        m_Path.Update( pOuter );
    }
    else
    {
        m_bMovingOutOfRange = false;
        m_bMovingToRange = false;
    }


    Vector vecAimTarget = pEnemy->WorldSpaceCenter();

    pOuter->GetMotor()->FaceTowards( vecAimTarget );


    float grace = IsMeleeing() ? 60.0f : 24.0f; // Widen facing grace angle for ranged weapons

    float flEnemyDist = pEnemy->GetAbsOrigin().DistTo( pOuter->GetPosition() );

    if ( IsMeleeing() && pOuter->GetMotor()->IsFacing( vecAimTarget, grace ) )
    {
        // Melee wind-up prediction: start the swing early so the hit lands when in range.
        // Heavier weapons (sledge ~1.0s fire rate) need to start much earlier than
        // light weapons (improvised ~0.5s). Use fire rate as a proxy for wind-up time.
        CZMBaseWeapon* pWep = pOuter->GetActiveWeapon();
        float flFireRate = pWep ? pWep->GetFireRate() : 0.5f;

        // Estimated approach speed (~250 units/sec for walking bots)
        float flApproachSpeed = 250.0f;
        float flWindUpDist = flFireRate * flApproachSpeed * 0.6f;
        float flMeleeRange = pOuter->GetMaxAttackDistance();
        float flSwingStartDist = flMeleeRange + flWindUpDist;

        if ( flEnemyDist < flSwingStartDist && flEnemyDist > flMeleeRange * 0.4f )
        {
            pOuter->PressFire1( 0.15f );
        }
    }

    if ( pOuter->GetMotor()->IsFacing( vecAimTarget, grace ) )
    {
        // Point-blank override: if the enemy is extremely close, just shoot - don't rely on
        // range checks or LOS traces that can fail at very short distances
        if ( flEnemyDist < 100.0f )
        {
            pOuter->PressFire1( 0.15f );
        }
        else if ( IsInRangeToAttack( pEnemy ) )
        {
            // Use direct LOS trace instead of CanSee (which has FOV restrictions)
            if ( pOuter->GetSenses()->HasLOS( vecAimTarget ) )
            {
                // Run-and-gun: fire while moving for most weapons
                // Only stop for precision weapons (rifle, revolver)
                if ( !pOuter->MustStopToShoot() || pOuter->GetLocalVelocity().IsLengthLessThan( 10.0f ) )
                {
                    pOuter->PressFire1( 0.15f );
                }
            }
        }
    }

    // Wall-running stuck detection
    if ( m_bMovingOutOfRange && m_RetreatPosition != vec3_origin )
    {
        float flDistMoved = (pOuter->GetPosition() - m_RetreatPosition).Length();
        if ( flDistMoved < 10.0f && gpGlobals->curtime - m_RetreatTime > 1.0f )
        {
            // Invalidate path and try a perpendicular escape direction
            m_Path.Invalidate();
            MoveBackFromThreat( pEnemy );
        }
    }
}

NPCR::QueryResult_t CSurvivorAttackCloseRangeSchedule::IsBusy() const
{
    return m_Path.IsValid() ? NPCR::RES_YES : NPCR::RES_NONE;
}

NPCR::QueryResult_t CSurvivorAttackCloseRangeSchedule::ShouldChase( CBaseEntity* pEnemy ) const
{
    return IsDone() ? NPCR::RES_NONE : NPCR::RES_NO;
}

bool CSurvivorAttackCloseRangeSchedule::ShouldMoveInRange() const
{
    if ( m_NextMovingToRange.HasStarted() && !m_NextMovingToRange.IsElapsed() )
        return false;

    auto* pOuter = GetOuter();

    return pOuter->CanAttack();
}

bool CSurvivorAttackCloseRangeSchedule::IsInRangeToAttack( CBaseEntity* pEnemy ) const
{
    float flDist = GetOuter()->GetMaxAttackDistance();

    return pEnemy->GetAbsOrigin().DistToSqr( GetOuter()->GetPosition() ) < (flDist*flDist);
}

bool CSurvivorAttackCloseRangeSchedule::ShouldMoveBack( CBaseEntity* pEnemy ) const
{
    float flDistSqr = pEnemy->GetAbsOrigin().DistToSqr( GetOuter()->GetPosition() );

    // Melee: retreat after getting a hit in (hit-and-run) - back off when within 80 units
    // Ranged: kite backwards when zombie gets within 300 units to maintain safe distance
    float flMoveBackRange = IsMeleeing() ? 80.0f : 300.0f;

    if ( flDistSqr > (flMoveBackRange*flMoveBackRange) )
        return false;


    return true;
}

void CSurvivorAttackCloseRangeSchedule::MoveToShootingRange( CBaseEntity* pEnemy )
{
    CZMPlayerBot* pOuter = GetOuter();

    const Vector vecEnemyPos = pEnemy->GetAbsOrigin();
    const Vector vecMyPos = pOuter->GetPosition();

    Vector dir = (vecMyPos - vecEnemyPos).Normalized();

    float flDist = pOuter->GetOptimalAttackDistance();

    Vector vecTarget = vecEnemyPos + dir * flDist;

    CNavArea* pStart = pOuter->GetLastKnownArea();
    CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 128.0f, false );

    if ( pGoal )
    {
        pGoal->GetClosestPointOnArea( vecTarget, &vecTarget );
    }

    m_PathCost.SetStartPos( vecMyPos, pStart );
    m_PathCost.SetGoalPos( vecTarget, pGoal );

    m_Path.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost );


    m_bMovingOutOfRange = false;
    m_bMovingToRange = m_Path.IsValid();
}

void CSurvivorAttackCloseRangeSchedule::MoveBackFromThreat( CBaseEntity* pEnemy )
{
    CZMPlayerBot* pOuter = GetOuter();

    const Vector vecEnemyPos = pEnemy->GetAbsOrigin();
    const Vector vecMyPos = pOuter->GetPosition();

    Vector dir = (vecMyPos - vecEnemyPos);
    dir.z = 0.0f;
    dir.NormalizeInPlace();
    if ( dir.IsLengthLessThan( 0.1f ) )
        dir = Vector( 1.0f, 0.0f, 0.0f );

    // If we've been stuck retreating, try perpendicular directions
    if ( m_nRetreatAttempts > 0 )
    {
        // Alternate between left and right perpendicular
        float flRotate = ( m_nRetreatAttempts % 2 == 1 ) ? 90.0f : -90.0f;
        // On 3rd+ attempt, go further off-axis
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

    // Melee: retreat further for hit-and-run; ranged: kite back a good distance
    float flRetreatDist = IsMeleeing() ? 192.0f : 256.0f;
    Vector vecTarget = vecMyPos + dir * flRetreatDist;

    CNavArea* pStart = pOuter->GetLastKnownArea();
    CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 256.0f, false );

    if ( pGoal )
    {
        pGoal->GetClosestPointOnArea( vecTarget, &vecTarget );

        // Reject paths that would move us toward the enemy
        Vector toTarget = vecTarget - vecMyPos;
        toTarget.z = 0.0f;
        Vector toEnemy = vecEnemyPos - vecMyPos;
        toEnemy.z = 0.0f;
        if ( toTarget.LengthSqr() > 1.0f && toEnemy.LengthSqr() > 1.0f )
        {
            if ( toTarget.Normalized().Dot( toEnemy.Normalized() ) > 0.5f )
            {
                m_nRetreatAttempts++;
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
    m_bMovingToRange = false;

    // Melee: wait longer before re-engaging (hit-and-run rhythm)
    // Ranged: quickly re-evaluate for continuous kiting
    m_NextMovingToRange.Start( IsMeleeing() ? 2.0f : 0.3f );

    // Reset retreat attempts if we've tried too many times
    if ( m_nRetreatAttempts > 5 )
        m_nRetreatAttempts = 0;
}