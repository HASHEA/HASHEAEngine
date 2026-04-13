workspace "AshEngine"
	language "C++"
	cppdialect "C++17"
	architecture "x64"

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

--os.mkdir(distdir)
startproject "Editor"
include "project/src/engine"
include "project/src/editor"
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

