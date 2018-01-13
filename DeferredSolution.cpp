//--------------------------------------------------------------------------------------
//	Deferred.cpp
//
//	Deferred rendering
//--------------------------------------------------------------------------------------

#include <sstream>
#include <string>
#include <list>
#include <fstream>
using namespace std;

// General definitions used across all the project source files
#include "Defines.h"

// Declarations for supporting source files
#include "Mesh.h" 
#include "Camera.h"
#include "CTimer.h"
#include "Input.h"
#include "CVector4.h"
#include "MathDX.h"

#include "Resource.h" // Resource file (used to add icon for application)


//--------------------------------------------------------------------------------------
// Scene Data
//--------------------------------------------------------------------------------------

bool Deferred = true; // Forward or deferred rendering. Can toggle with backspace, but performance is erratic after that...?
	 
// Meshes and cameras
CMesh* Skybox;
CMesh* Level;
CCamera* MainCamera;

// Textures, meshes contain their own textures, only needed for custom rendering
ID3D11ShaderResourceView* LightDiffuseMap = NULL;

// Note: There are move & rotation speed constants in Defines.h


//--------------------------------------------------------------------------------------
// Lights
//--------------------------------------------------------------------------------------

// Global light data
D3DXVECTOR3 AmbientColour    = D3DXVECTOR3( 0.1f, 0.1f, 0.15f );

// Structure for a single point light
struct SPointLight
{
	CVector3 position;
	float    radius;
	CVector4 colour;
};

// A list of light structures will sent as a vertex buffer into the shaders for deferred rendering - need a "vertex layout" for this.
// This is the same as the GPU particle and Soft particle labs. Rendering the lights as a list on the GPU is more effecient as we've seen
// with particle systems, but it is not a requirement of deferred rendering.
D3D11_INPUT_ELEMENT_DESC LightVertexElts[] =
{
	// Semantic   Index  Format                          Slot  Offset  Slot Class                    Instance Step
	{ "POSITION", 0,     DXGI_FORMAT_R32G32B32_FLOAT,    0,    0,      D3D11_INPUT_PER_VERTEX_DATA,  0 }, 
    { "TEXCOORD", 0,     DXGI_FORMAT_R32_FLOAT,          0,    12,     D3D11_INPUT_PER_VERTEX_DATA,  0 }, // Non-standard data (alpha, scale) passed as texture coordinates
    { "COLOR",    0,     DXGI_FORMAT_R32G32B32_FLOAT,    0,    16,     D3D11_INPUT_PER_VERTEX_DATA,  0 }, 
};
UINT NumLightElts = sizeof( LightVertexElts ) / sizeof( LightVertexElts[0] ); // Length of array above
ID3D11InputLayout* LightVertexLayout; // Layout pointer that we will get from DirectX after we give it the array above

// Lights are a particle system
int NumPointLights = 1;              // Start with one big light
const float LightSpawnFreq = 5000.0f; // How many new lights per second
const int MaxPointLights = 25600;     // Will keep adding lights until there are this many

// Array of lights, one initialised to start with
SPointLight PointLights[MaxPointLights] = {
	CVector3( -18000, 4000, 6000),  25000,  CVector4(0.4f, 0.4f, 0.7f, 0),
};

// Vertex buffer in GPU memory, a copy of the PointLights array above
ID3D11Buffer* LightVertexBuffer;


//**| DEFERRED |**********************************************************/

// The G-Buffer will store pre-lighting data about each pixel in the scene, e.g. normal, diffuse colour, etc.
// There is no rule about what makes up a G-Buffer, there are many choices and trade-offs. There is a performance
// advantage to store the minimum of data, but storing more data gives more flexibility to the renderer
// For this tutorial, we will keep it extremely simple, and ignore performance issues:
//   GBuffer is three 8-bit RGBA textures:
//   1. Pixel diffuse colour in RGB, specular strength in Alpha (before lighting, i.e. the basic colour and shininess of that pixel)
//   2. Pixel world normal in RGB, Alpha unused
//   3. Pixel world position in RGB, Alpha unused
// In the first rendering pass, all the scene geometry is rendered to these three textures, *simultaneously*
ID3D11Texture2D*          GBuffer[3];
ID3D11RenderTargetView*   GBufferRenderTarget[3];
ID3D11ShaderResourceView* GBufferShaderResource[3];
ID3DX11EffectShaderResourceVariable* GBufferShaderVar[3];

//************************************************************************/


//--------------------------------------------------------------------------------------
// Shader Variables
//--------------------------------------------------------------------------------------
// Variables to connect C++ code to HLSL shaders

// Effects / techniques
ID3DX11Effect*          Effect = NULL;
ID3DX11EffectTechnique* PixelLitTexTechnique = NULL;
ID3DX11EffectTechnique* LightParticlesTechnique = NULL;
ID3DX11EffectTechnique* GBufferTechnique = NULL;
ID3DX11EffectTechnique* PointLightTechnique = NULL;
ID3DX11EffectTechnique* AmbientLightTechnique = NULL;

// Matrices
ID3DX11EffectMatrixVariable* WorldMatrixVar = NULL;
ID3DX11EffectMatrixVariable* ViewMatrixVar = NULL;
ID3DX11EffectMatrixVariable* InvViewMatrixVar = NULL;
ID3DX11EffectMatrixVariable* ProjMatrixVar = NULL;
ID3DX11EffectMatrixVariable* ViewProjMatrixVar = NULL;

// Dimensions of the viewport
ID3DX11EffectScalarVariable* ViewportWidthVar = NULL; 
ID3DX11EffectScalarVariable* ViewportHeightVar = NULL;

// Textures
ID3DX11EffectShaderResourceVariable* DiffuseMapVar = NULL;
ID3DX11EffectShaderResourceVariable* NormalHeightMapVar = NULL;

// Light variables
ID3DX11EffectScalarVariable* NumPointLightsVar = NULL;
ID3DX11EffectVariable*       PointLightsVar = NULL;
ID3DX11EffectVectorVariable* CameraPosVar = NULL;
ID3DX11EffectScalarVariable* CameraNearClipVar = NULL;
ID3DX11EffectVectorVariable* AmbientColourVar = NULL;


//--------------------------------------------------------------------------------------
// DirectX Variables
//--------------------------------------------------------------------------------------

// The main D3D interface
ID3D11Device*        g_pd3dDevice = NULL;  // The main device pointer has been split into two, one for the graphics device itself...
ID3D11DeviceContext* g_pd3dContext = NULL; // ...and one pointer for the current rendering thread of execution - allows multithreaded rendering

// Variables used to setup D3D
IDXGISwapChain*           SwapChain = NULL;
ID3D11Texture2D*          DepthStencil = NULL;
ID3D11DepthStencilView*   DepthStencilView = NULL;
ID3D11ShaderResourceView* DepthShaderView;
ID3D11RenderTargetView*   BackBufferRenderTarget = NULL;

// Variables used to setup the Window
HINSTANCE HInst = NULL;
HWND      HWnd = NULL;
int       g_ViewportWidth;
int       g_ViewportHeight;


// Amount of time to pass before calculating new average update time
const float FrameTimePeriod = 1.0f;

// Sum of recent update times and number of times in the sum - used to calculate
// average over a given time period
float SumFrameTimes = 0.0f;
int NumFrameTimes = 0;
float AverageFrameTime = -1.0f; // Invalid value at first


//--------------------------------------------------------------------------------------
// Forward declarations
//--------------------------------------------------------------------------------------

bool InitDevice();
void ReleaseResources();
bool LoadEffectFile();
bool InitScene();
void UpdateScene( float frameTime );
void RenderOpaqueModels();
void RenderTransparentModels();
void RenderScene();
bool InitWindow( HINSTANCE hInstance, int nCmdShow );
LRESULT CALLBACK WndProc( HWND, UINT, WPARAM, LPARAM );



//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
bool InitDevice()
{
	HRESULT hr = S_OK;


	////////////////////////////////
	// Initialise Direct3D

	// Calculate the visible area the window we are using - the "client rectangle" refered to in the first function is the 
	// size of the interior of the window, i.e. excluding the frame and title
	RECT rc;
	GetClientRect( HWnd, &rc );
	g_ViewportWidth = rc.right - rc.left;
	g_ViewportHeight = rc.bottom - rc.top;


	// Create a Direct3D device and create a swap-chain (create a back buffer to render to)
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );
	sd.BufferCount = 1;
	sd.BufferDesc.Width = g_ViewportWidth;             // Target window size
	sd.BufferDesc.Height = g_ViewportHeight;           // --"--
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Pixel format of target window
	sd.BufferDesc.RefreshRate.Numerator = 60;          // Refresh rate of monitor
	sd.BufferDesc.RefreshRate.Denominator = 1;         // --"--
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.OutputWindow = HWnd;                            // Target window
	sd.Windowed = TRUE;                                // Whether to render in a window (TRUE) or go fullscreen (FALSE)
	hr = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, 0, /*D3D11_CREATE_DEVICE_DEBUG*/0, 0, 0, D3D11_SDK_VERSION, &sd, &SwapChain, &g_pd3dDevice, NULL, &g_pd3dContext ); //D3D11_CREATE_DEVICE_DEBUG
	if( FAILED( hr ) ) return false;

	/// Create the render target view, a pointer that allows use the back buffer as a render target
	ID3D11Texture2D* pBackBuffer;
	hr = SwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), ( LPVOID* )&pBackBuffer );
	if( FAILED( hr ) ) return false;
	hr = g_pd3dDevice->CreateRenderTargetView( pBackBuffer, NULL, &BackBufferRenderTarget );
	pBackBuffer->Release();
	if( FAILED( hr ) ) return false;

	// Create a texture for a depth buffer
	D3D11_TEXTURE2D_DESC descDepth;
	descDepth.Width = g_ViewportWidth;
	descDepth.Height = g_ViewportHeight;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_R32_TYPELESS;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = g_pd3dDevice->CreateTexture2D( &descDepth, NULL, &DepthStencil );
	if( FAILED( hr ) ) return false;

	// Create the depth stencil view, a pointer that allows us to use the above texture as a depth buffer
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
	descDSV.Format = DXGI_FORMAT_D32_FLOAT;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Flags = 0;
	descDSV.Texture2D.MipSlice = 0;
	hr = g_pd3dDevice->CreateDepthStencilView( DepthStencil, &descDSV, &DepthStencilView );
	if( FAILED( hr ) ) return false;


	//**| DEFERRED SETUP |****************************************************/

	// Create three textures, which together form the G-Buffer. Each is the same size as the back buffer
	// The G-Buffer will store data about each pixel in the scene prior to lighting, e.g. normal, diffuse colour, etc.
	// There are many choices and trade-offs regarding the contents of a G-Buffer. There is a performance advantage to
	// store the minimum of data, but storing more data gives more flexibility to the renderer.
	// This tutorial keeps it extremely simple and ignores performance issues:
	//   G-Buffer is three 8-bit RGBA textures:
	//   1. Pixel diffuse colour in RGB, specular strength in Alpha (this is before lighting, i.e. the basic colour and shininess of that pixel)
	//   2. Pixel world normal in RGB, Alpha unused
	//   3. Pixel world position in RGB, Alpha unused
	// In the first rendering pass, all the scene geometry is rendered to these three textures, *simultaneously*

	descDepth.Width = g_ViewportWidth;
	descDepth.Height = g_ViewportHeight;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	for (int b = 0; b < 3; b++)
	{
		// Create the texture itself (reserve GPU memory)
		hr = g_pd3dDevice->CreateTexture2D( &descDepth, NULL, &GBuffer[b] );
		if( FAILED( hr ) ) return false;

		// Create the render target view, a pointer that allows us to render to the GBuffer textures (first rendering pass)
		hr = g_pd3dDevice->CreateRenderTargetView( GBuffer[b], NULL, &GBufferRenderTarget[b] );
		if( FAILED( hr ) ) return false;

		// Create the shadeer resource view, a pointer that allow us to read from the GBuffer textures in a shader (second rendering pass)
		hr = g_pd3dDevice->CreateShaderResourceView( GBuffer[b], NULL, &GBufferShaderResource[b] );
		if( FAILED( hr ) ) return false;
	}

	//************************************************************************/

	return true;
}


// Release the memory held by all objects created
void ReleaseResources()
{
	if( g_pd3dContext ) g_pd3dContext->ClearState();

	delete Level;
	delete Skybox;
	delete MainCamera;

	if (LightVertexBuffer)      LightVertexBuffer->Release();
	if (LightDiffuseMap)        LightDiffuseMap->Release();
    if (Effect)                 Effect->Release();
	if (DepthShaderView)        DepthShaderView->Release();
	if (DepthStencilView)       DepthStencilView->Release();
	if (BackBufferRenderTarget) BackBufferRenderTarget->Release();
	if (DepthStencil)           DepthStencil->Release();
	if (SwapChain)              SwapChain->Release();
	if (g_pd3dDevice)           g_pd3dDevice->Release();
}



//--------------------------------------------------------------------------------------
// Load and compile Effect file (.fx file containing shaders)
//--------------------------------------------------------------------------------------

// All techniques in one file in this lab
bool LoadEffectFile()
{

	ID3DBlob* pCompiled; // This strangely typed variable collects the compiled shader code (not ready for use as an effect yet, see next code)
	ID3DBlob* pErrors;   // Variable to collect error messages
	DWORD dwShaderFlags = D3D10_SHADER_ENABLE_STRICTNESS; // These "flags" are used to set the compiler options
	HRESULT hr = D3DX11CompileFromFile( L"Deferred.fx", NULL, NULL, NULL, "fx_5_0", dwShaderFlags, 0, NULL, &pCompiled, &pErrors, NULL );
	if( FAILED( hr ) )
	{
		if (pErrors != 0)  MessageBox( NULL, CA2CT(reinterpret_cast<char*>(pErrors->GetBufferPointer())), L"Error", MB_OK ); // Compiler error: display error message
		else               MessageBox( NULL, L"Error loading FX file. Ensure your FX file is in the same folder as this executable.", L"Error", MB_OK );  // No error message - probably file not found
		return false;
	}


	// Load and compile the effect file
	hr = D3DX11CreateEffectFromMemory( pCompiled->GetBufferPointer(), pCompiled->GetBufferSize(), 0, g_pd3dDevice, &Effect );
	if( FAILED( hr ) )
	{
		MessageBox( NULL, L"Error creating effects", L"Error", MB_OK );
		return false;
	}

	// Select techniques from the compiled effect file
	PixelLitTexTechnique    = Effect->GetTechniqueByName( "PixelLitTex" );
	LightParticlesTechnique = Effect->GetTechniqueByName( "LightParticles" );
	GBufferTechnique        = Effect->GetTechniqueByName( "GBuffer" );
	AmbientLightTechnique   = Effect->GetTechniqueByName( "AmbientLight" );
	PointLightTechnique     = Effect->GetTechniqueByName( "PointLight" );

	// Create variables to access global variables in the shaders from C++
	WorldMatrixVar      = Effect->GetVariableByName( "WorldMatrix"    )->AsMatrix();
	ViewMatrixVar       = Effect->GetVariableByName( "ViewMatrix"     )->AsMatrix();
	InvViewMatrixVar    = Effect->GetVariableByName( "InvViewMatrix"  )->AsMatrix();
	ProjMatrixVar       = Effect->GetVariableByName( "ProjMatrix"     )->AsMatrix();
	ViewProjMatrixVar   = Effect->GetVariableByName( "ViewProjMatrix" )->AsMatrix();
	

	// Textures in shader (shader resources)
	DiffuseMapVar      = Effect->GetVariableByName( "DiffuseMap" )->AsShaderResource();
	NormalHeightMapVar = Effect->GetVariableByName( "NormalHeightMap" )->AsShaderResource();

	// G-buffer used as textures in shader for lighting pass
	GBufferShaderVar[0] = Effect->GetVariableByName( "GBuff_DiffuseSpecular" )->AsShaderResource();
	GBufferShaderVar[1] = Effect->GetVariableByName( "GBuff_WorldPosition" )->AsShaderResource();
	GBufferShaderVar[2] = Effect->GetVariableByName( "GBuff_WorldNormal" )->AsShaderResource();

	// Viewport dimensions
	ViewportWidthVar  = Effect->GetVariableByName( "ViewportWidth"  )->AsScalar();
	ViewportHeightVar = Effect->GetVariableByName( "ViewportHeight" )->AsScalar();

	// Also access shader variables needed for lighting
	NumPointLightsVar = Effect->GetVariableByName( "NumPointLights" )->AsScalar();
	PointLightsVar    = Effect->GetVariableByName( "PointLights"    );
	CameraPosVar      = Effect->GetVariableByName( "CameraPos"      )->AsVector();
	CameraNearClipVar = Effect->GetVariableByName( "CameraNearClip" )->AsScalar();
	AmbientColourVar  = Effect->GetVariableByName( "AmbientColour"  )->AsVector();

	return true;
}



//--------------------------------------------------------------------------------------
// Scene Setup / Update
//--------------------------------------------------------------------------------------

// Create / load the camera, models and textures for the scene
bool InitScene()
{
	///////////////////
	// Create cameras

	MainCamera = new CCamera();
	MainCamera->SetPosition( D3DXVECTOR3(-320, 70, 100) );
	MainCamera->SetRotation( D3DXVECTOR3(ToRadians(8.0f), ToRadians(115.0f), 0.0f) ); // ToRadians is a new helper function to convert degrees to radians

	// Create models
	Skybox  = new CMesh;
	Level = new CMesh;

	// Load .X files for each model
	if (!Level-> Load( "level2.x", PixelLitTexTechnique )) return false; // Note: don't need to change the "example" technique for deferred rendering...
	if (!Skybox->Load( "Stars.x",  PixelLitTexTechnique )) return false; //... technique are the same

	// Initial positions
	Skybox->Matrix().SetScale( 10000.0f );
	Skybox->GetNode(1).positionMatrix.SetScale( 10000.0f );
	Skybox->GetNode(2).positionMatrix.SetScale( 10000.0f );


	//////////////////
	// Lights

	// Create a vertex buffer for the lights in GPU memory and copy over the contents just created (from CPU-memory)
	// We are going to update this vertex buffer every frame, so it must be defined as "dynamic" and writable (D3D11_USAGE_DYNAMIC & D3D11_CPU_ACCESS_WRITE)
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = MaxPointLights * sizeof(SPointLight); // Buffer size
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA initData; // Initial data
	initData.pSysMem = PointLights;   
	if (FAILED( g_pd3dDevice->CreateBuffer( &bufferDesc, &initData, &LightVertexBuffer )))
	{
		return false;
	}

	// Create the vertex layout - to indicate to DirectX what data is contained in each vertex - see extended comment near LightVertexElts definition
	D3DX11_PASS_DESC PassDesc;
	PointLightTechnique->GetPassByIndex( 0 )->GetDesc( &PassDesc );
	g_pd3dDevice->CreateInputLayout( LightVertexElts, NumLightElts, PassDesc.pIAInputSignature, PassDesc.IAInputSignatureSize, &LightVertexLayout );



	//////////////////
	// Load textures

	if (FAILED( D3DX11CreateShaderResourceViewFromFile( g_pd3dDevice, L"flare.jpg", NULL, NULL, &LightDiffuseMap, NULL ) )) return false;

	return true;
}


//--------------------------------------------------------------------------------------
// Scene Update
//--------------------------------------------------------------------------------------

// Update the scene - move/rotate each model and the camera, then update their matrices
void UpdateScene( float frameTime )
{
	// Control camera position and update its matrices (monoscopic version)
	MainCamera->Control( frameTime, Key_Up, Key_Down, Key_Left, Key_Right, Key_W, Key_S, Key_A, Key_D );
	MainCamera->UpdateMatrices();

	// Gradually create lots more lights
	static float emit = 1.0f / LightSpawnFreq;
	emit -= frameTime;
	while (emit < 0)
	{
		if (NumPointLights < MaxPointLights)
		{
			PointLights[NumPointLights].position = CVector3(Random(-600.0f,600.0f), Random(5.0f,40.0f), Random(-600.0f,600.0f));
			PointLights[NumPointLights].radius   = Random(20.0f,40.0f);
			PointLights[NumPointLights].colour   = CVector4(Random(0.4f,1.0f), Random(0.4f,1.0f), Random(0.4f,1.0f), 0);
			NumPointLights++;
		}
		emit += 1.0f / LightSpawnFreq;
	}

	// Rotate all lights (except the first) around the origin in an interesting way
	for (int i = 1; i < NumPointLights; i++)
	{
		float dist = PointLights[i].position.Length();
		float rotateSpeed = (fmodf(dist, 1.0f) + -0.5f) * 200.0f / (dist + 0.1f);
		PointLights[i].position = MatrixRotationY(rotateSpeed*frameTime).TransformVector(PointLights[i].position);
	}

	// Copy all light data over to GPU every frame
	D3D11_MAPPED_SUBRESOURCE mappedData;
    g_pd3dContext->Map( LightVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData );
	CopyMemory( mappedData.pData, PointLights, NumPointLights * sizeof(SPointLight) );
    g_pd3dContext->Unmap( LightVertexBuffer, 0 );

	// Toggle deferred rendering
	if (KeyHit(Key_Back)) Deferred = !Deferred;


	// Accumulate update times to calculate the average over a given period
	SumFrameTimes += frameTime;
	++NumFrameTimes;
	if (SumFrameTimes >= FrameTimePeriod)
	{
		AverageFrameTime = SumFrameTimes / NumFrameTimes;
		SumFrameTimes = 0.0f;
		NumFrameTimes = 0;
	}

	// Write FPS text string
	stringstream outText;
	outText << (Deferred ? "Deferred Rendering - " : "Forward Rendering - ");
	outText << "Lights: " << NumPointLights;
	if (AverageFrameTime >= 0.0f)
	{
		outText << ", Frame Time: " << AverageFrameTime * 1000.0f << "ms, FPS:" << 1.0f / AverageFrameTime;
		SetWindowText( HWnd, CA2CT(outText.str().c_str()) );
		outText.str("");
	}

}


//--------------------------------------------------------------------------------------
// Scene Rendering
//--------------------------------------------------------------------------------------

// Render everything in the scene
void RenderScene()
{
	//---------------------------
	// Common rendering settings

	// Pass the camera's matrices to the vertex shader and position to the vertex shader
	ViewMatrixVar->SetMatrix( (float*)&MainCamera->GetViewMatrix() );
	InvViewMatrixVar->SetMatrix( (float*)&MainCamera->GetWorldMatrix() );
	ProjMatrixVar->SetMatrix( (float*)&MainCamera->GetProjectionMatrix() );
	ViewProjMatrixVar->SetMatrix( (float*)&MainCamera->GetViewProjectionMatrix() );
	CameraPosVar->SetRawValue( MainCamera->GetPosition(), 0, 12 );
	CameraNearClipVar->SetFloat( MainCamera->GetNearClip() );

	// Pass global light data to the shaders for both rendering methods
	AmbientColourVar->SetRawValue( AmbientColour, 0, 12 );

	// Setup the viewport - defines which part of the back-buffer we will render to (usually all of it)
	D3D11_VIEWPORT vp;
	vp.Width  = (FLOAT)g_ViewportWidth;
	vp.Height = (FLOAT)g_ViewportHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pd3dContext->RSSetViewports( 1, &vp );
	ViewportWidthVar->SetFloat( static_cast<float>(g_ViewportWidth) );
	ViewportHeightVar->SetFloat( static_cast<float>(g_ViewportHeight) );


	//---------------------------
	// Render scene

	// Clear depth buffer
	g_pd3dContext->ClearDepthStencilView( DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0 );

	// Although there are various preparations made for both forward and deferred rendering, this if statement shows the essential
	// difference between the techniques on the C++ side. Of course the shaders are quite different too.
	if (!Deferred)
	{
		// Forward rendering - set back buffer as render target as usual
		g_pd3dContext->OMSetRenderTargets( 1, &BackBufferRenderTarget, DepthStencilView );

		// Pass light list to the vertex shader
		NumPointLightsVar->SetInt( NumPointLights );
		PointLightsVar->SetRawValue( PointLights, 0, NumPointLights * sizeof(SPointLight) );

		// Render all non-transparent models using pixel lighting
		Level->Render( PixelLitTexTechnique );
	}
	else
	{
		//**| DEFERRED RENDERING |****************************************************/

		//GBufferRenderTarget[2] = BackBufferRenderTarget; // Temporary line to show content of a particular g-buffer (also comment out the Draw(4,0) below)

		// Deferred rendering - set the three g-buffer render targets (see comment by declaration of GBuffer)
		g_pd3dContext->OMSetRenderTargets( 3, GBufferRenderTarget, DepthStencilView );

		// Render non-transparent objects to the g-buffer. This also renders scene depths into the depth buffer (in the usual way), used by the later passes
		Level->Render( GBufferTechnique );

		// Now select the g-buffer as texture inputs for the next rendering stages
		g_pd3dContext->OMSetRenderTargets( 1, &BackBufferRenderTarget, DepthStencilView );
		GBufferShaderVar[0]->SetResource( GBufferShaderResource[0] );
		GBufferShaderVar[1]->SetResource( GBufferShaderResource[1] );
		GBufferShaderVar[2]->SetResource( GBufferShaderResource[2] );

		// Render ambient light as a full-screen quad. Copies the diffuse-colour part of the g-buffer, blends it 
		// with the ambient colour and writes that out to the back buffer to gives a basic rendering of the scene
		g_pd3dContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ); // Special vertex shader generates a triangle strip to make a quad, no vertex data is needed
		AmbientLightTechnique->GetPassByIndex( 0 )->Apply( 0, g_pd3dContext );
		g_pd3dContext->Draw( 4, 0 );

		// Render areas affected by the point lights. The lights are sent over as a vertex buffer, and a quad is rendered in front of each one. The quad size is calculated (in the 
		// geometry shader) to be large enough to cover the area affected by that light. The pixel shader uses the g-buffer to calculatea the light effect from the current light
		// and adds that effect (additive blending) into the scene. It's effectively a particle system to render the *effect* of each light
		UINT offset = 0;
		UINT vertexSize = sizeof(SPointLight);
		g_pd3dContext->IASetVertexBuffers( 0, 1, &LightVertexBuffer, &vertexSize, &offset );
		g_pd3dContext->IASetInputLayout( LightVertexLayout );
		g_pd3dContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST ); // Vertex data is the lights, each is a point, geometry shader generates a quad from each one
		PointLightTechnique->GetPassByIndex( 0 )->Apply( 0, g_pd3dContext );
		g_pd3dContext->Draw( NumPointLights, 0 );

		// Stop DirectX warnings about render targets still being bound
		GBufferShaderVar[0]->SetResource( 0 );
		GBufferShaderVar[1]->SetResource( 0 );
		GBufferShaderVar[2]->SetResource( 0 );
		PointLightTechnique->GetPassByIndex( 0 )->Apply( 0, g_pd3dContext );

		//**| DEFERRED RENDERING |****************************************************/
	}


	// Render skybox afterwards using forward rendering in either case (because no lights affect the skybox - no need for deferred)
	// I really need another technique because this way the skybox is only affected by ambient light, but this is already a complex lab...!
	Skybox->Render( PixelLitTexTechnique );


	// Finally render the point lights themselves (the little flares) as a particle system of camera-facing quads (additive blending)
	// This is just a particle system, nothing to do with deferred rendering. In most situations transparent models must be rendered 
	// last (regardless of rendering method) due to sorting issues. Transparency is hard to do with deferred rendering (see lecture), 
	// so often transparent objects are rendered using a normal forward rendering pass after the deferred rendering part is complete. 
	// So this part is same for forward and deferred rendering.
	UINT offset = 0;
	UINT vertexSize = sizeof(SPointLight);
	vertexSize = sizeof(SPointLight);
	g_pd3dContext->IASetVertexBuffers( 0, 1, &LightVertexBuffer, &vertexSize, &offset );
	g_pd3dContext->IASetInputLayout( LightVertexLayout );
	g_pd3dContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST ); // Vertex data is the lights, each is a point, geometry shader generates a quad from each one
    DiffuseMapVar->SetResource( LightDiffuseMap );
	LightParticlesTechnique->GetPassByIndex( 0 )->Apply( 0, g_pd3dContext );
	g_pd3dContext->Draw( NumPointLights, 0 );


	// After we've finished rendering, we "present" the back buffer to the front buffer (the screen)
	SwapChain->Present( 0, 0 );
}



////////////////////////////////////////////////////////////////////////////////////////
// Window Setup
////////////////////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
	// Initialise everything in turn
	if( !InitWindow( hInstance, nCmdShow) )
	{
		return 0;
	}
	if( !InitDevice() || !LoadEffectFile() || !InitScene() )
	{
		ReleaseResources();
		return 0;
	}

	// Initialise simple input functions
	InitInput();

	// Initialise a timer class, start it counting now
	CTimer Timer;
	Timer.Start();

	// Main message loop
	MSG msg = {0};
	while( WM_QUIT != msg.message )
	{
		// Check for and deal with window messages (window resizing, minimizing, etc.). When there are none, the window is idle and D3D rendering occurs
		if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
		{
			// Deal with messages
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		else // Otherwise render & update
		{
			RenderScene();

			// Get the time passed since the last frame
			float frameTime = Timer.GetLapTime();
			UpdateScene( frameTime );

			if (KeyHit( Key_Escape )) 
			{
				DestroyWindow( HWnd );
			}

		}
	}

	ReleaseResources();

	return ( int )msg.wParam;
}


//--------------------------------------------------------------------------------------
// Register class and create window
//--------------------------------------------------------------------------------------
bool InitWindow( HINSTANCE hInstance, int nCmdShow )
{
	// Register class
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof( WNDCLASSEX );
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon( hInstance, ( LPCTSTR )IDI_TUTORIAL1 );
	wcex.hCursor = LoadCursor( NULL, IDC_ARROW );
	wcex.hbrBackground = ( HBRUSH )( COLOR_WINDOW + 1 );
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = L"TutorialWindowClass";
	wcex.hIconSm = LoadIcon( wcex.hInstance, ( LPCTSTR )IDI_TUTORIAL1 );
	if( !RegisterClassEx( &wcex ) )	return false;

	// Create window
	HInst = hInstance;
	RECT rc = { 0, 0, 1280, 960 };
	AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
	HWnd = CreateWindow( L"TutorialWindowClass", L"Deferred Rendering", WS_OVERLAPPEDWINDOW,
	                     CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL );
	if( !HWnd )	return false;

	ShowWindow( HWnd, nCmdShow );

	return true;
}


//--------------------------------------------------------------------------------------
// Called every time the application receives a message
//--------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	PAINTSTRUCT ps;
	HDC hdc;

	switch( message )
	{
		case WM_PAINT:
			hdc = BeginPaint( hWnd, &ps );
			EndPaint( hWnd, &ps );
			break;
		
		case WM_DESTROY:
			PostQuitMessage( 0 );
			break;

		// These windows messages (WM_KEYXXXX) can be used to get keyboard input to the window
		// This application has added some simple functions (not DirectX) to process these messages (all in Input.cpp/h)
		case WM_KEYDOWN:
			KeyDownEvent( static_cast<EKeyState>(wParam) );
			break;

		case WM_KEYUP:
			KeyUpEvent( static_cast<EKeyState>(wParam) );
			break;
		
		default:
			return DefWindowProc( hWnd, message, wParam, lParam );
	}

	return 0;
}
