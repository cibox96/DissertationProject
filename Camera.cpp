//--------------------------------------------------------------------------------------
//	Camera.cpp
//
//	The camera class encapsulates the camera's view and projection matrix
//--------------------------------------------------------------------------------------

#include "Camera.h"  // Declaration of this class

///////////////////////////////
// Constructors / Destructors

// Constructor - initialise all camera settings - look at the constructor declaration in the header file to see that there are defaults provided for everything
CCamera::CCamera( D3DXVECTOR3 position, D3DXVECTOR3 rotation, float fov, float nearClip, float farClip )
{
	m_Position = position;
	m_Rotation = rotation;
	m_Aspect = 1.333f; // Shouldn't be hard-coded (viewport width / viewport height)

	SetFOV( fov );
	SetNearClip( nearClip );
	SetFarClip( farClip );

	UpdateMatrices();
}


///////////////////////////////
// Camera Usage

// Update the matrices used for the camera in the rendering pipeline. Treat the camera like a model and create a world matrix for it. Then convert that into
// the view matrix that the rendering pipeline actually uses. Also create the projection matrix
void CCamera::UpdateMatrices()
{
	// Make matrices for position and rotations, then multiply together to get a "camera world matrix"
	D3DXMATRIXA16 matrixXRot, matrixYRot, matrixZRot, matrixTranslation;
	D3DXMatrixRotationX( &matrixXRot, m_Rotation.x );
	D3DXMatrixRotationY( &matrixYRot, m_Rotation.y );
	D3DXMatrixRotationZ( &matrixZRot, m_Rotation.z );
	D3DXMatrixTranslation( &matrixTranslation, m_Position.x, m_Position.y, m_Position.z);
	m_WorldMatrix = matrixZRot * matrixXRot * matrixYRot * matrixTranslation;

	// The rendering pipeline actually needs the inverse of the camera world matrix - called the view matrix. Creating an inverse is easy with DirectX:
	D3DXMatrixInverse( &m_ViewMatrix, NULL, &m_WorldMatrix );

	// Initialize the projection matrix. This determines viewing properties of the camera such as field of view (FOV) and near clip distance
	// One other factor in the projection matrix is the aspect ratio of screen (width/height) - used to adjust FOV between horizontal and vertical
	float aspect = (float)g_ViewportWidth / g_ViewportHeight; 
	D3DXMatrixPerspectiveFovLH( &m_ProjMatrix, m_FOV, m_Aspect, m_NearClip, m_FarClip );

	// Combine the view and projection matrix into a single matrix - which can (optionally) be used in the vertex shaders to save one matrix multiply per vertex
	m_ViewProjMatrix = m_ViewMatrix * m_ProjMatrix;
}


////////////////////////////////////////////
// Camera matrix access / stereo offsets

// Get position of the camera. If a left or right stereo view is selected, then adjust position returned accordingly
D3DXVECTOR3 CCamera::GetPosition( EStereoscopic stereo, float interocular )
{
	if (stereo == Monoscopic) return m_Position;

	// Offset camera by half interocular distance left or right as appropriate
	float offset = interocular * (stereo == StereoscopicLeft ? -0.5f : 0.5f);
	
	D3DXVECTOR3 position = m_Position;
	position.x += m_WorldMatrix._11 * offset;
	position.y += m_WorldMatrix._12 * offset;
	position.z += m_WorldMatrix._13 * offset;

	return position;
}

// Get effective "world" matrix of camera, which is the inverse of the more commonly used View matrix
// If a left or right stereo view is selected, then adjust position in matrix accordingly
D3DXMATRIX CCamera::GetWorldMatrix( EStereoscopic stereo, float interocular )
{
	// Not stereoscopic
	if (stereo == Monoscopic) return m_WorldMatrix;

	// Offset camera by half interocular distance left or right as appropriate
	float offset = interocular * (stereo == StereoscopicLeft ? -0.5f : 0.5f);

	D3DXMATRIXA16 worldMatrix = m_WorldMatrix;
	worldMatrix._41 += worldMatrix._11 * offset;
	worldMatrix._42 += worldMatrix._12 * offset;
	worldMatrix._43 += worldMatrix._13 * offset;
	return worldMatrix;
}

// Get view matrix of camera. If a left or right stereo view is selected, then adjust position in matrix accordingly
D3DXMATRIX CCamera::GetViewMatrix( EStereoscopic stereo, float interocular )
{
	// Not stereoscopic
	if (stereo == Monoscopic) return m_ViewMatrix;

	// Offset camera by half interocular distance left or right as appropriate
	float offset = interocular * (stereo == StereoscopicLeft ? -0.5f : 0.5f);

	D3DXMATRIXA16 viewMatrix;
	D3DXMATRIXA16 worldMatrix = m_WorldMatrix;
	worldMatrix._41 += worldMatrix._11 * offset;
	worldMatrix._42 += worldMatrix._12 * offset;
	worldMatrix._43 += worldMatrix._13 * offset;
	D3DXMatrixInverse( &viewMatrix, NULL, &worldMatrix );
	return viewMatrix;
}

// Get projection matrix of camera. If a left or right stereo view is selected, then adjust matrix as discussed in lecture
D3DXMATRIX CCamera::GetProjectionMatrix( EStereoscopic stereo, float interocular, float screenDistance )
{
	// Not stereoscopic
	if (stereo == Monoscopic) return m_ProjMatrix;

	// Offset camera by half interocular distance left or right as appropriate
	float offset = interocular * (stereo == StereoscopicLeft ? -0.5f : 0.5f);

	// Offset viewing frustum based on camera offset
	D3DXMATRIXA16 projMatrix = m_ProjMatrix;
	projMatrix._31 = (offset / screenDistance) / (m_Aspect * tanf(m_FOV/2));

	return projMatrix;
}


// Get projection matrix of camera. If a left or right stereo view is selected, then adjust matrix as discussed in lecture
D3DXMATRIX CCamera::GetViewProjectionMatrix( EStereoscopic stereo, float interocular, float screenDistance )
{
	// Not stereoscopic
	if (stereo == Monoscopic) return m_ViewProjMatrix;

	return GetViewMatrix( stereo, interocular ) * GetProjectionMatrix( stereo, interocular, screenDistance );
}


// Control the camera's position and rotation using keys provided. Amount of motion performed depends on frame time
void CCamera::Control( float frameTime, EKeyCode turnUp, EKeyCode turnDown, EKeyCode turnLeft, EKeyCode turnRight,  
                       EKeyCode moveForward, EKeyCode moveBackward, EKeyCode moveLeft, EKeyCode moveRight)
{
	if (KeyHeld( turnDown ))
	{
		m_Rotation.x += RotSpeed * frameTime;
	}
	if (KeyHeld( turnUp ))
	{
		m_Rotation.x -= RotSpeed * frameTime;
	}
	if (KeyHeld( turnRight ))
	{
		m_Rotation.y += RotSpeed * frameTime;
	}
	if (KeyHeld( turnLeft ))
	{
		m_Rotation.y -= RotSpeed * frameTime;
	}

	// Local X movement - move in the direction of the X axis, get axis from camera's "world" matrix
	if (KeyHeld( moveRight ))
	{
		m_Position.x += m_WorldMatrix._11 * MoveSpeed * frameTime;
		m_Position.y += m_WorldMatrix._12 * MoveSpeed * frameTime;
		m_Position.z += m_WorldMatrix._13 * MoveSpeed * frameTime;
	}
	if (KeyHeld( moveLeft ))
	{
		m_Position.x -= m_WorldMatrix._11 * MoveSpeed * frameTime;
		m_Position.y -= m_WorldMatrix._12 * MoveSpeed * frameTime;
		m_Position.z -= m_WorldMatrix._13 * MoveSpeed * frameTime;
	}

	// Local Z movement - move in the direction of the Z axis, get axis from view matrix
	if (KeyHeld( moveForward ))
	{
		m_Position.x += m_WorldMatrix._31 * MoveSpeed * frameTime;
		m_Position.y += m_WorldMatrix._32 * MoveSpeed * frameTime;
		m_Position.z += m_WorldMatrix._33 * MoveSpeed * frameTime;
	}
	if (KeyHeld( moveBackward ))
	{
		m_Position.x -= m_WorldMatrix._31 * MoveSpeed * frameTime;
		m_Position.y -= m_WorldMatrix._32 * MoveSpeed * frameTime;
		m_Position.z -= m_WorldMatrix._33 * MoveSpeed * frameTime;
	}
}
