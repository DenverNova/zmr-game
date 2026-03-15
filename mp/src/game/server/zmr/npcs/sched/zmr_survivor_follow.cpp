#include "cbase.h"

#include "ammodef.h"
#include "soundent.h"

#include "zmr_survivor_follow.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar zm_sv_bot_default_behavior;


CSurvivorFollowSchedule::CSurvivorFollowSchedule()
{
    m_vecFormationOffset = vec3_origin;
    m_vecDefendPos = vec3_origin;
    m_bHasDefendPos = false;
    m_vecHeardLookAt = vec3_origin;
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
        m_NextPeripheralScan.Start( random->RandomFloat( 1.5f, 3.0f ) );

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

    // No explicit follow target - use the default behavior mode
    switch ( behavior )
    {
    case 1: // Explore
        UpdateExploreMode();
        return;
    case 2: // Defend Spawn
        UpdateDefendMode();
        return;
    case 3: // Objectives
        UpdateObjectiveMode();
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
}

void CSurvivorFollowSchedule::OnHeardSound( CSound* pSound )
{
    if ( m_NextHeardLook.HasStarted() && !m_NextHeardLook.IsElapsed() )
        return;

    int soundType = pSound->SoundType();
    bool bInteresting = false;

    if ( soundType & ( SOUND_COMBAT | SOUND_BULLET_IMPACT ) )
        bInteresting = true;
    if ( soundType & SOUND_DANGER )
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

    m_NextHeardLook.Start( random->RandomFloat( 2.5f, 4.5f ) );
}

//NPCR::QueryResult_t CSurvivorFollowSchedule::IsBusy() const
//{
//    return m_Path.IsValid() ? NPCR::RES_YES : NPCR::RES_NONE;
//}

NPCR::QueryResult_t CSurvivorFollowSchedule::ShouldChase( CBaseEntity* pEnemy ) const
{
    auto* pFollow = m_hFollowTarget.Get();
    return pFollow && m_Path.IsValid() && ShouldMoveCloser( pFollow ) ? NPCR::RES_NO : NPCR::RES_NONE;
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

void CSurvivorFollowSchedule::UpdateDefendMode()
{
    CZMPlayerBot* pOuter = GetOuter();

    if ( !m_bHasDefendPos )
    {
        m_vecDefendPos = pOuter->GetAbsOrigin();
        m_bHasDefendPos = true;
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

void CSurvivorFollowSchedule::UpdateObjectiveMode()
{
    CZMPlayerBot* pOuter = GetOuter();

    // Periodically scan for usable entities
    if ( !m_NextObjectiveScan.HasStarted() || m_NextObjectiveScan.IsElapsed() )
    {
        m_NextObjectiveScan.Start( 3.0f );

        CBaseEntity* pTarget = FindNearestUsableEntity();
        if ( pTarget && pTarget != m_hObjectiveTarget.Get() )
        {
            m_hObjectiveTarget.Set( pTarget );
            m_ObjPath.Invalidate();

            Vector vecMyPos = pOuter->GetAbsOrigin();
            Vector vecTarget = pTarget->GetAbsOrigin();
            CNavArea* pStart = pOuter->GetLastKnownArea();
            CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 512.0f, false );

            if ( pStart && pGoal )
            {
                m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
                m_PathCost.SetStartPos( vecMyPos, pStart );
                m_ObjPath.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost );
            }
        }
    }

    auto* pTarget = m_hObjectiveTarget.Get();
    if ( !pTarget )
    {
        // No objective found - explore instead
        UpdateExploreMode();
        return;
    }

    // If close enough to the target, try to USE it
    float flDist = pOuter->GetAbsOrigin().DistTo( pTarget->GetAbsOrigin() );
    if ( flDist < 96.0f )
    {
        pOuter->GetMotor()->FaceTowards( pTarget->GetAbsOrigin() );
        pOuter->PressUse( 0.15f );

        m_hObjectiveTarget.Set( nullptr );
        m_ObjPath.Invalidate();
        m_NextObjectiveScan.Start( 5.0f );
        return;
    }

    // Navigate toward the objective
    bool bBusy = pOuter->IsBusy() == NPCR::RES_YES;
    if ( m_ObjPath.IsValid() && !bBusy )
    {
        pOuter->GetMotor()->FaceTowards( pTarget->GetAbsOrigin() );
        m_ObjPath.Update( pOuter );
    }
}

CBaseEntity* CSurvivorFollowSchedule::FindNearestUsableEntity() const
{
    CZMPlayerBot* pOuter = GetOuter();
    Vector myPos = pOuter->GetAbsOrigin();

    CBaseEntity* pBest = nullptr;
    float flBestDist = FLT_MAX;
    float flMaxRange = 2048.0f;

    // Scan for func_button entities
    CBaseEntity* pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "func_button" )) != nullptr )
    {
        if ( !pEnt->HasSpawnFlags( 256 ) ) // SF_BUTTON_TOUCH_ACTIVATES - skip touch-only buttons
        {
            float dist = myPos.DistTo( pEnt->GetAbsOrigin() );
            if ( dist < flMaxRange && dist < flBestDist )
            {
                flBestDist = dist;
                pBest = pEnt;
            }
        }
    }

    // Scan for func_rot_button
    pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "func_rot_button" )) != nullptr )
    {
        float dist = myPos.DistTo( pEnt->GetAbsOrigin() );
        if ( dist < flMaxRange && dist < flBestDist )
        {
            flBestDist = dist;
            pBest = pEnt;
        }
    }

    // Scan for momentary_rot_button (levers)
    pEnt = nullptr;
    while ( (pEnt = gEntList.FindEntityByClassname( pEnt, "momentary_rot_button" )) != nullptr )
    {
        float dist = myPos.DistTo( pEnt->GetAbsOrigin() );
        if ( dist < flMaxRange && dist < flBestDist )
        {
            flBestDist = dist;
            pBest = pEnt;
        }
    }

    return pBest;
}

void CSurvivorFollowSchedule::TryPickupNearbyWeapons()
{
    if ( !m_NextWeaponScan.IsElapsed() )
        return;

    m_NextWeaponScan.Start( 3.0f );

    CZMPlayerBot* pOuter = GetOuter();
    if ( !pOuter->IsAlive() )
        return;

    // Determine what loadout slots we still need
    bool bNeedMainGun = !pOuter->HasWeaponOfType( BOTWEPRANGE_MAINGUN );
    bool bNeedSidearm = !pOuter->HasWeaponOfType( BOTWEPRANGE_SECONDARYWEAPON );
    bool bNeedMelee   = !pOuter->HasWeaponOfType( BOTWEPRANGE_MELEE );
    bool bNeedGrenade = !pOuter->HasWeaponOfType( BOTWEPRANGE_THROWABLE );

    // Check if we need ammo for any ranged weapon we carry
    // Only scan for ammo if we have the weapon but are low on reserve ammo
    bool bNeedAmmo = false;
    int iNeededAmmoType = -1;  // ammo type index of the weapon with lowest ammo
    float flLowestAmmoRatio = 0.75f;  // only pick up ammo if below 75% reserve
    for ( int i = 0; i < MAX_WEAPONS; i++ )
    {
        CZMBaseWeapon* pWep = ToZMBaseWeapon( pOuter->GetWeapon( i ) );
        if ( !pWep || !pWep->UsesPrimaryAmmo() )
            continue;

        ZMBotWeaponTypeRange_t wtype = CZMPlayerBot::GetWeaponType( pWep );
        if ( wtype != BOTWEPRANGE_MAINGUN && wtype != BOTWEPRANGE_SECONDARYWEAPON )
            continue;

        int iAmmoType = pWep->GetPrimaryAmmoType();
        if ( iAmmoType < 0 )
            continue;

        int iCurrent = pOuter->GetAmmoCount( iAmmoType );
        int iMax = GetAmmoDef()->MaxCarry( iAmmoType );
        if ( iMax <= 0 )
            continue;

        float flRatio = (float)iCurrent / (float)iMax;
        if ( flRatio < flLowestAmmoRatio )
        {
            flLowestAmmoRatio = flRatio;
            iNeededAmmoType = iAmmoType;
            bNeedAmmo = true;
        }
    }

    // Fully loaded - nothing to pick up at all
    if ( !bNeedMainGun && !bNeedSidearm && !bNeedMelee && !bNeedGrenade && !bNeedAmmo )
        return;

    Vector myPos = pOuter->GetAbsOrigin();
    Vector eyePos = pOuter->EyePosition();
    float flBestDist = 512.0f;
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

            // Determine if we actually need this weapon type
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

            // Prefer higher priority weapons, or closer ones at same priority
            if ( nPriority < nBestPriority )
                continue;
            if ( nPriority == nBestPriority && dist >= flBestDist )
                continue;
            if ( dist > 512.0f )
                continue;

            // Check line of sight - don't try to pick up weapons behind solid walls
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
            // Only scan for ammo types that match our needed type
            int iBoxAmmoType = GetAmmoDef()->Index( s_szAmmoClassnames[a].ammoname );
            if ( iBoxAmmoType != iNeededAmmoType )
                continue;

            CBaseEntity* pAmmoEnt = nullptr;
            while ( (pAmmoEnt = gEntList.FindEntityByClassname( pAmmoEnt, s_szAmmoClassnames[a].classname )) != nullptr )
            {
                float dist = myPos.DistTo( pAmmoEnt->GetAbsOrigin() );
                if ( dist > 512.0f )
                    continue;

                int nPriority = 1;  // ammo is lower priority than missing weapons
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
        Vector vecMyPos = pOuter->GetAbsOrigin();
        Vector vecTarget = pBestWeapon->GetAbsOrigin();
        CNavArea* pStart = pOuter->GetLastKnownArea();
        CNavArea* pGoal = TheNavMesh->GetNearestNavArea( vecTarget, true, 256.0f, false );

        if ( pStart && pGoal )
        {
            m_PathCost.SetStepHeight( pOuter->GetMotor()->GetStepHeight() );
            m_PathCost.SetStartPos( vecMyPos, pStart );

            if ( !m_ExplorePath.Compute( vecMyPos, vecTarget, pStart, pGoal, m_PathCost ) )
            {
                m_NextWeaponScan.Start( 10.0f );
            }
        }
    }
}
