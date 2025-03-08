#include "cbase.h"
#include "vscript_server.h"

#include "zmr_nav_mesh.h"
#include "zmr_nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


bool CNavAreaScriptInstanceHelper::ToString( void* p, char* pBuf, int bufSize )
{
    CZMRNavArea* pArea = static_cast<CZMRNavArea*>( p );
    V_snprintf( pBuf, bufSize, "([%u] Area)", pArea->GetID() );
    return true;
}

CNavAreaScriptInstanceHelper g_NavAreaScriptInstanceHelper;

DEFINE_SCRIPT_INSTANCE_HELPER( CZMRNavArea, &g_NavAreaScriptInstanceHelper )

BEGIN_ENT_SCRIPTDESC_ROOT( CZMRNavArea, "Navigation areas class" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetID, "GetID", "Get area ID." )
DEFINE_SCRIPTFUNC( GetAttributes, "Get area attribute bits" )
DEFINE_SCRIPTFUNC( SetAttributes, "Set area attribute bits" )
DEFINE_SCRIPTFUNC( HasAttributes, "Has area attribute bits" )
DEFINE_SCRIPTFUNC( RemoveAttributes, "Removes area attribute bits" )
DEFINE_SCRIPTFUNC( GetCenter, "Get center origin of area" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetCorner, "GetCorner", "( corner ) - Get corner origin of area" )
//DEFINE_SCRIPTFUNC( FindRandomSpot, "Get random origin within extent of area" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptConnectToArea, "ConnectTo", "( area, dir ) - Connect this area to given area in given direction" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptDisconnectArea, "Disconnect", "( area ) - Disconnect this area from given area" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptIsConnectedArea, "IsConnected", "( area, dir ) - Return true if given area is connected in given direction" )
DEFINE_SCRIPTFUNC( IsDamaging, "Return true if continuous damage (ie: fire) is in this area" )
DEFINE_SCRIPTFUNC( MarkAsDamaging, "( duration ) - Mark this area is damaging for the next 'duration' seconds" )
DEFINE_SCRIPTFUNC( IsBlocked, "( team ) - Return true if team is blocked in this area" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptMarkAsBlocked, "MarkAsBlocked", "( team ) - Mark this area as blocked for team" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetAdjacentCount, "GetAdjacentCount", "( dir ) - Get the number of adjacent areas in the given direction" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetAdjacentAreas, "GetAdjacentAreas", "( dir, table ) - Fills a passed in table with all adjacent areas in the given direction" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetAdjacentArea, "GetAdjacentArea", "( dir, n ) - Return the i'th adjacent area in the given direction" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetRandomAdjacentArea, "GetRandomAdjacentArea", "( dir ) - Return a random adjacent area in the given direction" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetIncomingConnections, "GetIncomingConnections", "( dir, table ) - Fills a passed in table with areas connected TO this area by a ONE-WAY link (ie: we have no connection back to them)" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptAddIncomingConnection, "AddIncomingConnection", "( area, dir ) - Add areas that connect TO this area by a ONE-WAY link" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetPlaceName, "GetPlaceName", "Get place name" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptSetPlaceName, "SetPlaceName", "( name ) - Set place name" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptComputeDirection, "ComputeDirection", "( point ) - Return direction from this area to the given point" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetPlayerCount, "GetPlayerCount", "( team ) - Return number of players of given team currently within this area (team of zero means any/all)" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptIsOverlapping, "IsOverlapping", "( area ) - Return true if 'area' overlaps our 2D extents" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptIsOverlappingOrigin, "IsOverlappingOrigin", "( pos, tolerance ) - Return true if 'pos' is within 2D extents of area" )
DEFINE_SCRIPTFUNC( IsPotentiallyVisibleToTeam, "( team ) - Return true if any portion of this area is visible to anyone on the given team" )
DEFINE_SCRIPTFUNC( IsCompletelyVisibleToTeam, "( team ) - Return true if given area is completely visible from somewhere in this area by someone on the team" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptIsEdge, "IsEdge", "( dir ) - Return true if there are no bi-directional links on the given side" )
DEFINE_SCRIPTFUNC( HasAvoidanceObstacle, "( maxheight ) - Returns true if there's a large, immobile object obstructing this area" )
DEFINE_SCRIPTFUNC( MarkObstacleToAvoid, "( height ) - Marks the obstructed status of the nav area" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptContains, "Contains", "( area ) - Return true if other area is on or above this area, but no others" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptContainsOrigin, "ContainsOrigin", "( point ) - Return true if given point is on or above this area, but no others" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetParent, "GetParent", "Returns the area just prior to this one in the search path" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetParentHow, "GetParentHow", "Returns how we get from parent to us" )
DEFINE_SCRIPTFUNC_NAMED( DrawFilled, "DebugDrawFilled", "Draw area as a filled rect of the given color" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptUnblockArea, "UnblockArea", "Unblocks this area" )
DEFINE_SCRIPTFUNC( IsRoughlySquare, "Return true if this area is approximately square" )
DEFINE_SCRIPTFUNC( IsFlat, "Return true if this area is approximately flat" )
DEFINE_SCRIPTFUNC( IsDegenerate, "Return true if this area is badly formed" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptIsVisible, "IsVisible", "( point ) - Return true if area is visible from the given eyepoint" )
DEFINE_SCRIPTFUNC( GetSizeX, "Return the area size along the X axis" )
DEFINE_SCRIPTFUNC( GetSizeY, "Return the area size along the Y axis" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetZ, "GetZ", "( pos ) - Return Z of area at (x,y) of 'pos'" )
DEFINE_SCRIPTFUNC( GetDistanceSquaredToPoint, "( pos ) - Return shortest distance between point and this area" )
DEFINE_SCRIPTFUNC( IsUnderwater, "Return true if area is underwater" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptIsCoplanar, "IsCoplanar", "( area ) - Return true if this area and given area are approximately co-planar" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptRemoveOrthogonalConnections, "RemoveOrthogonalConnections", "( dir ) - Removes all connections in directions to left and right of specified direction" )
DEFINE_SCRIPTFUNC( GetAvoidanceObstacleHeight, "Returns the maximum height of the obstruction above the ground" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetElevator, "GetElevator", "Returns the elevator if in an elevator's path" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetElevatorAreas, "GetElevatorAreas", "( table ) - Fills table with a collection of areas reachable via elevator from this area" )
//DEFINE_SCRIPTFUNC_NAMED( ScriptGetDoor, "GetDoor", "Returns the door entity above the area" )
//DEFINE_SCRIPTFUNC( IsBottleneck, "Returns true if area is a bottleneck" )
//DEFINE_SCRIPTFUNC( IsValidForWanderingPopulation, "Returns true if area is valid for wandering population" )
//DEFINE_SCRIPTFUNC( GetTravelDistanceToBombTarget, "Gets the travel distance to the MvM bomb target" )
//DEFINE_SCRIPTFUNC( IsReachableByTeam, "Is this area reachable by the given team?" )
//DEFINE_SCRIPTFUNC( IsTFMarked, "Is this nav area marked with the current marking scope?" )
//DEFINE_SCRIPTFUNC( TFMark, "Mark this nav area with the current marking scope." )
//DEFINE_SCRIPTFUNC_NAMED( ScriptComputeClosestPointInPortal, "ComputeClosestPointInPortal", "Compute closest point within the portal between to adjacent areas." )
END_SCRIPTDESC();

CZMRNavArea::CZMRNavArea()
{
    m_bHasNoFloor = false;
    m_hScriptInstance = NULL;
}

CZMRNavArea::~CZMRNavArea()
{
    if ( g_pScriptVM && m_hScriptInstance )
    {
        g_pScriptVM->RemoveInstance( m_hScriptInstance );
        m_hScriptInstance = NULL;
    }
}

void CZMRNavArea::ComputeHidingSpots()
{
}

void CZMRNavArea::ComputeSniperSpots()
{
}

void CZMRNavArea::ComputeSpotEncounters()
{
}

void CZMRNavArea::UpdateBlocked( bool force, int teamID )
{
    //if ( !force && m_BlockedTimer.IsElapsed() )
    //{
    //    return;
    //}

    // Transient areas are updated separately.
    if ( HasAttributes( NAV_MESH_TRANSIENT ) )
    {
        return;
    }


    BaseClass::UpdateBlocked( force, teamID );


    /*m_BlockedTimer.Start( 1.0f );

    CZMNavTransientFilter filter;
    Vector mins, maxs, center;


    GetBlockTraceBox( center, mins, maxs );

    trace_t tr;
        
    UTIL_TraceHull( center, center, mins, maxs, MASK_SOLID, &filter, &tr );


    bool bWasBlocked = IsBlocked( TEAM_ANY );
    bool bBlocked = tr.fraction != 1.0f || tr.startsolid || tr.m_pEnt;
    bool bChanged = bBlocked != bWasBlocked;




	if ( bChanged )
	{
        for ( int i = 0; i < 2; i++ )
        {
            m_isBlocked[i] = bBlocked;
        }

        auto* pEvent = gameeventmanager->CreateEvent( "nav_blocked" );
		if ( pEvent )
		{
			pEvent->SetInt( "area", GetID() );
			pEvent->SetInt( "blocked", bBlocked );
			gameeventmanager->FireEvent( pEvent );
		}

		if ( bBlocked )
		{
			TheNavMesh->OnAreaBlocked( this );
		}
		else
		{
			TheNavMesh->OnAreaUnblocked( this );
		}
	}

	if ( TheNavMesh->GetMarkedArea() == this )
	{
		if ( IsBlocked( teamID ) )
		{
			NDebugOverlay::Box( center, mins, maxs, 255, 0, 0, 64, 3.0f );
		}
		else
		{
			NDebugOverlay::Box( center, mins, maxs, 0, 255, 0, 64, 3.0f );
		}
	}*/
}

bool CZMRNavArea::IsBlocked( int teamID, bool ignoreNavBlockers ) const
{
    return BaseClass::IsBlocked( teamID, ignoreNavBlockers ) || m_bHasNoFloor;
}

void CZMRNavArea::GetWorldBounds( Vector& mins, Vector& maxs ) const
{
    Vector temp;
    mins = Vector( FLT_MAX, FLT_MAX, FLT_MAX );
    maxs = Vector( -FLT_MAX, -FLT_MAX, -FLT_MAX );

    // Find heights for proper bounds.
    for ( int j = 0; j < NUM_CORNERS; j++ )
    {
        temp = GetCorner( (NavCornerType)j );
        if ( temp.x < mins.x )
            mins.x = temp.x;
        if ( temp.y < mins.y )
            mins.y = temp.y;
        if ( temp.z < mins.z )
            mins.z = temp.z;

        if ( temp.x > maxs.x )
            maxs.x = temp.x;
        if ( temp.y > maxs.y )
            maxs.y = temp.y;
        if ( temp.z > maxs.z )
            maxs.z = temp.z;
    }
}

HSCRIPT CZMRNavArea::GetScriptInstance()
{
    if ( !m_hScriptInstance )
    {
        m_hScriptInstance = g_pScriptVM->RegisterInstance( GetScriptDesc(), this );
    }

    return m_hScriptInstance;
}
