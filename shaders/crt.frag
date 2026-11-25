#version 450
#extension GL_GOOGLE_include_directive : require
#include "platform.glsl"

// The optional picture-tube look, drawn last over the whole frame: see CrtFilter.h.
SAMPLER_2D(0, scene)
VARYING(0) in vec2 uv;
layout(location = 0) out vec4 color;
PUSH_CONSTANTS_BEGIN(Crt)
    mat4 unused;
    vec4 frame; // the composed frame's corners in framebuffer UV: left, top, right, bottom
    vec4 look;  // the frame's columns and rows, scan line strength, grille strength
    vec4 pixel; // reciprocal framebuffer size, corner exponent
PUSH_CONSTANTS_END(Crt, settings)

void main() {
    vec4 original = texture(scene, uv);
    vec2 size = settings.frame.zw - settings.frame.xy;
    vec2 at = (uv - settings.frame.xy) / size;
    // The bars beside a letterboxed frame are no part of the tube.
    if (at.x < 0.0 || at.y < 0.0 || at.x > 1.0 || at.y > 1.0) {
        color = original;
        return;
    }
    // The beam spreads half a frame pixel to either side along its line.
    float reach = 0.5 * size.x / settings.look.x;
    float left = settings.frame.x + 0.5 * settings.pixel.x;
    float right = settings.frame.z - 0.5 * settings.pixel.x;
    vec3 lit = 0.5 * original.rgb +
               0.25 * texture(scene, vec2(clamp(uv.x - reach, left, right), uv.y)).rgb +
               0.25 * texture(scene, vec2(clamp(uv.x + reach, left, right), uv.y)).rgb;
    // A line is brightest a quarter of the way down its row and darkest three quarters down,
    // so that at two framebuffer rows a line one is lit and the next is its gap. Bright
    // picture spreads into the gap, as a hot beam does.
    float row = at.y * settings.look.y;
    float beam = 0.5 + 0.5 * cos(6.28318530718 * (fract(row) - 0.25));
    float glow = max(lit.r, max(lit.g, lit.b));
    float scan = 1.0 - settings.look.z * (1.0 - beam) * (1.0 - 0.6 * glow);
    // An aperture grille: each framebuffer column favours one of red, green and blue.
    vec3 grille = vec3(1.0 - settings.look.w);
    grille[int(mod(gl_FragCoord.x, 3.0))] = 1.0;
    // What the lines and the grille take on average is given back, short of clipping.
    float gain = 1.0 / ((1.0 - 0.5 * settings.look.z) * (1.0 - settings.look.w * 2.0 / 3.0));
    vec2 edge = at * (1.0 - at);
    float corners = pow(clamp(16.0 * edge.x * edge.y, 0.0, 1.0), settings.pixel.z);
    color = vec4(min(lit * scan * grille * gain, vec3(1.0)) * corners, original.a);
}
