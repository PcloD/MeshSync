﻿#include "pch.h"
#include "MayaUtils.h"
#include "MeshSyncClientMaya.h"
#include "Commands.h"

#ifdef _WIN32
#pragma comment(lib, "Foundation.lib")
#pragma comment(lib, "OpenMaya.lib")
#pragma comment(lib, "OpenMayaAnim.lib")
#endif



static void OnIdle(float elapsedTime, float lastTime, void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->update();
}

static void OnSceneLoadBegin(void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onSceneLoadBegin();
}

static void OnSceneLoadEnd(void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onSceneLoadEnd();
}

static void OnNodeRenamed(void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onNodeRenamed();
}

static void OnDagChange(MDagMessage::DagMessage msg, MDagPath &child, MDagPath &parent, void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onSceneUpdated();
}

static void OnTimeChange(MTime& time, void* _this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onTimeChange(time);
}

static void OnNodeRemoved(MObject& node, void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onNodeRemoved(node);
}


static void OnTransformUpdated(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& other_plug, void *_this)
{
    if (msg == MNodeMessage::kAttributeEval) { return; }
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onNodeUpdated(plug.node());
}

static void OnCameraUpdated(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& other_plug, void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onNodeUpdated(plug.node());
}

static void OnLightUpdated(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& other_plug, void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onNodeUpdated(plug.node());
}

static void OnMeshUpdated(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& other_plug, void *_this)
{
    reinterpret_cast<MeshSyncClientMaya*>(_this)->onNodeUpdated(plug.node());
}


void MeshSyncClientMaya::onNodeUpdated(const MObject & node)
{
    m_dagnode_records[node].dirty = true;
}

void MeshSyncClientMaya::onNodeRemoved(const MObject & node)
{
    {
        auto it = m_dagnode_records.find(node);
        if (it != m_dagnode_records.end()) {
            for (auto tn : it->second.branches) {
                m_deleted.push_back(tn->path);
            }
            m_dagnode_records.erase(it);
        }
    }
}

void MeshSyncClientMaya::onNodeRenamed()
{
    bool needs_update = false;
    for (auto& tn : m_tree_pool) {
        if (checkRename(tn.get()))
            needs_update = true;
    }

    if (needs_update) {
        m_scene_updated = true;
    }
}

void MeshSyncClientMaya::onSceneUpdated()
{
    m_scene_updated = true;
}

void MeshSyncClientMaya::onSceneLoadBegin()
{
    m_ignore_update = true;
}

void MeshSyncClientMaya::onSceneLoadEnd()
{
    m_ignore_update = false;
}

void MeshSyncClientMaya::onTimeChange(const MTime & time)
{
    if (m_settings.auto_sync) {
        m_pending_scope = SendScope::All;
        // timer callback won't be called while scrubbing time slider. so call update() immediately
        update();
    }
}


void MeshSyncClientMaya::TaskRecord::add(const task_t & task)
{
    tasks.push_back(task);
}

void MeshSyncClientMaya::TaskRecord::process()
{
    for (auto& t : tasks)
        t();
    tasks.clear();
}


static std::unique_ptr<MeshSyncClientMaya> g_plugin;


MeshSyncClientMaya& MeshSyncClientMaya::getInstance()
{
    return *g_plugin;
}

MeshSyncClientMaya::MeshSyncClientMaya(MObject obj)
    : m_obj(obj)
    , m_iplugin(obj, msVendor, msReleaseDateStr)
{
#define Body(CmdType) m_iplugin.registerCommand(CmdType::name(), CmdType::create, CmdType::createSyntax);
    EachCommand(Body)
#undef Body

    registerGlobalCallbacks();
}

MeshSyncClientMaya::~MeshSyncClientMaya()
{
    waitAsyncSend();
    removeNodeCallbacks();
    removeGlobalCallbacks();
#define Body(CmdType) m_iplugin.deregisterCommand(CmdType::name());
    EachCommand(Body)
#undef Body
}

bool MeshSyncClientMaya::isSending() const
{
    if (m_future_send.valid()) {
        return m_future_send.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    }
    return false;
}

void MeshSyncClientMaya::waitAsyncSend()
{
    if (m_future_send.valid()) {
        m_future_send.wait_for(std::chrono::milliseconds(m_settings.timeout_ms));
    }
}

void MeshSyncClientMaya::registerGlobalCallbacks()
{
    MStatus stat;
    m_cids_global.push_back(MTimerMessage::addTimerCallback(0.03f, OnIdle, this, &stat));
    m_cids_global.push_back(MSceneMessage::addCallback(MSceneMessage::kBeforeNew, OnSceneLoadBegin, this, &stat));
    m_cids_global.push_back(MSceneMessage::addCallback(MSceneMessage::kBeforeOpen, OnSceneLoadBegin, this, &stat));
    m_cids_global.push_back(MSceneMessage::addCallback(MSceneMessage::kAfterNew, OnSceneLoadEnd, this, &stat));
    m_cids_global.push_back(MSceneMessage::addCallback(MSceneMessage::kAfterOpen, OnSceneLoadEnd, this, &stat));
    m_cids_global.push_back(MDagMessage::addAllDagChangesCallback(OnDagChange, this, &stat));
    m_cids_global.push_back(MEventMessage::addEventCallback("NameChanged", OnNodeRenamed, this, &stat));
    m_cids_global.push_back(MDGMessage::addTimeChangeCallback(OnTimeChange, this));
    m_cids_global.push_back(MDGMessage::addNodeRemovedCallback(OnNodeRemoved, kDefaultNodeType, this));

    // shut up warning about blendshape
    MGlobal::executeCommand("cycleCheck -e off");
}

void MeshSyncClientMaya::registerNodeCallbacks()
{
    // joints
    EnumerateNode(MFn::kJoint, [this](MObject& node) {
        auto& rec = m_dagnode_records[node];
        if (!rec.cid)
            rec.cid = MNodeMessage::addAttributeChangedCallback(node, OnTransformUpdated, this);
    });

    // cameras
    EnumerateNode(MFn::kCamera, [this](MObject& node) {
        MFnDagNode fn(node);
        if (!fn.isIntermediateObject()) {
            auto& rec = m_dagnode_records[node];
            if (!rec.cid)
                rec.cid = MNodeMessage::addAttributeChangedCallback(node, OnCameraUpdated, this);
        }
    });

    // lights
    EnumerateNode(MFn::kLight, [this](MObject& node) {
        MFnDagNode fn(node);
        if (!fn.isIntermediateObject()) {
            auto& rec = m_dagnode_records[node];
            if (!rec.cid)
                rec.cid = MNodeMessage::addAttributeChangedCallback(node, OnLightUpdated, this);
        }
    });

    //  meshes
    EnumerateNode(MFn::kMesh, [this](MObject& node) {
        MFnDagNode fn(node);
        if (!fn.isIntermediateObject()) {
            auto& rec = m_dagnode_records[node];
            if (!rec.cid)
                m_dagnode_records[node].cid = MNodeMessage::addAttributeChangedCallback(node, OnMeshUpdated, this);
        }
    });
}

void MeshSyncClientMaya::removeGlobalCallbacks()
{
    for (auto& cid : m_cids_global) {
        MMessage::removeCallback(cid);
    }
    m_cids_global.clear();
}

void MeshSyncClientMaya::removeNodeCallbacks()
{
    for (auto& rec : m_dagnode_records) {
        if (rec.second.cid) {
            MMessage::removeCallback(rec.second.cid);
            rec.second.cid = 0;
        }
    }
}


int MeshSyncClientMaya::getMaterialID(const MString& name)
{
    auto it = std::find(m_material_id_table.begin(), m_material_id_table.end(), name);
    return it != m_material_id_table.end() ? (int)std::distance(m_material_id_table.begin(), it) : -1;
}



void MeshSyncClientMaya::constructTree()
{
    m_tree_roots.clear();
    m_tree_pool.clear();
    for (auto& kvp : m_dagnode_records)
        kvp.second.branches.clear();
    m_index_seed = 0;

    EnumerateNode(MFn::kTransform, [&](MObject& node) {
        if (MFnDagNode(node).parent(0).hasFn(MFn::kWorld)) {
            constructTree(node, nullptr, "");
        }
    });
}

void MeshSyncClientMaya::constructTree(const MObject& node, TreeNode *parent, const std::string& base)
{
    auto shape = GetShape(node);
    std::string name = GetName(node);
    std::string path = base;
    path += '/';
    path += name;

    auto& rec_node = m_dagnode_records[node];
    auto& rec_shape = m_dagnode_records[shape];
    if (rec_node.node.isNull()) {
        rec_node.node = node;
        rec_shape.node = shape;
    }

    auto *tn = new TreeNode();
    m_tree_pool.emplace_back(tn);
    tn->trans = &rec_node;
    tn->shape = &rec_shape;
    tn->name = name;
    tn->path = path;
    tn->index = ++m_index_seed;
    tn->parent = parent;

    if (parent)
        parent->children.push_back(tn);
    else
        m_tree_roots.push_back(tn);

    rec_node.branches.push_back(tn);
    rec_shape.branches.push_back(tn);

    EachChild(node, [&](const MObject & c) {
        if (c.hasFn(MFn::kTransform))
            constructTree(c, tn, path);
    });
}

bool MeshSyncClientMaya::checkRename(TreeNode *tn)
{
    if (tn->name != GetName(tn->trans->node)) {
        m_deleted.push_back(tn->path);
        return true;
    }
    else {
        for (auto child : tn->children) {
            if (checkRename(child))
                return true;
        }
    }
    return false;
}


bool MeshSyncClientMaya::sendScene(SendScope scope)
{
    if (isSending()) {
        m_pending_scope = scope;
        return false;
    }
    m_pending_scope = SendScope::None;

    int num_exported = 0;
    auto export_branches = [&](DAGNode& rec) {
        for (auto *tn : rec.branches) {
            if (exportObject(tn, false))
                ++num_exported;
        }
        rec.dirty = false;
    };

    if (scope == SendScope::All) {
        auto handler = [&](MObject& node) {
            export_branches(m_dagnode_records[node]);
        };
        EnumerateNode(MFn::kJoint, handler);
        EnumerateNode(MFn::kCamera, handler);
        EnumerateNode(MFn::kLight, handler);
        EnumerateNode(MFn::kMesh, handler);
    }
    else if (scope == SendScope::Updated) {
        for (auto& kvp : m_dagnode_records) {
            auto& rec = kvp.second;
            if (rec.dirty)
                export_branches(rec);
        }
    }
    else if (scope == SendScope::Selected) {
        MSelectionList list;
        MGlobal::getActiveSelectionList(list);
        for (uint32_t i = 0; i < list.length(); i++) {
            MObject node;
            list.getDependNode(i, node);
            export_branches(m_dagnode_records[node]);
        }
    }

    if (num_exported > 0 || !m_deleted.empty()) {
        kickAsyncSend();
        return true;
    }
    else {
        return false;
    }
}

bool MeshSyncClientMaya::sendAnimations(SendScope scope)
{
    // wait for previous request to complete
    if (m_future_send.valid()) {
        m_future_send.get();
    }

    if (exportAnimations(scope) > 0) {
        kickAsyncSend();
        return true;
    }
    else {
        return false;
    }
}

void MeshSyncClientMaya::update()
{
    if (m_ignore_update)
        return;

    if (m_scene_updated) {
        m_scene_updated = false;

        constructTree();
        registerNodeCallbacks();
        if (m_settings.auto_sync) {
            m_pending_scope = SendScope::All;
        }
    }

    if (m_pending_scope != SendScope::None) {
        sendScene(m_pending_scope);
    }
    else if (m_settings.auto_sync) {
        sendScene(SendScope::Updated);
    }
}


void MeshSyncClientMaya::kickAsyncSend()
{
    // process parallel extract tasks
    if (!m_extract_tasks.empty()) {
        if (m_settings.multithreaded) {
            mu::parallel_for_each(m_extract_tasks.begin(), m_extract_tasks.end(), [](TaskRecords::value_type& kvp) {
                kvp.second.process();
            });
        }
        else {
            for (auto& kvp : m_extract_tasks) {
                kvp.second.process();
            }
        }
        m_extract_tasks.clear();
    }

    // cleanup
    for (auto& tn : m_tree_pool) {
        tn->added = false;
    }
    m_material_id_table.clear();


    float to_meter = 1.0f;
    {
        MDistance dist;
        dist.setValue(1.0f);
        to_meter = (float)dist.asMeters();
    }

    // begin async send
    m_future_send = std::async(std::launch::async, [this, to_meter]() {
        ms::Client client(m_settings.client_settings);

        ms::SceneSettings scene_settings;
        scene_settings.handedness = ms::Handedness::Right;
        scene_settings.scale_factor = m_settings.scale_factor / to_meter;

        // notify scene begin
        {
            ms::FenceMessage fence;
            fence.type = ms::FenceMessage::FenceType::SceneBegin;
            client.send(fence);
        }

        // send delete message
        size_t num_deleted = m_deleted.size();
        if (num_deleted) {
            ms::DeleteMessage del;
            del.targets.resize(num_deleted);
            for (uint32_t i = 0; i < num_deleted; ++i)
                del.targets[i].path = m_deleted[i];
            m_deleted.clear();

            client.send(del);
        }

        // send scene data
        {
            ms::SetMessage set;
            set.scene.settings  = scene_settings;
            set.scene.objects = m_objects;
            set.scene.materials = m_materials;
            client.send(set);

            m_objects.clear();
            m_materials.clear();
        }

        // send meshes one by one to Unity can respond quickly
        for(auto& mesh : m_meshes) {
            ms::SetMessage set;
            set.scene.settings = scene_settings;
            set.scene.objects = { mesh };
            client.send(set);
        };
        m_meshes.clear();

        // send animations and constraints
        if (!m_animations.empty() || !m_constraints.empty()) {
            ms::SetMessage set;
            set.scene.settings = scene_settings;
            set.scene.animations = m_animations;
            set.scene.constraints = m_constraints;
            client.send(set);

            m_animations.clear();
            m_constraints.clear();
        }

        // notify scene end
        {
            ms::FenceMessage fence;
            fence.type = ms::FenceMessage::FenceType::SceneEnd;
            client.send(fence);
        }
    });
}

bool MeshSyncClientMaya::recvScene()
{
    waitAsyncSend();

    ms::Client client(m_settings.client_settings);
    ms::GetMessage gd;
    gd.flags.get_transform = 1;
    gd.flags.get_indices = 1;
    gd.flags.get_points = 1;
    gd.flags.get_uv0 = 1;
    gd.flags.get_uv1 = 1;
    gd.flags.get_material_ids = 1;
    gd.scene_settings.handedness = ms::Handedness::Right;
    gd.scene_settings.scale_factor = 1.0f / m_settings.scale_factor;
    gd.refine_settings.flags.bake_skin = m_settings.bake_skin;
    gd.refine_settings.flags.bake_cloth = m_settings.bake_cloth;

    auto ret = client.send(gd);
    if (!ret) {
        return false;
    }


    // todo: 

    return true;
}




MStatus initializePlugin(MObject obj)
{
    g_plugin.reset(new MeshSyncClientMaya(obj));
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    g_plugin.reset();
    return MS::kSuccess;
}
