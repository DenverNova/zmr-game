#include "createmultiplayergamemappage.h"
#include "CreateMultiplayerGameServerPage.h"

using namespace vgui;

#include <KeyValues.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/TextEntry.h>
#include <vgui_controls/Label.h>
#include "FileSystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


// Helper: write a float text entry value into a char buf
static void GetEntryFloat( vgui::TextEntry *pEntry, char *buf, int bufLen )
{
    pEntry->GetText( buf, bufLen );
}

// Helper: write an int text entry value into a char buf
static void GetEntryInt( vgui::TextEntry *pEntry, char *buf, int bufLen )
{
    pEntry->GetText( buf, bufLen );
}


CCreateMultiplayerGameMapPage::CCreateMultiplayerGameMapPage( vgui::Panel *parent, const char *name, CCreateMultiplayerGameServerPage *pServerPage )
    : PropertyPage( parent, name )
{
    m_pServerPage = pServerPage;
    m_szCurrentMap[0] = '\0';

    m_pMapNameLabel    = new Label( this, "MapNameLabel", "No map selected" );
    m_pEnableMapConfig = new CheckButton( this, "EnableMapConfig", "Enable map-specific overrides" );

    m_pResourceMult          = new TextEntry( this, "ResourceMult" );
    m_pResourcePerPlayerMult = new TextEntry( this, "ResourcePerPlayerMult" );
    m_pZombieHealthMult      = new TextEntry( this, "ZombieHealthMult" );
    m_pZombieDamageMult      = new TextEntry( this, "ZombieDamageMult" );
    m_pZombieMax             = new TextEntry( this, "ZombieMax" );
    m_pZombieMaxBanshee      = new TextEntry( this, "ZombieMaxBanshee" );
    m_pZombieMaxHulk         = new TextEntry( this, "ZombieMaxHulk" );
    m_pZombieMaxDrifter      = new TextEntry( this, "ZombieMaxDrifter" );
    m_pZombieMaxImmolator    = new TextEntry( this, "ZombieMaxImmolator" );
    m_pAIZMDifficulty        = new TextEntry( this, "AIZMDifficulty" );
    m_pAIZMSpawnInterval     = new TextEntry( this, "AIZMSpawnInterval" );

    LoadControlSettings( "Resource/CreateMultiplayerGameMapPage.res" );

    LoadMapConfig();
}

CCreateMultiplayerGameMapPage::~CCreateMultiplayerGameMapPage()
{
}

void CCreateMultiplayerGameMapPage::OnMapSelectionChanged()
{
    LoadMapConfig();
}

float CCreateMultiplayerGameMapPage::ParseCvarFloat( const char *cfgText, const char *cvarName, float defaultVal )
{
    if ( !cfgText || !cvarName )
        return defaultVal;

    // Search for lines of the form: <cvarName> <value>
    const char *p = cfgText;
    int nameLen = Q_strlen( cvarName );
    while ( *p )
    {
        // Skip leading whitespace/newlines
        while ( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' )
            p++;

        // Skip comment lines
        if ( p[0] == '/' && p[1] == '/' )
        {
            while ( *p && *p != '\n' ) p++;
            continue;
        }

        if ( Q_strncasecmp( p, cvarName, nameLen ) == 0 && ( p[nameLen] == ' ' || p[nameLen] == '\t' ) )
        {
            p += nameLen;
            while ( *p == ' ' || *p == '\t' ) p++;
            return (float)atof( p );
        }

        // Skip to end of line
        while ( *p && *p != '\n' ) p++;
    }
    return defaultVal;
}

int CCreateMultiplayerGameMapPage::ParseCvarInt( const char *cfgText, const char *cvarName, int defaultVal )
{
    return (int)ParseCvarFloat( cfgText, cvarName, (float)defaultVal );
}

void CCreateMultiplayerGameMapPage::LoadMapConfig()
{
    if ( !m_pServerPage )
        return;

    const char *szMap = m_pServerPage->GetMapName();
    if ( !szMap || !szMap[0] )
    {
        m_pMapNameLabel->SetText( "No map selected" );
        m_pEnableMapConfig->SetSelected( false );
        m_szCurrentMap[0] = '\0';
        return;
    }

    Q_strncpy( m_szCurrentMap, szMap, sizeof( m_szCurrentMap ) );

    char labelBuf[128];
    Q_snprintf( labelBuf, sizeof( labelBuf ), "Overrides for: %s", m_szCurrentMap );
    m_pMapNameLabel->SetText( labelBuf );

    char cfgPath[256];
    Q_snprintf( cfgPath, sizeof( cfgPath ), "cfg/%s.cfg", m_szCurrentMap );

    char *pBuf = nullptr;
    FileHandle_t fh = g_pFullFileSystem->Open( cfgPath, "rb", "GAME" );
    if ( fh )
    {
        int fileSize = g_pFullFileSystem->Size( fh );
        if ( fileSize > 0 && fileSize < 65536 )
        {
            pBuf = new char[ fileSize + 1 ];
            g_pFullFileSystem->Read( pBuf, fileSize, fh );
            pBuf[ fileSize ] = '\0';
        }
        g_pFullFileSystem->Close( fh );
        m_pEnableMapConfig->SetSelected( true );
    }
    else
    {
        m_pEnableMapConfig->SetSelected( false );
    }

    // Populate controls - use defaults from global convars if no override found
    const char *cfg = pBuf ? pBuf : "";
    char buf[32];

    Q_snprintf( buf, sizeof(buf), "%.3g", ParseCvarFloat( cfg, "zm_sv_resource_multiplier",     1.0f ) );
    m_pResourceMult->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%.3g", ParseCvarFloat( cfg, "zm_sv_resource_per_player_mult", 0.05f ) );
    m_pResourcePerPlayerMult->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%.3g", ParseCvarFloat( cfg, "zm_sv_zombie_health_mult",       1.0f ) );
    m_pZombieHealthMult->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%.3g", ParseCvarFloat( cfg, "zm_sv_zombie_damage_mult",       1.0f ) );
    m_pZombieDamageMult->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%i",   ParseCvarInt( cfg, "zm_sv_zombiemax",                  64 ) );
    m_pZombieMax->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%i",   ParseCvarInt( cfg, "zm_sv_zombie_max_banshee",         -1 ) );
    m_pZombieMaxBanshee->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%i",   ParseCvarInt( cfg, "zm_sv_zombie_max_hulk",            -1 ) );
    m_pZombieMaxHulk->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%i",   ParseCvarInt( cfg, "zm_sv_zombie_max_drifter",         -1 ) );
    m_pZombieMaxDrifter->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%i",   ParseCvarInt( cfg, "zm_sv_zombie_max_immolator",       -1 ) );
    m_pZombieMaxImmolator->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%.3g", ParseCvarFloat( cfg, "zm_sv_ai_zm_difficulty",         1.0f ) );
    m_pAIZMDifficulty->SetText( buf );

    Q_snprintf( buf, sizeof(buf), "%.3g", ParseCvarFloat( cfg, "zm_sv_ai_zm_spawn_interval",     8.0f ) );
    m_pAIZMSpawnInterval->SetText( buf );

    delete[] pBuf;
}

void CCreateMultiplayerGameMapPage::SaveMapConfig()
{
    if ( !m_szCurrentMap[0] )
        return;

    char cfgPath[256];
    Q_snprintf( cfgPath, sizeof( cfgPath ), "cfg/%s.cfg", m_szCurrentMap );

    if ( !m_pEnableMapConfig->IsSelected() )
    {
        g_pFullFileSystem->RemoveFile( cfgPath, "GAME" );
        return;
    }

    FileHandle_t fh = g_pFullFileSystem->Open( cfgPath, "wb", "GAME" );
    if ( !fh )
        return;

    char val[32];

    g_pFullFileSystem->FPrintf( fh, "// Map overrides for %s - generated by server dialog\n", m_szCurrentMap );

    GetEntryFloat( m_pResourceMult, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_resource_multiplier %s\n", val );

    GetEntryFloat( m_pResourcePerPlayerMult, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_resource_per_player_mult %s\n", val );

    GetEntryFloat( m_pZombieHealthMult, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombie_health_mult %s\n", val );

    GetEntryFloat( m_pZombieDamageMult, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombie_damage_mult %s\n", val );

    GetEntryInt( m_pZombieMax, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombiemax %s\n", val );

    GetEntryInt( m_pZombieMaxBanshee, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombie_max_banshee %s\n", val );

    GetEntryInt( m_pZombieMaxHulk, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombie_max_hulk %s\n", val );

    GetEntryInt( m_pZombieMaxDrifter, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombie_max_drifter %s\n", val );

    GetEntryInt( m_pZombieMaxImmolator, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_zombie_max_immolator %s\n", val );

    GetEntryFloat( m_pAIZMDifficulty, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_ai_zm_difficulty %s\n", val );

    GetEntryFloat( m_pAIZMSpawnInterval, val, sizeof(val) );
    g_pFullFileSystem->FPrintf( fh, "zm_sv_ai_zm_spawn_interval %s\n", val );

    g_pFullFileSystem->Close( fh );
}

void CCreateMultiplayerGameMapPage::OnResetData()
{
    LoadMapConfig();
}

void CCreateMultiplayerGameMapPage::OnApplyChanges()
{
    SaveMapConfig();
}
