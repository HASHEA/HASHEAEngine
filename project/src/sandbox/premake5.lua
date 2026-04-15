project "Sandbox"
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
		"%{wks.location}/project/src/sandbox",
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
		"**.hlsl",
		"**.lua",
	}

	filter "files:**.hlsl"
		buildaction "None"

	filter {}

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
			"ASH_SANDBOX",
		}

	filter "configurations:Debug"
		defines "ASH_APP_DEBUG"
		runtime "Debug"
		symbols "on"
		editandcontinue "Off"
		optimize "off"
		debugcommand "%{wks.location}/product/bin64/Debug-windows-x86_64/Sandbox.exe"
		debugdir "%{wks.location}/product/bin64/Debug-windows-x86_64"
		postbuildcommands
		{
			'mkdir "%{wks.location}\\product\\bin64\\Debug-windows-x86_64" 2>nul',
			'copy /Y "$(TargetPath)" "%{wks.location}\\product\\bin64\\Debug-windows-x86_64\\Sandbox.exe"',
			'if exist "$(TargetDir)Sandbox.pdb" copy /Y "$(TargetDir)Sandbox.pdb" "%{wks.location}\\product\\bin64\\Debug-windows-x86_64\\Sandbox.pdb"',
			'copy /Y "%{wks.location}\\_BUILD\\Debug-windows-x86_64\\bin\\target\\Engine\\Engine.dll" "%{wks.location}\\product\\bin64\\Debug-windows-x86_64\\Engine.dll"',
			'if exist "%{wks.location}\\_BUILD\\Debug-windows-x86_64\\bin\\target\\Engine\\Engine.pdb" copy /Y "%{wks.location}\\_BUILD\\Debug-windows-x86_64\\bin\\target\\Engine\\Engine.pdb" "%{wks.location}\\product\\bin64\\Debug-windows-x86_64\\Engine.pdb"',
		}

	filter "configurations:Release"
		defines "ASH_APP_RELEASE"
		runtime "Release"
		optimize "on"
		debugcommand "%{wks.location}/product/bin64/Release-windows-x86_64/Sandbox.exe"
		debugdir "%{wks.location}/product/bin64/Release-windows-x86_64"
		postbuildcommands
		{
			'mkdir "%{wks.location}\\product\\bin64\\Release-windows-x86_64" 2>nul',
			'copy /Y "$(TargetPath)" "%{wks.location}\\product\\bin64\\Release-windows-x86_64\\Sandbox.exe"',
			'if exist "$(TargetDir)Sandbox.pdb" copy /Y "$(TargetDir)Sandbox.pdb" "%{wks.location}\\product\\bin64\\Release-windows-x86_64\\Sandbox.pdb"',
			'copy /Y "%{wks.location}\\_BUILD\\Release-windows-x86_64\\bin\\target\\Engine\\Engine.dll" "%{wks.location}\\product\\bin64\\Release-windows-x86_64\\Engine.dll"',
			'if exist "%{wks.location}\\_BUILD\\Release-windows-x86_64\\bin\\target\\Engine\\Engine.pdb" copy /Y "%{wks.location}\\_BUILD\\Release-windows-x86_64\\bin\\target\\Engine\\Engine.pdb" "%{wks.location}\\product\\bin64\\Release-windows-x86_64\\Engine.pdb"',
		}
