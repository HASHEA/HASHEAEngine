project "spirv-cross"
	language "C"
	kind "StaticLib"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")
	staticruntime "off"
	files
	{
		"**.c",
		"**.cpp",
		"**.hpp",
		"**.lua",
	}
	includedirs
    {
		"./include",
    }

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"
	filter "configurations:Release"
		runtime "Release"
		optimize "on"