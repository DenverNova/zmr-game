#pragma once


class CZMCharCircle
{
public:
    CZMCharCircle( const char* material, float size );
    ~CZMCharCircle();

    void Draw();

    void SetPos( const Vector& origin );
    void SetColor( float r, float g, float b );
    void SetAlpha( float a );
    void SetYaw( float yaw );
    
protected:
    float m_flSize;
    Vector m_vecOrigin;
    float m_flColor[4];
    IMaterial* m_pMaterial;
    float m_flYaw;
};
