#include "createmultiplayergamezmrpage.h"

using namespace vgui;

#include <KeyValues.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/TextEntry.h>
#include <vgui_controls/Label.h>
#include "tier1/convar.h"
#include "EngineInterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


CCreateMultiplayerGameZMRPage::CCreateMultiplayerGameZMRPage( vgui::Panel *parent, const char *name )
    : PropertyPage( parent, name )
{
    m_pAIZMMode = new ComboBox( this, "AIZMMode", 3, false );
    m_pAIZMMode->AddItem( "Disabled", new KeyValues( "data", "value", "0" ) );
    m_pAIZMMode->AddItem( "Always On", new KeyValues( "data", "value", "1" ) );
    m_pAIZMMode->AddItem( "Fallback (No Volunteers)", new KeyValues( "data", "value", "2" ) );

    m_pBotSurvivors = new TextEntry( this, "BotSurvivors" );
    m_pResourceMult = new TextEntry( this, "ResourceMult" );
    m_pResourcePerPlayerMult = new TextEntry( this, "ResourcePerPlayerMult" );
    m_pZombieHealthMult = new TextEntry( this, "ZombieHealthMult" );
    m_pZombieDamageMult = new TextEntry( this, "ZombieDamageMult" );
    m_pFlashlightInfinite = new CheckButton( this, "FlashlightInfinite", "" );
    m_pAFKTimer = new TextEntry( this, "AFKTimer" );
    m_pAIZMSpawnInterval = new TextEntry( this, "AIZMSpawnInterval" );
    m_pAIZMAggression = new TextEntry( this, "AIZMAggression" );

    LoadControlSettings( "Resource/CreateMultiplayerGameZMRPage.res" );

    LoadValues();
}

CCreateMultiplayerGameZMRPage::~CCreateMultiplayerGameZMRPage()
{
}

void CCreateMultiplayerGameZMRPage::LoadValues()
{
    ConVarRef aiZM( "zm_sv_ai_zm" );
    if ( aiZM.IsValid() )
    {
        int mode = aiZM.GetInt();
        mode = clamp( mode, 0, 2 );
        m_pAIZMMode->ActivateItemByRow( mode );
    }

    ConVarRef botSurvivors( "zm_sv_bot_survivors" );
    if ( botSurvivors.IsValid() )
        m_pBotSurvivors->SetText( botSurvivors.GetString() );

    ConVarRef resMult( "zm_sv_resource_multiplier" );
    if ( resMult.IsValid() )
        m_pResourceMult->SetText( resMult.GetString() );

    ConVarRef resPerPlayer( "zm_sv_resource_per_player_mult" );
    if ( resPerPlayer.IsValid() )
        m_pResourcePerPlayerMult->SetText( resPerPlayer.GetString() );

    ConVarRef healthMult( "zm_sv_zombie_health_mult" );
    if ( healthMult.IsValid() )
        m_pZombieHealthMult->SetText( healthMult.GetString() );

    ConVarRef damageMult( "zm_sv_zombie_damage_mult" );
    if ( damageMult.IsValid() )
        m_pZombieDamageMult->SetText( damageMult.GetString() );

    ConVarRef flashlight( "zm_sv_flashlight_infinite" );
    if ( flashlight.IsValid() )
        m_pFlashlightInfinite->SetSelected( flashlight.GetBool() );

    ConVarRef afk( "zm_sv_antiafk" );
    if ( afk.IsValid() )
        m_pAFKTimer->SetText( afk.GetString() );

    ConVarRef spawnInterval( "zm_sv_ai_zm_spawn_interval" );
    if ( spawnInterval.IsValid() )
        m_pAIZMSpawnInterval->SetText( spawnInterval.GetString() );

    ConVarRef aggression( "zm_sv_ai_zm_aggression" );
    if ( aggression.IsValid() )
        m_pAIZMAggression->SetText( aggression.GetString() );
}

void CCreateMultiplayerGameZMRPage::ApplyConVar( const char *cvarName, const char *controlName, bool isFloat )
{
    char buf[64];
    TextEntry *pEntry = dynamic_cast<TextEntry*>( FindChildByName( controlName ) );
    if ( !pEntry )
        return;

    pEntry->GetText( buf, sizeof( buf ) );

    ConVarRef var( cvarName );
    if ( var.IsValid() )
    {
        if ( isFloat )
            var.SetValue( (float)atof( buf ) );
        else
            var.SetValue( atoi( buf ) );
    }
}

void CCreateMultiplayerGameZMRPage::ApplyConVarCheck( const char *cvarName, const char *controlName )
{
    CheckButton *pCheck = dynamic_cast<CheckButton*>( FindChildByName( controlName ) );
    if ( !pCheck )
        return;

    ConVarRef var( cvarName );
    if ( var.IsValid() )
    {
        var.SetValue( pCheck->IsSelected() ? 1 : 0 );
    }
}

void CCreateMultiplayerGameZMRPage::OnResetData()
{
    LoadValues();
}

void CCreateMultiplayerGameZMRPage::OnApplyChanges()
{
    // AI ZM mode
    KeyValues *kv = m_pAIZMMode->GetActiveItemUserData();
    if ( kv )
    {
        ConVarRef aiZM( "zm_sv_ai_zm" );
        if ( aiZM.IsValid() )
            aiZM.SetValue( kv->GetInt( "value", 0 ) );
    }

    ApplyConVar( "zm_sv_bot_survivors", "BotSurvivors" );
    ApplyConVar( "zm_sv_resource_multiplier", "ResourceMult", true );
    ApplyConVar( "zm_sv_resource_per_player_mult", "ResourcePerPlayerMult", true );
    ApplyConVar( "zm_sv_zombie_health_mult", "ZombieHealthMult", true );
    ApplyConVar( "zm_sv_zombie_damage_mult", "ZombieDamageMult", true );
    ApplyConVarCheck( "zm_sv_flashlight_infinite", "FlashlightInfinite" );
    ApplyConVar( "zm_sv_antiafk", "AFKTimer" );
    ApplyConVar( "zm_sv_ai_zm_spawn_interval", "AIZMSpawnInterval", true );
    ApplyConVar( "zm_sv_ai_zm_aggression", "AIZMAggression", true );
}
