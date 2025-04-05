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
		"meshoptimizer"
	}

	local source_dir = "%{wks.location}/_BUILD/"..outputdir.."/bin/target/Engine/"
	local dest_dir = distdir
	postbuildcommands {

        "if not exist \""..dest_dir.."\" mkdir \""..dest_dir.."\"",
        "robocopy \""..source_dir.."\" \""..dest_dir.."\" Engine.dll Engine.pdb /NFL /NDL /NJH /NJS >nul || exit 0"
    }

	filter {"system:windows", "configurations:Debug"}
		system "Windows"
		systemversion "latest"
		staticruntime "Off"
		defines
		{
			"_UNICODE",
            "UNICODE",
			"_CONSOLE",
			"GLFW_INCLUDE_NONE",
			"ASH_WINDOWS",
			"ASH_ENGINE",
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

		}						  

	filter {"system:windows","configurations:Release"}
		system "Windows"
		systemversion "latest"
		staticruntime "Off"
		defines
		{
			"_UNICODE",
            "UNICODE",
			"_CONSOLE",
			"GLFW_INCLUDE_NONE",
			"ASH_WINDOWS",
			"ASH_ENGINE",
		}
		defines "ASH_RELEASE"
		runtime "Release"
		optimize "on"	
		includedirs
		{
			thirdparty .. "/glslang/release/windows-x64/include",
		}
		links {
			thirdparty.."/glslang/release/windows-x64/lib/glslang.lib",
			thirdparty.."/glslang/release/windows-x64/lib/SPIRV.lib",
			thirdparty.."/glslang/release/windows-x64/lib/OGLCompiler.lib",
			thirdparty.."/glslang/release/windows-x64/lib/MachineIndependent.lib",
			thirdparty.."/glslang/release/windows-x64/lib/GenericCodeGen.lib",
			thirdparty.."/glslang/release/windows-x64/lib/SPIRV-Tools.lib",
			thirdparty.."/glslang/release/windows-x64/lib/SPIRV-Tools-opt.lib",
			thirdparty.."/glslang/release/windows-x64/lib/OSDependent.lib",

		}	
		
		

	
	