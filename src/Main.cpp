///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for Reshade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
// 
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////

#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID unsigned long long // Change ImGui texture ID type to that of a 'reshade::api::resource_view' handle

#include <imgui.h>
#include <reshade.hpp>
#include "crc32_hash.hpp"
#include <map>
#include "ShaderManager.h"

using namespace reshade::api;

extern "C" __declspec(dllexport) const char *NAME = "Shader Toggler";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Add-on which allows you to define groups of shaders to toggle on/off with one key press.";

static thread_local ShaderToggler::ShaderManager g_pixelShaderManager;
static thread_local ShaderToggler::ShaderManager g_vertexShaderManager;
static thread_local std::map<const void*, uint32_t> g_shaderCodePointerToHash;
static bool g_osdInfoVisible = false;


static bool onCreatePipeline(device *device, pipeline_layout, uint32_t subobject_count, const pipeline_subobject *subobjects)
{
	// Go through all shader stages that are in this pipeline and calculate their hashes, then store that based on the code pointer.
	// we have to do that now, as after the shader has been passed to the driver the code is locked.
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		switch (subobjects[i].type)
		{
		case pipeline_subobject_type::vertex_shader:
		case pipeline_subobject_type::hull_shader:
		case pipeline_subobject_type::domain_shader:
		case pipeline_subobject_type::geometry_shader:
		case pipeline_subobject_type::pixel_shader:
		case pipeline_subobject_type::compute_shader:
			{
				const auto shaderDesc = *static_cast<shader_desc *>(subobjects[i].data);
				const auto shaderHash = compute_crc32(static_cast<const uint8_t *>(shaderDesc.code), shaderDesc.code_size);
				g_shaderCodePointerToHash[shaderDesc.code] = shaderHash;
			}
			break;
		}
	}

	return false;
}


static void onInitPipeline(device *device, pipeline_layout, uint32_t subobject_count, const pipeline_subobject *subobjects, pipeline shaderHandle)
{
	// shader has been created, we will now create a hash and store it with the handle we got.
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		switch (subobjects[i].type)
		{
		case pipeline_subobject_type::vertex_shader:
			{
				const auto shaderDesc = *static_cast<shader_desc *>(subobjects[i].data);
				g_vertexShaderManager.addHashHandlePair(g_shaderCodePointerToHash[shaderDesc.code], shaderHandle);
			}
			break;
		case pipeline_subobject_type::pixel_shader:
			{
				const auto shaderDesc = *static_cast<shader_desc *>(subobjects[i].data);
				g_pixelShaderManager.addHashHandlePair(g_shaderCodePointerToHash[shaderDesc.code], shaderHandle);
			}
			break;
		}
	}
	// we're done with the hashes so we can simply clear them
	//g_shaderCodePointerToHash.clear();
}


static void onReshadeOverlay(reshade::api::effect_runtime *runtime)
{
	if(g_osdInfoVisible)
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		if (!ImGui::Begin("ShaderTogglerInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | 
												  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}
		const ImVec4 foregroundColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, foregroundColor);
		ImGui::Text("# of vertex shaders gathered: %d. In hunting mode: %s", g_vertexShaderManager.getCount(), g_vertexShaderManager.isInHuntingMode() ? "true" : "false");
		ImGui::Text("# of pixel shaders gathered: %d. In hunting mode: %s", g_pixelShaderManager.getCount(), g_pixelShaderManager.isInHuntingMode() ? "true" : "false");
		if(g_pixelShaderManager.isInHuntingMode())
		{
			ImGui::Text("Current selected pixel shader: %d / %d", g_pixelShaderManager.getActiveHuntedShaderIndex(), g_pixelShaderManager.getCount());
		}
		if(g_vertexShaderManager.isInHuntingMode())
		{
			ImGui::Text("Current selected vertex shader: %d / %d", g_vertexShaderManager.getActiveHuntedShaderIndex(), g_vertexShaderManager.getCount());
		}
		ImGui::PopStyleColor();
		ImGui::End();
	}
}


//#error THIS GIVES A PROBLEM WITH MULTI_THREADED ENGINES

static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline shaderHandle)
{
	if(nullptr!=commandList && shaderHandle.handle!=0)
	{
		switch(stages)
		{
			case pipeline_stage::all:
			case pipeline_stage::all_graphics:
				// dx12
				break;	
			case pipeline_stage::pixel_shader:
				g_pixelShaderManager.setBoundShaderHandlePerCommandList(commandList, shaderHandle.handle);
				break;
			case pipeline_stage::vertex_shader:
				g_vertexShaderManager.setBoundShaderHandlePerCommandList(commandList, shaderHandle.handle);
				break;
		}
	}
}


bool blockDrawCallForCommandList(command_list* commandList)
{
	if(nullptr==commandList)
	{
		return false;
	}

	bool blockCall=g_pixelShaderManager.isBlockedShader(commandList);
	blockCall|=g_vertexShaderManager.isBlockedShader(commandList);
	return blockCall;
}


static bool onDraw(command_list* commandList, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	// check if for this command list the active shader handles are part of the blocked set. If so, return true
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawIndexed(command_list* commandList, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	// same as onDraw
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawOrDispatchIndirect(command_list* commandList, indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride)
{
	switch(type)
	{
		case indirect_command::unknown:
		case indirect_command::draw:
		case indirect_command::draw_indexed: 
			// same as OnDraw
			return blockDrawCallForCommandList(commandList);
		// the rest aren't blocked processed
	}
	// no blocking for compute shaders.
	return false;
}


static void onReshadePresent(effect_runtime* runtime)
{
	// handle key presses. For now hardcoded, pixel shaders only
	// Keys:
	// END: toggle hunting mode
	// PageUp: next shader
	// PageDown: previous shader
	if(runtime->is_key_pressed(VK_END))
	{
		g_pixelShaderManager.toggleHuntingMode();
		g_vertexShaderManager.toggleHuntingMode();
	}
	if(runtime->is_key_pressed(VK_NUMPAD1))
	{
		g_pixelShaderManager.huntPreviousShader();
	}
	if(runtime->is_key_pressed(VK_NUMPAD2))
	{
		g_pixelShaderManager.huntNextShader();
	}
	if(runtime->is_key_pressed(VK_NUMPAD4))
	{
		g_vertexShaderManager.huntPreviousShader();
	}
	if(runtime->is_key_pressed(VK_NUMPAD5))
	{
		g_vertexShaderManager.huntNextShader();
	}

	// clear command list - shader handle pairs
	g_vertexShaderManager.clearBoundShaderHandlesPerCommandList();
	g_pixelShaderManager.clearBoundShaderHandlesPerCommandList();
}


static void displaySettings(reshade::api::effect_runtime *runtime)
{
	ImGui::Checkbox("Show OSD Info", &g_osdInfoVisible);	
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		if (!reshade::register_addon(hModule))
		{
			return FALSE;
		}
		reshade::register_event<reshade::addon_event::create_pipeline>(onCreatePipeline);
		reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
		reshade::register_event<reshade::addon_event::reshade_present>(onReshadePresent);
		reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::register_event<reshade::addon_event::draw>(onDraw);
		reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
		reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
		reshade::register_overlay(nullptr, &displaySettings);
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
		reshade::unregister_event<reshade::addon_event::create_pipeline>(onCreatePipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
		reshade::unregister_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::unregister_event<reshade::addon_event::draw>(onDraw);
		reshade::unregister_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
		reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
		reshade::unregister_overlay(nullptr, &displaySettings);
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}
