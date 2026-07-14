project "Tests"
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
		"%{wks.location}/project/src/sandbox",
		"%{wks.location}/project/src/tests",
		thirdparty .. "/doctest",
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
		"**.hpp",
		"**.cpp",
		"**.lua",
		"%{wks.location}/project/src/editor/Services/EditorGizmoMath.cpp",
		"%{wks.location}/project/src/editor/Services/EditorGizmoViewport.cpp",
		"%{wks.location}/project/src/editor/App/SceneWorkflowCoordinator.cpp",
		"%{wks.location}/project/src/editor/Core/EditorCommand.cpp",
		"%{wks.location}/project/src/editor/Core/EditorScenePathUtils.cpp",
		"%{wks.location}/project/src/editor/Core/SceneComponentSerialization.cpp",
		"%{wks.location}/project/src/editor/Core/SceneSnapshotComponentUtils.cpp",
		"%{wks.location}/project/src/editor/Core/TerrainEditorSessionCore.cpp",
		"%{wks.location}/project/src/editor/Core/TerrainCommands.cpp",
		"%{wks.location}/project/src/editor/Core/TerrainViewportInputRouter.cpp",
		"%{wks.location}/project/src/editor/Services/SceneService.cpp",
		"%{wks.location}/project/src/editor/Services/SelectionService.cpp",
		"%{wks.location}/project/src/editor/Services/EditorSettingsService.cpp",
		"%{wks.location}/project/src/editor/Services/TerrainBrushOverlayRenderer.cpp",
		"%{wks.location}/project/src/editor/Services/TerrainEditorService.cpp",
		"%{wks.location}/project/src/editor/Services/UndoRedoService.cpp",
		"%{wks.location}/project/src/sandbox/App/SandboxFreeCameraController.cpp",
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
			"ASH_TESTS",
		}

	filter "configurations:Debug"
		defines "ASH_APP_DEBUG"
		runtime "Debug"
		symbols "on"
		editandcontinue "Off"
		optimize "off"
		debugcommand (rootdir .. "/product/bin64/Debug-windows-x86_64/Tests.exe")
		debugdir (rootdir .. "/product/bin64/Debug-windows-x86_64")
		postbuildcommands
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Debug") .. "\\Tests.exe", false),
			sync_runtime_artifact_command("$(TargetDir)Tests.pdb", product_runtime_dir("Debug") .. "\\Tests.pdb", true),
		}

	filter "configurations:Release"
		defines "ASH_APP_RELEASE"
		runtime "Release"
		optimize "on"
		debugcommand (rootdir .. "/product/bin64/Release-windows-x86_64/Tests.exe")
		debugdir (rootdir .. "/product/bin64/Release-windows-x86_64")
		postbuildcommands
		{
			sync_runtime_artifact_command("$(TargetPath)", product_runtime_dir("Release") .. "\\Tests.exe", false),
			sync_runtime_artifact_command("$(TargetDir)Tests.pdb", product_runtime_dir("Release") .. "\\Tests.pdb", true),
		}
