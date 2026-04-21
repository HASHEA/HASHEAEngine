project "Editor"
	language "C++"
	cppdialect "c++17"
	architecture "x64"
	kind "ConsoleApp"
	staticruntime "off"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")
	includedirs
	{
		"%{wks.location}/project/src/engine",
		"%{wks.location}/project/src/editor",
		thirdparty .. "/spdlog/include",
		thirdparty .. "/GLFW/include",
		thirdparty .. "/ImGui",
		thirdparty .. "/glm/include",
		thirdparty .. "/entt/include",
		thirdparty .. "/Json",
	}

	files
	{
		"**.h",
		"**.cpp",

		"**.lua",
	}
	pchheader "EditorPCH.h"
	pchsource "EditorPCH.cpp"
	forceincludes
	{
		"EditorPCH.h",
	}
	-- The runtime editor now renders through engine-side UIContext.
	-- These legacy files owned a separate native ImGui host/context on the editor side,
	-- so they stay excluded until UIContext grows equivalent extension points.
	removefiles
	{
		"ImGui/EditorImGuiLayer.h",
		"ImGui/EditorImGuiLayer.cpp",
		"ImGui/EditorStyle.h",
		"ImGui/EditorStyle.cpp",
	}

	links
	{
		"Engine",

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
		debugcommand "%{wks.location}/product/bin64/Debug-windows-x86_64/Editor.exe"
		debugdir "%{wks.location}/product/bin64/Debug-windows-x86_64"			  
		postbuildcommands 
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Debug") .. "\\Editor.exe", false),
			sync_runtime_artifact_command("$(TargetDir)Editor.pdb", product_runtime_dir("Debug") .. "\\Editor.pdb", true),
		}
	filter "configurations:Release"
		defines "ASH_APP_RELEASE"
		runtime "Release"
		optimize "on"
		debugcommand "%{wks.location}/product/bin64/Release-windows-x86_64/Editor.exe"
		debugdir "%{wks.location}/product/bin64/Release-windows-x86_64"			  
		postbuildcommands 
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Release") .. "\\Editor.exe", false),
			sync_runtime_artifact_command("$(TargetDir)Editor.pdb", product_runtime_dir("Release") .. "\\Editor.pdb", true),
		}
