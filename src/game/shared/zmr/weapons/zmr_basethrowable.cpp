#include "cbase.h"
#include "in_buttons.h"
#include "npcevent.h"
#include "basegrenade_shared.h"


#include "zmr_basethrowable.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


IMPLEMENT_NETWORKCLASS_ALIASED( ZMBaseThrowableWeapon, DT_ZM_BaseThrowableWeapon )

BEGIN_NETWORK_TABLE( CZMBaseThrowableWeapon, DT_ZM_BaseThrowableWeapon )
#ifdef CLIENT_DLL
    RecvPropInt( RECVINFO( m_iThrowState ) ),
#else
    SendPropInt( SENDINFO( m_iThrowState ), Q_log2( MOLOTOVSTATE_MAX ) + 1, SPROP_UNSIGNED ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CZMBaseThrowableWeapon )
    DEFINE_PRED_FIELD( m_iThrowState, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

CZMBaseThrowableWeapon::CZMBaseThrowableWeapon()
{
    m_iThrowState = THROWSTATE_IDLE;
#ifndef CLIENT_DLL
    m_hCookedProjectile.Set( nullptr );
#endif
}

CZMBaseThrowableWeapon::~CZMBaseThrowableWeapon()
{
}

void CZMBaseThrowableWeapon::Precache()
{
    BaseClass::Precache();

#ifndef CLIENT_DLL
    UTIL_PrecacheOther( GetProjectileClassname() );
#endif
}

void CZMBaseThrowableWeapon::ItemPostFrame()
{
    CZMPlayer* pPlayer = GetPlayerOwner();

    if ( !pPlayer ) return;


    // We've thrown the grenade, remove our weapon.
#ifndef CLIENT_DLL
    if ( GetThrowState() == THROWSTATE_THROWN && IsViewModelSequenceFinished() )
    {
        PostThrowCleanup();
        return;
    }
#endif


    if ( GetThrowState() == THROWSTATE_IDLE && pPlayer->m_nButtons & IN_ATTACK && m_flNextPrimaryAttack <= gpGlobals->curtime )
    {
        PrimaryAttack();
    }

    // We've finished our drawback animation, now we're ready to throw.
    if ( GetThrowState() == THROWSTATE_DRAW_BACK && IsViewModelSequenceFinished() )
    {
        SetThrowState( THROWSTATE_READYTOTHROW );
#ifndef CLIENT_DLL
        TryArmOnReady();
#endif
    }


    if ( GetThrowState() == THROWSTATE_READYTOTHROW && !(pPlayer->m_nButtons & IN_ATTACK) )
    {
        SendWeaponAnim( ACT_VM_THROW );
        SetThrowState( THROWSTATE_THROWN );

        auto HasAnimEvent = [&]( int iAnimEvent ) {
            return GetFirstInstanceOfAnimEventTime( GetSequence(), iAnimEvent, false ) != -1.0f;
        };

        // We don't have an animation event for throwing. Just throw now!
        if ( !HasAnimEvent( EVENT_WEAPON_THROW ) && !HasAnimEvent( EVENT_WEAPON_THROW2 ) && !HasAnimEvent( EVENT_WEAPON_THROW3 ) )
        {
            Throw( pPlayer );
        }
    }

    //
    // Check weapon idle anims unless holding for throw or thrown.
    //
    if ( GetThrowState() < THROWSTATE_READYTOTHROW )
    {
        WeaponIdle();
    }
}

bool CZMBaseThrowableWeapon::Deploy()
{
    bool ret = BaseClass::Deploy();

    if ( ret )
    {
        SetThrowState( THROWSTATE_IDLE );
    }

    return ret;
}

void CZMBaseThrowableWeapon::Equip( CBaseCombatCharacter* pCharacter )
{
    BaseClass::Equip( pCharacter );

#ifndef CLIENT_DLL
    if ( pCharacter && GetOwner() == pCharacter && pCharacter->GetAmmoCount( GetPrimaryAmmoType() ) < 1 )
    {
        pCharacter->GiveAmmo( 1, GetPrimaryAmmoType(), true );
    }
#endif
}

#ifndef CLIENT_DLL
void CZMBaseThrowableWeapon::Drop( const Vector& vecVelocity )
{
    CZMPlayer* pPlayer = GetPlayerOwner();
    if ( pPlayer )
    {
        pPlayer->RemoveAmmo( 1, GetPrimaryAmmoType() );
    }

    auto* pProjectile = GetCookedProjectile();
    if ( pProjectile )
    {
        DropCookedProjectile();
        UTIL_Remove( this );
    }

    BaseClass::Drop( vecVelocity );
}

void CZMBaseThrowableWeapon::Operator_HandleAnimEvent( animevent_t* pEvent, CBaseCombatCharacter* pOperator )
{
	switch( pEvent->event )
	{
    case EVENT_WEAPON_THROW :
    case EVENT_WEAPON_THROW2 :
    case EVENT_WEAPON_THROW3 :
        Throw( GetPlayerOwner() );
        break;
	default:
		BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
		break;
	}
}

void CZMBaseThrowableWeapon::OnProjectileExplode()
{
    // Cleanup after explosion (if handheld)
    if ( GetThrowState() != THROWSTATE_THROWN )
    {
        PostThrowCleanup();
    }
}

void CZMBaseThrowableWeapon::PostThrowCleanup()
{
    auto* pPlayer = GetPlayerOwner();
    
    if ( pPlayer )
    {
        pPlayer->Weapon_Drop( this, nullptr, nullptr );
    }

    UTIL_Remove( this );
}

void CZMBaseThrowableWeapon::UnfollowCookedProjectile()
{
    auto* pProjectile = m_hCookedProjectile.Get();
    if ( pProjectile )
    {
        pProjectile->StopFollowingEntity();
        pProjectile->RemoveEffects( EF_NODRAW );
    }
}

void CZMBaseThrowableWeapon::DropCookedProjectile()
{
    auto* pProjectile = m_hCookedProjectile.Get();
    if ( !pProjectile )
    {
        return;
    }

    UnfollowCookedProjectile();

    auto* pOwner = GetOwner();
    if ( pOwner )
    {
        pProjectile->Teleport( nullptr, nullptr, &pOwner->GetAbsVelocity() );
    }

    m_hCookedProjectile.Set( nullptr );
}

void CZMBaseThrowableWeapon::TryArmOnReady()
{
    if ( !GetBaseThrowableConfig()->bArmOnReady )
    {
        return;
    }

    auto* pProjectile = CreateProjectile();
    if ( !pProjectile )
    {
        return;
    }

    pProjectile->Teleport( &GetAbsOrigin(), nullptr, nullptr );

    pProjectile->FollowEntity( this, true );
    pProjectile->AddEffects( EF_NODRAW );

    m_hCookedProjectile.Set( pProjectile );
}
#endif

const CZMBaseThrowableConfig* CZMBaseThrowableWeapon::GetBaseThrowableConfig() const
{
    return static_cast<const CZMBaseThrowableConfig*>( GetWeaponConfig() );
}

const char* CZMBaseThrowableWeapon::GetProjectileClassname() const
{
    return "";
}

Vector CZMBaseThrowableWeapon::GetThrowPos( CZMPlayer* pPlayer )
{
    Assert( pPlayer );


    trace_t trace;
    Vector fwd;

    AngleVectors( pPlayer->EyeAngles(), &fwd );
    fwd[2] += 0.1f;

    Vector maxs = Vector( 6, 6, 6 );

    UTIL_TraceHull( pPlayer->EyePosition(), pPlayer->EyePosition() + fwd * 18.0f, -maxs, maxs, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &trace );


    return trace.endpos;
}

Vector CZMBaseThrowableWeapon::GetThrowDirection( CZMPlayer* pPlayer )
{
    Assert( pPlayer );

    Vector fwd;
    AngleVectors( pPlayer->EyeAngles(), &fwd );
    fwd[2] += 0.1f;

    return fwd;
}

float CZMBaseThrowableWeapon::GetThrowVelocity() const
{
    return GetBaseThrowableConfig()->flThrowVelocity;
}

QAngle CZMBaseThrowableWeapon::GetThrowAngularVelocity() const
{
    auto& min = GetBaseThrowableConfig()->vecAngularVel_Min;
    auto& max = GetBaseThrowableConfig()->vecAngularVel_Max;

    return QAngle( random->RandomFloat( min.x, max.x ), random->RandomFloat( min.y, max.y ), random->RandomFloat( min.z, max.z ) );
}

void CZMBaseThrowableWeapon::Throw( CZMPlayer* pPlayer )
{
    SetThrowState( THROWSTATE_THROWN );


    if ( !pPlayer ) return;


#ifndef CLIENT_DLL
    Vector pos = GetThrowPos( pPlayer );
    Vector vel = pPlayer->GetAbsVelocity() + GetThrowDirection( pPlayer ) * GetThrowVelocity();

    auto* pExistingProjectile = GetCookedProjectile();

    CZMBaseProjectile* pProjectile;
    if ( pExistingProjectile )
    {
        pProjectile = pExistingProjectile;
        UnfollowCookedProjectile();
    }
    else
    {
        pProjectile = CreateProjectile();
        if ( !pProjectile )
        {
            return;
        }
    }

    pProjectile->Teleport( &pos, nullptr, nullptr );

    pProjectile->OnThrow();

    AngularImpulse impulse;
    QAngleToAngularImpulse( GetThrowAngularVelocity(), impulse );

    pProjectile->ApplyAbsVelocityImpulse( vel );
    pProjectile->ApplyLocalAngularVelocityImpulse( impulse );

    m_hCookedProjectile.Set( nullptr );
    PostThrow( pPlayer, pProjectile );
#endif

    pPlayer->DoAnimationEvent( PLAYERANIMEVENT_ATTACK_PRIMARY );

    //WeaponSound( SINGLE );
}

#ifndef CLIENT_DLL
CZMBaseProjectile* CZMBaseThrowableWeapon::CreateProjectile() const
{
    auto* pPlayer = GetPlayerOwner();
    auto* pszProjectileClassname = GetProjectileClassname();
    auto* pProjectile = dynamic_cast<CZMBaseProjectile*>( CBaseEntity::CreateNoSpawn( pszProjectileClassname, vec3_origin, vec3_angle ) );

    if ( !pProjectile )
    {
        Warning( "Couldn't create projectile entity '%s'!\n", pszProjectileClassname );
        return nullptr;
    }

    pProjectile->SetOwnerEntity( pPlayer );
    pProjectile->SetDamage( GetBaseThrowableConfig()->flProjectileDamage );
    pProjectile->SetDamageRadius( GetBaseThrowableConfig()->flProjectileRadius );
    pProjectile->SetDetonationTime( GetBaseThrowableConfig()->flDetonationTime );
    pProjectile->SetThrower( pPlayer );
    pProjectile->m_takedamage = DAMAGE_EVENTS_ONLY;
    pProjectile->AddEffects( EF_BONEMERGE_FASTCULL );

    DispatchSpawn( pProjectile );

    return pProjectile;
}
#endif

//
//
//
CZMBaseThrowableConfig::CZMBaseThrowableConfig( const char* wepname, const char* configpath ) : CZMBaseWeaponConfig( wepname, configpath )
{
    flThrowVelocity = 100.0f;
    flProjectileDamage = 100.0f;
    flProjectileRadius = 100.0f;

    vecAngularVel_Min = vec3_origin;
    vecAngularVel_Max = vec3_origin;
}

void CZMBaseThrowableConfig::LoadFromConfig( KeyValues* kv )
{
    CZMBaseWeaponConfig::LoadFromConfig( kv );

    flThrowVelocity = kv->GetFloat( "throw_velocity", 100.0f );
    flProjectileDamage = kv->GetFloat( "projectile_damage", 100.0f );
    flProjectileRadius = kv->GetFloat( "projectile_radius", 100.0f );

    CopyVector( kv->GetString( "angvel_min" ), vecAngularVel_Min );
    CopyVector( kv->GetString( "angvel_max" ), vecAngularVel_Max );

    bArmOnReady = kv->GetBool( "arm_on_ready", false );

    flDetonationTime = kv->GetFloat( "detonation_time", 3.0f );
}

bool CZMBaseThrowableConfig::OverrideFromConfig( KeyValues* kv )
{
    auto ret = CZMBaseWeaponConfig::OverrideFromConfig( kv );

    OVERRIDE_FROM_WEPCONFIG_F( kv, throw_velocity, flThrowVelocity );
    OVERRIDE_FROM_WEPCONFIG_F( kv, projectile_damage, flProjectileDamage );
    OVERRIDE_FROM_WEPCONFIG_F( kv, projectile_radius, flProjectileRadius );
    
    bool bGotDefault = false;
    auto bArmOnReady_ = kv->GetBool( "arm_on_ready", false, &bGotDefault );
    if ( !bGotDefault )
    {
        kv->SetBool( "arm_on_ready", bArmOnReady_ );
    }

    OVERRIDE_FROM_WEPCONFIG_F( kv, detonation_time, flDetonationTime );

    // ZMRTODO: Override angular velocity.

    return ret;
}

KeyValues* CZMBaseThrowableConfig::ToKeyValues() const
{
    auto* kv = CZMBaseWeaponConfig::ToKeyValues();

    kv->SetFloat( "throw_velocity", flThrowVelocity );
    kv->SetFloat( "projectile_damage", flProjectileDamage );
    kv->SetFloat( "projectile_radius", flProjectileRadius );

    VectorToKv( kv, "angvel_min", vecAngularVel_Min );
    VectorToKv( kv, "angvel_max", vecAngularVel_Max );

    kv->SetBool( "arm_on_ready", bArmOnReady );
    kv->SetFloat( "detonation_time", flDetonationTime );

    return kv;
}
