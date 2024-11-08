-- Third party projects

project( "volk" )
	language "C"
	kind "StaticLib"
	
	targetdir ("%{wks.location}/bin/target/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin/obj/" .. outputdir .. "/%{prj.name}")

	includedirs
	{
		thirdparty .."/VulkanSDK/include",
		thirdparty .."/volk/include"

	}
	
	files
	{
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