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


CCreateMultiplayerGameMapPage::CCreateMultiplayerGameMapPage( vgui::Panel *parent, const char *name, CCreateMultiplayerGameServerPage *pServerPage )
    : PropertyPage( parent, name )
{
    m_pServerPage = pServerPage;
    m_szCurrentMap[0] = '\0';

    m_pMapNameLabel = new Label( this, "MapNameLabel", "No map selected" );
    m_pEnableMapConfig = new CheckButton( this, "EnableMapConfig", "Enable map-specific config" );
    m_pConfigEditor = new TextEntry( this, "ConfigEditor" );
    m_pConfigEditor->SetMultiline( true );
    m_pConfigEditor->SetVerticalScrollbar( true );
    m_pConfigEditor->SetCatchEnterKey( false );

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

void CCreateMultiplayerGameMapPage::LoadMapConfig()
{
    if ( !m_pServerPage )
        return;

    const char *szMap = m_pServerPage->GetMapName();
    if ( !szMap || !szMap[0] )
    {
        m_pMapNameLabel->SetText( "No map selected" );
        m_pConfigEditor->SetText( "" );
        m_pEnableMapConfig->SetSelected( false );
        m_szCurrentMap[0] = '\0';
        return;
    }

    Q_strncpy( m_szCurrentMap, szMap, sizeof( m_szCurrentMap ) );

    char labelBuf[128];
    Q_snprintf( labelBuf, sizeof( labelBuf ), "Config for: %s", m_szCurrentMap );
    m_pMapNameLabel->SetText( labelBuf );

    // Try to load cfg/<mapname>.cfg
    char cfgPath[256];
    Q_snprintf( cfgPath, sizeof( cfgPath ), "cfg/%s.cfg", m_szCurrentMap );

    FileHandle_t fh = g_pFullFileSystem->Open( cfgPath, "rb", "GAME" );
    if ( fh )
    {
        int fileSize = g_pFullFileSystem->Size( fh );
        if ( fileSize > 0 && fileSize < 32768 )
        {
            char *pBuf = new char[ fileSize + 1 ];
            g_pFullFileSystem->Read( pBuf, fileSize, fh );
            pBuf[ fileSize ] = '\0';
            m_pConfigEditor->SetText( pBuf );
            delete[] pBuf;
        }
        else
        {
            m_pConfigEditor->SetText( "" );
        }
        g_pFullFileSystem->Close( fh );
        m_pEnableMapConfig->SetSelected( true );
    }
    else
    {
        // No config file exists yet - provide a template
        char defaultCfg[512];
        Q_snprintf( defaultCfg, sizeof( defaultCfg ),
            "// Map config for %s\n"
            "// These commands run automatically when this map loads.\n"
            "// Example:\n"
            "// zm_sv_zombiemax 100\n"
            "// zm_sv_resource_multiplier 1.5\n",
            m_szCurrentMap );
        m_pConfigEditor->SetText( defaultCfg );
        m_pEnableMapConfig->SetSelected( false );
    }
}

void CCreateMultiplayerGameMapPage::SaveMapConfig()
{
    if ( !m_szCurrentMap[0] )
        return;

    char cfgPath[256];
    Q_snprintf( cfgPath, sizeof( cfgPath ), "cfg/%s.cfg", m_szCurrentMap );

    if ( !m_pEnableMapConfig->IsSelected() )
    {
        // If disabled, delete the config file if it exists
        g_pFullFileSystem->RemoveFile( cfgPath, "GAME" );
        return;
    }

    // Write the editor contents to the config file
    char buf[32768];
    m_pConfigEditor->GetText( buf, sizeof( buf ) );

    FileHandle_t fh = g_pFullFileSystem->Open( cfgPath, "wb", "GAME" );
    if ( fh )
    {
        g_pFullFileSystem->Write( buf, Q_strlen( buf ), fh );
        g_pFullFileSystem->Close( fh );
    }
}

void CCreateMultiplayerGameMapPage::OnResetData()
{
    LoadMapConfig();
}

void CCreateMultiplayerGameMapPage::OnApplyChanges()
{
    SaveMapConfig();
}
