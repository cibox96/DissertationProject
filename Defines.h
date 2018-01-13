//--------------------------------------------------------------------------------------
//	Defines.h
//
//	General definitions shared across entire project
//--------------------------------------------------------------------------------------

#ifndef DEFINES_H_INCLUDED // Header guard - prevents file being included more than once (would cause errors)
#define DEFINES_H_INCLUDED

#include <windows.h>
#include <d3d11.h>
#include <d3dx11.h>
#include <d3dx11effect.h> // Have to compile the effect (.fx) file source code ourselves - this is the main include file from that project
#include <d3d10.h>
#include <d3dx10.h>       // DX maths classes are not in D3DX11, so use DX10 (or alternatively switch to XNA maths libraries)
#include <atlbase.h>

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

// Move speed constants (shared between camera and model class)
const float MoveSpeed = 120.0f;
const float RotSpeed = 1.3f;


//-----------------------------------------------------------------------------
// Helper functions and macros
//-----------------------------------------------------------------------------

// Helper macro to release DirectX pointers only if they are not NULL
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p) = NULL; } }


//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

// Make DirectX render device & context available to other source files. We declare
// these variable global in one file, then make it "extern" to all others. In general
// this is not good practice - this is a kind of super-global variable. Would be
// better to have a Device class responsible for this data. However, this example aims
// for a minimum of code to help demonstrate the focus topic
extern ID3D11Device*        g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dContext;

extern ID3DX11Effect*       Effect; // Also make effect file global


// Dimensions of viewport - shared between setup code and camera class (which needs this to create the projection matrix - see code there)
extern int g_ViewportWidth, g_ViewportHeight;


#endif // End of header guard - see top of file
