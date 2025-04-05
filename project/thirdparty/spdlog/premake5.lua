project "spdlog"
    kind "StaticLib"
    language "C++"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")

    files
	{
		thirdparty .. "/spdlog/include/spdlog/**.cpp",
		thirdparty .. "/spdlog/include/spdlog/**.h",

		thirdparty .. "/spdlog/src/**.cpp",
		thirdparty .. "/spdlog/src/**.h",

		"**.lua",
	}

    includedirs
    {
        thirdparty .. "/spdlog/include",
		thirdparty .. "/spdlog/src"
    }

	defines
	{
		"SPDLOG_COMPILED_LIB",
	}

    filter "system:windows"
		systemversion "latest"
		cppdialect "C++17"


    filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"
