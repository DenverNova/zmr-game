#include "cbase.h"

#include "items.h"


#include "zmr_ammo.h"
#include "weapons/zmr_base.h"
#include "zmr_shareddefs.h"
#include "zmr_player.h"
#include "zmr_gamerules.h"
#include "zmr_voicelines.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
    Drop ammo
*/
void ZM_DropAmmo( const CCommand &args )
{
    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    
    if ( !pPlayer ) return;

    if ( !pPlayer->IsHuman() || !pPlayer->IsAlive() ) return;



    CZMBaseWeapon* pWeapon = ToZMBaseWeapon( pPlayer->GetActiveWeapon() );

    if ( !pWeapon ) return;

    if ( !pWeapon->CanBeDropped() ) return;


    const char* ammoname = pWeapon->GetDropAmmoName();
    int amount = pWeapon->GetDropAmmoAmount();
    
    // We have no ammo to drop.
    if ( !ammoname ) return;

    if ( pPlayer->GetAmmoCount( pWeapon->GetPrimaryAmmoType() ) < amount )
    {
        return;
    }

    trace_t trace;
    Vector src, end;
    Vector fwd;

    pPlayer->EyeVectors( &fwd, nullptr, nullptr );

    src = pPlayer->EyePosition() + Vector( 0.0f, 0.0f, -12.0f );
    end = src + fwd * 38.0f;

    // Make sure we don't spawn in the wall.
    Vector testhull( 6, 6, 6 );
    
    UTIL_TraceHull( src, end, -testhull, testhull, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &trace );


    CZMAmmo* ammobox = (CZMAmmo*)CBaseEntity::Create( ammoname, trace.endpos, pWeapon->GetAbsAngles(), nullptr );
    
    if ( !ammobox ) return;


    // Don't let players pickup this ammo instantly...
    ammobox->SetNextPickupTouch( 1.0f );


    IPhysicsObject* pPhys = ammobox->VPhysicsGetObject();

    if ( pPhys )
    {
        Vector vel = pPlayer->GetAbsVelocity() + fwd * 200.0f;

        AngularImpulse angvel( 100, 100, 100 );

        pPhys->AddVelocity( &vel, &angvel );
    }

    ammobox->AddSpawnFlags( SF_NORESPAWN );

    pPlayer->RemoveAmmo( amount, pWeapon->GetPrimaryAmmoType() );
}

static ConCommand dropammo( "dropammo", ZM_DropAmmo, "Drop your ammo from your current weapon." );

/*
    Drop weapon
*/
void ZM_DropWeapon( const CCommand &args )
{
    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    
    if ( !pPlayer ) return;

    if ( !pPlayer->IsHuman() || !pPlayer->IsAlive() ) return;


    CZMBaseWeapon* pWeapon = ToZMBaseWeapon( pPlayer->GetActiveWeapon() );

    if ( !pWeapon )
    {
        return;
    }

    if ( !pWeapon->CanBeDropped() ) return;


    pPlayer->Weapon_Drop( pWeapon, nullptr, nullptr );
}

static ConCommand dropweapon( "dropweapon", ZM_DropWeapon, "Drop your current weapon" );

/*
    Round restart
*/
void ZM_RoundRestart( const CCommand &args )
{
    if ( !UTIL_IsCommandIssuedByServerAdmin() )
    {
        CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );

        if ( !pPlayer ) return;

        if ( !pPlayer->IsZM() && !sv_cheats->GetBool() )
        {
            // ZMRTODO: Vote for round restart.
            return;
        }
    }
    

    ZMRules()->EndRound( ZMROUND_ZMSUBMIT );
}

static ConCommand roundrestart( "roundrestart", ZM_RoundRestart, "Restart the round as ZM." );

void ZM_MoveToSurvivor( const CCommand& args )
{
    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    if ( !pPlayer || !( pPlayer->IsZM() || pPlayer->GetTeamNumber() == ZMTEAM_SPECTATOR ) )
    {
        return;
    }

    CUtlVector<CZMPlayer*> vSurvivors;
    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CZMPlayer* pLoop = ToZMPlayer( UTIL_PlayerByIndex( i ) );

        if ( pLoop && pLoop != pPlayer && pLoop->IsHuman() && pLoop->IsAlive() )
        {
            vSurvivors.AddToTail( pLoop );
        }
    }

    if ( vSurvivors.IsEmpty() )
    {
        return;
    }

    vSurvivors.Sort( []( CZMPlayer* const* p1, CZMPlayer* const* p2 )
    {
        float a = ( *p1 )->GetAbsOrigin().x;
        float b = ( *p2 )->GetAbsOrigin().x;
        if ( a < b ) return -1;
        if ( a > b ) return 1;
        return 0;
    } );
    
    Vector myPos = pPlayer->GetAbsOrigin();


    const float flMinDistSqr = 512.0f * 512.0f;

    int closestIndex = -1;
    float closestDistSqr = FLT_MAX;
    FOR_EACH_VEC( vSurvivors, i )
    {
        CZMPlayer* pSurvivor = vSurvivors[i];
        float distSqr = pSurvivor->GetAbsOrigin().DistToSqr( myPos );
        if ( distSqr < closestDistSqr && distSqr < flMinDistSqr )
        {
            closestIndex = i;
            closestDistSqr = distSqr;
        }
    }

    int nextIndex = closestIndex + 1;
    if ( !vSurvivors.IsValidIndex( nextIndex ) )
    {
        nextIndex = 0;
    }


    Vector pos = vSurvivors[nextIndex]->EyePosition();
    pPlayer->Teleport( &pos, nullptr, nullptr );
}

static ConCommand zm_movetosurvivor( "zm_movetosurvivor", ZM_MoveToSurvivor, "Move yourself to a survivor. Cycles to the next one when executed rapidly." );


void ZM_ObserveZombie( const CCommand &args )
{
    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );

    if ( !pPlayer ) return;

    if ( !pPlayer->IsObserver() ) return;


    CZMBaseZombie* pZombie = nullptr;
    if ( args.ArgC() > 1 )
    {
        pZombie = ToZMBaseZombie( UTIL_EntityByIndex( atoi( args.Arg( 1 ) ) ) );
    }
    else
    {
        // No argument, trace a line.
        CBaseEntity* pIgnore = (pPlayer->GetObserverTarget() && pPlayer->GetObserverMode() != OBS_MODE_ROAMING)
            ? pPlayer->GetObserverTarget()
            : pPlayer;

        trace_t tr;
        UTIL_TraceLine( pPlayer->EyePosition(), pPlayer->EyeDirection3D() * MAX_COORD_FLOAT, MASK_NPCSOLID, pIgnore, COLLISION_GROUP_NPC, &tr );

        if ( tr.m_pEnt )
        {
            pZombie = ToZMBaseZombie( tr.m_pEnt );
        }
    }


    CBaseCombatCharacter* pCharacter = pZombie;

    // No valid zombie found. Try a random one.
    if ( !pCharacter || !pCharacter->IsAlive() )
    {
        pCharacter = nullptr;


        int i;
        CUtlVector<CBaseCombatCharacter*> vChars;
        CBaseEntity* pCurTarget = pPlayer->GetObserverTarget();
        
        // Flip from players to zombies and vice versa.
        if ( !pCurTarget || pCurTarget->IsPlayer() )
        {
            g_ZombieManager.ForEachAliveZombie( [ &vChars ]( CZMBaseZombie* pZombie )
            {
                vChars.AddToTail( pZombie );
            } );
        }
        else
        {
            for ( i = 1; i <= gpGlobals->maxClients; i++ )
            {
                CBasePlayer* pLoop = UTIL_PlayerByIndex( i );

                if ( pLoop && pLoop->IsAlive() )
                    vChars.AddToTail( pLoop );
            }
        }


        if ( vChars.Count() > 0 )
            pCharacter = vChars[random->RandomInt( 0, vChars.Count() - 1 )];
    }


    if ( pCharacter && pPlayer->SetObserverTarget( pCharacter ) )
    {
        pPlayer->SetObserverMode( OBS_MODE_CHASE );
    }
}

static ConCommand zm_observezombie( "zm_observezombie", ZM_ObserveZombie, "Allows observer to spectate a given zombie (ent index)." );


/*

*/
void ZM_VoiceMenu( const CCommand &args )
{
    if ( args.ArgC() <= 1 ) return;


    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    if ( !pPlayer )  return;

    if ( !pPlayer->IsHuman() || !pPlayer->IsAlive() ) return;



    ZMGetVoiceLines()->OnVoiceLine( pPlayer, atoi( args.Arg( 1 ) ) );
}

static ConCommand zm_cmd_voicemenu( "zm_cmd_voicemenu", ZM_VoiceMenu );

/*
    Rat it up!  
*/
static ConVar zm_sv_ratmode( "zm_sv_ratmode", "0", 0, "Enable rat mode? (zm_ratmode)" );
void ZM_RatMode( const CCommand& args )
{
    if ( !zm_sv_ratmode.GetBool() )
    {
        return;
    }

    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    
    if ( !pPlayer ) return;

    if ( pPlayer->GetTeamNumber() != ZMTEAM_SPECTATOR || pPlayer->IsRat() )
    {
        return;
    }

    // ZMRTODO: Change this for a better check.
    if ( UTIL_PointContents( pPlayer->GetAbsOrigin() ) & CONTENTS_SOLID )
    {
        return;
    }

    pPlayer->StartRatMode();
}

static ConCommand zm_ratmode( "zm_ratmode", ZM_RatMode, "Turn into a rat." );

/*
    Mirror legacy cvars to the new ones.
    Only set because some maps can break without these.
*/
#define LEGACY_CVAR( oldcvar, newcvar ) \
    CON_COMMAND( oldcvar, "DON'T EDIT THIS CVAR, USE "#newcvar" INSTEAD!" ) \
    { \
        extern ConVar newcvar; \
        if ( UTIL_IsCommandIssuedByServerAdmin() ) \
        { \
            Msg( "LEGACY CVAR %s | %s -> %s\n", #oldcvar, #newcvar, args.ArgS() ); \
            newcvar.SetValue( args.ArgS() ); \
        } \
    }


// taters_made_this
LEGACY_CVAR( zm_resource_limit, zm_sv_resource_max )
LEGACY_CVAR( zm_resource_refill_rate, zm_sv_resource_rate )
LEGACY_CVAR( zm_initial_resources, zm_sv_resource_init )

// bman
LEGACY_CVAR( zm_physexp_cost, zm_sv_physexp_cost )
LEGACY_CVAR( zm_spotcreate_cost, zm_sv_hidden_cost_shambler )

// kink
LEGACY_CVAR( zm_flashlight_drainrate, zm_sv_flashlightdrainrate )
