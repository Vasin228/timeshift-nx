// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8, local_size_y=8) in;
layout(set=0,binding=0) uniform texture2D source_depth;
layout(set=0,binding=1,r32f) writeonly uniform image2D destination;
layout(push_constant) uniform Params {
  ivec2 source_origin;
  ivec2 destination_origin;
  uvec2 size;
} params;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(p, params.size))) return;
  float depth = texelFetch(source_depth, params.source_origin + ivec2(p), 0).r;
  imageStore(destination, params.destination_origin + ivec2(p), vec4(depth,0,0,1));
}
