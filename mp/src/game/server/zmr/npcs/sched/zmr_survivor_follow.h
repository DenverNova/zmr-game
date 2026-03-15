#pragma once

#include "npcs/zmr_playerbot.h"

#include "zmr_bot_main.h"

#include "npcr_schedule.h"
#include "npcr_path_chase.h"
#include "npcr_path_follow.h"
#include "npcr_path_cost.h"
#include "npcr_motor.h"


//
// Follow real players.
//
class CSurvivorFollowSchedule : public CBasePlayerBotSchedule
{
public:
    CSurvivorFollowSchedule();
    ~CSurvivorFollowSchedule();

    virtual const char* GetName() const OVERRIDE { return "SurvivorFollow"; }

    virtual void OnStart() OVERRIDE;

    virtual void OnContinue() OVERRIDE;

    virtual void OnUpdate() OVERRIDE;

    virtual void OnSpawn() OVERRIDE;

    virtual void OnHeardSound( CSound* pSound ) OVERRIDE;

    //virtual NPCR::QueryResult_t IsBusy() const OVERRIDE;

    virtual NPCR::QueryResult_t ShouldChase( CBaseEntity* pEnemy ) const OVERRIDE;

    //virtual void OnMoveSuccess( NPCR::CBaseNavPath* pPath ) OVERRIDE;


private:
    bool IsValidFollowTarget( CBasePlayer* pPlayer, bool bCheckLoop = false ) const;

    void NextFollow();

    void StartFollow( CBasePlayer* pFollow );

    bool ShouldMoveCloser( CBasePlayer* pFollow ) const;

    CBasePlayer* FindSurvivorToFollow( CBasePlayer* pIgnore = nullptr, bool bAllowBot = false ) const;

    void UpdateExploreMode();
    void UpdateDefendMode();
    void UpdateMixedMode();
    void TryPickupNearbyWeapons();

    NPCR::CChaseNavPath m_Path;
    NPCR::CPathCostGroundOnly m_PathCost;
    NPCR::CFollowNavPath m_ObjPath;
    NPCR::CFollowNavPath m_ExplorePath;

    CountdownTimer m_NextFollowTarget;
    CountdownTimer m_NextObjectiveScan;
    CountdownTimer m_NextExplorePath;
    CountdownTimer m_NextWeaponScan;
    CountdownTimer m_NextHeardLook;         // Cooldown after reacting to a heard sound
    CountdownTimer m_NextPeripheralScan;    // Periodic peripheral threat check
    Vector m_vecHeardLookAt;               // Direction to look toward after hearing something

    CHandle<CBasePlayer> m_hFollowTarget;
    Vector m_vecFormationOffset;
    Vector m_vecDefendPos;
    bool m_bHasDefendPos;
    int m_iMixedBehavior;    // Randomly assigned behavior for Mixed Mode (0=follow, 1=explore, 2=defend)
};
