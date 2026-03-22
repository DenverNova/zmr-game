#pragma once

#include "zmr_player.h"
#include "zmr_entities.h"
#include "utlvector.h"
#include "utlmap.h"

//
// AI Zombie Master - Server-side tactical controller that manages zombie spawning,
// trap activation, and zombie rally commands using a dynamic plan system.
//
// The AI builds concrete spawn plans (e.g. "10 banshees", "5 hulks then 5 shamblers")
// and executes them wave by wave. It continuously monitors trap opportunities and
// will opportunistically fire traps regardless of the current spawn plan.
// Plans are replaced once completed or after a timeout, and trap priority is elevated
// when survivors are in range.
//

// A single wave step within a plan: spawn N of a specific class at a specific spawner
struct AIZMSpawnStep_t
{
    ZombieClass_t   zclass;         // Class to spawn
    int             count;          // How many to queue in this step
    float           flDelay;        // Seconds to wait before executing this step
    int             iSpawnerIndex;  // Index into the spawner list at plan-build time (-1 = pick at runtime)
};

class CZMAIZombieMaster
{
public:
    CZMAIZombieMaster();

    void Reset();
    void Update();

    bool IsActive() const;

private:
    // Plan generation - builds a random multi-step spawn plan distributed across spawners
    void BuildNewPlan();
    void ExecutePlanStep( CZMPlayer* pZM );

    // Trap opportunism - always runs independently of plan
    void UpdateTrapOpportunism( CZMPlayer* pZM );

    // Hidden spawn - occasionally place a zombie behind survivors
    void TryHiddenSpawn( CZMPlayer* pZM );

    // Explosive barrel opportunism - detonate barrels near survivors
    void TryDetonateBarrel( CZMPlayer* pZM );

    // Spawning helpers
    bool TrySpawnZombies( ZombieClass_t zclass, int count, CZMEntZombieSpawn* pSpawner );
    ZombieClass_t PickCheapestClass( CZMEntZombieSpawn* pSpawner ) const;
    ZombieClass_t PickExpensiveClass( CZMEntZombieSpawn* pSpawner ) const;
    ZombieClass_t PickRandomAffordableClass( CZMEntZombieSpawn* pSpawner ) const;
    bool CanAffordClass( ZombieClass_t zclass, CZMPlayer* pZM ) const;

    // Spawner selection helpers
    ZombieClass_t PickClassForSpawner( CZMEntZombieSpawn* pSpawner, ZombieClass_t avoid ) const;
    bool SpawnerSupportsClass( CZMEntZombieSpawn* pSpawner, ZombieClass_t zclass ) const;
    bool IsClassCheap( ZombieClass_t zclass ) const;

    // Rally - send zombies toward survivors
    void RallyZombiesToSurvivors();
    CBasePlayer* PickRallyTarget( const Vector& zombiePos ) const;

    // Utility
    CZMPlayer* GetZMPlayer() const;
    void GatherHumans( CUtlVector<CBasePlayer*>& humans ) const;
    CBasePlayer* FindNearestHuman( const Vector& pos, float* outDist = nullptr ) const;
    void GatherActiveSpawners( CUtlVector<CZMEntZombieSpawn*>& spawners ) const;
    void LogAllSpawners() const;
    CZMEntManipulate* FindBestTrap() const;
    bool HasSurvivorNearTrap() const;

    // Active spawn plan queue
    CUtlVector<AIZMSpawnStep_t> m_Plan;
    int     m_iPlanStep;            // Current step index in m_Plan
    float   m_flPlanStepReadyTime;  // When the current step can execute
    float   m_flPlanExpireTime;     // Give up on current plan after this time
    int     m_iSaveTarget;          // Resources we're saving up to before executing current step (-1 = no save)

    // Spawner list snapshot taken at plan-build time (indices into this used by plan steps)
    CUtlVector<CZMEntZombieSpawn*> m_PlanSpawners;
    int m_iLastSpawnerUsed;         // Tracks last spawner index to distribute across spawners

    // Timers
    float m_flNextTrapTime;
    float m_flNextRallyTime;
    float m_flNextHiddenSpawnTime;
    float m_flLastUpdateTime;
    bool  m_bLoggedSpawners;

    // Post-plan cooldown: pause between finishing one plan and starting the next
    float m_flPlanCooldownUntil;

    // Per-trap cooldown: tracks last trigger time by entity index
    CUtlMap<int, float> m_TrapCooldowns;

    // Hidden spawn rate limiting: max N per time window
    int   m_nHiddenSpawnsThisWindow;
    float m_flHiddenSpawnWindowStart;

    // Explosive barrel detonation cooldown
    float m_flNextBarrelDetonateTime;

    // Camera roaming: AI moves the ZM view to follow survivors
    void UpdateRoamCamera( CZMPlayer* pZM );
    float m_flNextCameraMove;       // When to move to the next look target
    int   m_iCameraTargetIdx;       // Index of the current watched survivor

    // Within-view filtering helpers
    bool IsVisibleFromZM( CZMPlayer* pZM, const Vector& pos ) const;
};

extern CZMAIZombieMaster g_ZMAIZombieMaster;
