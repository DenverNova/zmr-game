#include "cbase.h"

#ifndef CLIENT_DLL
#include "filesystem.h"
#endif

#include "zmr_handedness.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#ifdef CLIENT_DLL
ConVar zm_cl_lefthanded( "zm_cl_lefthanded", "0", FCVAR_ARCHIVE | FCVAR_USERINFO, "Use left-handed viewmodels?" );
#endif

#ifndef CLIENT_DLL
CZMHandednessSystem& GetZMHandednessSystem()
{
    static CZMHandednessSystem system;
    return system;
}

CZMHandednessSystem::CZMHandednessSystem() : CAutoGameSystem( "ZMHandednessSystem" ), m_map( true )
{
}

void CZMHandednessSystem::LevelShutdownPostEntity()
{
    Clear( "LevelShutdownPostEntity" );
}

void CZMHandednessSystem::LevelInitPreEntity()
{
    Clear( "LevelInitPreEntity" );
}

void CZMHandednessSystem::Clear( const char* reason )
{
    DevMsg( "ZMHandednessSystem: Clearing string map of %i precached left-handed models. (%s)\n", m_map.Count(), reason );
    m_map.Clear();
}

const char* CZMHandednessSystem::GetLeftHandedModelName( const char* rightHandedViewModel ) const
{
    auto index = m_map.Find( rightHandedViewModel );
    return index != m_map.InvalidIndex() ? m_map[index].lhName : nullptr;
}

int CZMHandednessSystem::GetLeftHandedModelIndex( const char* rightHandedViewModel ) const
{
    auto index = m_map.Find( rightHandedViewModel );
    return index != m_map.InvalidIndex() ? m_map[index].iModelIndex : -1;
}

void CZMHandednessSystem::PrecacheLeftHanded( const char* rightHandedViewModel )
{
    char lhName[MAX_PATH];
    GetLeftHandedModelName( rightHandedViewModel, lhName, sizeof( lhName ) );

    if ( !filesystem->FileExists( lhName, "GAME" ) )
    {
        return;
    }

    int lhModelIndex = CBaseEntity::PrecacheModel( lhName, true );

    if ( lhModelIndex == -1 )
    {
        return;
    }

    auto index = m_map.Find( rightHandedViewModel );
    if ( index != m_map.InvalidIndex() )
    {
        return;
    }

    LeftHandedData_t& data = m_map[rightHandedViewModel];
    data.iModelIndex = lhModelIndex;
    Q_strncpy( data.lhName, lhName, sizeof( data.lhName ) );

    DevMsg( "ZMHandednessSystem: Precached left-handed viewmodel: %s\n", lhName );
}

void CZMHandednessSystem::GetLeftHandedModelName( const char* rightHandedViewModel, char* dest, size_t destSizeInBytes )
{
    Q_strncpy( dest, rightHandedViewModel, destSizeInBytes - 3 );

    int len = Q_strlen( dest );
    if ( len > 4 )
    {
        dest[len - 4] = '\0'; // Remove ".mdl"

        // Format as left handed.
        Q_strncat( dest, "_lh.mdl", destSizeInBytes );
    }
}

bool CZMHandednessSystem::UsesLeftHanded( int playerEntIndex ) const
{
    const char* value = engine->GetClientConVarValue( playerEntIndex, "zm_cl_lefthanded" );
    if ( value && *value )
    {
        return *value == '1';
    }

    return false;
}
#endif
