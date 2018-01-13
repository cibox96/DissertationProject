//--------------------------------------------------------------------------------------
// File: Deferred.fx
//
// Deferred Rendering
//--------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------
// Global Variables
//--------------------------------------------------------------------------------------

// The matrices (4x4 matrix of floats) for transforming from 3D model to 2D projection (used in vertex shader)
float4x4 WorldMatrix;
float4x4 ViewMatrix;
float4x4 ProjMatrix;
float4x4 ViewProjMatrix;
float4x4 InvViewMatrix;

// Viewport Dimensions
float ViewportWidth;
float ViewportHeight;

// Lights are stored in a stucture so we can pass lists of them
struct SPointLight
{
  float3 LightPosition;
  float  LightRadius;
  float4 LightColour;
};

// Point lights for forward-rendering. The deferred implementation passes the lights in as a vertex buffer (although that is
// not a requirement of deferred rendering - could use these variables instead)
static const int  MaxPointLights = 256;  // Maximum number of point lights the shader supports (this is for forward-rendering only)
int         NumPointLights;              // Actual number of point lights currently in use (this is for forward-rendering only)
SPointLight PointLights[MaxPointLights]; // List of point lights (for forward-rendering only)

// Other light data
float3 AmbientColour;
float3 DiffuseColour;
float3 SpecularColour;
float  SpecularPower;
float3 CameraPos;
float  CameraNearClip;

// Textures
Texture2D DiffuseMap; // Diffuse texture map (with optional specular map in alpha)
Texture2D NormalMap;  // Normal map (with optional height map in alpha)

// G-Buffer when used as textures for lighting pass
Texture2D GBuff_DiffuseSpecular; // Diffuse colour in rgb, specular strength in a
Texture2D GBuff_WorldPosition;   // World position at pixel in rgb (xyz)
Texture2D GBuff_WorldNormal;     // World normal at pixel in rgb (xyz)


// Samplers to use with the above textures
SamplerState TrilinearWrap
{
  Filter = MIN_MAG_MIP_LINEAR;
  AddressU = Wrap;
  AddressV = Wrap;
};

// Nearly always sample the g-buffer with point sampling (i.e. no bilinear, trilinear etc.) because we don't want to introduce blur
SamplerState PointClamp
{
  Filter = MIN_MAG_MIP_POINT;
  AddressU = Clamp;
  AddressV = Clamp;
};


//--------------------------------------------------------------------------------------
// Forward Rendering and Common Structures
//--------------------------------------------------------------------------------------

// This structure describes generic vertex data to be sent into the vertex shader
// Used in forward rendering and when creating the g-buffer in deferred rendering
struct VS_INPUT
{
  float3 Pos    : POSITION;
  float3 Normal : NORMAL;
  float2 UV     : TEXCOORD0;
};

// This stucture contains the vertex data transformed into projection space & world space, i.e. the result of the usual vertex processing
// Used in forward rendering for the standard pixel lighting stage, but also used when building the g-buffer - the main geometry processing
// doesn't change much for deferred rendering - it's all about how this data is used next.
struct PS_TRANSFORMED_INPUT
{
  float4 ProjPos       : SV_Position;
  float3 WorldPosition : POSITION;
  float3 WorldNormal   : NORMAL;
  float2 UV            : TEXCOORD0;
};

// For both forward and deferred rendering, the light flares (sprites showing the position of the lights) are rendered as
// a particle system (this is not really to do with deferred rendering, just a visual nicety). Because the particles are
// transparent (additive blending), they must be rendered last, and they can't use deferred rendering (see lecture).
// This is the input to the particle pixel shader.
struct PS_LIGHTPARTICLE_INPUT
{
  float4 ProjPos                     : SV_Position;
  float2 UV                          : TEXCOORD0;
  nointerpolation float3 LightColour : COLOR0; // The light colour is passed to the pixel shader so the flare can be tinted. See below about "nointerpolation"
};


//--------------------------------------------------------------------------------------
// Deferred Rendering Structures
//--------------------------------------------------------------------------------------

// The output of the g-buffer pixel shader. Normally pixel shaders only output a final pixel colour to a render target (occasionally depth as well).
// Here we are outputing three different values, to *three different render targets*. This is called MRT (multiple render targets). 
// These three render targets together are the g-buffer, and they contain the information needed to light the scene in later passes.
// The exact contents of a g-buffer are implementation dependent. This particular choice is simplistic for the purposes of this
// tutorial. It would be more efficient to reduce the amount of data stored (e.g. store x & y & a direction bit for normal and derive
// the z in shaders; or use 16-bit floats; or other trickery...)
struct GBUFFER
{
  float4 DiffuseSpecular : SV_Target0;
  float4 WorldPosition   : SV_Target1;
  float WorldNormal      : SV_Depth;/*MISSING - Line is very wrong, want to store a normal in the G-Buffer. Not difficuly - I'm just highlighting the syntax for multiple render targets*/
};


// In deferred rendering, ambient/directional light is applied as a full screen quad. The vertex shader generates the quad vertices using a special
// input type, the automatically generated vertex ID. It starts at 0 and increases with each vertex processed (did something similar in post-processing)
struct VS_AMBIENT_INPUT
{
  uint vertexId : SV_VertexID;
};

// The deferred rendering ambient pixel shader needs only the positions of the quad points, it is a simple operation.
struct PS_AMBIENTLIGHT_INPUT
{
  float4 ProjPos : SV_Position;
};


// For deferred rendering, the effect of a single point light is rendered by passing the light data and rendering a quad over the area affected
struct VS_POINTLIGHT_INPUT
{
  float3 LightPosition : POSITION;
  float  LightRadius   : TEXCOORD0;
  float4 LightColour   : COLOR0;
};

// The pixel shader for deferred rendering renders the area affected by a single point light. It renders a quad in front of the light's sphere of effect
struct PS_POINTLIGHT_INPUT
{
  float4 ProjPos                       : SV_Position;
  nointerpolation float3 LightPosition : POSITION;     // Information about the point light is passed in from the vertex/geometry shader...
  nointerpolation float  LightRadius   : TEXCOORD0;    // ...We don't want to interpolate this data, it's the same light over the entire...
  nointerpolation float3 LightColour   : COLOR0;       // ...affected area. The nointerpolation statement indicates that (it's a performance boost)
};


//--------------------------------------------------------------------------------------
// Deferred Rendering Shaders
//--------------------------------------------------------------------------------------

// This pixel shader writes the g-buffer. First the scene is rendered through a standard vertex shader. Then this pixel shader, instead of lighting and 
// rendering the pixels, stores the texture colour, position and normal for this pixel in the g-buffer. Later lighting passes will uses this data to
// light the scene. G-buffer layout is for us to decide, this setup is fairly basic
GBUFFER PS_GBuffer( PS_TRANSFORMED_INPUT pIn )
{
  GBUFFER gBuffer;

  float4 colour = DiffuseMap.Sample( TrilinearWrap, pIn.UV ); // Sample texture
//	clip( colour.a - 0.5f ); // Discard pixels with alpha < 0.5 - the models in this lab use a lot of alpha transparency, but this impacts performance testing

  gBuffer.DiffuseSpecular = float4(colour.rgb, dot(SpecularColour.rgb,0.333f)); // Store diffuse.rgb colour from texture, and specular intensity from average of X-File specular colour r,g & b
  gBuffer.WorldPosition   = float4(pIn.WorldPosition, 1.0f);                    // Store world position of pixel, storing the 1.0f is redundant, could store something more useful in a more complex example
  gBuffer.WorldNormal     = 0/*MISSING - not 0, note that normals need renormalizing coming into a pixel shader (we've seen that before)*/; // Store world normal of pixel, same comment as above about the 0.0f

  return gBuffer;
}


// The vertex shader for the ambient light geometry shader below. This shader self-generates a full-screen quad without requiring vertex data (similar to post-processing)
PS_AMBIENTLIGHT_INPUT VS_AmbientLight( VS_AMBIENT_INPUT vIn )
{
    PS_AMBIENTLIGHT_INPUT vOut;
  
  // The four points of a full-screen quad in projection space
  float2 Quad[4] =  { float2(-1.0,  1.0),   // Top-left
                      float2( 1.0,  1.0),   // Top-right
                      float2(-1.0, -1.0),   // Bottom-left
                      float2( 1.0, -1.0) }; // Bottom-right
  
  // Output vertex from quad, with 0 depth
  vOut.ProjPos  = float4( Quad[vIn.vertexId], 0.0f, 1.0f ); 
    return vOut;
}

// Deferred rendering ambient light pixel shader - just sample the diffuse colour from the g-buffer, then multiply by the constant ambient level - fills entire
// screen with an ambient version of the scene.
float4 PS_AmbientLight( PS_AMBIENTLIGHT_INPUT pIn ) : SV_Target
{
  // Get the texture coordinate into the g-buffer by looking at the position of the pixel being used - did a similar thing in post-processing
  float2 uv = pIn.ProjPos.xy;
  uv.x /= ViewportWidth;
  uv.y /= ViewportHeight;

  // MISSING: We're going to copy the diffuse part of the g-buffer, multiplied by ambient. Steps:
  //          - Sample the diffuse part of the g-buffer at the above uv, use point sampling because we don't want any filtering/blurring
  //          - Multiply the rgb part of that colour by AmbientColour (global, already prepared)
  //          - return that
  return 0;// Remove this line when you've done the above
}

// Dummy vertex shader for the point light geometry shader below. The geometry shader does all the work
VS_POINTLIGHT_INPUT VS_PointLight( VS_POINTLIGHT_INPUT vIn )
{
  return vIn;
}

// Geometry shader that inputs a point light and "expands" it into a camera-facing quad covering the effective area of the light
// Some similarity to shaders used in area post-processing, but some unusual complexity.
[maxvertexcount(4)]  
void GS_PointLight
(
  point VS_POINTLIGHT_INPUT light[1],
  inout TriangleStream<PS_POINTLIGHT_INPUT> outStrip  // Triangle stream output
)
{
  // MISSING - no, not really - I'd love to make you write some part of the code below, but it is tricky - do spend the time reading the comments though */
  // This code calculates the size of the quad required to cover the sphere of effect of the point light. This is complex
  // because a sphere rendered in perspective becomes an elipse. I will discuss this in the lecture.
  float3 p =  mul( float4(light[0].LightPosition, 1.0f), ViewMatrix ); // Light position in camera space
  float  r = light[0].LightRadius;      // Light radius in a simpler named variable
  if (p.z + r < CameraNearClip) return; // If light sphere is entirely behind camera near clip plane then quit the geometry shader before outputing any 
                                        // geometry - no effect visible from this light. Could do frustum culling here too.
  float3 fbl = p + float3(-r, -r, -r); // Get axis aligned cube around light sphere (camera space)
  float3 ftr = p + float3( r,  r, -r); // We want a screen quad that covers the front and back face of this cube
  float3 bbl = p + float3(-r, -r,  r); 
  float3 btr = p + float3( r,  r,  r); 
  fbl.z = clamp( fbl.z, CameraNearClip, 1.#INF ); // Although the cube is not behind the camera, the front face might be, so clamp that face to the near clip plane
  ftr.z = clamp( ftr.z, CameraNearClip, 1.#INF ); // 1.#INF is +ve infinity as a float (rare to use!), -1.#INF is -ve infinity
  float4 ps_fbl = mul( float4(fbl, 1.0f), ProjMatrix ); // Project the bounding cube into screen space so we can get a 2D bounding quad
  float4 ps_ftr = mul( float4(ftr, 1.0f), ProjMatrix );
  float4 ps_bbl = mul( float4(bbl, 1.0f), ProjMatrix );
  float4 ps_btr = mul( float4(btr, 1.0f), ProjMatrix );
  ps_bbl.xy *= ps_fbl.w / ps_bbl.w; // Reproject the back points of the cube into the plane of the front points (a tricky point)
  ps_btr.xy *= ps_fbl.w / ps_bbl.w;
  float2 ps_bl = min(ps_fbl.xy, ps_bbl.xy); // Find the maximum extents of front and back plane of cube to give required quad that will cover it
  float2 ps_tr = max(ps_ftr.xy, ps_btr.xy);
    
  // The data about the light this quad represents is put into each vertex of the quad. The pixel shader that will do the lighting can then pick
  // up information it needs to do the lighting
  PS_POINTLIGHT_INPUT outVert;
  outVert.LightPosition = light[0].LightPosition;
  outVert.LightRadius = light[0].LightRadius;
  outVert.LightColour = light[0].LightColour;

  // Create a quad of the size calculated above in x & y, the depth values w & z are taken from the near face of the cube
  // calculated earlier, which guarantees the quad is in front of all the pixels affected by the light
  outVert.ProjPos = float4(ps_bl.x, ps_tr.y, ps_fbl.zw);
  outStrip.Append( outVert );
  outVert.ProjPos = float4(ps_tr, ps_fbl.zw);
  outStrip.Append( outVert );
  outVert.ProjPos = 0/*MISSING, not 0. Work out the missing point, bl = bottom-left, tr = top-right*/;
  outStrip.Append( outVert );
  outVert.ProjPos = float4(ps_tr.x, ps_bl.y, ps_fbl.zw);
  outStrip.Append( outVert );
  outStrip.RestartStrip();
}

// The geometry shader above has created a quad just in front of, and covering the sphere affected by a light. This pixel shader now
// lights each pixel behind that quad. It uses the light information passed on from the geometry shader (light colour, position...) and
// it gets normals, positions from the g-buffer. Pixels nearer than the light sphere won't be touched because the quad is further away
// and the depth buffer will sort that out. However, pixels that are far beyond, but behind the sphere will be lit. However, this isn't
// a problem because the attenuation calculations will mean those distant pixels get no light anyway. Some schemes also attempt to cull
// those distant pixels to gain performance, but this quad based method is considered the most effective on modern hardware (I think!)
float4 PS_PointLight( PS_POINTLIGHT_INPUT pIn ) : SV_Target
{
  // Get the texture coordinate into the g-buffer by looking at the position of the pixel being used - did a similar thing in post-processing
  float2 uv = pIn.ProjPos.xy;
  uv.x /= ViewportWidth;
  uv.y /= ViewportHeight;
  
  // Get the position of the pixel to be lit
  float3 WorldPosition = GBuff_WorldPosition.Sample( PointClamp, uv );

  // Immediately calculate attenuation (it's a different formula here, a linear fall-off, not so attractive but guaranteed to lie within the sphere)
  // If the intensity is 0 (because the pixel is far away) then immediately discard to save further processing (try commenting out the discard line to see the performance effect)
  float3 LightVec = pIn.LightPosition - WorldPosition;
  float LightIntensity = saturate(1.0f - length(LightVec) / pIn.LightRadius); 
  if (LightIntensity == 0.0f) discard;

  // Get the texture diffuse colour and normal for this pixel from the g-buffer
  float4 DiffuseSpecular = GBuff_DiffuseSpecular.Sample( PointClamp, uv );
  float3 WorldNormal     = 0/*MISSING, not 0. THhs should be easy after doing the earlier one*/;

  // The rest of the code is standard per-pixel lighting. We have all the usual data to do this, it just via a very different route.
  float3 LightDir = normalize(LightVec);
  float3 CameraDir = normalize(CameraPos - WorldPosition);
  
  // The specular power is stored in the X-files per material and passed over to the global "SpecularPower" variable during the g-buffer stage.
  // We could store the specular power in the g-buffer (there is some space) and fetch it here instead of using a fixed value for specular power
  // MISSING - actually not really missing, but if you finish the worksheet you could fix the specularPower issue noted above. Leave it to the end, not very important
  float specularPower = 256.0f;
  float3 DiffuseLight = LightIntensity * pIn.LightColour * max( dot(WorldNormal, LightDir), 0 );
  float3 halfway = normalize(LightDir + CameraDir);
  float3 SpecularLight = DiffuseLight * pow( max( dot(WorldNormal, halfway), 0 ), specularPower );

  float4 combinedColour;
  combinedColour.rgb = DiffuseSpecular.rgb * DiffuseLight + DiffuseSpecular.a * SpecularLight;
  combinedColour.a = 1.0f;
  
  return combinedColour;
  //return combinedColour + ((LightIntensity == 0.0f) ? float4(0.1,0,0,0) : float4(0,0.1,0,0)); // Comment this line in, and comment out the line above and the earlier "discard" line
                                                                                                  // to see the quad rendered for each light and the actual pixels that are affected by it
}


//--------------------------------------------------------------------------------------
// Forward rendering shaders - nothing particularly new here
//--------------------------------------------------------------------------------------

// This vertex shader transforms the vertex into projection space & world space and passes on the UV, i.e. the usual vertex processing
PS_TRANSFORMED_INPUT VS_TransformTex( VS_INPUT vIn )
{
  PS_TRANSFORMED_INPUT vOut;

  // Transform the input model vertex position into world space
  float4 modelPos = float4(vIn.Pos, 1.0f);
  float4 worldPos = mul( modelPos, WorldMatrix );
  vOut.WorldPosition = worldPos.xyz;

  // Further transform the vertex from world space into view space and into 2D projection space for rendering
  float4 viewPos  = mul( worldPos, ViewMatrix );
  vOut.ProjPos    = mul( viewPos,  ProjMatrix );

  // Transform the vertex normal from model space into world space
  float4 modelNormal = float4(vIn.Normal, 0.0f);
  vOut.WorldNormal = mul( modelNormal, WorldMatrix ).xyz;

  // Pass texture coordinates (UVs) on to the pixel shader, the vertex shader doesn't need them
  vOut.UV = vIn.UV;

  return vOut;
}

// Pixel shader that calculates per-pixel lighting and combines with diffuse and specular map
// Basically the same as previous pixel lighting shaders except this one processes an array of lights rather than a fixed number
// Obviously, this isn't efficient for large number of lights, which is the point of using deferred rendering instead of this
float4 PS_PixelLitDiffuseMap( PS_TRANSFORMED_INPUT pIn ) : SV_Target
{
  ////////////////////
  // Sample texture

  // Extract diffuse material colour for this pixel from a texture
  float4 DiffuseMaterial = DiffuseMap.Sample( TrilinearWrap, pIn.UV );
//	clip( DiffuseMaterial.a - 0.5f ); // Discard pixels with alpha < 0.5, the model in this lab uses a lot of alpha transparency, but this impacts performance

  // Renormalise normals that have been interpolated from the vertex shader
  float3 worldNormal = normalize(pIn.WorldNormal); 

  ///////////////////////
  // Calculate lighting

  // Calculate direction of camera
  float3 CameraDir = normalize(CameraPos - pIn.WorldPosition); // Position of camera - position of current vertex (or pixel) (in world space)
  
  // Sum the effects of each light, 
  float3 TotalDiffuse = AmbientColour;
  float3 TotalSpecular = 0;
  for (int i = 0; i < NumPointLights; i++)
  {
    float3 LightVec = PointLights[i].LightPosition - pIn.WorldPosition;
    float  LightIntensity = saturate(1.0f - length(LightVec) / PointLights[i].LightRadius); // Tweaked the attenuation approach, see the function PS_PointLight above
    float3 LightDir = normalize(LightVec);
  
    float3 Diffuse = LightIntensity * PointLights[i].LightColour * max( dot(worldNormal, LightDir), 0 );
    TotalDiffuse += Diffuse;
    float3 halfway = normalize(LightDir + CameraDir);
    TotalSpecular += Diffuse * pow( max( dot(worldNormal, halfway), 0 ), SpecularPower );
  }

  ////////////////////
  // Combine colours 
  
  // Combine maps and lighting for final pixel colour
  float4 combinedColour;
  combinedColour.rgb = DiffuseMaterial.rgb * TotalDiffuse + SpecularColour * TotalSpecular; // The models in this lab have no specular in texture alpha, so use specular colour from X-file
  combinedColour.a = 1.0f;

  return combinedColour;
}


// Dummy vertex shader for the light particle system geometry shader below. The geometry shader does all the work
VS_POINTLIGHT_INPUT VS_LightParticles( VS_POINTLIGHT_INPUT vIn )
{
  return vIn;
}

// Geometry shader that inputs a single light vertex and "expands" it into a camera-facing quad (two triangles)
// Used in both forward and deferred rendering to render the flares showing where the lights are, basically a standard particle system
[maxvertexcount(4)]  
void GS_LightParticles
(
  point VS_POINTLIGHT_INPUT light[1],                    // One light in, as a vertex
  inout TriangleStream<PS_LIGHTPARTICLE_INPUT> outStrip  // Triangle stream output, camera-facing quad
)
{
  // The four points of a quad in projection space
  float2 Quad[4] =  { float2(-1.0,  1.0),   // Top-left
                      float2( 1.0,  1.0),   // Top-right
                      float2(-1.0, -1.0),   // Bottom-left
                      float2( 1.0, -1.0) }; // Bottom-right

  // Texture coordinates of the quad
  const float2 UVs[4] = { float2(0, 1), 
                          float2(1, 1),
                          float2(0, 0),
                          float2(1, 0) };
  

  PS_LIGHTPARTICLE_INPUT outVert; // Used to build output vertices
  outVert.LightColour = light[0].LightColour;

  // Position of light in camera space
  float4 cameraPosition = mul( float4(light[0].LightPosition, 1.0f), ViewMatrix );
  float scale = sqrt(light[0].LightRadius * 0.25f);

  // Output the four corner vertices of a quad centred on the particle position
  for (int i = 0; i < 4; ++i)
  {
    // Offset light position to corners of quad
    float4 quadPt = cameraPosition;
    quadPt.xy += Quad[i] * scale;
        
        // Transform to 2D position and output along with an appropriate UV
    outVert.ProjPos = mul( quadPt, ProjMatrix );
    outVert.UV = UVs[i];

    outStrip.Append( outVert );
  }
  outStrip.RestartStrip();
}

// Pixel shader to render the flares at the centre of each light, nothing special here
float4 PS_LightParticles( PS_LIGHTPARTICLE_INPUT pIn ) : SV_Target
{
  // Tint texture with colour of the light
  float3 diffuse = DiffuseMap.Sample( TrilinearWrap, pIn.UV ) * pIn.LightColour;
  return float4( diffuse, 0.0f );
}


//--------------------------------------------------------------------------------------
// States
//--------------------------------------------------------------------------------------

// States are needed to switch between additive blending for the lights and no blending for other models

RasterizerState CullNone  // Cull none of the polygons, i.e. show both sides
{
  CullMode = None;
  FillMode = SOLID;
};
RasterizerState CullBack  // Cull back side of polygon - normal behaviour, only show front of polygons
{
  CullMode = Back;
  FillMode = SOLID;
};
RasterizerState Wireframe
{
  CullMode = None;
  FillMode = WIREFRAME;
};


DepthStencilState DepthWritesOff // Don't write to the depth buffer - polygons rendered will not obscure other polygons
{
  DepthFunc      = LESS;
  DepthWriteMask = ZERO;
};
DepthStencilState DepthWritesOn  // Write to the depth buffer - normal behaviour 
{
  DepthFunc      = LESS;
  DepthWriteMask = ALL;
};
DepthStencilState DisableDepth   // Disable depth buffer entirely
{
  DepthFunc      = ALWAYS;
  DepthWriteMask = ZERO;
};


BlendState NoBlending // Switch off blending - pixels will be opaque
{
  BlendEnable[0] = FALSE;
};
BlendState AdditiveBlending // Additive blending is used for lighting effects
{
  BlendEnable[0] = TRUE;
  SrcBlend = ONE;
  DestBlend = ONE;
  BlendOp = ADD;
};
BlendState AlphaBlending // Alpha blending is used for the particles
{
  BlendEnable[0] = TRUE;
  SrcBlend = SRC_ALPHA;
  DestBlend = INV_SRC_ALPHA;
  BlendOp = ADD;
};


//--------------------------------------------------------------------------------------
// Techniques
//--------------------------------------------------------------------------------------

// Techniques are used to render models in our scene. They select a combination of vertex, geometry and pixel shader from those provided above. Can also set states.

// Render opaque objects to the GBuffer
technique11 GBuffer
{
  pass P0
  {
    SetVertexShader( CompileShader( vs_5_0, VS_TransformTex() ) );
    SetGeometryShader( NULL );                                   
    SetPixelShader( CompileShader( ps_5_0, PS_GBuffer() ) );

    // Switch off blending states
    SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
    SetRasterizerState( CullNone ); // The level model uses lots of two-sided faces, quick fix rather than edit the model and add extra shaders
    SetDepthStencilState( DepthWritesOn, 0 );
  }
}

// Render the effect of a point light when using deferred rendering
// Renders a quad covering the extents of a light's effect, use data from the G-buffer to calculate contribution of the light within that area
technique11 AmbientLight
{
  pass P0
  {
    SetVertexShader( CompileShader( vs_5_0, VS_AmbientLight() ) );
    SetGeometryShader( NULL );                                   
    SetPixelShader( CompileShader( ps_5_0, PS_AmbientLight() ) );

    // Switch off blending states
    SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
    SetRasterizerState( CullBack ); 
    SetDepthStencilState( DepthWritesOff, 0 );
  }
}

// Render the effect of a point light when using deferred rendering
// Renders a quad covering the extents of a light's effect, use data from the G-buffer to calculate contribution of the light within that area
technique11 PointLight
{
  pass P0
  {
    SetVertexShader( CompileShader( vs_5_0, VS_PointLight() ) );
    SetGeometryShader( CompileShader( gs_5_0, GS_PointLight() ) );                                   
    SetPixelShader( CompileShader( ps_5_0, PS_PointLight() ) );

    // Switch off blending states
    SetBlendState( 0/*MISSING, not 0, what blending mode to use for the deferred light areas?*/, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
    SetRasterizerState( CullBack ); 
    SetDepthStencilState( DepthWritesOff, 0 );
  }
}

// Per-pixel lighting with diffuse map
technique11 PixelLitTex
{
  pass P0
  {
    SetVertexShader( CompileShader( vs_5_0, VS_TransformTex() ) );
    SetGeometryShader( NULL );                                   
    SetPixelShader( CompileShader( ps_5_0, PS_PixelLitDiffuseMap() ) );

    // Switch off blending states
    SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
    SetRasterizerState( CullNone );  // The level model uses lots of two-sided faces, quick fix rather than edit the model and add extra shaders
    SetDepthStencilState( DepthWritesOn, 0 );
  }
}


// A particle system of lights (just the sprite to show the location, not the effect of the light). Rendered as camera-facing quads with additive blending
technique11 LightParticles
{
  pass P0
  {
    SetVertexShader( CompileShader( vs_5_0, VS_LightParticles() ) );
    SetGeometryShader( CompileShader( gs_5_0, GS_LightParticles() ) );                                   
    SetPixelShader( CompileShader( ps_5_0, PS_LightParticles() ) );

    SetBlendState( AdditiveBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
    SetRasterizerState( CullNone ); 
    SetDepthStencilState( DepthWritesOff, 0 );
  }
}
