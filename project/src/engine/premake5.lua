project "Engine"
	language "C++"
	architecture "x64"
	kind "SharedLib"
	staticruntime "off"
	
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")
	files
	{

		"**.h",
		"**.cpp",
		"**.hpp",
		"**.hlsl",
		"**.lua",
		"**.hlsli",
		thirdparty .. "/glm/include/**.hpp",
		thirdparty .. "/glm/include/**.inl",
		thirdparty .. "/vulkanmemoryallocator/include/*.h",
		thirdparty .. "/vulkanmemoryallocator/src/*.cpp",
		thirdparty .. "/ImGui/**.h",
		thirdparty .. "/ImGui/**.hpp",
		thirdparty .. "/ImGui/**.cpp",
		thirdparty .. "/tlsf/**.h",
		thirdparty .. "/tlsf/**.c",
		-- editor begin 修改原因：节点画布真库必须与 ImGui 同处 Engine.dll，Editor 只通过 Engine facade 使用。
		-- 仅显式列出核心 4 个 cpp，避免把 examples/external 带进构建。
		thirdparty .. "/imgui-node-editor/imgui_node_editor.cpp",
		thirdparty .. "/imgui-node-editor/imgui_node_editor_api.cpp",
		thirdparty .. "/imgui-node-editor/imgui_canvas.cpp",
		thirdparty .. "/imgui-node-editor/crude_json.cpp",
		-- editor end
	}

	includedirs
	{
		".",
		thirdparty .. "/ImGui",
		-- editor begin 修改原因：暴露给 Engine 内部 facade 编译使用，不让 Editor 直接 include 第三方节点编辑器头。
		thirdparty .. "/imgui-node-editor",
		-- editor end
		thirdparty .. "/GLFW/include",
		thirdparty .. "/spdlog/include",
		thirdparty .. "/glad/include",
		thirdparty .. "/VulkanSDK/include",
		thirdparty .. "/volk/include",
		thirdparty .. "/vulkanmemoryallocator/include",
		thirdparty .. "/glm/include",
		thirdparty .. "/entt/include",
		thirdparty .. "/tiny_obj_loader",
		thirdparty .. "/tiny_gltf",
		thirdparty .. "/openFBX/include",
		thirdparty .. "/mio",
		thirdparty .. "/stb",
		thirdparty .. "/Json",
		thirdparty .. "/SPIRV-Cross",
		thirdparty .. "/Eigen",
		thirdparty .. "/meshoptimizer/include",
		thirdparty .. "/tlsf",
		thirdparty .. "/wyhash",
		thirdparty .. "/thsvs",
		assetsdir,
	}
	if not _OPTIONS["no-tracy"] then
		includedirs { thirdparty .. "/tracy" }
	end
	links
	{
		"GLFW",
		"spdlog",
		"Glad",
		"volk",
		"OFBX",
		"spirv-cross",
		"meshoptimizer",
	}

	filter "files:**.hlsl"
		buildaction "None"

	filter {}

	filter {"system:windows", "configurations:Debug"}
		system "Windows"
		systemversion "latest"
		staticruntime "Off"
		defines
		{
			"ASH_HAS_VULKAN",
			"ASH_HAS_DX12",
			"ASH_HAS_DXC",
		}
		defines
		{
			"_UNICODE",
            "UNICODE",
			"_CONSOLE",
			"GLFW_INCLUDE_NONE",
			"ASH_WINDOWS",
			"ASH_ENGINE",
			"NOMINMAX",
			"WIN32_LEAN_AND_MEAN",
		}
		includedirs
		{
			thirdparty .. "/D3D12MA/include",
			thirdparty .. "/dxc/inc",
		}
		defines
		{
			"ASH_DEBUG",
			"VULKAN_SYNCHRONIZATION_VALIDATION",
			"VULKAN_DEBUG_REPORT",
		}
		if not _OPTIONS["no-tracy"] then
			defines
			{
				"TRACY_ENABLE",
				"TRACY_ON_DEMAND",
			}
		end
		links
		{
			"dbghelp",
			"psapi",
			"d3d12",
			"dxgi",
			"dxguid",
			"D3D12MA",
		}
		if not _OPTIONS["no-tracy"] then
			links { "tracy" }
		end
		runtime "Debug"
		symbols "on"
		editandcontinue "Off"
		optimize "off"
		links {
			thirdparty.."/dxc/lib/x64/dxcompiler.lib",
		}
		dest_dir = rootdir .. "/product/bin64"	
		debugcommand (rootdir .. "/product/bin64/Debug-windows-x86_64/Editor.exe")
		debugdir (rootdir .. "/product/bin64/Debug-windows-x86_64")			  
		postbuildcommands 
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Debug") .. "\\Engine.dll", false),
			sync_runtime_artifact_command("$(TargetDir)Engine.pdb", product_runtime_dir("Debug") .. "\\Engine.pdb", true),
			sync_runtime_artifact_command(dxc_redist_dir .. "\\dxcompiler.dll", product_runtime_dir("Debug") .. "\\dxcompiler.dll", false),
			sync_runtime_artifact_command(dxc_redist_dir .. "\\dxil.dll", product_runtime_dir("Debug") .. "\\dxil.dll", false),
			sync_runtime_artifact_command(vulkan_validation_layer_redist_dir .. "\\VkLayer_khronos_validation.json", product_runtime_dir("Debug") .. "\\vulkan_layers\\VkLayer_khronos_validation.json", false),
			sync_runtime_artifact_command(vulkan_validation_layer_redist_dir .. "\\VkLayer_khronos_validation.dll", product_runtime_dir("Debug") .. "\\vulkan_layers\\VkLayer_khronos_validation.dll", false),
		}
	filter {"system:windows","configurations:Release"}
		system "Windows"
		systemversion "latest"
		staticruntime "Off"
		defines
		{
			"ASH_HAS_VULKAN",
			"ASH_HAS_DX12",
			"ASH_HAS_DXC",
		}
		defines
		{
			"_UNICODE",
            "UNICODE",
			"_CONSOLE",
			"GLFW_INCLUDE_NONE",
			"ASH_WINDOWS",
			"ASH_ENGINE",
			"NOMINMAX",
			"WIN32_LEAN_AND_MEAN",
		}
		includedirs
		{
			thirdparty .. "/D3D12MA/include",
			thirdparty .. "/dxc/inc",
		}
		defines "ASH_RELEASE"
		if not _OPTIONS["no-tracy"] then
			defines
			{
				"TRACY_ENABLE",
				"TRACY_ON_DEMAND",
			}
		end
		runtime "Release"
		optimize "on"
		links {
			"dbghelp",
			"psapi",
			"d3d12",
			"dxgi",
			"dxguid",
			"D3D12MA",
			thirdparty.."/dxc/lib/x64/dxcompiler.lib",
		}
		if not _OPTIONS["no-tracy"] then
			links { "tracy" }
		end
		debugcommand (rootdir .. "/product/bin64/Release-windows-x86_64/Editor.exe")
		debugdir (rootdir .. "/product/bin64/Release-windows-x86_64")
		postbuildcommands 
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Release") .. "\\Engine.dll", false),
			sync_runtime_artifact_command("$(TargetDir)Engine.pdb", product_runtime_dir("Release") .. "\\Engine.pdb", true),
			sync_runtime_artifact_command(dxc_redist_dir .. "\\dxcompiler.dll", product_runtime_dir("Release") .. "\\dxcompiler.dll", false),
			sync_runtime_artifact_command(dxc_redist_dir .. "\\dxil.dll", product_runtime_dir("Release") .. "\\dxil.dll", false),
		}

	filter "system:not windows"
		removefiles
		{
			"Graphics/DirectX12/**",
			"Graphics/DXC/**",
			thirdparty .. "/ImGui/imgui_impl_dx12.h",
			thirdparty .. "/ImGui/imgui_impl_dx12.cpp",
		}
		

	
	
