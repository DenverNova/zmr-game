#pragma once

#include "zmr_player.h"
#include "zmr_entities.h"
#include "utlvector.h"
#include "utlmap.h"

//
// AI Zombie Master - Server-side tactical controller.
//
// Resource cycle: RESERVE -> SPAWN -> HIDDEN_SPAWN -> RESERVE -> ...
//   RESERVE: Accumulate resources until we can afford the most expensive trap
//     on the map. No zombie spawning during this phase. Traps still fire
//     opportunistically if a survivor walks into range and the AI can afford
//     that specific trap.
//   SPAWN: Plan a wave of 1-10 zombies, spawn one per tick using ALL available
//     resources (no reserve held back). Traps still fire mid-wave if affordable.
//     Wave ends when batch complete or resources run dry. Uses weighted type
//     selection (60% shambler, 10% each special). Respects per-type limits.
//   HIDDEN_SPAWN: Place one surprise zombie behind survivors. Spends freely.
//     Respects per-type limits. Then cycles back to RESERVE.
//
// Camera: Smoothly glides between survivors, panning naturally like a real player.
// Culling: Kills stranded zombies too far from survivors when pop cap is stressed.
// View mode: When "Within View", only uses spawners/traps visible to the ZM camera.
// Rush prevention: AI only looks around during the configured window at round start.
// Rally: Continuously pushes idle zombies toward the nearest survivors.
//

enum AIZMCyclePhase_t
{
    AIZM_PHASE_RESERVE = 0,
    AIZM_PHASE_SPAWN,
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
    void DoReservePhase( CZMPlayer* pZM );
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

    // Trap reserve: cost of the most expensive trap on the map
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
