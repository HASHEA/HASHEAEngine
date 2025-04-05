-- Third party projects

project( "volk" )
	language "C"
	kind "StaticLib"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")

	includedirs
	{
		thirdparty .."/VulkanSDK/include",
		thirdparty .."/volk/include"

	}
	
	files
	{
		"**.lua",
		thirdparty .."/volk/src/**.c",
		thirdparty .."/volk/include/**.h",
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