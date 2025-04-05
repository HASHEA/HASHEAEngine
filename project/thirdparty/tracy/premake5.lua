-- Third party projects

project( "tracy" )
	language "C"
	kind "StaticLib"
	

	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")

	includedirs
	{
		"./",

	}
	
	files
	{
		"**.lua",
		thirdparty .."/tracy/TracyClient.cpp",
	}
	defines
	{
		"TRACY_ENABLE",
	}
	filter "system:windows"
		systemversion "latest"
		staticruntime "off"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"