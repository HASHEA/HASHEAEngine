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
		debugcommand "%{wks.location}/product/bin64/Debug/Editor.exe"
		debugdir "%{wks.location}/product/bin64/Debug"			  
		postbuildcommands
		{
			("{MKDIR %{wks.location}/product/bin64/Debug"),
			("{COPYDIR} %{cfg.buildtarget.directory} %{wks.location}/product/bin64/Debug")
		} 
	filter "configurations:Release"
		defines "ASH_APP_RELEASE"
		runtime "Release"
		optimize "on"
		debugcommand "%{wks.location}/product/bin64/Release/Editor.exe"
		debugdir "%{wks.location}/product/bin64/Release"			  
		postbuildcommands
		{
			("{MKDIR %{wks.location}/product/bin64/Release"),
			("{COPYDIR} %{cfg.buildtarget.directory} %{wks.location}/product/bin64/Release")
		}  