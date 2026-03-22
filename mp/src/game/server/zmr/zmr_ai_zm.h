#pragma once

#include "zmr_player.h"
#include "zmr_entities.h"
#include "utlvector.h"
#include "utlmap.h"

//
// AI Zombie Master - Server-side tactical controller.
//
// Resource priority: The AI always holds back resources equal to the highest-cost
// trap on the map (the "reserve"). All spending decisions subtract the reserve first.
// Traps and barrels fire opportunistically every tick when a survivor is in range
// and the AI has the resources. After spending on a trap, the AI pauses spawning
// until its resources climb back above the reserve threshold, then resumes.
//
// Spawn cycle: SPAWN -> HIDDEN_SPAWN -> SPAWN -> HIDDEN_SPAWN -> ...
//   SPAWN: Find nearest spawners to survivors (spreads across multiple if similar
//     distance). Picks a batch size (1-10) but spawns one zombie at a time as
//     resources become available (no waiting for the full batch cost). Uses
//     weighted type selection (60% shambler, 10% each special). Respects limits.
//   HIDDEN_SPAWN: Place one surprise zombie behind survivors. Respects type limits.
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
    void DoSpawnPhase( CZMPlayer* pZM );
    void DoHiddenSpawnPhase( CZMPlayer* pZM );

    // Camera system
    void UpdateCamera( CZMPlayer* pZM );

    // Zombie culling
    void CullStrandedZombies( CZMPlayer* pZM );

    // Trap and barrel opportunism (checked every update, uses reserves)
    bool TryFireTrap( CZMPlayer* pZM );
    bool TryDetonateBarrel( CZMPlayer* pZM );

    // Spawning helpers
    bool TrySpawnZombies( ZombieClass_t zclass, int count, CZMEntZombieSpawn* pSpawner );
    ZombieClass_t PickWeightedClass( CZMEntZombieSpawn* pSpawner, CZMPlayer* pZM ) const;
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

    // Trap reserve tracking (always prioritized over spawning)
    int   m_iReservedResources;

    // Spawn burst: spawn one zombie at a time from a chosen batch
    int m_iSpawnBurstRemaining;
    ZombieClass_t m_SpawnBurstClass;
    CHandle<CZMEntZombieSpawn> m_hSpawnBurstSpawner;

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
