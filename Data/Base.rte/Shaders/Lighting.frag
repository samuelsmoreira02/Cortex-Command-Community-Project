#version 330 core

// Lighting pass: outputs how lit each pixel of a player screen is, which is multiplied into what's already drawn there.
// Light is the ambient light plus every light source, each fading out quadratically towards its radius.

in vec2 textureUV;
in vec4 vertexColor;

out vec4 FragColor;

const int c_MaxLights = 64;

uniform vec2 rteScreenSize;
uniform vec3 rteAmbientLight;
uniform int rteLightCount;
uniform vec4 rteLightPosRadiusIntensity[c_MaxLights]; // xy: position on the screen in pixels, z: radius in pixels, w: intensity.
uniform vec3 rteLightColor[c_MaxLights];

void main() {
	vec2 pixel = textureUV * rteScreenSize;
	vec3 light = rteAmbientLight;
	for (int i = 0; i < rteLightCount; ++i) {
		vec4 lightData = rteLightPosRadiusIntensity[i];
		float distanceRatio = distance(pixel, lightData.xy) / lightData.z;
		if (distanceRatio < 1.0) {
			float falloff = 1.0 - distanceRatio;
			light += rteLightColor[i] * lightData.w * falloff * falloff;
		}
	}
	FragColor = vec4(min(light, vec3(1.0)), 1.0);
}
