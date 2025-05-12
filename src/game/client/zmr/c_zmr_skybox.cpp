#include "cbase.h"
#include "view.h"

#include "c_zmr_skybox.h"
#include "c_zmr_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static const char* DirectionToStr( SkyboxDirection_t dir )
{
	switch ( dir )
	{
	case SKYDIRECTION_FRONT: return "ft";
	case SKYDIRECTION_LEFT: return "lf";
	case SKYDIRECTION_RIGHT: return "rt";
	case SKYDIRECTION_BACK: return "bk";
	case SKYDIRECTION_UP: return "up";
	case SKYDIRECTION_DOWN: return "dn";
	default:
	{
		Assert( 0 );
		return "error";
	}
	}
}

CZMSkybox::CZMSkybox() :
	m_bDrawBlank( false ),
	m_visibility( SKYBOX_3DSKYBOX_VISIBLE ),
	m_previousValidVisibility( SKYBOX_3DSKYBOX_VISIBLE ),
	m_pSkyboxMats{ nullptr },
	m_flSolidAlpha( 0.0f )
{
	m_TranslucentSingleColor.Init( "debug/debugtranslucentsinglecolor", TEXTURE_GROUP_OTHER );
}

const char* CZMSkybox::GetSkyboxMaterialName()
{
	static ConVarRef sv_skyname("sv_skyname");
	return sv_skyname.GetString();
}

bool CZMSkybox::IsSkyboxDirty()
{
	static char szPreviousSky[MAX_PATH] = {};
	const char* skyname = GetSkyboxMaterialName();

	if ( !szPreviousSky[0] || Q_strcmp( skyname, szPreviousSky ) != 0 )
	{
		Q_strncpy( szPreviousSky, skyname, sizeof( szPreviousSky ) );
		return true;
	}

	return false;
}

IMaterial* CZMSkybox::GetSkyboxMaterial( SkyboxDirection_t dir, bool dirty )
{
	auto* pMat = m_pSkyboxMats[dir];
	if ( dirty || !pMat )
	{
		if ( pMat )
		{
			pMat->DecrementReferenceCount();
			pMat = nullptr;
		}

		static char szFullName[MAX_PATH];
		const char* skyname = GetSkyboxMaterialName();

		Q_snprintf( szFullName, sizeof( szFullName ), "skybox/%s%s", skyname, DirectionToStr( dir ) );
		pMat = m_pSkyboxMats[dir] = materials->FindMaterial( szFullName, TEXTURE_GROUP_SKYBOX );

		if ( pMat )
		{
			pMat->IncrementReferenceCount();
			
			// If skybox does not have ignorez, it will break in certain rendering situations.
			// Eg. flashlight shadows will break. (zm_exodus_dawn)
			bool ignoreZ = pMat->GetMaterialVarFlag( MATERIAL_VAR_IGNOREZ );
			if ( !ignoreZ )
			{
				Warning( "Skybox material '%s' did not have $ignorez enabled! Bad mapper, bad!\n", szFullName );
				pMat->SetMaterialVarFlag( MATERIAL_VAR_IGNOREZ, true );
			}
		}
	}

	return pMat;
}

void CZMSkybox::Update()
{
	const Vector& viewPos = MainViewOrigin();

	bool bOutsideWorld = enginetrace->PointOutsideWorld( viewPos );

	if ( bOutsideWorld )
	{
		m_visibility = SKYBOX_NOT_VISIBLE;
	}
	else
	{
		m_visibility = engine->IsSkyboxVisibleFromPoint( viewPos );
	}
	

	m_bDrawBlank = false;
	if ( m_visibility == SKYBOX_NOT_VISIBLE )
	{
		// If we're ZMing / spectating / noclipping then we want something to be drawn.
		auto* pLocalPlayer = C_ZMPlayer::GetLocalPlayer();
		if ( pLocalPlayer && ( pLocalPlayer->GetTeamNumber() != ZMTEAM_HUMAN || pLocalPlayer->GetMoveType() == MOVETYPE_NOCLIP ) )
		{
			m_bDrawBlank = true;
		}
	}

	if ( m_previousValidVisibility != SKYBOX_NOT_VISIBLE )
	{
		m_flSolidAlpha = 0.0f;
	}
	else if ( m_visibility == SKYBOX_NOT_VISIBLE )
	{
		m_flSolidAlpha = 1.0f;
	}
	else
	{
		// ZMRTODO: Unused?
		if ( bOutsideWorld )
		{
			m_flSolidAlpha += gpGlobals->frametime * 1.0f;
			m_flSolidAlpha = min( m_flSolidAlpha, 1.0f );
		}
		else
		{
			m_flSolidAlpha -= gpGlobals->frametime * 0.5f;
			m_flSolidAlpha = max( m_flSolidAlpha, 0.0f );
		}
	}

	if ( !bOutsideWorld )
	{
		m_previousValidVisibility = m_visibility;
	}
}

void CZMSkybox::RenderSkybox( const Vector& viewPos, const Vector& forward, const float fov, const float farZ )
{

	bool dirty = IsSkyboxDirty();

	const Vector normals[] = {
		Vector( 0, 1, 0 ), // Front
		Vector( 1, 0, 0 ), // Left
		Vector( -1, 0, 0 ), // Right
		Vector( 0, -1, 0 ), // Back
		Vector( 0, 0, -1 ), // Top
		Vector( 0, 0, 1 ) // Down
	};

	const Vector points[][4] = {
		{ // Front
			Vector( -1, -1, -1 ),
			Vector( 1, -1, -1 ),
			Vector( 1, -1, 1 ),
			Vector( -1, -1, 1 )
        },
		{ // Left
			Vector( -1, 1, -1 ),
			Vector( -1, -1, -1 ),
			Vector( -1, -1, 1 ),
			Vector( -1, 1, 1 )
        },
		{ // Right
			Vector( 1, -1, -1 ),
			Vector( 1, 1, -1 ),
			Vector( 1, 1, 1 ),
			Vector( 1, -1, 1 )
		},
		{ // Back
			Vector( 1, 1, -1 ),
			Vector( -1, 1, -1 ),
			Vector( -1, 1, 1 ),
			Vector( 1, 1, 1 )
		},
		{ // Top
            Vector( 1, -1, 1 ),
            Vector( 1, 1, 1 ),
            Vector( -1, 1, 1 ),
			Vector( -1, -1, 1 )
        },
		{ // Bottom
			Vector( -1, -1, -1 ),
			Vector( -1, 1, -1 ),
			Vector( 1, 1, -1 ),
			Vector( 1, -1, -1 )
		}
	};

	const float texture_coords[4][2] = {
		{ 1, 1 },
		{ 0, 1 },
		{ 0, 0 },
		{ 1, 0 }
	};

	// Will clip to far z otherwise.
	const float distance = farZ * ( 1.0f / sqrtf( 3.0f ) );

	const float fovcheck = -cos( DEG2RAD( fov + 0.001f ) );
	for ( int i = SKYDIRECTION_FIRST; i <= SKYDIRECTION_LAST; i++ )
	{
		auto* pMaterial = GetSkyboxMaterial( (SkyboxDirection_t)i, dirty );
        if ( !pMaterial )
        {
            continue;
        }

		if ( forward.Dot( normals[i] ) > fovcheck )
        {
            continue;
        }


		CMatRenderContextPtr pRenderContext( materials );

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true, nullptr, nullptr, pMaterial );
		CMeshBuilder meshBuilder;

		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		for ( int j = 0; j < 4; j++ )
		{
			Vector pos = viewPos + points[i][j] * distance;
			meshBuilder.Position3fv( pos.Base() );
			meshBuilder.TexCoord2fv( 0, texture_coords[j] );
			meshBuilder.AdvanceVertex();
		}

		meshBuilder.End();
		pMesh->Draw();
	}
}

void CZMSkybox::Render( const Vector& viewPos, const Vector& forward, const float fov, const float farZ )
{
	if ( !m_bDrawBlank && m_visibility == SKYBOX_NOT_VISIBLE )
	{
		return;
	}

	if ( m_flSolidAlpha < 1.0f )
	{
		RenderSkybox( viewPos, forward, fov, farZ );
	}

	if ( m_flSolidAlpha > 0.0f )
	{
		byte color[4] = { 0 };
		color[3] = m_flSolidAlpha * 255.0f;

		CMatRenderContextPtr pRenderContext( materials );

		// ZMRTODO: Figure out if this is save to do with cameras, etc?
		MaterialFogMode_t prevMode = pRenderContext->GetFogMode();
		pRenderContext->FogMode( MATERIAL_FOG_NONE );
		render->ViewDrawFade( color, m_TranslucentSingleColor );
		pRenderContext->FogMode( prevMode );
	}
}

CZMSkybox& GetZMSkybox()
{
    static CZMSkybox s_ZMSkybox;
    return s_ZMSkybox;
}
