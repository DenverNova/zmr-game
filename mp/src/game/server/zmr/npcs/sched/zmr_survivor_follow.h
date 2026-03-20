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
    void TryPickupNearbyWeapons();
    void UpdateExploreLookAngles();
    CBaseEntity* FindNearestZombie( float flMaxRange ) const;

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

    // Weapon/ammo scavenging state
    bool m_bScavenging;                // Currently walking to pick up a weapon/ammo
    Vector m_vecPreScavengePos;        // Position to return to after scavenging
    EHANDLE m_hScavengeTarget;         // Entity we're walking toward
    int m_nScavengeStuckCount;         // Consecutive frames not making progress toward scavenge target
    float m_flLastScavengeDist;        // Distance to scavenge target on previous check
    CUtlVector<EHANDLE> m_BlacklistedItems; // Items we failed to reach - skip them for a while
    CountdownTimer m_BlacklistClearTimer;   // Periodically clear the blacklist

    // Explore mode idle pauses and look scanning
    CountdownTimer m_ExploreIdlePause;     // Timer for standing still and looking around
    CountdownTimer m_ExploreLookScan;      // Timer for periodic look direction changes while moving
    float m_flExploreScanPitch;            // Target pitch for natural scanning while moving
    float m_flExploreScanYawOffset;        // Yaw offset for scanning while moving
    bool m_bExploreIdling;                 // Currently in an idle pause

    // Defend mode look-around
    CountdownTimer m_DefendLookTimer;      // Timer for periodic look direction changes in defend mode
    float m_flDefendLookYaw;               // Target yaw for defend mode scanning

    // Position deconfliction
    CountdownTimer m_NextDeconflict;       // Periodic check to avoid stacking on other bots

    // Debug log throttle
    CountdownTimer m_NextDebugLog;         // Throttle debug output to every few seconds

    // Explosion dodge
    CountdownTimer m_NextExplosionCheck;   // Periodic scan for nearby ZM explosions
    NPCR::CFollowNavPath m_FleePath;      // Path to flee from explosion
    bool m_bFleeingExplosion;              // Currently running from an explosion

    // Explore stuck detection
    Vector m_vecLastExplorePos;            // Last recorded position during explore
    int m_nExploreStuckCount;              // Consecutive checks where bot hasn't moved much
    CountdownTimer m_NextExploreStuckCheck; // Timer for stuck checks during explore

    // Voice line cooldowns
    CountdownTimer m_NextAlertVoice;       // Cooldown for zombie sight alert (60s)
    CountdownTimer m_NextTauntVoice;       // Cooldown for kill taunt (30s)
    CountdownTimer m_NextHelpVoice;        // Cooldown for low-health help call

    // Ammo crate smashing
    CountdownTimer m_NextCrateCheck;       // Timer for scanning nearby ammo crates
    EHANDLE m_hTargetCrate;                // Crate we're walking to smash
};
