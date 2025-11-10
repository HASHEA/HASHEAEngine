
struct UniformBufferObject
{
	mat4 ModelView;
	mat4 Projection;
	mat4 ModelViewInverse;
	mat4 ProjectionInverse;
	float Aperture;
	float FocusDistance;
	uint NumberOfSamples;
	uint NumberOfBounces;
	uint RandomSeed;
	bool HasSky;
};
