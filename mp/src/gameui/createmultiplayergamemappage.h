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
}

//
// Map-specific server options page for the Create Server dialog.
// Allows editing a per-map config file (cfg/<mapname>.cfg) that runs
// automatically when the map loads via ExecuteMapConfigs.
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

    CCreateMultiplayerGameServerPage *m_pServerPage;

    vgui::CheckButton *m_pEnableMapConfig;
    vgui::TextEntry *m_pConfigEditor;
    vgui::Label *m_pMapNameLabel;

    char m_szCurrentMap[64];
};

#endif // CREATEMULTIPLAYERGAMEMAPPAGE_H
