#pragma once

#include "zmr_player.h"
#include "zmr_entities.h"
#include "utlvector.h"
#include "utlmap.h"

//
// AI Zombie Master - Server-side tactical controller.
//
// Phase cycle: SPAWN -> HIDDEN_SPAWN -> RESERVE -> SPAWN -> ...
//
//   SPAWN (start of round):
//     Pick a burst of 1-15 zombies using weighted type selection (60% shambler,
//     10% each special). Spawn one per tick, spending resources as they become
//     available. Before traps are unlocked, spends ALL resources freely. After
//     traps are unlocked, spends only excess above the reserve — unless a trap
//     just fired, in which case it finishes spending remaining resources freely
//     to keep pressure up. Respects per-type limits.
//
//   HIDDEN_SPAWN:
//     Attempt 1-3 surprise spawns near survivors (positions not in line of
//     sight). Retries different positions for up to 30 seconds before giving
//     up. Spends freely regardless of reserve. Then -> RESERVE.
//
//   RESERVE:
//     Save resources until we can cover the most expensive trap on the map.
//     Once met, set m_bTrapsUnlocked = true (permanent for the round) and
//     immediately -> SPAWN. If reserve is already met on entry, skip straight
//     to SPAWN.
//
// Traps: Once m_bTrapsUnlocked is true, traps fire opportunistically in ANY
//   phase whenever a survivor enters range and the AI can afford the cost.
//   Before unlocked, traps never fire.
//
// Camera: Smoothly glides between survivors, panning naturally like a real player.
// Culling: Kills stranded zombies too far from survivors when pop cap is stressed.
// View mode: When "Within View", only uses spawners/traps visible to the ZM camera.
// Rush prevention: AI only looks around during the configured window at round start.
// Rally: Continuously pushes idle zombies toward the nearest survivors.
//

enum AIZMCyclePhase_t
{
    AIZM_PHASE_SPAWN = 0,
    AIZM_PHASE_HIDDEN_SPAWN,
    AIZM_PHASE_RESERVE,
};

class CZMAIZombieMaster
{
public:
    CZMAIZombieMaster();

    void Reset();
    void Update();

    bool IsActive() const;

private:
    // Cycle phases
    void DoReservePhase( CZMPlayer* pZM );
    void DoSpawnPhase( CZMPlayer* pZM );
    void DoHiddenSpawnPhase( CZMPlayer* pZM );

    // Camera system
    void UpdateCamera( CZMPlayer* pZM );

    // Zombie culling
    void CullStrandedZombies( CZMPlayer* pZM );

    // Trap and barrel opportunism (only after traps are unlocked)
    bool TryFireTrap( CZMPlayer* pZM );
    bool TryDetonateBarrel( CZMPlayer* pZM );

    // Spawning helpers
    bool TrySpawnZombies( ZombieClass_t zclass, int count, CZMEntZombieSpawn* pSpawner );
    ZombieClass_t PickWeightedClass( CZMEntZombieSpawn* pSpawner, CZMPlayer* pZM, int holdBack = 0 ) const;
    bool CanAffordClass( ZombieClass_t zclass, CZMPlayer* pZM, int reservedResources = 0 ) const;
    bool SpawnerSupportsClass( CZMEntZombieSpawn* pSpawner, ZombieClass_t zclass ) const;
    bool IsClassAtTypeLimit( ZombieClass_t zclass ) const;

    // Multi-spawner spread: find all spawners near survivors within a threshold
    void GatherNearestSpawners( CUtlVector<CZMEntZombieSpawn*>& outSpawners ) const;

    // View mode filtering
    bool IsEntityInView( CZMPlayer* pZM, CBaseEntity* pEnt ) const;

    // Rally idle zombies toward survivors
    void RallyZombiesToSurvivors();
    CBasePlayer* PickRallyTarget( const Vector& zombiePos ) const;

    // Utility
    CZMPlayer* GetZMPlayer() const;
    void GatherHumans( CUtlVector<CBasePlayer*>& humans ) const;
    CBasePlayer* FindNearestHuman( const Vector& pos, float* outDist = nullptr ) const;
    void GatherActiveSpawners( CUtlVector<CZMEntZombieSpawn*>& spawners ) const;
    void LogAllSpawners() const;
    CZMEntManipulate* FindBestTrap( CZMPlayer* pZM ) const;
    int GetHighestTrapCost( CZMPlayer* pZM ) const;

    // Cycle state
    AIZMCyclePhase_t m_Phase;
    float m_flNextActionTime;

    // Trap reserve: cost of the most expensive trap on the map
    int   m_iReservedResources;
    bool  m_bTrapsUnlocked;    // True once first reserve target is met (permanent for round)

    // Spawn burst: spawn one zombie at a time from a chosen batch
    int m_iSpawnBurstRemaining;
    ZombieClass_t m_SpawnBurstClass;
    CHandle<CZMEntZombieSpawn> m_hSpawnBurstSpawner;

    // Hidden spawn tracking
    int   m_iHiddenSpawnsRemaining;  // How many hidden spawns left this cycle (1-3)
    float m_flHiddenSpawnDeadline;   // Give up after this time (30s timeout)

    // Per-entity cooldown (traps AND barrels share this)
    CUtlMap<int, float> m_EntityCooldowns;

    // Camera state
    Vector m_vecCameraPos;
    QAngle m_angCameraAng;
    int    m_iCameraTargetIndex;    // Player index we're currently looking at
    float  m_flCameraNextSwitch;    // When to pick a new camera target
    float  m_flCameraSwitchLerp;    // 0-1 lerp progress for smooth transitions

    // Zombie culling
    float m_flNextCullTime;

    // Timers
    float m_flRoundStartTime;
    float m_flNextRallyTime;
    float m_flLastUpdateTime;
    bool  m_bLoggedSpawners;
};

extern CZMAIZombieMaster g_ZMAIZombieMaster;
