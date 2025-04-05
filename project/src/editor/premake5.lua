project "Editor"
	language "C++"
	cppdialect "c++17"
	architecture "x64"
	kind "ConsoleApp"
	staticruntime "off"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")
	debugcommand ("%{wks.location}/_BUILD/"..outputdir.."/bin64/Editor.exe")   

	includedirs
	{
		"%{wks.location}/project/src/engine",
		thirdparty .. "/spdlog/include",
		thirdparty .. "/GLFW/include",
		thirdparty .. "/ImGui",
		thirdparty .. "/glm/include",
		thirdparty .. "/entt/include",
	}

	files
	{
		"**.h",
		"**.cpp",

		"**.lua",
	}

	links
	{
		"Engine",

	}
	local source_dir = "%{wks.location}/_BUILD/"..outputdir.."/bin/target/Editor/"
	local dest_dir = distdir
	postbuildcommands {

        "if not exist \""..dest_dir.."\" mkdir \""..dest_dir.."\"",
        "robocopy \""..source_dir.."\" \""..dest_dir.."\" Editor.exe Editor.pdb /NFL /NDL /NJH /NJS >nul || exit 0"
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
			"ASH_EDITOR",
		}


	filter "configurations:Debug"
		defines "ASH_APP_DEBUG"
		runtime "Debug"
		symbols "on"
		editandcontinue "Off"
		optimize "off"

	filter "configurations:Release"
		defines "ASH_APP_RELEASE"
		runtime "Release"
		optimize "on"