project "Engine"
	language "C++"
	architecture "x64"
	kind "SharedLib"
	staticruntime "off"
	
	targetdir ("%{wks.location}/bin/target/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin/obj/" .. outputdir .. "/%{prj.name}")
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


 postbuildcommands
		{
			("{COPY} %{wks.location}/bin/target/" .. outputdir .. "/Engine/Engine.dll   %{wks.location}/bin/target/" .. outputdir .. "/Editor "),
			("{COPY} %{wks.location}/bin/target/" .. outputdir .. "/Engine/Engine.pdb   %{wks.location}/bin/target/" .. outputdir .. "/Editor ")
		}

  
	filter "system:windows"
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
			"ASH_ROOT_DIR=\"%{wks.location}\"",
			"ASH_ASSETS_DIR=\"%{wks.location}/assets\"",
		}
		
	dependson "shaders"
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

	filter "configurations:Debug"
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

	filter "configurations:Release"
		defines "ASH_RELEASE"
		runtime "Release"
		optimize "on"	

	