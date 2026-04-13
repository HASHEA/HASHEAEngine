-- Third party projects

project( "D3D12MA" )
	language "C++"
	cppdialect "C++17"
	kind "StaticLib"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")

	includedirs
	{
		thirdparty .."/D3D12MA/include",
	}

	files
	{
		"**.lua",
		thirdparty .."/D3D12MA/src/**.cpp",
		thirdparty .."/D3D12MA/include/**.h",
	}

	links
	{
		"d3d12",
		"dxgi",
	}

	filter "system:windows"
		systemversion "latest"
		staticruntime "Off"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"
