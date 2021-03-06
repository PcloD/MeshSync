#pragma once
#include "msIdentifier.h"

namespace ms {

// Mesh

struct MeshDataFlags
{
    uint32_t has_refine_settings : 1;
    uint32_t has_indices : 1;
    uint32_t has_counts : 1;
    uint32_t has_points : 1;
    uint32_t has_normals : 1;
    uint32_t has_tangents : 1;
    uint32_t has_uv0 : 1;
    uint32_t has_uv1 : 1;
    uint32_t has_colors : 1;
    uint32_t has_velocities : 1; // 10
    uint32_t has_material_ids : 1;
    uint32_t has_bones : 1;
    uint32_t has_blendshape_weights : 1;
    uint32_t has_blendshapes : 1;
    uint32_t apply_trs : 1;
};

struct MeshRefineFlags
{
    uint32_t split : 1;
    uint32_t no_reindexing : 1;
    uint32_t triangulate : 1;
    uint32_t optimize_topology : 1;
    uint32_t swap_handedness : 1;
    uint32_t swap_yz : 1;
    uint32_t swap_faces : 1;
    uint32_t gen_normals : 1;
    uint32_t gen_normals_with_smooth_angle : 1;
    uint32_t flip_normals : 1; // 10
    uint32_t gen_tangents : 1;
    uint32_t apply_local2world : 1;
    uint32_t apply_world2local : 1;
    uint32_t bake_skin : 1;
    uint32_t bake_cloth : 1;

    uint32_t flip_u : 1;
    uint32_t flip_v : 1;
    uint32_t mirror_x : 1;
    uint32_t mirror_y : 1;
    uint32_t mirror_z : 1; // 20
    uint32_t mirror_x_weld : 1;
    uint32_t mirror_y_weld : 1;
    uint32_t mirror_z_weld : 1;
    uint32_t mirror_basis : 1;
    uint32_t make_double_sided : 1;
};

struct MeshRefineSettings
{
    MeshRefineFlags flags = { 0 };
    float scale_factor = 1.0f;
    float smooth_angle = 0.0f; // in degree
    uint32_t split_unit = 65000;
    uint32_t max_bones_par_vertices = 4;
    float4x4 local2world = float4x4::identity();
    float4x4 world2local = float4x4::identity();
    float4x4 mirror_basis = float4x4::identity();

    uint64_t checksum() const;
};

struct SubmeshData
{
    enum class Topology
    {
        Points,
        Lines,
        Triangles,
        Quads,
    };

    IArray<int> indices;
    Topology topology = Topology::Triangles;
    int material_id = 0;
};

struct SplitData
{
    int index_count = 0;
    int index_offset = 0;
    int vertex_count = 0;
    int vertex_offset = 0;
    IArray<SubmeshData> submeshes;
    float3 bound_center = float3::zero();
    float3 bound_size = float3::zero();
};

struct BlendShapeFrameData
{
    float weight = 0.0f;
    RawVector<float3> points;
    RawVector<float3> normals;
    RawVector<float3> tangents;

protected:
    BlendShapeFrameData();
    ~BlendShapeFrameData();
public:
    msDefinePool(BlendShapeFrameData);
    static std::shared_ptr<BlendShapeFrameData> create(std::istream& is);
    void serialize(std::ostream& os) const;
    void deserialize(std::istream& is);
    void clear();

    void convertHandedness(bool x, bool yz);
    void applyScaleFactor(float scale);
};
msSerializable(BlendShapeFrameData);
msDeclPtr(BlendShapeFrameData);

struct BlendShapeData
{
    std::string name;
    float weight = 0.0f;
    std::vector<BlendShapeFrameDataPtr> frames;

protected:
    BlendShapeData();
    ~BlendShapeData();
public:
    msDefinePool(BlendShapeData);
    static std::shared_ptr<BlendShapeData> create(std::istream& is);
    void serialize(std::ostream& os) const;
    void deserialize(std::istream& is);
    void clear();

    void sort();
    void convertHandedness(bool x, bool yz);
    void applyScaleFactor(float scale);
};
msSerializable(BlendShapeData);
msDeclPtr(BlendShapeData);

struct BoneData 
{
    std::string path;
    float4x4 bindpose = float4x4::identity();
    RawVector<float> weights;

protected:
    BoneData();
    ~BoneData();
public:
    msDefinePool(BoneData);
    static std::shared_ptr<BoneData> create(std::istream& is);
    void serialize(std::ostream& os) const;
    void deserialize(std::istream& is);
    void clear();

    void convertHandedness(bool x, bool yz);
    void applyScaleFactor(float scale);
};
msSerializable(BoneData);
msDeclPtr(BoneData);

class Mesh : public Transform
{
using super = Transform;
public:
    MeshDataFlags      flags = { 0 };
    MeshRefineSettings refine_settings;

    RawVector<float3> points;
    RawVector<float3> normals;
    RawVector<float4> tangents;
    RawVector<float2> uv0, uv1;
    RawVector<float4> colors;
    RawVector<float3> velocities;
    RawVector<int>    counts;
    RawVector<int>    indices;
    RawVector<int>    material_ids;

    std::string root_bone;
    std::vector<BoneDataPtr> bones;
    std::vector<BlendShapeDataPtr> blendshapes;

    // non-serializable
    RawVector<float3> tmp_normals;
    RawVector<float2> tmp_uv0, tmp_uv1;
    RawVector<float4> tmp_colors;
    RawVector<float3> tmp_velocities;
    RawVector<int> remap_normals, remap_uv0, remap_uv1, remap_colors;
    RawVector<Weights4> tmp_weights4;

    RawVector<Weights4> weights4;
    std::vector<SubmeshData> submeshes;
    std::vector<SplitData> splits;


protected:
    Mesh();
    ~Mesh() override;
public:
    msDefinePool(Mesh);
    Type getType() const override;
    bool isGeometry() const override;
    void serialize(std::ostream& os) const override;
    void deserialize(std::istream& is) override;
    void clear() override;
    uint64_t hash() const override;
    uint64_t checksumGeom() const override;
    bool lerp(const Entity& src1, const Entity& src2, float t) override;
    EntityPtr clone() override;

    void convertHandedness(bool x, bool yz) override;
    void applyScaleFactor(float scale) override;

    void refine(const MeshRefineSettings& mrs);
    void makeDoubleSided();
    void applyMirror(const float3& plane_n, float plane_d, bool welding = false);
    void applyTransform(const float4x4& t);

    void setupBoneData();
    void setupFlags();

    void convertHandedness_Mesh(bool x, bool yz);
    void convertHandedness_BlendShapes(bool x, bool yz);
    void convertHandedness_Bones(bool x, bool yz);

    BoneDataPtr addBone(const std::string& path);
    BlendShapeDataPtr addBlendShape(const std::string& name);
};
msSerializable(Mesh);
msDeclPtr(Mesh);

} // namespace ms
