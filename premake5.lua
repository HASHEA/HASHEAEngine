workspace "AshEngine"
	language "C++"
	cppdialect "C++17"
	architecture "x64"

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

function product_runtime_dir(config_name)
	return rootdir_win .. "\\product\\bin64\\" .. config_name .. "-windows-x86_64"
end

function sync_runtime_artifact_command(src, dst, optional)
	local command = 'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. runtime_sync_script .. '" -Source "' .. src .. '" -Destination "' .. dst .. '"'
	if optional then
		command = command .. " -Optional"
	end
	return 'cmd /c "' .. command .. '"'
end

--os.mkdir(distdir)
startproject "Editor"
include "project/src/engine"
include "project/src/editor"
include "project/src/sandbox"
-- include "project/src/shader"
	

group "ThirdParty"
	include "project/thirdparty/GLFW"
	include "project/thirdparty/Glad"
	include "project/thirdparty/spdlog"
	include "project/thirdparty/volk"
	include "project/thirdparty/VulkanSDK"
	include "project/thirdparty/openFBX"
	include "project/thirdparty/SPIRV-Cross"
	include "project/thirdparty/tracy"
	include "project/thirdparty/meshoptimizer"
	include "project/thirdparty/D3D12MA"
