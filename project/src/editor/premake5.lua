project "Editor"
	language "C++"
	cppdialect "c++17"
	architecture "x64"
	kind "ConsoleApp"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/target/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin/obj/" .. outputdir .. "/%{prj.name}")

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
	
	postbuildcommands
	{
		("{COPY} %{wks.location}/bin/target/" .. outputdir .. "/Engine/Engine.dll   %{wks.location}/bin/target/" .. outputdir .. "/Editor "),
		("{COPY} %{wks.location}/bin/target/" .. outputdir .. "/Engine/Engine.pdb   %{wks.location}/bin/target/" .. outputdir .. "/Editor ")
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