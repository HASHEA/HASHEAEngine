
project "shaders"
	kind "Utility"
	targetdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/target/%{prj.name}")
	objdir ("%{wks.location}/_BUILD/"..outputdir .."/bin/obj/%{prj.name}")
	local shaders = { 
		"%{wks.location}/assets/Ash-shaders/**.vert",
		"%{wks.location}/assets/Ash-shaders/**.glsl",
		"%{wks.location}/assets/Ash-shaders/**.frag",
	}

	files( shaders )
