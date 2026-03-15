#ifndef CREATEMULTIPLAYERGAMEZMRPAGE_H
#define CREATEMULTIPLAYERGAMEZMRPAGE_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/PropertyPage.h>

namespace vgui
{
    class TextEntry;
    class CheckButton;
    class ComboBox;
}

//
// ZMR-specific server options page for the Create Server dialog.
// Controls AI ZM, AI survivors, resource multipliers, zombie multipliers,
// flashlight drain, and AFK timer settings.
//
class CCreateMultiplayerGameZMRPage : public vgui::PropertyPage
{
    DECLARE_CLASS_SIMPLE( CCreateMultiplayerGameZMRPage, vgui::PropertyPage );

public:
    CCreateMultiplayerGameZMRPage( vgui::Panel *parent, const char *name );
    ~CCreateMultiplayerGameZMRPage();

    virtual void OnApplyChanges() OVERRIDE;
    virtual void OnResetData() OVERRIDE;

private:
    void LoadValues();
    void ApplyConVar( const char *cvarName, const char *controlName, bool isFloat = false );
    void ApplyConVarCheck( const char *cvarName, const char *controlName );

    vgui::ComboBox *m_pAIZMMode;
    vgui::TextEntry *m_pBotSurvivors;
    vgui::TextEntry *m_pResourceMult;
    vgui::TextEntry *m_pResourcePerPlayerMult;
    vgui::TextEntry *m_pZombieHealthMult;
    vgui::TextEntry *m_pZombieDamageMult;
    vgui::CheckButton *m_pFlashlightInfinite;
    vgui::TextEntry *m_pAFKTimer;
    vgui::TextEntry *m_pAIZMSpawnInterval;
    vgui::TextEntry *m_pAIZMTrapInterval;
    vgui::TextEntry *m_pAIZMAggression;
};

#endif // CREATEMULTIPLAYERGAMEZMRPAGE_H
