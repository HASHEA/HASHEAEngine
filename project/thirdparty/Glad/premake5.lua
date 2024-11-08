project "Glad"
	language "C"
	kind "StaticLib"

	targetdir ("%{wks.location}/bin/target/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin/obj/" .. outputdir .. "/%{prj.name}")
	includedirs
	{
		"src",
		"include",
	}
	
	files
	{
		"**.h",
		"**.c",

		"**.lua",
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