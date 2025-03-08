#include "cbase.h"
#include "gameinterface.h"
#include "mapentities.h"

#include "zmr_mapentities.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void CServerGameClients::GetPlayerLimits( int& minplayers, int& maxplayers, int &defaultMaxPlayers ) const
{
    minplayers = 2;
    defaultMaxPlayers = 16;
    maxplayers = MAX_PLAYERS;
}

void CServerGameDLL::LevelInit_ParseAllEntities( const char *pMapEntities )
{
    g_ZMMapEntities.InitialSpawn( pMapEntities );
}
