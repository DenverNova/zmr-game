#include "cbase.h"
#include "soundent.h"

#include "zmr_baseprojectile.h"
#include "weapons/zmr_basethrowable.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


extern short g_sModelIndexFireball;
extern short g_sModelIndexWExplosion;


ConVar zm_sv_debug_projectile( "zm_sv_debug_projectile", "0" );

CZMBaseProjectile::CZMBaseProjectile()
{
	m_flDetonateTime = 3.0f;
}

void CZMBaseProjectile::Spawn()
{
	float delay = GetDetonationTime();
	if ( delay >= 0.0f )
	{
		SetThink( &CZMBaseProjectile::ArmedDetonateThink );
		SetNextThink( gpGlobals->curtime + delay );
	}

	BaseClass::Spawn();
}

void CZMBaseProjectile::ArmedDetonateThink()
{
	Detonate();
	UTIL_Remove( this );
}

void CZMBaseProjectile::Detonate()
{
	SetModelName( NULL_STRING );
	AddSolidFlags( FSOLID_NOT_SOLID );

	m_takedamage = DAMAGE_NO;

	auto* pThrower = GetThrower();

	Vector vecExplosionOrigin = WorldSpaceCenter();

	auto* pFollowedEntity = GetFollowedEntity();
	// Is handheld? Try using hand position.
	if ( IsFollowingEntity() && pFollowedEntity && pFollowedEntity->IsBaseCombatWeapon() && pThrower && pThrower->IsAlive() )
	{
		Vector vecHand;
		QAngle ang;
		bool foundAttachment = pThrower->GetAttachment( "anim_attachment_RH", vecHand, ang );
		if ( foundAttachment && !(UTIL_PointContents( vecHand ) & MASK_SOLID_BRUSHONLY) )
		{
			vecExplosionOrigin = vecHand;
		}
	}

	trace_t tr;
	UTIL_TraceLine( vecExplosionOrigin, vecExplosionOrigin + Vector( 0, 0, -32 ), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction == 1.0f )
	{
		UTIL_TraceLine( vecExplosionOrigin, vecExplosionOrigin + Vector( 0, 0, 32 ), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );
	}

	if ( zm_sv_debug_projectile.GetFloat() > 0.0f )
	{
		NDebugOverlay::Axis( vecExplosionOrigin, vec3_angle, 32.0f, true, zm_sv_debug_projectile.GetFloat() );
	}

	const bool bInWater = ( UTIL_PointContents( vecExplosionOrigin ) & MASK_WATER ) != 0;
	const short iExplosionModel = bInWater ? g_sModelIndexWExplosion : g_sModelIndexFireball;

	if ( tr.fraction != 1.0f )
	{
		Vector vecNormal = tr.plane.normal;
		surfacedata_t* pdata = physprops->GetSurfaceData( tr.surface.surfaceProps );
		CPASFilter filter( vecExplosionOrigin );

		te->Explosion( filter, -1.0f, // don't apply cl_interp delay
			&vecExplosionOrigin,
			iExplosionModel,
			m_DmgRadius * .03,
			25,
			TE_EXPLFLAG_NONE,
			m_DmgRadius,
			m_flDamage,
			&vecNormal,
			(char)pdata->game.material );
	}
	else
	{
		CPASFilter filter( vecExplosionOrigin );
		te->Explosion( filter, -1.0f, // don't apply cl_interp delay
			&vecExplosionOrigin,
			iExplosionModel,
			m_DmgRadius * .03,
			25,
			TE_EXPLFLAG_NONE,
			m_DmgRadius,
			m_flDamage );
	}

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), BASEGRENADE_EXPLOSION_VOLUME, 3.0f );

	// Use the thrower's position as the reported position
	Vector vecReport;
	Vector* vecReported = nullptr;
	if ( pThrower )
	{
		vecReport = pThrower->GetAbsOrigin();
		vecReported = &vecReport;
	}

	CTakeDamageInfo info( this, pThrower, GetBlastForce(), vecExplosionOrigin, m_flDamage, DMG_BLAST, 0, vecReported );

	RadiusDamage( info, vecExplosionOrigin, m_DmgRadius, CLASS_NONE, nullptr );

	UTIL_DecalTrace( &tr, "Scorch" );

	EmitSound( "BaseGrenade.Explode" );

	SetThink( &CBaseGrenade::SUB_Remove );
	SetTouch( nullptr );
	SetSolid( SOLID_NONE );

	AddEffects( EF_NODRAW );
	SetAbsVelocity( vec3_origin );

	SetNextThink( gpGlobals->curtime );

	PostExplode();
}

void CZMBaseProjectile::PostExplode()
{
	auto* pWeapon = dynamic_cast<CZMBaseThrowableWeapon*>( GetParent() );
	if ( pWeapon )
	{
		pWeapon->OnProjectileExplode();
	}
}
