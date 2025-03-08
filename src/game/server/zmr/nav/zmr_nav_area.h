#pragma once

#include "nav_mesh.h"

class CZMRNavArea : public CNavArea
{
public:
    typedef CNavArea BaseClass;
    typedef CZMRNavArea ThisClass;
    //DECLARE_CLASS( CZMRNavArea, CNavArea );

    CZMRNavArea();
    ~CZMRNavArea();

    // Override to make these do nothing.
    virtual void ComputeHidingSpots() OVERRIDE;
    virtual void ComputeSniperSpots() OVERRIDE;
    virtual void ComputeSpotEncounters() OVERRIDE;
    virtual void UpdateBlocked( bool force = false, int teamID = TEAM_ANY ) OVERRIDE;

    virtual bool IsBlocked( int teamID, bool ignoreNavBlockers = false ) const OVERRIDE;
    void SetNoFloor( bool state ) { m_bHasNoFloor = state; }

    void GetWorldBounds( Vector& mins, Vector& maxs ) const;

    DECLARE_ENT_SCRIPTDESC();
    HSCRIPT GetScriptInstance();
    
private:
    bool m_bHasNoFloor;

    HSCRIPT m_hScriptInstance;
};

inline HSCRIPT ToHScript( CNavArea* pArea )
{
    CZMRNavArea* area = static_cast<CZMRNavArea*>( pArea );
    return ( area ) ? area->GetScriptInstance() : nullptr;
}

inline HSCRIPT ToHScript( CZMRNavArea* pArea )
{
    return ( pArea ) ? pArea->GetScriptInstance() : nullptr;
}

template <> ScriptClassDesc_t* GetScriptDesc<CZMRNavArea>( CZMRNavArea* );
inline CZMRNavArea* ToNavArea( HSCRIPT hScript )
{
    return ( IsValid( hScript ) ) ? static_cast<CZMRNavArea*>( g_pScriptVM->GetInstanceValue( hScript, GetScriptDescForClass( CZMRNavArea ) ) ) : nullptr;
}
