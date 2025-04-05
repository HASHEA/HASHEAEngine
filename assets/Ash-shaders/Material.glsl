
const uint MaterialLambertian = 0;
const uint MaterialMetallic = 1;
const uint MaterialDielectric = 2;
const uint MaterialDiffuseLight = 3;

struct Material
{
	vec4 Diffuse;
	int DiffuseTextureId;
	float Fuzziness;
	float RefractionIndex;
	uint MaterialModel;
};
