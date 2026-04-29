-- Third party projects

project( "tracy" )
	language "C++"
	cppdialect "C++20"
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
		editandcontinue "Off"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"
		editandcontinue "Off"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"