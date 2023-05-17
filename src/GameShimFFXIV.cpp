#include "GameShimFFXIV.h"

using namespace Shim::Resources;
using namespace reshade::api;

sig_ffxiv_texture_create* GameShimFFXIV::org_ffxiv_texture_create = nullptr;
sig_ffxiv_textures_create* GameShimFFXIV::org_ffxiv_textures_create = nullptr;

uintptr_t* GameShimFFXIV::p1 = nullptr;
uintptr_t* GameShimFFXIV::p2 = nullptr;
uintptr_t GameShimFFXIV::p1_1 = 0;

bool GameShimFFXIV::Init()
{
    return
        GameHookT<sig_ffxiv_textures_create>::Hook(&org_ffxiv_textures_create, detour_ffxiv_textures_create, ffxiv_textures_create) &&
        GameHookT<sig_ffxiv_texture_create>::Hook(&org_ffxiv_texture_create, detour_ffxiv_texture_create, ffxiv_texture_create);
}

bool GameShimFFXIV::UnInit()
{
    return MH_Uninitialize() == MH_OK;
}

bool GameShimFFXIV::OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state)
{
    if (static_cast<uint32_t>(desc.usage & resource_usage::render_target) && desc.type == resource_type::texture_2d && p1 != nullptr)
    {
        uintptr_t rtindex = reinterpret_cast<uintptr_t>(p1) - (p1_1);

        switch (rtindex)
        {
        case RT_UI:
            desc.texture.format = format::r10g10b10a2_typeless;
            return true;
        case RT_NORMALS:
        case RT_NORMALS_DECAL:
            desc.texture.format = format::r16g16b16a16_unorm;
            return true;
        default:
            return false;
        }
    }

    return false;
}


void GameShimFFXIV::OnDestroyResource(reshade::api::device* device, reshade::api::resource res)
{

}

void GameShimFFXIV::OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initData, reshade::api::resource_usage usage, reshade::api::resource handle)
{

}

bool GameShimFFXIV::OnCreateResourceView(reshade::api::device* device, reshade::api::resource resource, reshade::api::resource_usage usage_type, reshade::api::resource_view_desc& desc)
{
    const resource_desc texture_desc = device->get_resource_desc(resource);
    if(!static_cast<uint32_t>(texture_desc.usage & resource_usage::render_target) || texture_desc.type != resource_type::texture_2d || p1 == nullptr)
        return false;

    if (desc.type == resource_view_type::unknown)
    {
        desc.type = texture_desc.texture.depth_or_layers > 1 ? resource_view_type::texture_2d_array : resource_view_type::texture_2d;
        desc.texture.first_level = 0;
        desc.texture.level_count = (usage_type == resource_usage::shader_resource) ? UINT32_MAX : 1;
        desc.texture.first_layer = 0;
        desc.texture.layer_count = (usage_type == resource_usage::shader_resource) ? UINT32_MAX : 1;
    }

    uintptr_t rtindex = reinterpret_cast<uintptr_t>(p1) - (p1_1);

    switch (rtindex)
    {
    case RT_UI:
        desc.format = format::r10g10b10a2_unorm;
        return true;
    case RT_NORMALS:
    case RT_NORMALS_DECAL:
        desc.format = format::r16g16b16a16_unorm;
        return true;
    default:
        return false;
    }

    return false;
}

void __fastcall GameShimFFXIV::detour_ffxiv_texture_create(uintptr_t* param_1, uintptr_t* param_2)
{
    p1 = param_1;

    org_ffxiv_texture_create(param_1, param_2);

    p1 = nullptr;
}

void __fastcall GameShimFFXIV::detour_ffxiv_textures_create(uintptr_t param_1)
{
    p1_1 = param_1;

    org_ffxiv_textures_create(param_1);
}