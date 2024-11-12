#[raygen]

#version 460

//#VERSION_DEFINES

#define MAX_VIEWS 2

#include "../scene_data_inc.glsl"

#pragma shader_stage(raygen)
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadEXT vec3 payload;

// Render target in raytracing is a storage image with general layout.
layout(set = 0, binding = 0, rgba32f) uniform image2D image;

// Bounding Volume Hierarchy: top-level acceleration structure.
layout(set = 0, binding = 1) uniform accelerationStructureEXT tlas;

layout(set = 0, binding = 2, std140) uniform SceneDataBlock {
	SceneData data;
} scene_data_block;


void main() {
	const vec2 pixel_center = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 in_uv = pixel_center / vec2(gl_LaunchSizeEXT.xy);
	vec2 d = in_uv * 2.0 - 1.0;

	float size_y = gl_LaunchSizeEXT.y;
	float aspect_ratio = gl_LaunchSizeEXT.x / size_y;

	vec4 target = scene_data_block.data.inv_projection_matrix * vec4(d.x /* aspect_ratio */, d.y, 1.0, 1.0);
	vec4 origin = scene_data_block.data.inv_view_matrix * vec4(0.0, 0.0, 0.0, 1.0);
	vec4 direction = scene_data_block.data.inv_view_matrix * vec4(normalize(target.xyz), 0);

	float t_min = 0.001;
	float t_max = 10000.0;

	traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin.xyz, t_min, direction.xyz, t_max, 0);

	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(payload, 1.0));
}

#[miss]

#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {
	payload = vec3(1.0, 0.0, 0.0);
}

#[closest_hit]

#version 460

#pragma shader_stage(closest_hit)
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 payload;
//hitAttributeEXT vec3 attribs;

void main() {
	//const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
	//payload = barycentrics;
	payload = vec3(0.0, 1.0, 0.0);
}
