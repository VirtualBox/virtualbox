#pragma once

#include <stdint.h>

// NOTE: This is in no way a complete .obj parser.
// I just did the minimum required to load simple .obj files,
// for simplicity it always returns a vertex buffer containing
// positions, texture coordinates and normals.
// It will assert() if there are no vertex positions, if there
// are no UVs or normals they'll simply be padded with zeros.

// For a robust/fully-compliant .obj parser look elsewhere.
// In truth the file format has many flaws and for a real
// game/3D application I would rather write a much simpler 
// custom file format!  

#pragma pack(push, 1)
struct VertexData
{
    float pos[3];
    float uv[2];
    float norm[3];
};
#pragma pack(pop)

struct LoadedObj
{
    uint32_t numVertices;
    uint32_t numIndices;

    VertexData* vertexBuffer;
    uint16_t* indexBuffer;
};

// Returns a vertex and index buffer loaded from .obj file 'filename'.
// Vertex buffer format: (tightly packed)
//   vp.x, vp.y, vp.z, vt.u, vt.v, vn.x, vn.y, vn.z ...
// Allocates buffers using malloc().
//
// Usage:
// LoadedObj myObj = loadObj("test.obj");
// ... // Send myObj.vertexBuffer to GPU
// ... // Send myObj.indexBuffer to GPU
// freeLoadedObj(myObj);
LoadedObj loadObj(const char* filename);
void freeLoadedObj(LoadedObj loadedObj);