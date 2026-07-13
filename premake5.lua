newoption {
	trigger = "no-tracy",
	description = "Build Engine without TRACY_ENABLE/TRACY_ON_DEMAND for GPU timing validation"
}

workspace "AshEngine"
	language "C++"
	cppdialect "C++17"
	architecture "x64"

	filter { "action:vs*", "system:windows" }
		flags { "MultiProcessorCompile" }
		buildoptions { "/FS" }
	filter {}

rootdir = path.getabsolute(".")
rootdir_win = rootdir:gsub("/", "\\")

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}
	defines
		{
			"GLM_FORCE_DEPTH_ZERO_TO_ONE",
			"GLM_FORCE_LEFT_HANDED",
		}
	
thirdparty = "%{wks.location}/project/thirdparty"
assetsdir = "%{wks.location}/assets"
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
runtime_sync_script = rootdir_win .. "\\scripts\\SyncRuntimeArtifact.ps1"
dxc_redist_dir = rootdir_win .. "\\project\\thirdparty\\dxc\\bin\\x64"
vulkan_validation_layer_redist_dir = rootdir_win .. "\\project\\thirdparty\\VulkanSDK\\redist\\windows-x64\\layers"

function product_runtime_dir(config_name)
	return rootdir_win .. "\\product\\bin64\\" .. config_name .. "-windows-x86_64"
end

function sync_runtime_artifact_command(src, dst, optional)
	local command = 'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. runtime_sync_script .. '" -Source "' .. src .. '" -Destination "' .. dst .. '"'
	if optional then
		command = command .. " -Optional"
	end
	-- MSBuild's PostBuildEvent runs every line in a single cmd batch and only
	-- checks the LAST line's ERRORLEVEL. Without explicit propagation, a failed
	-- required copy (e.g. a locked Engine.dll/exe) is masked by a later command
	-- succeeding, so the build is reported green while the runtime dir stays
	-- stale. Abort the batch immediately on any required copy failure. Optional
	-- copies already swallow a missing source via -Optional, so leave them be.
	if optional then
		return 'cmd /c "' .. command .. '"'
	end
	return 'cmd /c "' .. command .. '" || exit /b 1'
end

--os.mkdir(distdir)
startproject "Editor"
include "project/src/engine"
include "project/src/editor"
include "project/src/sandbox"
include "project/src/tests"
-- include "project/src/shader"

group "Tools"
	include "tools/imagediff"
group ""
	

group "ThirdParty"
	include "project/thirdparty/GLFW"
	include "project/thirdparty/Glad"
	include "project/thirdparty/spdlog"
	include "project/thirdparty/volk"
	include "project/thirdparty/VulkanSDK"
	include "project/thirdparty/openFBX"
	include "project/thirdparty/SPIRV-Cross"
	if not _OPTIONS["no-tracy"] then
		include "project/thirdparty/tracy"
	end
	include "project/thirdparty/meshoptimizer"
	include "project/thirdparty/D3D12MA"
