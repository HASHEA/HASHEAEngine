
-- GLSLC helpers
dofile( "glslc.lua" )
project "shaders"
	kind "Utility"
	targetdir ("%{wks.location}/bin/target/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin/obj/" .. outputdir .. "/%{prj.name}")
	local shaders = { 
		"src/**.vert",
		"src/**.frag"
	}

	files( shaders )

	handle_glsl_files( "-g", "assets/shaders/spv", {})
