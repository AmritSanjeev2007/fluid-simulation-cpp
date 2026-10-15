#pragma once

#include "mesh.hpp"

#include <filesystem>

// Parses a Wavefront .obj file into an indexed triangle mesh.
//
// Supports v / vt / vn / f (any of the v, v/vt, v//vn, v/vt/vn forms, including
// negative indices). Polygons are fan-triangulated, identical v/vt/vn
// combinations are shared, and vertices without a normal get smooth normals
// computed from the surrounding faces. Materials, groups and other statements
// are ignored.
//
// Throws std::runtime_error (with file and line) if the file can't be read or is malformed.
MeshData loadObj(const std::filesystem::path& path);

// True if the path names a Wavefront .obj file (by extension, ignoring case).
bool isObjPath(const std::filesystem::path& path);
