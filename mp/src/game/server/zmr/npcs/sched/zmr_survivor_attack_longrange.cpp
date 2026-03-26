#include "cbase.h"

#include "zmr_survivor_attack_longrange.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



CSurvivorAttackLongRangeSchedule::CSurvivorAttackLongRangeSchedule()
{
    m_pAttackCloseRangeSched = new CSurvivorAttackCloseRangeSchedule;
}

CSurvivorAttackLongRangeSchedule::~CSurvivorAttackLongRangeSchedule()
{
    delete m_pAttackCloseRangeSched;
}

void CSurvivorAttackLongRangeSchedule::OnStart()
{
    CZMPlayerBot* pOuter = GetOuter();

    if ( !pOuter->EquipWeaponOfType( BOTWEPRANGE_MAINGUN ) && !pOuter->EquipWeaponOfType( BOTWEPRANGE_SECONDARYWEAPON ) )
    {
        End( "Couldn't equip any ranged weapons!" );
        return;
    }
}

void CSurvivorAttackLongRangeSchedule::OnContinue()
{
    End( "Done." );
}

void CSurvivorAttackLongRangeSchedule::OnUpdate()
{
    CZMPlayerBot* pOuter = GetOuter();

    CBaseEntity* pEnemy = GetAttackTarget();
    if ( !pEnemy || !pEnemy->IsAlive() )
    {
        End( "No enemy left!" );
        return;
    }


    if ( !pOuter->WeaponHasAmmo( pOuter->GetActiveWeapon() ) )
    {
        // Try to switch to melee or fists before giving up
        if ( !pOuter->EquipBestWeapon() )
        {
            End( "No ammo left!" );
            return;
        }
        // Switched to melee/fists - fall through to close-range intercept below
    }


    float flDist = pEnemy->GetAbsOrigin().DistTo( pOuter->GetPosition() );

    if ( flDist < 128.0f )
    {
        m_pAttackCloseRangeSched->SetAttackTarget( pEnemy );
        Intercept( m_pAttackCloseRangeSched, "Enemy too close!" );
        return;
    }

    // Move toward optimal range if we're too far and not already pathing
    float flOptimal = pOuter->GetOptimalAttackDistance();
    if ( flDist > flOptimal * 1.2f && !m_Path.IsValid() )
    {
        const Vector vecEnemyPos = pEnemy->GetAbsOrigin();
        const Vector vecMyPos = pOuter->GetPosition();
        Vector dir = (vecMyPos - vecEnemyPos).Normalized();
        Vector vecTarget = vecEnemyPos + dir * flOptimal;

        CNavArea* pStart = pOuter->GetLastKnownArea();
        CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 128.0f, false );
        if ( pGoal )
            pGoal->GetClosestPointOnArea( vecTarget, &vecTarget );

        m_PathCost.SetStartPos( vecMyPos, pStart );
        m_PathCost.SetGoalPos( vecTarget, pGoal );
        m_Path.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost );
    }

    if ( m_Path.IsValid() )
        m_Path.Update( pOuter );

    Vector vecAimTarget = pEnemy->WorldSpaceCenter();

    pOuter->GetMotor()->FaceTowards( vecAimTarget );

    if ( pOuter->GetMotor()->IsFacing( vecAimTarget, 5.0f ) )
    {
        if ( pOuter->GetSenses()->HasLOS( vecAimTarget ) )
        {
            if ( !pOuter->MustStopToShoot() || pOuter->GetLocalVelocity().IsLengthLessThan( 10.0f ) )
            {
                pOuter->PressFire1( 0.1f );
            }
        }
    }
}

NPCR::QueryResult_t CSurvivorAttackLongRangeSchedule::IsBusy() const
{
    return m_Path.IsValid() ? NPCR::RES_YES : NPCR::RES_NONE;
}

NPCR::QueryResult_t CSurvivorAttackLongRangeSchedule::ShouldChase( CBaseEntity* pEnemy ) const
{
    return IsDone() ? NPCR::RES_NONE : NPCR::RES_NO;
}
