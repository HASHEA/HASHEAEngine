project "VulkanSDK"
	language "C"
	kind "StaticLib"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")
	includedirs
	{
		
		 "./include"
	}
	
	files
	{
		"**.lua",
		thirdparty .."/VulkanSDK/include/**.h",
		thirdparty .."/VulkanSDK/include/**.hpp",
	}

	filter "system:windows"
		systemversion "latest"
		staticruntime "On"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"