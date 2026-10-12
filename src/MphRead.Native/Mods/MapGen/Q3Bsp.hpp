#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <charconv>
#include <cmath>
#include <memory>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class Q3String final
    {
    public:
        Q3String() noexcept = default;
        Q3String(std::nullptr_t) noexcept {}
        Q3String(std::string value)
            : _value(std::make_shared<std::string>(std::move(value))) {}
        Q3String(const char* value)
            : _value(value == nullptr ? nullptr : std::make_shared<std::string>(value)) {}

        [[nodiscard]] bool HasValue() const noexcept { return _value != nullptr; }
        [[nodiscard]] const std::string* Get() const noexcept { return _value.get(); }
        [[nodiscard]] const std::string& Value() const
        {
            if (!_value) throw std::invalid_argument("String reference is null.");
            return *_value;
        }
        [[nodiscard]] bool empty() const noexcept { return !_value || _value->empty(); }
        [[nodiscard]] std::size_t size() const noexcept { return _value ? _value->size() : 0; }
        [[nodiscard]] const char* data() const noexcept { return _value ? _value->data() : nullptr; }
        [[nodiscard]] const char* c_str() const noexcept { return _value ? _value->c_str() : nullptr; }
        explicit operator bool() const noexcept { return HasValue(); }
        operator const std::string&() const { return Value(); }

        friend bool operator==(const Q3String& left, const Q3String& right) noexcept
        {
            if (!left._value || !right._value) return left._value == right._value;
            return *left._value == *right._value;
        }
        friend bool operator!=(const Q3String& left, const Q3String& right) noexcept
        {
            return !(left == right);
        }
        friend bool operator==(const Q3String& left, const std::string& right) noexcept
        {
            return left._value != nullptr && *left._value == right;
        }
        friend bool operator==(const std::string& left, const Q3String& right) noexcept
        {
            return right == left;
        }

    private:
        std::shared_ptr<std::string> _value{};
    };

    struct Q3StringHash
    {
        [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept;
    };

    struct Q3StringEqual
    {
        [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const noexcept;
    };

    class Q3Entity final
    {
    public:
        using value_type = std::pair<std::string, std::string>;
        using Storage = std::vector<value_type>;
        using const_iterator = Storage::const_iterator;

        [[nodiscard]] std::string& operator[](const std::string& key)
        {
            for (value_type& entry : _entries)
            {
                if (Q3StringEqual{}(entry.first, key))
                {
                    return entry.second;
                }
            }
            _entries.emplace_back(key, std::string{});
            return _entries.back().second;
        }

        [[nodiscard]] const_iterator find(const std::string& key) const noexcept
        {
            for (auto iterator = _entries.cbegin(); iterator != _entries.cend(); ++iterator)
            {
                if (Q3StringEqual{}(iterator->first, key))
                {
                    return iterator;
                }
            }
            return _entries.cend();
        }

        [[nodiscard]] const_iterator begin() const noexcept { return _entries.cbegin(); }
        [[nodiscard]] const_iterator end() const noexcept { return _entries.cend(); }
        [[nodiscard]] const_iterator cbegin() const noexcept { return _entries.cbegin(); }
        [[nodiscard]] const_iterator cend() const noexcept { return _entries.cend(); }
        [[nodiscard]] std::size_t size() const noexcept { return _entries.size(); }
        [[nodiscard]] bool empty() const noexcept { return _entries.empty(); }

    private:
        Storage _entries{};
    };
    using Q3FloatArray = std::shared_ptr<std::vector<float>>;
    using Q3IntArray = std::shared_ptr<std::vector<std::int32_t>>;
    using Q3ByteArray = std::shared_ptr<std::vector<std::uint8_t>>;

    class Q3Texture
    {
    public:
        Q3Texture(
            Q3String name,
            std::int32_t flags,
            std::int32_t contents) noexcept;
        Q3Texture(
            std::nullptr_t name,
            std::int32_t flags,
            std::int32_t contents) noexcept;
        Q3Texture(
            std::string name,
            std::int32_t flags,
            std::int32_t contents);
        Q3Texture(
            const char* name,
            std::int32_t flags,
            std::int32_t contents);

        virtual ~Q3Texture() = default;
        Q3Texture(Q3Texture&&) = delete;
        Q3Texture& operator=(const Q3Texture&) = delete;
        Q3Texture& operator=(Q3Texture&&) = delete;

        [[nodiscard]] const Q3String& Name() const noexcept;
        [[nodiscard]] std::int32_t Flags() const noexcept;
        [[nodiscard]] std::int32_t Contents() const noexcept;

        [[nodiscard]] virtual bool Equals(const Q3Texture* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3Texture> Clone() const;

        void Deconstruct(
            Q3String& name,
            std::int32_t& flags,
            std::int32_t& contents) const noexcept;
        void Deconstruct(
            std::string& name,
            std::int32_t& flags,
            std::int32_t& contents) const;

        [[nodiscard]] std::shared_ptr<Q3Texture> With(
            std::optional<Q3String> name = std::nullopt,
            std::optional<std::int32_t> flags = std::nullopt,
            std::optional<std::int32_t> contents = std::nullopt) const;

    protected:
        Q3Texture(const Q3Texture&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        Q3String _name;
        std::int32_t _flags;
        std::int32_t _contents;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3Texture>& left,
        const std::shared_ptr<Q3Texture>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3Texture>& left,
        const std::shared_ptr<Q3Texture>& right) noexcept;

    class Q3Plane
    {
    public:
        Q3Plane(
            float x,
            float y,
            float z,
            float distance) noexcept;

        virtual ~Q3Plane() = default;
        Q3Plane(Q3Plane&&) = delete;
        Q3Plane& operator=(const Q3Plane&) = delete;
        Q3Plane& operator=(Q3Plane&&) = delete;

        [[nodiscard]] float X() const noexcept;
        [[nodiscard]] float Y() const noexcept;
        [[nodiscard]] float Z() const noexcept;
        [[nodiscard]] float Distance() const noexcept;

        [[nodiscard]] virtual bool Equals(const Q3Plane* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3Plane> Clone() const;

        void Deconstruct(
            float& x,
            float& y,
            float& z,
            float& distance) const noexcept;

        [[nodiscard]] std::shared_ptr<Q3Plane> With(
            std::optional<float> x = std::nullopt,
            std::optional<float> y = std::nullopt,
            std::optional<float> z = std::nullopt,
            std::optional<float> distance = std::nullopt) const;

    protected:
        Q3Plane(const Q3Plane&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        float _x;
        float _y;
        float _z;
        float _distance;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3Plane>& left,
        const std::shared_ptr<Q3Plane>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3Plane>& left,
        const std::shared_ptr<Q3Plane>& right) noexcept;

    class Q3Brush
    {
    public:
        Q3Brush(
            std::int32_t firstSide,
            std::int32_t sideCount,
            std::int32_t texture) noexcept;

        virtual ~Q3Brush() = default;
        Q3Brush(Q3Brush&&) = delete;
        Q3Brush& operator=(const Q3Brush&) = delete;
        Q3Brush& operator=(Q3Brush&&) = delete;

        [[nodiscard]] std::int32_t FirstSide() const noexcept;
        [[nodiscard]] std::int32_t SideCount() const noexcept;
        [[nodiscard]] std::int32_t Texture() const noexcept;

        [[nodiscard]] virtual bool Equals(const Q3Brush* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3Brush> Clone() const;

        void Deconstruct(
            std::int32_t& firstSide,
            std::int32_t& sideCount,
            std::int32_t& texture) const noexcept;

        [[nodiscard]] std::shared_ptr<Q3Brush> With(
            std::optional<std::int32_t> firstSide = std::nullopt,
            std::optional<std::int32_t> sideCount = std::nullopt,
            std::optional<std::int32_t> texture = std::nullopt) const;

    protected:
        Q3Brush(const Q3Brush&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        std::int32_t _firstSide;
        std::int32_t _sideCount;
        std::int32_t _texture;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3Brush>& left,
        const std::shared_ptr<Q3Brush>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3Brush>& left,
        const std::shared_ptr<Q3Brush>& right) noexcept;

    class Q3BrushSide
    {
    public:
        Q3BrushSide(
            std::int32_t plane,
            std::int32_t texture) noexcept;

        virtual ~Q3BrushSide() = default;
        Q3BrushSide(Q3BrushSide&&) = delete;
        Q3BrushSide& operator=(const Q3BrushSide&) = delete;
        Q3BrushSide& operator=(Q3BrushSide&&) = delete;

        [[nodiscard]] std::int32_t Plane() const noexcept;
        [[nodiscard]] std::int32_t Texture() const noexcept;

        [[nodiscard]] virtual bool Equals(const Q3BrushSide* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3BrushSide> Clone() const;

        void Deconstruct(
            std::int32_t& plane,
            std::int32_t& texture) const noexcept;

        [[nodiscard]] std::shared_ptr<Q3BrushSide> With(
            std::optional<std::int32_t> plane = std::nullopt,
            std::optional<std::int32_t> texture = std::nullopt) const;

    protected:
        Q3BrushSide(const Q3BrushSide&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        std::int32_t _plane;
        std::int32_t _texture;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3BrushSide>& left,
        const std::shared_ptr<Q3BrushSide>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3BrushSide>& left,
        const std::shared_ptr<Q3BrushSide>& right) noexcept;

    class Q3Vertex
    {
    public:
        Q3Vertex(
            Q3FloatArray position,
            Q3FloatArray surface,
            Q3FloatArray normal,
            Q3ByteArray color) noexcept;

        virtual ~Q3Vertex() = default;
        Q3Vertex(Q3Vertex&&) = delete;
        Q3Vertex& operator=(const Q3Vertex&) = delete;
        Q3Vertex& operator=(Q3Vertex&&) = delete;

        [[nodiscard]] Q3FloatArray Position() const noexcept;
        [[nodiscard]] Q3FloatArray Surface() const noexcept;
        [[nodiscard]] Q3FloatArray Normal() const noexcept;
        [[nodiscard]] Q3ByteArray Color() const noexcept;
        // Where the vertex reads its face's lightmap, 0 to 1 across the page.
        std::array<float, 2> Lightmap{};

        [[nodiscard]] virtual bool Equals(const Q3Vertex* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3Vertex> Clone() const;

        void Deconstruct(
            Q3FloatArray& position,
            Q3FloatArray& surface,
            Q3FloatArray& normal,
            Q3ByteArray& color) const noexcept;

        [[nodiscard]] std::shared_ptr<Q3Vertex> With(
            std::optional<Q3FloatArray> position = std::nullopt,
            std::optional<Q3FloatArray> surface = std::nullopt,
            std::optional<Q3FloatArray> normal = std::nullopt,
            std::optional<Q3ByteArray> color = std::nullopt) const;

    protected:
        Q3Vertex(const Q3Vertex&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        Q3FloatArray _position;
        Q3FloatArray _surface;
        Q3FloatArray _normal;
        Q3ByteArray _color;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3Vertex>& left,
        const std::shared_ptr<Q3Vertex>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3Vertex>& left,
        const std::shared_ptr<Q3Vertex>& right) noexcept;

    class Q3Face
    {
    public:
        Q3Face(
            std::int32_t texture,
            std::int32_t effect,
            std::int32_t type,
            std::int32_t vertex,
            std::int32_t vertexCount,
            std::int32_t meshVert,
            std::int32_t meshVertCount,
            Q3FloatArray normal,
            Q3IntArray size) noexcept;

        virtual ~Q3Face() = default;
        Q3Face(Q3Face&&) = delete;
        Q3Face& operator=(const Q3Face&) = delete;
        Q3Face& operator=(Q3Face&&) = delete;

        [[nodiscard]] std::int32_t Texture() const noexcept;
        [[nodiscard]] std::int32_t Effect() const noexcept;
        [[nodiscard]] std::int32_t Type() const noexcept;
        [[nodiscard]] std::int32_t Vertex() const noexcept;
        [[nodiscard]] std::int32_t VertexCount() const noexcept;
        [[nodiscard]] std::int32_t MeshVert() const noexcept;
        [[nodiscard]] std::int32_t MeshVertCount() const noexcept;
        [[nodiscard]] Q3FloatArray Normal() const noexcept;
        [[nodiscard]] Q3IntArray Size() const noexcept;
        // Its lightmap page, -1 for a surface lit by its vertices alone.
        std::int32_t LightmapIndex = -1;
        // Its block of that page, in luxels: x, y, width, height.
        std::array<std::int32_t, 4> LightmapRect{};

        [[nodiscard]] virtual bool Equals(const Q3Face* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3Face> Clone() const;

        void Deconstruct(
            std::int32_t& texture,
            std::int32_t& effect,
            std::int32_t& type,
            std::int32_t& vertex,
            std::int32_t& vertexCount,
            std::int32_t& meshVert,
            std::int32_t& meshVertCount,
            Q3FloatArray& normal,
            Q3IntArray& size) const noexcept;

        [[nodiscard]] std::shared_ptr<Q3Face> With(
            std::optional<std::int32_t> texture = std::nullopt,
            std::optional<std::int32_t> effect = std::nullopt,
            std::optional<std::int32_t> type = std::nullopt,
            std::optional<std::int32_t> vertex = std::nullopt,
            std::optional<std::int32_t> vertexCount = std::nullopt,
            std::optional<std::int32_t> meshVert = std::nullopt,
            std::optional<std::int32_t> meshVertCount = std::nullopt,
            std::optional<Q3FloatArray> normal = std::nullopt,
            std::optional<Q3IntArray> size = std::nullopt) const;

    protected:
        Q3Face(const Q3Face&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        std::int32_t _texture;
        std::int32_t _effect;
        std::int32_t _type;
        std::int32_t _vertex;
        std::int32_t _vertexCount;
        std::int32_t _meshVert;
        std::int32_t _meshVertCount;
        Q3FloatArray _normal;
        Q3IntArray _size;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3Face>& left,
        const std::shared_ptr<Q3Face>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3Face>& left,
        const std::shared_ptr<Q3Face>& right) noexcept;

    class Q3Model
    {
    public:
        Q3Model(
            Q3FloatArray mins,
            Q3FloatArray maxs,
            std::int32_t face,
            std::int32_t faceCount,
            std::int32_t brush,
            std::int32_t brushCount) noexcept;

        virtual ~Q3Model() = default;
        Q3Model(Q3Model&&) = delete;
        Q3Model& operator=(const Q3Model&) = delete;
        Q3Model& operator=(Q3Model&&) = delete;

        [[nodiscard]] Q3FloatArray Mins() const noexcept;
        [[nodiscard]] Q3FloatArray Maxs() const noexcept;
        [[nodiscard]] std::int32_t Face() const noexcept;
        [[nodiscard]] std::int32_t FaceCount() const noexcept;
        [[nodiscard]] std::int32_t Brush() const noexcept;
        [[nodiscard]] std::int32_t BrushCount() const noexcept;

        [[nodiscard]] virtual bool Equals(const Q3Model* other) const noexcept;
        [[nodiscard]] virtual std::int32_t GetHashCode() const noexcept;
        [[nodiscard]] virtual std::string ToString() const;
        [[nodiscard]] virtual std::shared_ptr<Q3Model> Clone() const;

        void Deconstruct(
            Q3FloatArray& mins,
            Q3FloatArray& maxs,
            std::int32_t& face,
            std::int32_t& faceCount,
            std::int32_t& brush,
            std::int32_t& brushCount) const noexcept;

        [[nodiscard]] std::shared_ptr<Q3Model> With(
            std::optional<Q3FloatArray> mins = std::nullopt,
            std::optional<Q3FloatArray> maxs = std::nullopt,
            std::optional<std::int32_t> face = std::nullopt,
            std::optional<std::int32_t> faceCount = std::nullopt,
            std::optional<std::int32_t> brush = std::nullopt,
            std::optional<std::int32_t> brushCount = std::nullopt) const;

    protected:
        Q3Model(const Q3Model&) = default;
        [[nodiscard]] virtual const std::type_info& EqualityContract() const noexcept;
        virtual bool PrintMembers(std::string& builder) const;

    private:
        Q3FloatArray _mins;
        Q3FloatArray _maxs;
        std::int32_t _face;
        std::int32_t _faceCount;
        std::int32_t _brush;
        std::int32_t _brushCount;
    };

    [[nodiscard]] bool operator==(const std::shared_ptr<Q3Model>& left,
        const std::shared_ptr<Q3Model>& right) noexcept;
    [[nodiscard]] bool operator!=(const std::shared_ptr<Q3Model>& left,
        const std::shared_ptr<Q3Model>& right) noexcept;

    class Q3UsedLumps final
    {
    public:
        Q3UsedLumps()
            : _values(std::make_shared<std::array<std::int32_t, 13>>(
                std::array<std::int32_t, 13>{0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14})) {}

        [[nodiscard]] std::int32_t& operator[](std::size_t index) const { return _values->at(index); }
        [[nodiscard]] auto begin() const noexcept { return _values->begin(); }
        [[nodiscard]] auto end() const noexcept { return _values->end(); }
        [[nodiscard]] std::size_t size() const noexcept { return _values->size(); }

    private:
        std::shared_ptr<std::array<std::int32_t, 13>> _values;
    };

    class Q3Bsp
    {
    public:
        using TextureList = std::vector<std::shared_ptr<Q3Texture>>;
        using PlaneList = std::vector<std::shared_ptr<Q3Plane>>;
        using BrushList = std::vector<std::shared_ptr<Q3Brush>>;
        using BrushSideList = std::vector<std::shared_ptr<Q3BrushSide>>;
        using VertexList = std::vector<std::shared_ptr<Q3Vertex>>;
        using MeshVertList = std::vector<std::int32_t>;
        using FaceList = std::vector<std::shared_ptr<Q3Face>>;
        using ModelList = std::vector<std::shared_ptr<Q3Model>>;
        using EntityList = std::vector<std::shared_ptr<Q3Entity>>;

        static constexpr std::int32_t ContentsSolid = 0x1;
        static constexpr std::int32_t ContentsFog = 0x40;
        static constexpr std::int32_t ContentsPlayerClip = 0x10000;
        static constexpr std::int32_t SurfaceSky = 0x4;
        static constexpr std::int32_t SurfaceNoDraw = 0x80;
        static constexpr std::int32_t SurfaceHint = 0x100;
        static constexpr std::int32_t SurfaceSkip = 0x200;
        static constexpr std::int32_t SurfaceNoLightmap = 0x400;

        static const Q3UsedLumps UsedLumps;

        Q3Bsp() = default;
        Q3Bsp(const Q3Bsp&) = delete;
        Q3Bsp(Q3Bsp&&) = delete;
        Q3Bsp& operator=(const Q3Bsp&) = delete;
        Q3Bsp& operator=(Q3Bsp&&) = delete;

        [[nodiscard]] const TextureList& Textures() const noexcept;
        [[nodiscard]] const PlaneList& Planes() const noexcept;
        [[nodiscard]] const BrushList& Brushes() const noexcept;
        [[nodiscard]] const BrushSideList& BrushSides() const noexcept;
        [[nodiscard]] const VertexList& Vertices() const noexcept;
        [[nodiscard]] const MeshVertList& MeshVerts() const noexcept;
        [[nodiscard]] const FaceList& Faces() const noexcept;
        // The lightmap pages, each 128 by 128 RGB, one after another.
        [[nodiscard]] const std::vector<std::uint8_t>& Lightmaps() const noexcept { return _lightmaps; }
        static constexpr std::int32_t LightmapSize = 128;
        // A fog volume: its shader, its brush, and which of the brush's sides
        // (counted from its first) is the surface seen from outside, -1 none.
        // A face's Effect is the fog it is in.
        struct Fog
        {
            std::string Shader;
            std::int32_t Brush = 0;
            std::int32_t VisibleSide = -1;
        };
        [[nodiscard]] const std::vector<Fog>& Fogs() const noexcept { return _fogs; }
        // Whether a point (Quake's units and axes) is somewhere a player can
        // be: in a leaf of the tree that belongs to a cluster, not in the
        // solid or the void outside the level, which are in none.
        [[nodiscard]] bool Open(float x, float y, float z) const;
        [[nodiscard]] const ModelList& Models() const noexcept;
        [[nodiscard]] const EntityList& Entities() const noexcept;

        [[nodiscard]] static std::vector<std::uint8_t> Trim(const std::vector<std::uint8_t>& bsp);
        [[nodiscard]] static std::vector<std::uint8_t> Trim(const std::vector<std::uint8_t>* bsp);
        [[nodiscard]] static std::shared_ptr<Q3Bsp> Load(
            const std::string& source, const std::optional<std::string>& mapName);
        [[nodiscard]] static std::shared_ptr<Q3Bsp> Load(
            const std::string* source, const std::optional<std::string>& mapName);
        [[nodiscard]] static std::vector<std::uint8_t> ReadLevel(
            const std::string& source, const std::optional<std::string>& mapName);
        [[nodiscard]] static std::vector<std::uint8_t> ReadLevel(
            const std::string* source, const std::optional<std::string>& mapName);
        [[nodiscard]] static std::vector<std::string> ListMaps(const std::string& source);
        [[nodiscard]] static std::vector<std::string> ListMaps(const std::string* source);

    private:
        std::vector<std::uint8_t> _lightmaps{};
        std::vector<Fog> _fogs{};
        // The tree: each node's plane and its two children (a leaf as
        // -(leaf + 1)), and each leaf's cluster.
        struct Node
        {
            std::int32_t Plane = 0;
            std::int32_t Front = 0;
            std::int32_t Back = 0;
        };
        std::vector<Node> _nodes{};
        std::vector<std::int32_t> _leafClusters{};
        template <typename T>
        class ListStorage final
        {
        public:
            using List = std::vector<T>;

            ListStorage() = default;

            ListStorage& operator=(List value)
            {
                _value = std::move(value);
                _assigned = true;
                return *this;
            }

            [[nodiscard]] const List& Get() const noexcept
            {
                return _assigned ? _value : Empty();
            }

        private:
            [[nodiscard]] static const List& Empty() noexcept
            {
                static const List value{};
                return value;
            }

            List _value{};
            bool _assigned = false;
        };

        ListStorage<std::shared_ptr<Q3Texture>> _textures{};
        ListStorage<std::shared_ptr<Q3Plane>> _planes{};
        ListStorage<std::shared_ptr<Q3Brush>> _brushes{};
        ListStorage<std::shared_ptr<Q3BrushSide>> _brushSides{};
        ListStorage<std::shared_ptr<Q3Vertex>> _vertices{};
        ListStorage<std::int32_t> _meshVerts{};
        ListStorage<std::shared_ptr<Q3Face>> _faces{};
        ListStorage<std::shared_ptr<Q3Model>> _models{};
        ListStorage<std::shared_ptr<Q3Entity>> _entities{};

        [[nodiscard]] static std::shared_ptr<Q3Bsp> Parse(const std::vector<std::uint8_t>& bytes);
        [[nodiscard]] static EntityList ParseEntities(const std::string& text);
    };

    namespace Q3RecordRuntime
    {
        [[nodiscard]] std::uint32_t TypeHash(const std::type_info& type) noexcept;
        [[nodiscard]] std::uint32_t StringHash(const Q3String& value) noexcept;
        [[nodiscard]] std::uint32_t ReferenceHash(const void* value) noexcept;
        [[nodiscard]] std::string IntString(std::int32_t value);
        [[nodiscard]] std::string FloatString(float value);
    }

    namespace
    {
        constexpr std::uint32_t Q3RecordHashFactor = 0xA5555529U;

        [[nodiscard]] inline std::uint32_t RecordHashStart(const std::type_info& type) noexcept
        {
            return Q3RecordRuntime::TypeHash(type);
        }

        [[nodiscard]] inline std::uint32_t RecordHashNext(std::uint32_t hash, std::uint32_t value) noexcept
        {
            return hash * Q3RecordHashFactor + value;
        }

        [[nodiscard]] inline std::uint32_t IntHash(std::int32_t value) noexcept
        {
            return std::bit_cast<std::uint32_t>(value);
        }

        [[nodiscard]] inline bool FloatEquals(float left, float right) noexcept
        {
            return left == right || (std::isnan(left) && std::isnan(right));
        }

        [[nodiscard]] inline std::uint32_t FloatHash(float value) noexcept
        {
            std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
            if (((bits - 1U) & 0x7FFFFFFFU) >= 0x7F800000U)
            {
                bits &= 0x7F800000U;
            }
            return bits;
        }

        [[nodiscard]] inline std::uint32_t StringHash(const Q3String& value) noexcept
        {
            return Q3RecordRuntime::StringHash(value);
        }

        template <typename T>
        [[nodiscard]] inline std::uint32_t ReferenceHash(const std::shared_ptr<T>& value) noexcept
        {
            return Q3RecordRuntime::ReferenceHash(value.get());
        }

        [[nodiscard]] inline std::string FloatString(float value)
        {
            return Q3RecordRuntime::FloatString(value);
        }

        [[nodiscard]] inline std::string IntString(std::int32_t value)
        {
            return Q3RecordRuntime::IntString(value);
        }
    }

    inline const std::type_info& Q3Texture::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3Texture::Equals(const Q3Texture* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && _name == other->_name
                && _flags == other->_flags
                && _contents == other->_contents);
    }

    inline std::int32_t Q3Texture::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, StringHash(_name));
        hash = RecordHashNext(hash, IntHash(_flags));
        hash = RecordHashNext(hash, IntHash(_contents));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3Texture::PrintMembers(std::string& builder) const
    {
        builder += "Name = ";
        if (_name.HasValue()) builder += _name.Value();
        builder += ", ";
        builder += "Flags = ";
        builder += IntString(_flags);
        builder += ", ";
        builder += "Contents = ";
        builder += IntString(_contents);
        return true;
    }

    inline std::string Q3Texture::ToString() const
    {
        std::string builder = "Q3Texture { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3Texture> Q3Texture::Clone() const
    {
        return std::shared_ptr<Q3Texture>(new Q3Texture(*this));
    }

    inline void Q3Texture::Deconstruct(
        Q3String& name,
        std::int32_t& flags,
        std::int32_t& contents) const noexcept
    {
        name = _name;
        flags = _flags;
        contents = _contents;
    }

    inline void Q3Texture::Deconstruct(
        std::string& name,
        std::int32_t& flags,
        std::int32_t& contents) const
    {
        name = _name.Value();
        flags = _flags;
        contents = _contents;
    }

    inline std::shared_ptr<Q3Texture> Q3Texture::With(
        std::optional<Q3String> name,
        std::optional<std::int32_t> flags,
        std::optional<std::int32_t> contents) const
    {
        std::shared_ptr<Q3Texture> clone = Clone();
        if (name.has_value()) clone->_name = *name;
        if (flags.has_value()) clone->_flags = *flags;
        if (contents.has_value()) clone->_contents = *contents;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3Texture>& left,
        const std::shared_ptr<Q3Texture>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3Texture>& left,
        const std::shared_ptr<Q3Texture>& right) noexcept
    {
        return !(left == right);
    }

    inline const std::type_info& Q3Plane::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3Plane::Equals(const Q3Plane* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && FloatEquals(_x, other->_x)
                && FloatEquals(_y, other->_y)
                && FloatEquals(_z, other->_z)
                && FloatEquals(_distance, other->_distance));
    }

    inline std::int32_t Q3Plane::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, FloatHash(_x));
        hash = RecordHashNext(hash, FloatHash(_y));
        hash = RecordHashNext(hash, FloatHash(_z));
        hash = RecordHashNext(hash, FloatHash(_distance));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3Plane::PrintMembers(std::string& builder) const
    {
        builder += "X = ";
        builder += FloatString(_x);
        builder += ", ";
        builder += "Y = ";
        builder += FloatString(_y);
        builder += ", ";
        builder += "Z = ";
        builder += FloatString(_z);
        builder += ", ";
        builder += "Distance = ";
        builder += FloatString(_distance);
        return true;
    }

    inline std::string Q3Plane::ToString() const
    {
        std::string builder = "Q3Plane { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3Plane> Q3Plane::Clone() const
    {
        return std::shared_ptr<Q3Plane>(new Q3Plane(*this));
    }

    inline void Q3Plane::Deconstruct(
        float& x,
        float& y,
        float& z,
        float& distance) const noexcept
    {
        x = _x;
        y = _y;
        z = _z;
        distance = _distance;
    }

    inline std::shared_ptr<Q3Plane> Q3Plane::With(
        std::optional<float> x,
        std::optional<float> y,
        std::optional<float> z,
        std::optional<float> distance) const
    {
        std::shared_ptr<Q3Plane> clone = Clone();
        if (x.has_value()) clone->_x = *x;
        if (y.has_value()) clone->_y = *y;
        if (z.has_value()) clone->_z = *z;
        if (distance.has_value()) clone->_distance = *distance;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3Plane>& left,
        const std::shared_ptr<Q3Plane>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3Plane>& left,
        const std::shared_ptr<Q3Plane>& right) noexcept
    {
        return !(left == right);
    }

    inline const std::type_info& Q3Brush::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3Brush::Equals(const Q3Brush* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && _firstSide == other->_firstSide
                && _sideCount == other->_sideCount
                && _texture == other->_texture);
    }

    inline std::int32_t Q3Brush::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, IntHash(_firstSide));
        hash = RecordHashNext(hash, IntHash(_sideCount));
        hash = RecordHashNext(hash, IntHash(_texture));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3Brush::PrintMembers(std::string& builder) const
    {
        builder += "FirstSide = ";
        builder += IntString(_firstSide);
        builder += ", ";
        builder += "SideCount = ";
        builder += IntString(_sideCount);
        builder += ", ";
        builder += "Texture = ";
        builder += IntString(_texture);
        return true;
    }

    inline std::string Q3Brush::ToString() const
    {
        std::string builder = "Q3Brush { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3Brush> Q3Brush::Clone() const
    {
        return std::shared_ptr<Q3Brush>(new Q3Brush(*this));
    }

    inline void Q3Brush::Deconstruct(
        std::int32_t& firstSide,
        std::int32_t& sideCount,
        std::int32_t& texture) const noexcept
    {
        firstSide = _firstSide;
        sideCount = _sideCount;
        texture = _texture;
    }

    inline std::shared_ptr<Q3Brush> Q3Brush::With(
        std::optional<std::int32_t> firstSide,
        std::optional<std::int32_t> sideCount,
        std::optional<std::int32_t> texture) const
    {
        std::shared_ptr<Q3Brush> clone = Clone();
        if (firstSide.has_value()) clone->_firstSide = *firstSide;
        if (sideCount.has_value()) clone->_sideCount = *sideCount;
        if (texture.has_value()) clone->_texture = *texture;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3Brush>& left,
        const std::shared_ptr<Q3Brush>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3Brush>& left,
        const std::shared_ptr<Q3Brush>& right) noexcept
    {
        return !(left == right);
    }

    inline const std::type_info& Q3BrushSide::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3BrushSide::Equals(const Q3BrushSide* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && _plane == other->_plane
                && _texture == other->_texture);
    }

    inline std::int32_t Q3BrushSide::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, IntHash(_plane));
        hash = RecordHashNext(hash, IntHash(_texture));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3BrushSide::PrintMembers(std::string& builder) const
    {
        builder += "Plane = ";
        builder += IntString(_plane);
        builder += ", ";
        builder += "Texture = ";
        builder += IntString(_texture);
        return true;
    }

    inline std::string Q3BrushSide::ToString() const
    {
        std::string builder = "Q3BrushSide { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3BrushSide> Q3BrushSide::Clone() const
    {
        return std::shared_ptr<Q3BrushSide>(new Q3BrushSide(*this));
    }

    inline void Q3BrushSide::Deconstruct(
        std::int32_t& plane,
        std::int32_t& texture) const noexcept
    {
        plane = _plane;
        texture = _texture;
    }

    inline std::shared_ptr<Q3BrushSide> Q3BrushSide::With(
        std::optional<std::int32_t> plane,
        std::optional<std::int32_t> texture) const
    {
        std::shared_ptr<Q3BrushSide> clone = Clone();
        if (plane.has_value()) clone->_plane = *plane;
        if (texture.has_value()) clone->_texture = *texture;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3BrushSide>& left,
        const std::shared_ptr<Q3BrushSide>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3BrushSide>& left,
        const std::shared_ptr<Q3BrushSide>& right) noexcept
    {
        return !(left == right);
    }

    inline const std::type_info& Q3Vertex::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3Vertex::Equals(const Q3Vertex* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && _position == other->_position
                && _surface == other->_surface
                && _normal == other->_normal
                && _color == other->_color);
    }

    inline std::int32_t Q3Vertex::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, ReferenceHash(_position));
        hash = RecordHashNext(hash, ReferenceHash(_surface));
        hash = RecordHashNext(hash, ReferenceHash(_normal));
        hash = RecordHashNext(hash, ReferenceHash(_color));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3Vertex::PrintMembers(std::string& builder) const
    {
        builder += "Position = ";
        if (_position) builder += "System.Single[]";
        builder += ", ";
        builder += "Surface = ";
        if (_surface) builder += "System.Single[]";
        builder += ", ";
        builder += "Normal = ";
        if (_normal) builder += "System.Single[]";
        builder += ", ";
        builder += "Color = ";
        if (_color) builder += "System.Byte[]";
        return true;
    }

    inline std::string Q3Vertex::ToString() const
    {
        std::string builder = "Q3Vertex { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3Vertex> Q3Vertex::Clone() const
    {
        return std::shared_ptr<Q3Vertex>(new Q3Vertex(*this));
    }

    inline void Q3Vertex::Deconstruct(
        Q3FloatArray& position,
        Q3FloatArray& surface,
        Q3FloatArray& normal,
        Q3ByteArray& color) const noexcept
    {
        position = _position;
        surface = _surface;
        normal = _normal;
        color = _color;
    }

    inline std::shared_ptr<Q3Vertex> Q3Vertex::With(
        std::optional<Q3FloatArray> position,
        std::optional<Q3FloatArray> surface,
        std::optional<Q3FloatArray> normal,
        std::optional<Q3ByteArray> color) const
    {
        std::shared_ptr<Q3Vertex> clone = Clone();
        if (position.has_value()) clone->_position = *position;
        if (surface.has_value()) clone->_surface = *surface;
        if (normal.has_value()) clone->_normal = *normal;
        if (color.has_value()) clone->_color = *color;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3Vertex>& left,
        const std::shared_ptr<Q3Vertex>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3Vertex>& left,
        const std::shared_ptr<Q3Vertex>& right) noexcept
    {
        return !(left == right);
    }

    inline const std::type_info& Q3Face::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3Face::Equals(const Q3Face* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && _texture == other->_texture
                && _effect == other->_effect
                && _type == other->_type
                && _vertex == other->_vertex
                && _vertexCount == other->_vertexCount
                && _meshVert == other->_meshVert
                && _meshVertCount == other->_meshVertCount
                && _normal == other->_normal
                && _size == other->_size);
    }

    inline std::int32_t Q3Face::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, IntHash(_texture));
        hash = RecordHashNext(hash, IntHash(_effect));
        hash = RecordHashNext(hash, IntHash(_type));
        hash = RecordHashNext(hash, IntHash(_vertex));
        hash = RecordHashNext(hash, IntHash(_vertexCount));
        hash = RecordHashNext(hash, IntHash(_meshVert));
        hash = RecordHashNext(hash, IntHash(_meshVertCount));
        hash = RecordHashNext(hash, ReferenceHash(_normal));
        hash = RecordHashNext(hash, ReferenceHash(_size));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3Face::PrintMembers(std::string& builder) const
    {
        builder += "Texture = ";
        builder += IntString(_texture);
        builder += ", ";
        builder += "Effect = ";
        builder += IntString(_effect);
        builder += ", ";
        builder += "Type = ";
        builder += IntString(_type);
        builder += ", ";
        builder += "Vertex = ";
        builder += IntString(_vertex);
        builder += ", ";
        builder += "VertexCount = ";
        builder += IntString(_vertexCount);
        builder += ", ";
        builder += "MeshVert = ";
        builder += IntString(_meshVert);
        builder += ", ";
        builder += "MeshVertCount = ";
        builder += IntString(_meshVertCount);
        builder += ", ";
        builder += "Normal = ";
        if (_normal) builder += "System.Single[]";
        builder += ", ";
        builder += "Size = ";
        if (_size) builder += "System.Int32[]";
        return true;
    }

    inline std::string Q3Face::ToString() const
    {
        std::string builder = "Q3Face { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3Face> Q3Face::Clone() const
    {
        return std::shared_ptr<Q3Face>(new Q3Face(*this));
    }

    inline void Q3Face::Deconstruct(
        std::int32_t& texture,
        std::int32_t& effect,
        std::int32_t& type,
        std::int32_t& vertex,
        std::int32_t& vertexCount,
        std::int32_t& meshVert,
        std::int32_t& meshVertCount,
        Q3FloatArray& normal,
        Q3IntArray& size) const noexcept
    {
        texture = _texture;
        effect = _effect;
        type = _type;
        vertex = _vertex;
        vertexCount = _vertexCount;
        meshVert = _meshVert;
        meshVertCount = _meshVertCount;
        normal = _normal;
        size = _size;
    }

    inline std::shared_ptr<Q3Face> Q3Face::With(
        std::optional<std::int32_t> texture,
        std::optional<std::int32_t> effect,
        std::optional<std::int32_t> type,
        std::optional<std::int32_t> vertex,
        std::optional<std::int32_t> vertexCount,
        std::optional<std::int32_t> meshVert,
        std::optional<std::int32_t> meshVertCount,
        std::optional<Q3FloatArray> normal,
        std::optional<Q3IntArray> size) const
    {
        std::shared_ptr<Q3Face> clone = Clone();
        if (texture.has_value()) clone->_texture = *texture;
        if (effect.has_value()) clone->_effect = *effect;
        if (type.has_value()) clone->_type = *type;
        if (vertex.has_value()) clone->_vertex = *vertex;
        if (vertexCount.has_value()) clone->_vertexCount = *vertexCount;
        if (meshVert.has_value()) clone->_meshVert = *meshVert;
        if (meshVertCount.has_value()) clone->_meshVertCount = *meshVertCount;
        if (normal.has_value()) clone->_normal = *normal;
        if (size.has_value()) clone->_size = *size;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3Face>& left,
        const std::shared_ptr<Q3Face>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3Face>& left,
        const std::shared_ptr<Q3Face>& right) noexcept
    {
        return !(left == right);
    }

    inline const std::type_info& Q3Model::EqualityContract() const noexcept
    {
        return typeid(*this);
    }

    inline bool Q3Model::Equals(const Q3Model* other) const noexcept
    {
        return this == other
            || (other != nullptr
                && EqualityContract() == other->EqualityContract()
                && _mins == other->_mins
                && _maxs == other->_maxs
                && _face == other->_face
                && _faceCount == other->_faceCount
                && _brush == other->_brush
                && _brushCount == other->_brushCount);
    }

    inline std::int32_t Q3Model::GetHashCode() const noexcept
    {
        std::uint32_t hash = RecordHashStart(EqualityContract());
        hash = RecordHashNext(hash, ReferenceHash(_mins));
        hash = RecordHashNext(hash, ReferenceHash(_maxs));
        hash = RecordHashNext(hash, IntHash(_face));
        hash = RecordHashNext(hash, IntHash(_faceCount));
        hash = RecordHashNext(hash, IntHash(_brush));
        hash = RecordHashNext(hash, IntHash(_brushCount));
        return std::bit_cast<std::int32_t>(hash);
    }

    inline bool Q3Model::PrintMembers(std::string& builder) const
    {
        builder += "Mins = ";
        if (_mins) builder += "System.Single[]";
        builder += ", ";
        builder += "Maxs = ";
        if (_maxs) builder += "System.Single[]";
        builder += ", ";
        builder += "Face = ";
        builder += IntString(_face);
        builder += ", ";
        builder += "FaceCount = ";
        builder += IntString(_faceCount);
        builder += ", ";
        builder += "Brush = ";
        builder += IntString(_brush);
        builder += ", ";
        builder += "BrushCount = ";
        builder += IntString(_brushCount);
        return true;
    }

    inline std::string Q3Model::ToString() const
    {
        std::string builder = "Q3Model { ";
        if (PrintMembers(builder))
        {
            builder += ' ';
        }
        builder += '}';
        return builder;
    }

    inline std::shared_ptr<Q3Model> Q3Model::Clone() const
    {
        return std::shared_ptr<Q3Model>(new Q3Model(*this));
    }

    inline void Q3Model::Deconstruct(
        Q3FloatArray& mins,
        Q3FloatArray& maxs,
        std::int32_t& face,
        std::int32_t& faceCount,
        std::int32_t& brush,
        std::int32_t& brushCount) const noexcept
    {
        mins = _mins;
        maxs = _maxs;
        face = _face;
        faceCount = _faceCount;
        brush = _brush;
        brushCount = _brushCount;
    }

    inline std::shared_ptr<Q3Model> Q3Model::With(
        std::optional<Q3FloatArray> mins,
        std::optional<Q3FloatArray> maxs,
        std::optional<std::int32_t> face,
        std::optional<std::int32_t> faceCount,
        std::optional<std::int32_t> brush,
        std::optional<std::int32_t> brushCount) const
    {
        std::shared_ptr<Q3Model> clone = Clone();
        if (mins.has_value()) clone->_mins = *mins;
        if (maxs.has_value()) clone->_maxs = *maxs;
        if (face.has_value()) clone->_face = *face;
        if (faceCount.has_value()) clone->_faceCount = *faceCount;
        if (brush.has_value()) clone->_brush = *brush;
        if (brushCount.has_value()) clone->_brushCount = *brushCount;
        return clone;
    }

    inline bool operator==(const std::shared_ptr<Q3Model>& left,
        const std::shared_ptr<Q3Model>& right) noexcept
    {
        return left.get() == right.get() || (left != nullptr && left->Equals(right.get()));
    }

    inline bool operator!=(const std::shared_ptr<Q3Model>& left,
        const std::shared_ptr<Q3Model>& right) noexcept
    {
        return !(left == right);
    }

}
