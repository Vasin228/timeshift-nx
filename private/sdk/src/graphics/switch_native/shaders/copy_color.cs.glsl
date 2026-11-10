// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform texture2D source_color;
layout(set=0,binding=1) writeonly uniform image2D destination;
layout(push_constant) uniform Params {
  ivec2 source_origin;
  ivec2 destination_origin;
  uvec2 size;
  int exponent;
} params;
void main() {
  uvec2 p=gl_GlobalInvocationID.xy;
  if(any(greaterThanEqual(p,params.size)))return;
  vec4 value=texelFetch(source_color,params.source_origin+ivec2(p),0);
  imageStore(destination,params.destination_origin+ivec2(p),
      value*exp2(float(params.exponent)));
}
