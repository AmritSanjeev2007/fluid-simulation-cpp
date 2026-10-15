#include "obj_loader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    // 1-based indices into the position / texCoord / normal lists; 0 = not given.
    struct FaceVertexKey
    {
        int position = 0;
        int texCoord = 0;
        int normal   = 0;

        bool operator==(const FaceVertexKey&) const = default;
    };

    struct FaceVertexKeyHash
    {
        std::size_t operator()(const FaceVertexKey& k) const noexcept
        {
            std::size_t h = std::hash<int>{}(k.position);
            h ^= std::hash<int>{}(k.texCoord) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(k.normal) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    class LineParser
    {
    public:
        LineParser(std::string_view line, const std::filesystem::path& path, int lineNumber)
            : m_rest(line), m_path(path), m_lineNumber(lineNumber) {}

        // Next whitespace-separated token, or empty if none remain.
        std::string_view token()
        {
            const std::size_t start = m_rest.find_first_not_of(" \t\r");
            if (start == std::string_view::npos)
            {
                m_rest = {};
                return {};
            }
            m_rest.remove_prefix(start);
            const std::size_t end = std::min(m_rest.find_first_of(" \t\r"), m_rest.size());
            const std::string_view tok = m_rest.substr(0, end);
            m_rest.remove_prefix(end);
            return tok;
        }

        float number()
        {
            const std::string_view tok = token();
            float value = 0.0f;
            const auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), value);
            if (tok.empty() || ec != std::errc{} || ptr != tok.data() + tok.size())
                fail("expected a number, got '" + std::string(tok) + "'");
            return value;
        }

        [[noreturn]] void fail(const std::string& message) const
        {
            throw std::runtime_error(m_path.string() + ":" + std::to_string(m_lineNumber) + ": " + message);
        }

    private:
        std::string_view             m_rest;
        const std::filesystem::path& m_path;
        int                          m_lineNumber;
    };

    // Converts an OBJ index (1-based, or negative = relative to the end) to a 1-based index.
    int resolveIndex(std::string_view text, std::size_t count, const LineParser& parser)
    {
        if (text.empty())
            return 0;

        int index = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), index);
        if (ec != std::errc{} || ptr != text.data() + text.size() || index == 0)
            parser.fail("invalid index '" + std::string(text) + "'");

        const int resolved = index > 0 ? index : static_cast<int>(count) + index + 1;
        if (resolved < 1 || resolved > static_cast<int>(count))
            parser.fail("index " + std::to_string(index) + " is out of range");
        return resolved;
    }
}

MeshData loadObj(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Could not open OBJ file: " + path.string());

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;

    MeshData mesh;
    std::unordered_map<FaceVertexKey, std::uint32_t, FaceVertexKeyHash> vertexLookup;
    std::vector<bool> needsNormal;  // per output vertex: true if the file gave no normal

    std::vector<std::uint32_t> polygon;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;
        LineParser parser(line, path, lineNumber);
        const std::string_view keyword = parser.token();

        if (keyword == "v")
        {
            const float x = parser.number();
            const float y = parser.number();
            const float z = parser.number();
            positions.emplace_back(x, y, z);
        }
        else if (keyword == "vt")
        {
            const float u = parser.number();
            const float v = parser.number();
            texCoords.emplace_back(u, v);
        }
        else if (keyword == "vn")
        {
            const float x = parser.number();
            const float y = parser.number();
            const float z = parser.number();
            normals.push_back(glm::normalize(glm::vec3(x, y, z)));
        }
        else if (keyword == "f")
        {
            polygon.clear();
            for (std::string_view tok = parser.token(); !tok.empty(); tok = parser.token())
            {
                // Split "v/vt/vn" (vt and vn optional) into its three parts.
                const std::size_t slash1 = tok.find('/');
                const std::size_t slash2 = slash1 == std::string_view::npos ? std::string_view::npos
                                                                            : tok.find('/', slash1 + 1);
                const std::string_view posText = tok.substr(0, slash1);
                const std::string_view uvText  = slash1 == std::string_view::npos
                                                     ? std::string_view{}
                                                     : tok.substr(slash1 + 1, slash2 - slash1 - 1);
                const std::string_view nrmText = slash2 == std::string_view::npos
                                                     ? std::string_view{}
                                                     : tok.substr(slash2 + 1);

                if (posText.empty())
                    parser.fail("face vertex '" + std::string(tok) + "' has no position index");

                const FaceVertexKey key{
                    resolveIndex(posText, positions.size(), parser),
                    resolveIndex(uvText, texCoords.size(), parser),
                    resolveIndex(nrmText, normals.size(), parser),
                };

                auto [it, inserted] = vertexLookup.try_emplace(key, static_cast<std::uint32_t>(mesh.vertices.size()));
                if (inserted)
                {
                    Vertex& v  = mesh.vertices.emplace_back();
                    v.position = positions[key.position - 1];
                    if (key.texCoord)
                        v.texCoord = texCoords[key.texCoord - 1];
                    if (key.normal)
                        v.normal = normals[key.normal - 1];
                    needsNormal.push_back(key.normal == 0);
                }
                polygon.push_back(it->second);
            }

            if (polygon.size() < 3)
                parser.fail("face has fewer than 3 vertices");

            // Fan triangulation (exact for triangles and convex polygons).
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
                mesh.indices.insert(mesh.indices.end(), { polygon[0], polygon[i], polygon[i + 1] });
        }
        // Everything else (comments, o, g, s, mtllib, usemtl, ...) is ignored.
    }

    if (mesh.indices.empty())
        throw std::runtime_error("OBJ file contains no faces: " + path.string());

    // Smooth normals for vertices that had none: area-weighted sum of adjacent face normals.
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        Vertex& a = mesh.vertices[mesh.indices[i]];
        Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);

        for (std::uint32_t index : { mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] })
            if (needsNormal[index])
                mesh.vertices[index].normal += faceNormal;
    }
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        glm::vec3& n = mesh.vertices[i].normal;
        if (needsNormal[i])
            n = glm::length(n) > 0.0f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    return mesh;
}

bool isObjPath(const std::filesystem::path& path)
{
    std::u8string extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char8_t c) { return static_cast<char8_t>(std::tolower(static_cast<unsigned char>(c))); });
    return extension == u8".obj";
}
