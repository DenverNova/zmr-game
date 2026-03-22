#ifndef CREATEMULTIPLAYERGAMEMAPPAGE_H
#define CREATEMULTIPLAYERGAMEMAPPAGE_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/PropertyPage.h>

class CCreateMultiplayerGameServerPage;

namespace vgui
{
    class TextEntry;
    class CheckButton;
    class Label;
    class ComboBox;
}

//
// Map-specific server options page for the Create Server dialog.
// Stores per-map convar overrides in cfg/<mapname>.cfg that run
// automatically when the map loads.
//
class CCreateMultiplayerGameMapPage : public vgui::PropertyPage
{
    DECLARE_CLASS_SIMPLE( CCreateMultiplayerGameMapPage, vgui::PropertyPage );

public:
    CCreateMultiplayerGameMapPage( vgui::Panel *parent, const char *name, CCreateMultiplayerGameServerPage *pServerPage );
    ~CCreateMultiplayerGameMapPage();

    virtual void OnApplyChanges() OVERRIDE;
    virtual void OnResetData() OVERRIDE;

    // Called when the selected map changes on the Server tab
    void OnMapSelectionChanged();

private:
    void LoadMapConfig();
    void SaveMapConfig();

    // Parse a single convar value from cfg file contents (returns defaultVal if not found)
    static float ParseCvarFloat( const char *cfgText, const char *cvarName, float defaultVal );
    static int   ParseCvarInt(   const char *cfgText, const char *cvarName, int   defaultVal );

    CCreateMultiplayerGameServerPage *m_pServerPage;
    char m_szCurrentMap[64];

    vgui::Label       *m_pMapNameLabel;
    vgui::CheckButton *m_pEnableMapConfig;

    // Resource settings
    vgui::TextEntry   *m_pResourceMult;
    vgui::TextEntry   *m_pResourcePerPlayerMult;

    // Zombie multipliers
    vgui::TextEntry   *m_pZombieHealthMult;
    vgui::TextEntry   *m_pZombieDamageMult;

    // Zombie population caps
    vgui::TextEntry   *m_pZombieMax;
    vgui::TextEntry   *m_pZombieMaxBanshee;
    vgui::TextEntry   *m_pZombieMaxHulk;
    vgui::TextEntry   *m_pZombieMaxDrifter;
    vgui::TextEntry   *m_pZombieMaxImmolator;

    // AI ZM
    vgui::TextEntry   *m_pAIZMDifficulty;
    vgui::TextEntry   *m_pAIZMSpawnInterval;
};

#endif // CREATEMULTIPLAYERGAMEMAPPAGE_H
