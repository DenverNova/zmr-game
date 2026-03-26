#pragma once

#include "zmr_player.h"
#include "zmr_entities.h"
#include "utlvector.h"
#include "utlmap.h"

//
// AI Zombie Master - Server-side tactical controller.
//
// Phase cycle:
//   Round start -> SPAWN -> HIDDEN_SPAWN -> RESERVE (if trap fired) -> SPAWN -> ...
//
//   SPAWN:
//     Pick a burst of 1-15 zombies. For each zombie, roll a weighted class
//     (60% shambler, 10% each special) based on the nearest spawner's allowed
//     types — blocked types have their weight redistributed equally among
//     valid types. Save up until resources >= reserve + zombieCost, then
//     spawn it and roll the next one. If the focused survivor moves closer
//     to a different spawner, re-evaluate and re-roll. Traps fire
//     opportunistically during this phase. If a trap fires, the AI does NOT
//     pause spawning — it continues with whatever resources remain (just
//     needs raw cost). After all zombies are spawned -> HIDDEN_SPAWN.
//
//   HIDDEN_SPAWN:
//     Attempt 1-3 surprise spawns near survivors. Same save-up logic as
//     SPAWN (reserve + cost). Each spawn is placed immediately when
//     affordable. Retries positions for up to 15 seconds. Traps fire
//     opportunistically here too. After done -> RESERVE (if a trap was
//     fired during SPAWN or HIDDEN_SPAWN) or -> SPAWN (if reserve intact).
//
//   RESERVE:
//     Only entered when a trap was fired during SPAWN or HIDDEN_SPAWN and
//     the reserve needs replenishing. Save resources without spawning any
//     zombies until resources >= highest trap cost. Then -> SPAWN.
//     If no traps exist on the map, this phase is skipped.
//
// Traps: Once the first reserve target is met, traps fire in SPAWN and
//   HIDDEN_SPAWN phases whenever a survivor enters range and the AI can
//   afford it. Traps do NOT fire during RESERVE (saving up).
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
    void GatherNearestSpawnersToTarget( CBasePlayer* pTarget, CUtlVector<CZMEntZombieSpawn*>& outSpawners ) const;

    // Focused target: the survivor the AI is currently "picking on"
    CBasePlayer* GetFocusedTarget() const;

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
    bool  m_bTrapsUnlocked;         // True once first reserve target is met
    bool  m_bTrapFiredDuringCycle;  // True if a trap fired during SPAWN or HIDDEN_SPAWN

    // Spawn burst: internal queue of N zombies to spawn one at a time.
    // Each zombie is rolled independently. The AI saves up resources for
    // the current class, spawns it, then rolls the next one.
    int m_iSpawnBurstRemaining;            // How many zombies left in this burst
    ZombieClass_t m_SpawnBurstClass;       // The class we're currently saving up for
    CHandle<CZMEntZombieSpawn> m_hSpawnBurstSpawner;  // Current spawner (may change dynamically)

    // Hidden spawn tracking
    int   m_iHiddenSpawnsRemaining;  // How many hidden spawns left this cycle (1-3)
    float m_flHiddenSpawnDeadline;   // Give up after this time (15s timeout)

    // Per-entity cooldown (traps AND barrels share this)
    CUtlMap<int, float> m_EntityCooldowns;

    // Camera state
    Vector m_vecCameraPos;
    Vector m_vecCameraDesired;  // Smoothed desired position (intermediate target)
    QAngle m_angCameraAng;

    // Focused target: round-robin through survivors, 5-20s each
    int    m_iFocusedTargetIndex;   // Entity index of the currently focused survivor
    float  m_flFocusedTargetExpiry; // When to switch to the next survivor
    int    m_iLastRoundRobinSlot;   // Tracks which survivor slot we visited last (for round-robin)

    // Zombie culling
    float m_flNextCullTime;

    // Timers
    float m_flRoundStartTime;
    float m_flNextRallyTime;
    float m_flLastUpdateTime;
    bool  m_bLoggedSpawners;
};

extern CZMAIZombieMaster g_ZMAIZombieMaster;
