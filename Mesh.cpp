/*******************************************
	Mesh.cpp

	Mesh class implementation
********************************************/

#include "Mesh.h"
#include "CImportXFile.h"

//-----------------------------------------------------------------------------
// Constructor / destructor
//-----------------------------------------------------------------------------

// Model constructor
CMesh::CMesh()
{
	// Initialise member variables
	m_HasGeometry = false;

	m_NumNodes = 0;
	m_Nodes = 0;

	m_NumSubMeshes = 0;
	m_SubMeshes = 0;
	m_SubMeshesDX = 0;

	m_NumMaterials = 0;
	m_Materials = 0;
}

// Model destructor
CMesh::~CMesh()
{
	ReleaseResources();
}


// Release all nodes, sub-meshes and materials along with any DirectX data
void CMesh::ReleaseResources()
{
	for (TUInt32 material = 0; material < m_NumMaterials; ++material)
	{
		for (TUInt32 texture = 0; texture < m_Materials[material].numTextures; ++texture)
		{
			if (m_Materials[material].textures[texture]) m_Materials[material].textures[texture]->Release();
		}
	}
	delete[] m_Materials;
	m_Materials = 0;
	m_NumMaterials = 0;

	for (TUInt32 subMesh = 0; subMesh < m_NumSubMeshes; ++subMesh)
	{
		if (m_SubMeshesDX[subMesh].indexBuffer)	 m_SubMeshesDX[subMesh].indexBuffer->Release();
		if (m_SubMeshesDX[subMesh].vertexBuffer) m_SubMeshesDX[subMesh].vertexBuffer->Release();
		if (m_SubMeshesDX[subMesh].vertexLayout) m_SubMeshesDX[subMesh].vertexLayout->Release();
	}
	delete[] m_SubMeshesDX;
	delete[] m_SubMeshes;
	m_SubMeshesDX = 0;
	m_SubMeshes = 0;
	m_NumSubMeshes = 0;

	delete[] m_Nodes;
	m_Nodes = 0;
	m_NumNodes = 0;

	m_HasGeometry = false;
}


//-----------------------------------------------------------------------------
// Geometry access / enumeration
//-----------------------------------------------------------------------------

// Return total number of triangles in the mesh
TUInt32 CMesh::GetNumTriangles()
{
	TUInt32 numTriangles = 0;

	// Go through all submeshes ...
	for (TUInt32 subMesh = 0; subMesh < m_NumSubMeshes; ++subMesh)
	{
		numTriangles += m_SubMeshes[subMesh].numFaces;
	}

	return numTriangles;
}

// Request an enumeration of the triangles in the mesh. Get the individual triangles with
// calls to GetTriangle, finish the enumeration with EndEnumTriangles
void CMesh::BeginEnumTriangles()
{
	m_EnumTriMesh = 0;
	m_EnumTri = 0;
}

// Get the next triangle in the mesh, used after BeginEnumTriangles. Fills the supplied
// CVector3 pointers with the three vertex coordinates of the triangle. Returns true if a
// triangle was successfully returned, false if there are no more triangles to enumerate
bool CMesh::GetTriangle( CVector3* pVertex1, CVector3* pVertex2, CVector3* pVertex3 )
{
	// If enumerated all meshes then finished
	if (m_EnumTriMesh >= m_NumSubMeshes)
	{
		return false;
	}

	// If enumerated all triangles in current mesh...
	if (m_EnumTri >= m_SubMeshes[m_EnumTriMesh].numFaces)
	{
		// Move to next mesh - finished if no more meshes
		++m_EnumTriMesh;
		if (m_EnumTriMesh >= m_NumSubMeshes)
		{
			return false;
		}
		m_EnumTri = 0; // Start at first triangle of next mesh
	}

	// Get current face from submesh
	SMeshFace face = m_SubMeshes[m_EnumTriMesh].faces[m_EnumTri];

	// Get pointer to vertex refered to by first face index - deal with flexible vertex size
	TUInt8* pVertexData = m_SubMeshes[m_EnumTriMesh].vertices + 
	                      face.aiVertex[0] * m_SubMeshes[m_EnumTriMesh].vertexSize;

	// Copy vertex coordinate to output pointer
	// Assuming first three floats are the vertex coord x,y & z. See comment in CMesh::PreProcess
	TFloat32* pVertexCoord = reinterpret_cast<TFloat32*>(pVertexData);
	pVertex1->x = *pVertexCoord++;
	pVertex1->y = *pVertexCoord++;
	pVertex1->z = *pVertexCoord;

	// Get second vertex coordinate
	pVertexData = m_SubMeshes[m_EnumTriMesh].vertices +
	              face.aiVertex[1] * m_SubMeshes[m_EnumTriMesh].vertexSize;
	pVertexCoord = reinterpret_cast<TFloat32*>(pVertexData);
	pVertex2->x = *pVertexCoord++;
	pVertex2->y = *pVertexCoord++;
	pVertex2->z = *pVertexCoord;

	// Get third vertex coordinate
	pVertexData = m_SubMeshes[m_EnumTriMesh].vertices +
	              face.aiVertex[2] * m_SubMeshes[m_EnumTriMesh].vertexSize;
	pVertexCoord = reinterpret_cast<TFloat32*>(pVertexData);
	pVertex3->x = *pVertexCoord++;
	pVertex3->y = *pVertexCoord++;
	pVertex3->z = *pVertexCoord;

	return true;
}


// Return total number of vertices in the mesh
TUInt32 CMesh::GetNumVertices()
{
	TUInt32 numVertices = 0;

	// Go through all submeshes ...
	for (TUInt32 subMesh = 0; subMesh < m_NumSubMeshes; ++subMesh)
	{
		numVertices += m_SubMeshes[subMesh].numVertices;
	}

	return numVertices;
}

// Request an enumeration of the vertices in the mesh. Get the individual vertices with calls
// to GetVertex, finish the enumeration with EndEnumVertices
void CMesh::BeginEnumVertices()
{
	m_EnumVertMesh = 0;
	m_EnumVert = 0;
}

// Get the next vertex in the mesh, used after BeginEnumVertices. Fills the supplied CVector3
// pointer with the next vertex. Returns true if a vertex was successfully returned, false if
// there are no more vertices to enumerate
bool CMesh::GetVertex( CVector3* pVertex )
{
	// If enumerated all meshes then finished
	if (m_EnumVertMesh >= m_NumSubMeshes)
	{
		return false;
	}

	// If enumerated all vertices in current mesh...
	if (m_EnumVert >= m_SubMeshes[m_EnumVertMesh].numVertices)
	{
		// Move to next mesh - finished if no more meshes
		++m_EnumVertMesh;
		if (m_EnumVertMesh >= m_NumSubMeshes)
		{
			return false;
		}
		m_EnumVert = 0; // Start at first vertex of next mesh
	}

	// Get pointer to current vertex in current mesh - deal with flexible vertex size
	TUInt8* pVertexData = m_SubMeshes[m_EnumVertMesh].vertices + 
	                      m_EnumVert * m_SubMeshes[m_EnumVertMesh].vertexSize;

	// Copy vertex coordinate to output pointer
	// Assuming first three floats are the vertex coord x,y & z. See comment in CMesh::PreProcess
	TFloat32* pVertexCoord = reinterpret_cast<TFloat32*>(pVertexData);
	pVertex->x = *pVertexCoord++;
	pVertex->y = *pVertexCoord++;
	pVertex->z = *pVertexCoord;

	return true;
}


//-----------------------------------------------------------------------------
// Creation
//-----------------------------------------------------------------------------

// Create the model from an X-File, returns true on success
bool CMesh::Load( const string& fileName, ID3DX11EffectTechnique* shaderCode, bool needTangents /*= false*/)
{
	// Create a X-File import helper class
	CImportXFile importFile;

	// Add media folder path
	string fullFileName = fileName;

	// Check that the given file is an X-file
	if (!importFile.IsXFile( fullFileName ))
	{
		return false;
	}

	// Import the file, return on failure
	EImportError error = importFile.ImportFile( fullFileName );
	if (error != kSuccess)
	{
		if (error == kFileError)
		{
			string errorMsg = "Error loading mesh " + fullFileName;
			SystemMessageBox( errorMsg.c_str(), "Mesh Error" );
		}
		return false;
	}

	// Release any existing geometry
	if (m_HasGeometry)
	{
		ReleaseResources();
	}

	// Get node data from import class
	m_NumNodes = importFile.GetNumNodes();
	m_Nodes = new SMeshNode[m_NumNodes];
	if (!m_Nodes)
	{
		return false;
	}
	for (TUInt32 node = 0; node < m_NumNodes; ++node)
	{
		importFile.GetNode( node, &m_Nodes[node] );
	}

	// Get material data from import class, also load textures
	TUInt32 requiredMaterials = importFile.GetNumMaterials();
	m_Materials = new SMeshMaterialDX[requiredMaterials];
	if (!m_Materials)
	{
		ReleaseResources();
		return false;
	}
	for (m_NumMaterials = 0; m_NumMaterials < requiredMaterials; ++m_NumMaterials)
	{
		SMeshMaterial importMaterial; 
		importFile.GetMaterial( m_NumMaterials, &importMaterial );
		if (!CreateMaterialDX( importMaterial, &m_Materials[m_NumMaterials] ))
		{
			ReleaseResources();
			return false;
		}
	}

	// Get submesh data from import class - convert to DirectX data for rendering
	// but retain original data for easy access to vertices / faces
	TUInt32 requiredSubMeshes = importFile.GetNumSubMeshes();
	m_SubMeshes = new SSubMesh[requiredSubMeshes];
	m_SubMeshesDX = new SSubMeshDX[requiredSubMeshes];
	if (!m_SubMeshes || !m_SubMeshesDX)
	{
		ReleaseResources();
		return false;
	}
	for (m_NumSubMeshes = 0; m_NumSubMeshes < requiredSubMeshes; ++m_NumSubMeshes)
	{
		importFile.GetSubMesh( m_NumSubMeshes, &m_SubMeshes[m_NumSubMeshes], needTangents );
		if (!CreateSubMeshDX( m_SubMeshes[m_NumSubMeshes], &m_SubMeshesDX[m_NumSubMeshes], shaderCode ))
		{
			ReleaseResources();
			return false;
		}
	}

	// Geometry pre-processing - just calculating bounding box in this example
	if (!PreProcess())
	{
		ReleaseResources();
		return false;
	}

	m_HasGeometry = true;
	return true;
}

// Creates a DirectX specific sub-mesh from an imported sub-mesh (mesh materials must already have been prepared as we need to know render method to setup vertex data)
bool CMesh::CreateSubMeshDX
(
	const SSubMesh& subMesh,
	SSubMeshDX*     subMeshDX,
	ID3DX11EffectTechnique* shaderCode
)
{
	// Copy node and material
	subMeshDX->node = subMesh.node;
	subMeshDX->material = subMesh.material;

	// Buffer sizes
	subMeshDX->numVertices = subMesh.numVertices;
	subMeshDX->numIndices = subMesh.numFaces * 3; // Using triangle lists, so always 3 indexes per face

	// Create vertex element list & layout.
	unsigned int numElts = 0;
	unsigned int offset = 0;

	// Position is always required
	subMeshDX->vertexElts[numElts].SemanticName = "POSITION";   // Semantic in HLSL (what is this data for)
	subMeshDX->vertexElts[numElts].SemanticIndex = 0;           // Index to add to semantic (a count for this kind of data, when using multiple of the same type, e.g. TEXCOORD0, TEXCOORD1)
	subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R32G32B32_FLOAT; // Type of data - this one will be a float3 in the shader. Most data communicated as though it were colours
	subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;  // Offset of element from start of vertex data (e.g. if we have position (float3), uv (float2) then normal, the normal's offset is 5 floats = 5*4 = 20)
	subMeshDX->vertexElts[numElts].InputSlot = 0;               // For when using multiple vertex buffers (e.g. instancing - an advanced topic)
	subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA; // Use this value for most cases (only changed for instancing)
	subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;                     // --"--
	offset += 12;
	++numElts;

	// Repeat for each kind of vertex data
	if (subMesh.hasSkinningData) // If sub-mesh contains skinning data
	{
		subMeshDX->vertexElts[numElts].SemanticName = "BLENDWEIGHT";
		subMeshDX->vertexElts[numElts].SemanticIndex = 0;
		subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;
		subMeshDX->vertexElts[numElts].InputSlot = 0;
		subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;
		offset += 16;
		++numElts;
		subMeshDX->vertexElts[numElts].SemanticName = "BLENDINDICES";
		subMeshDX->vertexElts[numElts].SemanticIndex = 0;
		subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R8G8B8A8_UINT;
		subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;
		subMeshDX->vertexElts[numElts].InputSlot = 0;
		subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;
		offset += 4;
		++numElts;
	}
	if (subMesh.hasNormals)
	{
		subMeshDX->vertexElts[numElts].SemanticName = "NORMAL";
		subMeshDX->vertexElts[numElts].SemanticIndex = 0;
		subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;
		subMeshDX->vertexElts[numElts].InputSlot = 0;
		subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;
		offset += 12;
		++numElts;
	}
	if (subMesh.hasTangents)
	{
		subMeshDX->vertexElts[numElts].SemanticName = "TANGENT";
		subMeshDX->vertexElts[numElts].SemanticIndex = 0;
		subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;
		subMeshDX->vertexElts[numElts].InputSlot = 0;
		subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;
		offset += 12;
		++numElts;
	}
	if (subMesh.hasTextureCoords)
	{
		subMeshDX->vertexElts[numElts].SemanticName = "TEXCOORD";
		subMeshDX->vertexElts[numElts].SemanticIndex = 0;
		subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R32G32_FLOAT;
		subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;
		subMeshDX->vertexElts[numElts].InputSlot = 0;
		subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;
		offset += 8;
		++numElts;
	}
	if (subMesh.hasVertexColours)
	{
		subMeshDX->vertexElts[numElts].SemanticName = "COLOR";
		subMeshDX->vertexElts[numElts].SemanticIndex = 0;
		subMeshDX->vertexElts[numElts].Format = DXGI_FORMAT_R8G8B8A8_UNORM; // A RGBA colour with 1 byte (0-255) per component
		subMeshDX->vertexElts[numElts].AlignedByteOffset = offset;
		subMeshDX->vertexElts[numElts].InputSlot = 0;
		subMeshDX->vertexElts[numElts].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		subMeshDX->vertexElts[numElts].InstanceDataStepRate = 0;
		offset += 4;
		++numElts;
	}
	subMeshDX->vertexSize = offset;

	// Given the vertex element list, pass it to DirectX to create a vertex layout. We also need to pass an example of a technique that will
	// render this model. We will only be able to render this model with techniques that have the same vertex input as the example we use here
	D3DX11_PASS_DESC PassDesc;
	shaderCode->GetPassByIndex( 0 )->GetDesc( &PassDesc );
	g_pd3dDevice->CreateInputLayout( subMeshDX->vertexElts, numElts, PassDesc.pIAInputSignature, PassDesc.IAInputSignatureSize, &subMeshDX->vertexLayout );


	// Create the vertex buffer and fill it with the sub-mesh vertex data
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT; // Not a dynamic buffer
	bufferDesc.ByteWidth = subMeshDX->numVertices * subMeshDX->vertexSize; // Buffer size
	bufferDesc.CPUAccessFlags = 0;   // Indicates that CPU won't access this buffer at all after creation
	bufferDesc.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA initData; // Initial data
	initData.pSysMem = subMesh.vertices;   
	if (FAILED( g_pd3dDevice->CreateBuffer( &bufferDesc, &initData, &subMeshDX->vertexBuffer )))
	{
		return false;
	}


	// Create the index buffer - assuming 2-byte (WORD) index data
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = subMeshDX->numIndices * sizeof(WORD);
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;
	initData.pSysMem = subMesh.faces;   
	if (FAILED( g_pd3dDevice->CreateBuffer( &bufferDesc, &initData, &subMeshDX->indexBuffer )))
	{
		return false;
	}

	return true;
}

// Creates a DirectX specific material from an imported material
bool CMesh::CreateMaterialDX
(
	const SMeshMaterial& material,
	SMeshMaterialDX*     materialDX
)
{
	// Copy colours and shininess from material
	materialDX->diffuseColour = D3DXCOLOR( material.diffuseColour.r, material.diffuseColour.g, 
	                                       material.diffuseColour.b, material.diffuseColour.a );
	materialDX->specularColour = D3DXCOLOR( material.specularColour.r, material.specularColour.g, 
	                                        material.specularColour.b, material.specularColour.a );
	materialDX->specularPower = material.specularPower;

	// Load material textures
	materialDX->numTextures = material.numTextures;
	for (TUInt32 texture = 0; texture < material.numTextures; ++texture)
	{
		string fullFileName = material.textureFileNames[texture];
		if (FAILED( D3DX11CreateShaderResourceViewFromFile( g_pd3dDevice, CA2CT(fullFileName.c_str()), NULL, NULL, &materialDX->textures[texture], NULL ) ))
		{
			string errorMsg = "Error loading texture " + fullFileName;
			SystemMessageBox( errorMsg.c_str(), "Mesh Error" );
			return false;
		}
	}
	return true;
}


// Pre-processing after loading, returns true on success - just calculates bounding box here
// Rejects mesh if no sub-meshes or any empty sub-meshes
bool CMesh::PreProcess()
{
	// Ensure at least one non-empty sub-mesh
	if (m_NumSubMeshes == 0 || m_SubMeshes[0].numVertices == 0)
	{
		return false;
	}

	// Set initial bounds from first vertex
	// Assuming first three floats are the vertex coord x,y & z. Would be better to support
	// a flexible data type system like DirectX vertex declarations (D3DVERTEXELEMENT9)
	TFloat32* pVertexCoord = reinterpret_cast<TFloat32*>(m_SubMeshes[0].vertices);
	m_MinBounds.x = m_MaxBounds.x = *pVertexCoord++;
	m_MinBounds.y = m_MaxBounds.y = *pVertexCoord++;
	m_MinBounds.z = m_MaxBounds.z = *pVertexCoord;
	m_BoundingRadius = m_MinBounds.Length();

	// Go through all submeshes ...
	for (TUInt32 subMesh = 0; subMesh < m_NumSubMeshes; ++subMesh)
	{
		// Reject mesh if it contains empty sub-meshes
		if (m_SubMeshes[subMesh].numVertices == 0)
		{
			return false;
		}

		// Go through all vertices
		TUInt8* pVertex = m_SubMeshes[subMesh].vertices;
		for (TUInt32 vert = 0; vert < m_SubMeshes[subMesh].numVertices; ++vert)
		{
			// Get vertex coord as vector
			pVertexCoord = reinterpret_cast<TFloat32*>(pVertex); // Assume float x,y,z coord again
			CVector3 vertex;
			vertex.x = *pVertexCoord++;
			vertex.y = *pVertexCoord++;
			vertex.z = *pVertexCoord;
			
			// Compare vertex against current bounds, updating bounds where necessary
			if (vertex.x < m_MinBounds.x)
			{
				m_MinBounds.x = vertex.x;
			}
			if (vertex.x > m_MaxBounds.x)
			{
				m_MaxBounds.x = vertex.x;
			}
			++pVertexCoord;

			if (vertex.y < m_MinBounds.y)
			{
				m_MinBounds.y = vertex.y;
			}
			if (vertex.y > m_MaxBounds.y)
			{
				m_MaxBounds.y = vertex.y;
			}
			++pVertexCoord;

			if (vertex.z < m_MinBounds.z)
			{
				m_MinBounds.z = vertex.z;
			}
			if (vertex.z > m_MaxBounds.z)
			{
				m_MaxBounds.z = vertex.z;
			}

			TFloat32 length = vertex.Length();
			if (length > m_BoundingRadius)
			{
				m_BoundingRadius = length;
			}

			// Step to next vertex (flexible vertex size)
			pVertex += m_SubMeshes[subMesh].vertexSize;
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

// Render the model
void CMesh::Render(	ID3DX11EffectTechnique* technique )
{
	if (!m_HasGeometry) return;

	// Render each sub-mesh
	for (TUInt32 subMesh = 0; subMesh < m_NumSubMeshes; ++subMesh)
	{
		// Get a reference to the submesh and its material to reduce code clutter
		SSubMeshDX& subMeshDX = m_SubMeshesDX[subMesh];
		SMeshMaterialDX& material = m_Materials[subMeshDX.material];

		// Set up shader variables based on material, assuming standard names
		Effect->GetVariableByName("WorldMatrix")->AsMatrix()->SetMatrix( &m_Nodes[subMeshDX.node].positionMatrix.e00 );
		Effect->GetVariableByName("DiffuseColour")->SetRawValue( material.diffuseColour, 0, 12 );
		Effect->GetVariableByName("SpecularColour")->SetRawValue( material.specularColour, 0, 12 );
		Effect->GetVariableByName("SpecularPower")->AsScalar()->SetFloat( material.specularPower );
		if (material.numTextures > 0) Effect->GetVariableByName("DiffuseMap")->AsShaderResource()->SetResource( material.textures[0] );
		if (material.numTextures > 1) Effect->GetVariableByName("NormalMap" )->AsShaderResource()->SetResource( material.textures[1] );

		// Select vertex and index buffer for sub-mesh - assuming all geometry data is triangle lists
		UINT offset = 0;
		g_pd3dContext->IASetVertexBuffers( 0, 1, &subMeshDX.vertexBuffer, &subMeshDX.vertexSize, &offset );
		g_pd3dContext->IASetInputLayout(subMeshDX.vertexLayout );
		g_pd3dContext->IASetIndexBuffer(subMeshDX.indexBuffer, DXGI_FORMAT_R16_UINT, 0 );
		g_pd3dContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

		// Render the sub-mesh. Geometry buffers and shader variables, just select the technique for this method and draw.
		D3DX11_TECHNIQUE_DESC techDesc;
		technique->GetDesc( &techDesc );
		for( UINT p = 0; p < techDesc.Passes; ++p )
		{
			technique->GetPassByIndex( p )->Apply( 0, g_pd3dContext );
			g_pd3dContext->DrawIndexed( subMeshDX.numIndices, 0, 0 );
		}
		g_pd3dContext->DrawIndexed( subMeshDX.numIndices, 0, 0 );
	}
}
