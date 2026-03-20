#pragma once

#include "npcr/npcr_senses.h"

#include "zmr_player.h"
#include "npcr/npcr_player.h"


enum ZMBotWeaponTypeRange_t
{
    BOTWEPRANGE_INVALID = 0,

    BOTWEPRANGE_MAINGUN,        // Shotgun, rifle, SMG, sniper
    BOTWEPRANGE_SECONDARYWEAPON, // Pistol, revolver
    BOTWEPRANGE_MELEE,          // Axe, sledge, improvised
    BOTWEPRANGE_THROWABLE,      // Molotov, pipebomb
    BOTWEPRANGE_FISTS,          // Fists/carry

    BOTWEPRANGE_MAX
};

class CZMPlayerBot : public NPCR::CPlayer<CZMPlayer>
{
public:
    DECLARE_CLASS( CZMPlayerBot, CZMPlayer );


    CZMPlayerBot();
    ~CZMPlayerBot();

    //
    virtual NPCR::CScheduleInterface*   CreateScheduleInterface() OVERRIDE;
    virtual NPCR::CBaseSenses*          CreateSenses() OVERRIDE;

    // HACK
    virtual void SetEyeAngles( const QAngle& ang ) OVERRIDE { SetAngles( ang ); pl.v_angle = ang; }
    
    virtual bool ShouldUpdate() const OVERRIDE;
    virtual bool IsEnemy( CBaseEntity* pEnt ) const OVERRIDE;
    //

    virtual void Spawn() OVERRIDE;

    //
    static CZMPlayer* CreateZMBot( const char* playername = "" );
    // Called from NPCR::CPlayer to create the player entity
    static CBasePlayer* BotPutInServer( edict_t* pEdict, const char* playername );
    //


    virtual bool OverrideUserCmd( CUserCmd& cmd ) OVERRIDE;
    

    //
    CZMBaseWeapon*  GetActiveWeapon() const { return ToZMBaseWeapon( m_hActiveWeapon.Get() ); }

    static ZMBotWeaponTypeRange_t GetWeaponType( CZMBaseWeapon* pWeapon );
    static ZMBotWeaponTypeRange_t GetWeaponType( const char* classname );
    bool            HasWeaponOfType( ZMBotWeaponTypeRange_t wepType ) const;
    bool            HasEquippedWeaponOfType( ZMBotWeaponTypeRange_t wepType ) const;
    bool            EquipWeaponOfType( ZMBotWeaponTypeRange_t wepType );
    bool            EquipBestWeapon();
    void            ThinkEquipBestWeapon();
    ZMBotWeaponTypeRange_t GetCurrentWeaponType() const;
    bool            WeaponHasAmmo( CZMBaseWeapon* pWeapon ) const;
    bool            ShouldReload() const;
    bool            HasAnyEffectiveRangeWeapons() const;
    bool            CanReload() const;
    bool            CanAttack() const;
    bool            MustStopToShoot() const;
    float           GetOptimalAttackDistance() const;
    float           GetMaxAttackDistance() const;
protected:
    CZMBaseWeapon*  FindWeaponOfType( ZMBotWeaponTypeRange_t wepType ) const;
public:
    //


    CBasePlayer*    GetFollowTarget() const { return m_hFollowTarget.Get(); }
    void            SetFollowTarget( CBasePlayer* pPlayer ) { m_hFollowTarget.Set( pPlayer ); m_bStayPut = false; }
    void            SetStayPut( bool bStay ) { m_bStayPut = bStay; }
    bool            IsStayingPut() const { return m_bStayPut; }

    void            SetBehaviorOverride( int iBehavior ) { m_iBehaviorOverride = iBehavior; }
    int             GetBehaviorOverride() const { return m_iBehaviorOverride; }

    void            SetMixedBehavior( int iMixed ) { m_iMixedBehavior = iMixed; }
    int             GetMixedBehavior() const { return m_iMixedBehavior; }

    // Player-commanded defend position (Hold E on ground)
    void            SetCommandedDefendPos( const Vector& pos ) { m_vecCommandedDefendPos = pos; m_bHasCommandedDefendPos = true; }
    void            ClearCommandedDefendPos() { m_bHasCommandedDefendPos = false; }
    bool            HasCommandedDefendPos() const { return m_bHasCommandedDefendPos; }
    const Vector&   GetCommandedDefendPos() const { return m_vecCommandedDefendPos; }

    // Player-commanded grab object (Hold E on physics object)
    void            SetCommandedGrabTarget( CBaseEntity* pEnt ) { m_hCommandedGrabTarget.Set( pEnt ); }
    CBaseEntity*    GetCommandedGrabTarget() const { return m_hCommandedGrabTarget.Get(); }
    void            ClearCommandedGrabTarget() { m_hCommandedGrabTarget.Set( nullptr ); }

    void            CheckObstacleJump();

private:
    CHandle<CBasePlayer> m_hFollowTarget;
    float           m_flNextObstacleCheck;
    bool            m_bStayPut;
    int             m_iBehaviorOverride; // -1 = no override, set by voice commands
    int             m_iMixedBehavior;    // -1 = unassigned, 0=follow, 1=explore, 2=defend (for mixed mode round-robin)

    Vector          m_vecCommandedDefendPos;
    bool            m_bHasCommandedDefendPos;
    EHANDLE         m_hCommandedGrabTarget;
};
