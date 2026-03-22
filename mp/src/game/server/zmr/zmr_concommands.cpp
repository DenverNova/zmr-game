#include "cbase.h"

#include "items.h"


#include "zmr_ammo.h"
#include "weapons/zmr_base.h"
#include "zmr_shareddefs.h"
#include "zmr_player.h"
#include "zmr_gamerules.h"
#include "zmr_voicelines.h"
#include "npcs/zmr_playerbot.h"

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
extern ConVar zm_sv_bot_command_range;

static void BotVoiceCommand_Help( CZMPlayer* pCaller )
{
    float flRange = zm_sv_bot_command_range.GetFloat();
    float flRangeSqr = flRange * flRange;
    Vector callerPos = pCaller->GetAbsOrigin();

    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CBasePlayer* pPlayer = UTIL_PlayerByIndex( i );
        if ( !pPlayer || !pPlayer->IsBot() || !pPlayer->IsAlive() )
            continue;
        if ( pPlayer->GetTeamNumber() != ZMTEAM_HUMAN )
            continue;
        if ( pPlayer == pCaller )
            continue;

        CZMPlayerBot* pBot = dynamic_cast<CZMPlayerBot*>( pPlayer );
        if ( !pBot )
            continue;

        // Don't grab bots already following a different human player
        CBasePlayer* pCurrentFollow = pBot->GetFollowTarget();
        if ( pCurrentFollow && pCurrentFollow != pCaller && !pCurrentFollow->IsBot() )
            continue;

        float distSqr = pPlayer->GetAbsOrigin().DistToSqr( callerPos );
        if ( distSqr > flRangeSqr )
            continue;

        pBot->SetFollowTarget( pCaller );
    }
}

static void BotVoiceCommand_Follow( CZMPlayer* pCaller )
{
    float flRange = zm_sv_bot_command_range.GetFloat();
    float flRangeSqr = flRange * flRange;
    Vector eyePos = pCaller->EyePosition();
    Vector fwd;
    AngleVectors( pCaller->EyeAngles(), &fwd );

    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CBasePlayer* pPlayer = UTIL_PlayerByIndex( i );
        if ( !pPlayer || !pPlayer->IsBot() || !pPlayer->IsAlive() )
            continue;
        if ( pPlayer->GetTeamNumber() != ZMTEAM_HUMAN )
            continue;

        CZMPlayerBot* pBot = dynamic_cast<CZMPlayerBot*>( pPlayer );
        if ( !pBot )
            continue;

        // Skip bots already following a different human player
        CBasePlayer* pCurrentFollow = pBot->GetFollowTarget();
        if ( pCurrentFollow && pCurrentFollow != pCaller && !pCurrentFollow->IsBot() )
            continue;

        float distSqr = pPlayer->GetAbsOrigin().DistToSqr( pCaller->GetAbsOrigin() );
        if ( distSqr > flRangeSqr )
            continue;

        // Must be visible to the caller (line of sight)
        trace_t tr;
        UTIL_TraceLine( eyePos, pPlayer->WorldSpaceCenter(), MASK_VISIBLE, pCaller, COLLISION_GROUP_NONE, &tr );
        if ( tr.fraction < 0.9f && tr.m_pEnt != pPlayer )
            continue;

        pBot->SetFollowTarget( pCaller );
    }
}

static void BotVoiceCommand_Go( CZMPlayer* pCaller )
{
    float flRange = zm_sv_bot_command_range.GetFloat();
    float flRangeSqr = flRange * flRange;
    Vector eyePos = pCaller->EyePosition();
    Vector fwd;
    AngleVectors( pCaller->EyeAngles(), &fwd );

    int nCommanded = 0;
    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CBasePlayer* pPlayer = UTIL_PlayerByIndex( i );
        if ( !pPlayer || !pPlayer->IsBot() || !pPlayer->IsAlive() )
            continue;
        if ( pPlayer->GetTeamNumber() != ZMTEAM_HUMAN )
            continue;

        CZMPlayerBot* pBot = dynamic_cast<CZMPlayerBot*>( pPlayer );
        if ( !pBot )
            continue;

        float distSqr = pPlayer->GetAbsOrigin().DistToSqr( pCaller->GetAbsOrigin() );
        if ( distSqr > flRangeSqr )
            continue;

        // Only affect bots the player is looking at (within a cone)
        Vector toBot = pPlayer->GetAbsOrigin() + Vector( 0, 0, 36 ) - eyePos;
        float dist = toBot.NormalizeInPlace();
        if ( dist > flRange )
            continue;
        float dot = fwd.Dot( toBot );
        if ( dot < 0.7f )
            continue;

        // Set to explore mode
        pBot->SetFollowTarget( nullptr );
        pBot->SetStayPut( false );
        pBot->ClearCommandedDefendPos();
        pBot->SetBehaviorOverride( 1 ); // 1 = Explore
        ZMGetVoiceLines()->OnVoiceLine( pBot, 0 );
        nCommanded++;
    }

    if ( nCommanded > 0 )
        ClientPrint( pCaller, HUD_PRINTCENTER, UTIL_VarArgs( "%d bot(s): Exploring", nCommanded ) );
}

#define VOICE_INDEX_HELP    2
#define VOICE_INDEX_FOLLOW  3
#define VOICE_INDEX_GO      6

void ZM_VoiceMenu( const CCommand &args )
{
    if ( args.ArgC() <= 1 ) return;


    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    if ( !pPlayer )  return;

    if ( !pPlayer->IsHuman() || !pPlayer->IsAlive() ) return;


    int voiceIndex = atoi( args.Arg( 1 ) );

    ZMGetVoiceLines()->OnVoiceLine( pPlayer, voiceIndex );

    // Bot commands triggered by voice lines
    switch ( voiceIndex )
    {
    case VOICE_INDEX_HELP:
        BotVoiceCommand_Help( pPlayer );
        break;
    case VOICE_INDEX_FOLLOW:
        BotVoiceCommand_Follow( pPlayer );
        break;
    case VOICE_INDEX_GO:
        BotVoiceCommand_Go( pPlayer );
        break;
    }
}

static ConCommand zm_cmd_voicemenu( "zm_cmd_voicemenu", ZM_VoiceMenu );


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


/*
    Bot Possession - spectator takes over a bot
*/
ConVar zm_sv_bot_possess( "zm_sv_bot_possess", "1", FCVAR_NOTIFY | FCVAR_ARCHIVE, "Allow dead players to possess AI bots by pressing USE while spectating them." );

void ZM_PossessBot( const CCommand &args )
{
    if ( !zm_sv_bot_possess.GetBool() )
        return;

    CZMPlayer* pPlayer = ToZMPlayer( UTIL_GetCommandClient() );
    if ( !pPlayer )
        return;

    // Must be dead and spectating
    if ( pPlayer->IsAlive() || !pPlayer->IsObserver() )
        return;

    // Must be on the human team
    if ( pPlayer->GetTeamNumber() != ZMTEAM_HUMAN )
        return;

    // Must be spectating a bot
    CBaseEntity* pTarget = pPlayer->GetObserverTarget();
    if ( !pTarget || !pTarget->IsPlayer() || !pTarget->IsAlive() )
        return;

    if ( pTarget->GetTeamNumber() != ZMTEAM_HUMAN )
        return;

    CZMPlayerBot* pBot = dynamic_cast<CZMPlayerBot*>( pTarget );
    if ( !pBot )
        return;

    // Store the bot's state before removing it
    Vector vecPos = pBot->GetAbsOrigin();
    QAngle angEyes = pBot->EyeAngles();
    int iHealth = pBot->GetHealth();
    int iArmor = pBot->ArmorValue();

    // Save the bot's model so the player looks identical
    char szBotModel[256];
    Q_strncpy( szBotModel, STRING( pBot->GetModelName() ), sizeof( szBotModel ) );

    // Collect the bot's weapons and ammo
    struct WepInfo_t
    {
        char szClassname[64];
        int iClip1;
        int iClip2;
    };
    CUtlVector<WepInfo_t> weapons;
    const char* pszActiveWep = nullptr;

    for ( int i = 0; i < MAX_WEAPONS; i++ )
    {
        CBaseCombatWeapon* pWep = pBot->GetWeapon( i );
        if ( !pWep )
            continue;

        WepInfo_t info;
        Q_strncpy( info.szClassname, pWep->GetClassname(), sizeof( info.szClassname ) );
        info.iClip1 = pWep->m_iClip1;
        info.iClip2 = pWep->m_iClip2;
        weapons.AddToTail( info );

        if ( pWep == pBot->GetActiveWeapon() )
            pszActiveWep = info.szClassname;
    }

    // Copy ammo counts
    int ammo[MAX_AMMO_SLOTS];
    for ( int i = 0; i < MAX_AMMO_SLOTS; i++ )
        ammo[i] = pBot->GetAmmoCount( i );

    // Move the bot to spectator instead of kicking - it stays until next round
    pBot->RemoveAllItems( true );
    pBot->ChangeTeam( ZMTEAM_SPECTATOR );
    pBot->m_lifeState = LIFE_DEAD;
    pBot->AddEffects( EF_NODRAW );

    // Respawn the player at the bot's position
    pPlayer->pl.deadflag = false;
    pPlayer->m_lifeState = LIFE_RESPAWNABLE;
    pPlayer->Spawn();

    // Apply the bot's model to the player
    if ( szBotModel[0] )
        pPlayer->SetModel( szBotModel );

    pPlayer->SetAbsOrigin( vecPos );
    pPlayer->SnapEyeAngles( angEyes );
    pPlayer->SetHealth( iHealth );
    pPlayer->SetArmorValue( iArmor );

    // Remove default items first
    pPlayer->RemoveAllItems( false );

    // Give the bot's weapons
    CBaseCombatWeapon* pActiveWep = nullptr;
    for ( int i = 0; i < weapons.Count(); i++ )
    {
        CBaseCombatWeapon* pWep = dynamic_cast<CBaseCombatWeapon*>( pPlayer->GiveNamedItem( weapons[i].szClassname ) );
        if ( pWep )
        {
            pWep->m_iClip1 = weapons[i].iClip1;
            pWep->m_iClip2 = weapons[i].iClip2;

            if ( pszActiveWep && Q_stricmp( weapons[i].szClassname, pszActiveWep ) == 0 )
                pActiveWep = pWep;
        }
    }

    // Restore ammo
    for ( int i = 0; i < MAX_AMMO_SLOTS; i++ )
    {
        if ( ammo[i] > 0 )
            pPlayer->SetAmmoCount( ammo[i], i );
    }

    // Switch to the weapon the bot was using
    if ( pActiveWep )
        pPlayer->Weapon_Switch( pActiveWep );

    ClientPrint( pPlayer, HUD_PRINTCENTER, "Bot possessed!" );
}

static ConCommand zm_cmd_possess_bot( "zm_cmd_possess_bot", ZM_PossessBot, "Take over the bot you are spectating." );
