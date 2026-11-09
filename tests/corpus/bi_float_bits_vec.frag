#version 320 es
precision highp float;
out vec4 color;
void main(){uvec3 u=floatBitsToUint(vec3(1.,2.,3.));ivec2 i=floatBitsToInt(gl_FragCoord.xy);color=vec4(uintBitsToFloat(u),float(i.x));}
