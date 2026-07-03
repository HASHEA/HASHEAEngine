project "AshImageDiff"
	language "C++"
	cppdialect "c++17"
	architecture "x64"
	kind "ConsoleApp"
	staticruntime "off"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")

	includedirs
	{
		thirdparty .. "/stb",
	}

	files
	{
		"**.cpp",
		"**.lua",
	}

	filter "system:windows"
		system "Windows"
		systemversion "latest"
		staticruntime "Off"
		defines
		{
			"_CONSOLE",
		}

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"
		optimize "off"
		postbuildcommands
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Debug") .. "\\AshImageDiff.exe", false),
		}

	filter "configurations:Release"
		runtime "Release"
		optimize "on"
		postbuildcommands
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Release") .. "\\AshImageDiff.exe", false),
		}
