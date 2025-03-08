#pragma once

enum SkyboxDirection_t
{
	SKYDIRECTION_FIRST = 0,

	SKYDIRECTION_FRONT = 0,
	SKYDIRECTION_LEFT,
	SKYDIRECTION_RIGHT,
	SKYDIRECTION_BACK,
	SKYDIRECTION_UP,
	SKYDIRECTION_DOWN,

	SKYDIRECTION_LAST = SKYDIRECTION_DOWN,
};

class CZMSkybox final
{
public:
	CZMSkybox();

    void Render( const Vector& viewPos, const Vector& forward, const float fov, const float farZ );
	void Update();

private:
    const char* GetSkyboxMaterialName();
    bool IsSkyboxDirty();
    IMaterial* GetSkyboxMaterial( SkyboxDirection_t dir, bool dirty );

	void RenderSkybox( const Vector& viewPos, const Vector& forward, const float fov, const float farZ );

    IMaterial* m_pSkyboxMats[SKYDIRECTION_LAST+1];
	SkyboxVisibility_t m_visibility;
	SkyboxVisibility_t m_previousValidVisibility;
	bool m_bDrawBlank;
	CMaterialReference m_TranslucentSingleColor;
	float m_flSolidAlpha;
};

extern CZMSkybox& GetZMSkybox();
