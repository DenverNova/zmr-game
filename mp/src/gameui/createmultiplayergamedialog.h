//========= Copyright ? 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef CREATEMULTIPLAYERGAMEDIALOG_H
#define CREATEMULTIPLAYERGAMEDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/PropertyDialog.h>

class CCreateMultiplayerGameServerPage;
class CCreateMultiplayerGameGameplayPage;
class CCreateMultiplayerGameBotPage;
class CCreateMultiplayerGameZMRPage;

//-----------------------------------------------------------------------------
// Purpose: dialog for launching a listenserver
//-----------------------------------------------------------------------------
class CCreateMultiplayerGameDialog : public vgui::PropertyDialog
{
	DECLARE_CLASS_SIMPLE( CCreateMultiplayerGameDialog,  vgui::PropertyDialog );

public:
	CCreateMultiplayerGameDialog(vgui::Panel *parent);
	~CCreateMultiplayerGameDialog();

protected:
	virtual bool OnOK(bool applyOnly);

private:
	CCreateMultiplayerGameServerPage *m_pServerPage;
	CCreateMultiplayerGameGameplayPage *m_pGameplayPage;
	CCreateMultiplayerGameBotPage *m_pBotPage;
	CCreateMultiplayerGameZMRPage *m_pZMRPage;

	bool m_bBotsEnabled;

	// for loading/saving game config
	KeyValues *m_pSavedData;
};


#endif // CREATEMULTIPLAYERGAMEDIALOG_H
