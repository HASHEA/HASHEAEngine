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
		"**.lua",
		assetsdir.."/ASH-shaders/**.hshader",
		thirdparty .. "/glm/include/**.hpp",
		thirdparty .. "/glm/include/**.inl",
		thirdparty .. "/vulkanmemoryallocator/include/*.h",
		thirdparty .. "/vulkanmemoryallocator/src/*.cpp",
		thirdparty .. "/ImGui/**.h",
		thirdparty .. "/ImGui/**.hpp",
		thirdparty .. "/ImGui/**.cpp",
		thirdparty .. "/tlsf/**.h",
		thirdparty .. "/tlsf/**.c",
	}

	includedirs
	{
		".",
		thirdparty .. "/ImGui",
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
		thirdparty .. "/tracy",
		thirdparty .. "/Eigen",
		thirdparty .. "/meshoptimizer/include",
		thirdparty .. "/tlsf",
		thirdparty .. "/wyhash",
		thirdparty .. "/thsvs",
		assetsdir,
	}
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
		}
		includedirs
		{
			thirdparty .. "/D3D12MA/include",
			thirdparty .. "/dxc/inc",
		}
		defines
		{
			"ASH_DEBUG",
			"TRACY_ENABLE",
			"VULKAN_SYNCHRONIZATION_VALIDATION",
			"VULKAN_DEBUG_REPORT",
		}
		links
		{
			"tracy",
			"dbghelp",
			"d3d12",
			"dxgi",
			"dxguid",
			"D3D12MA",
		}
		runtime "Debug"
		symbols "on"
		editandcontinue "Off"
		optimize "off"
		includedirs
		{
			thirdparty .. "/glslang/debug/windows-x64/include",
		}
		links {
			thirdparty.."/glslang/debug/windows-x64/lib/glslangd.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/SPIRVd.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/OGLCompilerd.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/MachineIndependentd.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/GenericCodeGend.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/SPIRV-Toolsd.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/SPIRV-Tools-optd.lib",
			thirdparty.."/glslang/debug/windows-x64/lib/OSDependentd.lib",
			thirdparty.."/dxc/lib/x64/dxcompiler.lib",
		}	
		dest_dir = rootdir .. "/product/bin64"	
		debugcommand (rootdir .. "/product/bin64/Debug-windows-x86_64/Editor.exe")
		debugdir (rootdir .. "/product/bin64/Debug-windows-x86_64")			  
		postbuildcommands 
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Debug") .. "\\Engine.dll", false),
			sync_runtime_artifact_command("$(TargetDir)Engine.pdb", product_runtime_dir("Debug") .. "\\Engine.pdb", true),
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
		}
		includedirs
		{
			thirdparty .. "/D3D12MA/include",
			thirdparty .. "/dxc/inc",
		}
		defines "ASH_RELEASE"
		runtime "Release"
		optimize "on"	
		includedirs
		{
			thirdparty .. "/glslang/release/windows-x64/include",
		}
		links {
			"dbghelp",
			"d3d12",
			"dxgi",
			"dxguid",
			"D3D12MA",
			thirdparty.."/glslang/release/windows-x64/lib/glslang.lib",
			thirdparty.."/glslang/release/windows-x64/lib/SPIRV.lib",
			thirdparty.."/glslang/release/windows-x64/lib/OGLCompiler.lib",
			thirdparty.."/glslang/release/windows-x64/lib/MachineIndependent.lib",
			thirdparty.."/glslang/release/windows-x64/lib/GenericCodeGen.lib",
			thirdparty.."/glslang/release/windows-x64/lib/SPIRV-Tools.lib",
			thirdparty.."/glslang/release/windows-x64/lib/SPIRV-Tools-opt.lib",
			thirdparty.."/glslang/release/windows-x64/lib/OSDependent.lib",
			thirdparty.."/dxc/lib/x64/dxcompiler.lib",
		}	
		debugcommand (rootdir .. "/product/bin64/Release-windows-x86_64/Editor.exe")
		debugdir (rootdir .. "/product/bin64/Release-windows-x86_64")
		postbuildcommands 
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Release") .. "\\Engine.dll", false),
			sync_runtime_artifact_command("$(TargetDir)Engine.pdb", product_runtime_dir("Release") .. "\\Engine.pdb", true),
		}

	filter "system:not windows"
		removefiles
		{
			"Graphics/DirectX12/**",
			"Graphics/DXC/**",
			thirdparty .. "/ImGui/imgui_impl_dx12.h",
			thirdparty .. "/ImGui/imgui_impl_dx12.cpp",
		}
		

	
	
