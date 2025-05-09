#pragma once

#include <string>
#include <format>
#include <cmath>

constexpr float              SHADER_ROUNDED_SMOOTHING_FACTOR = M_PI / 5.34665792551;

inline static constexpr auto ROUNDED_SHADER_FUNC = [](const std::string colorVarName) -> std::string {
    return R"#(

    // branchless baby!
    highp vec2 pixCoord = vec2(gl_FragCoord);
    pixCoord -= topLeft + fullSize * 0.5;
    pixCoord *= vec2(lessThan(pixCoord, vec2(0.0))) * -2.0 + 1.0;
    pixCoord -= fullSize * 0.5 - radius;
    pixCoord += vec2(1.0, 1.0) / fullSize; // center the pix dont make it top-left

    // smoothing constant for the edge: more = blurrier, but smoother
    const float SMOOTHING_CONSTANT = )#" +
        std::format("{:.7f}", SHADER_ROUNDED_SMOOTHING_FACTOR) + R"#(;

    if (pixCoord.x + pixCoord.y > radius) {

	    float dist = length(pixCoord);

	    if (dist > radius + SMOOTHING_CONSTANT * 2.0)
	        discard;

	    if (dist > radius - SMOOTHING_CONSTANT * 2.0) {
	        float dist = length(pixCoord);

            float normalized = 1.0 - smoothstep(0.0, 1.0, (dist - radius + SMOOTHING_CONSTANT) / (SMOOTHING_CONSTANT * 2.0));

	        )#" +
        colorVarName + R"#( = )#" + colorVarName + R"#( * normalized;
        }

    }
)#";
};

inline const std::string QUADVERTSRC = R"#(
uniform mat3 proj;
uniform vec4 color;
attribute vec2 pos;
attribute vec2 texcoord;
attribute vec2 texcoordMatte;
varying vec4 v_color;
varying vec2 v_texcoord;
varying vec2 v_texcoordMatte;

void main() {
    gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);
    v_color = color;
    v_texcoord = texcoord;
    v_texcoordMatte = texcoordMatte;
})#";

inline const std::string QUADFRAGSRC = R"#(
precision highp float;
varying vec4 v_color;

uniform vec2 topLeft;
uniform vec2 fullSize;
uniform float radius;

void main() {

    vec4 pixColor = v_color;

    if (radius > 0.0) {
	)#" +
    ROUNDED_SHADER_FUNC("pixColor") + R"#(
    }

    gl_FragColor = pixColor;
})#";

inline const std::string TEXVERTSRC = R"#(
uniform mat3 proj;
attribute vec2 pos;
attribute vec2 texcoord;
varying vec2 v_texcoord;

void main() {
    gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);
    v_texcoord = texcoord;
})#";

inline const std::string TEXFRAGSRCRGBA = R"#(
precision highp float;
varying vec2 v_texcoord; // is in 0-1
uniform sampler2D tex;
uniform float alpha;

uniform vec2 topLeft;
uniform vec2 fullSize;
uniform float radius;

uniform int discardOpaque;
uniform int discardAlpha;
uniform float discardAlphaValue;

uniform int applyTint;
uniform vec3 tint;

void main() {

    vec4 pixColor = texture2D(tex, v_texcoord);

    if (discardOpaque == 1 && pixColor[3] * alpha == 1.0)
	    discard;

    if (discardAlpha == 1 && pixColor[3] <= discardAlphaValue)
        discard;

    if (applyTint == 1) {
	    pixColor[0] = pixColor[0] * tint[0];
	    pixColor[1] = pixColor[1] * tint[1];
	    pixColor[2] = pixColor[2] * tint[2];
    }

    if (radius > 0.0) {
    )#" +
    ROUNDED_SHADER_FUNC("pixColor") + R"#(
    }

    gl_FragColor = pixColor * alpha;
})#";

inline const std::string TEXMIXFRAGSRCRGBA = R"#(
precision highp float;
varying vec2 v_texcoord; // is in 0-1
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform float mixFactor;
uniform float alpha;

uniform vec2 topLeft;
uniform vec2 fullSize;
uniform float radius;

uniform int discardOpaque;
uniform int discardAlpha;
uniform float discardAlphaValue;

uniform int applyTint;
uniform vec3 tint;

void main() {

    vec4 pixColor = mix(texture2D(tex1, v_texcoord), texture2D(tex2, v_texcoord), smoothstep(0.0, 1.0, mixFactor));

    if (discardOpaque == 1 && pixColor[3] * alpha == 1.0)
	    discard;

    if (discardAlpha == 1 && pixColor[3] <= discardAlphaValue)
        discard;

    if (applyTint == 1) {
	    pixColor[0] = pixColor[0] * tint[0];
	    pixColor[1] = pixColor[1] * tint[1];
	    pixColor[2] = pixColor[2] * tint[2];
    }

    if (radius > 0.0) {
    )#" +
    ROUNDED_SHADER_FUNC("pixColor") + R"#(
    }

    gl_FragColor = pixColor * alpha;
})#";

inline const std::string FRAGBLUR1 = R"#(
#version 100
precision            highp float;
varying highp vec2   v_texcoord; // is in 0-1
uniform sampler2D    tex;

uniform float        radius;
uniform vec2         halfpixel;
uniform int          passes;
uniform float        vibrancy;
uniform float        vibrancy_darkness;

// see http://alienryderflex.com/hsp.html
const float Pr = 0.299;
const float Pg = 0.587;
const float Pb = 0.114;

// Y is "v" ( brightness ). X is "s" ( saturation )
// see https://www.desmos.com/3d/a88652b9a4
// Determines if high brightness or high saturation is more important
const float a = 0.93;
const float b = 0.11;
const float c = 0.66; //  Determines the smoothness of the transition of unboosted to boosted colors
//

// http://www.flong.com/archive/texts/code/shapers_circ/
float doubleCircleSigmoid(float x, float a) {
    a = clamp(a, 0.0, 1.0);

    float y = .0;
    if (x <= a) {
        y = a - sqrt(a * a - x * x);
    } else {
        y = a + sqrt(pow(1. - a, 2.) - pow(x - 1., 2.));
    }
    return y;
}

vec3 rgb2hsl(vec3 col) {
    float red   = col.r;
    float green = col.g;
    float blue  = col.b;

    float minc  = min(col.r, min(col.g, col.b));
    float maxc  = max(col.r, max(col.g, col.b));
    float delta = maxc - minc;

    float lum = (minc + maxc) * 0.5;
    float sat = 0.0;
    float hue = 0.0;

    if (lum > 0.0 && lum < 1.0) {
        float mul = (lum < 0.5) ? (lum) : (1.0 - lum);
        sat       = delta / (mul * 2.0);
    }

    if (delta > 0.0) {
        vec3  maxcVec = vec3(maxc);
        vec3  masks = vec3(equal(maxcVec, col)) * vec3(notEqual(maxcVec, vec3(green, blue, red)));
        vec3  adds = vec3(0.0, 2.0, 4.0) + vec3(green - blue, blue - red, red - green) / delta;

        hue += dot(adds, masks);
        hue /= 6.0;

        if (hue < 0.0)
            hue += 1.0;
    }

    return vec3(hue, sat, lum);
}

vec3 hsl2rgb(vec3 col) {
    const float onethird = 1.0 / 3.0;
    const float twothird = 2.0 / 3.0;
    const float rcpsixth = 6.0;

    float       hue = col.x;
    float       sat = col.y;
    float       lum = col.z;

    vec3        xt = vec3(0.0);

    if (hue < onethird) {
        xt.r = rcpsixth * (onethird - hue);
        xt.g = rcpsixth * hue;
        xt.b = 0.0;
    } else if (hue < twothird) {
        xt.r = 0.0;
        xt.g = rcpsixth * (twothird - hue);
        xt.b = rcpsixth * (hue - onethird);
    } else
        xt = vec3(rcpsixth * (hue - twothird), 0.0, rcpsixth * (1.0 - hue));

    xt = min(xt, 1.0);

    float sat2   = 2.0 * sat;
    float satinv = 1.0 - sat;
    float luminv = 1.0 - lum;
    float lum2m1 = (2.0 * lum) - 1.0;
    vec3  ct     = (sat2 * xt) + satinv;

    vec3  rgb;
    if (lum >= 0.5)
        rgb = (luminv * ct) + lum2m1;
    else
        rgb = lum * ct;

    return rgb;
}

void main() {
    vec2 uv = v_texcoord * 2.0;

    vec4 sum = texture2D(tex, uv) * 4.0;
    sum += texture2D(tex, uv - halfpixel.xy * radius);
    sum += texture2D(tex, uv + halfpixel.xy * radius);
    sum += texture2D(tex, uv + vec2(halfpixel.x, -halfpixel.y) * radius);
    sum += texture2D(tex, uv - vec2(halfpixel.x, -halfpixel.y) * radius);

    vec4 color = sum / 8.0;

    if (vibrancy == 0.0) {
        gl_FragColor = color;
    } else {
        // Invert it so that it correctly maps to the config setting
        float vibrancy_darkness1 = 1.0 - vibrancy_darkness;

        // Decrease the RGB components based on their perceived brightness, to prevent visually dark colors from overblowing the rest.
        vec3 hsl = rgb2hsl(color.rgb);
        // Calculate perceived brightness, as not boost visually dark colors like deep blue as much as equally saturated yellow
        float perceivedBrightness = doubleCircleSigmoid(sqrt(color.r * color.r * Pr + color.g * color.g * Pg + color.b * color.b * Pb), 0.8 * vibrancy_darkness1);

        float b1        = b * vibrancy_darkness1;
        float boostBase = hsl[1] > 0.0 ? smoothstep(b1 - c * 0.5, b1 + c * 0.5, 1.0 - (pow(1.0 - hsl[1] * cos(a), 2.0) + pow(1.0 - perceivedBrightness * sin(a), 2.0))) : 0.0;

        float saturation = clamp(hsl[1] + (boostBase * vibrancy) / float(passes), 0.0, 1.0);

        vec3  newColor = hsl2rgb(vec3(hsl[0], saturation, hsl[2]));

        gl_FragColor = vec4(newColor, color[3]);
    }
}
)#";

inline const std::string FRAGBLUR2 = R"#(
#version 100
precision highp float;
varying highp vec2 v_texcoord; // is in 0-1
uniform sampler2D tex;

uniform float radius;
uniform vec2 halfpixel;

void main() {
    vec2 uv = v_texcoord / 2.0;

    vec4 sum = texture2D(tex, uv + vec2(-halfpixel.x * 2.0, 0.0) * radius);

    sum += texture2D(tex, uv + vec2(-halfpixel.x, halfpixel.y) * radius) * 2.0;
    sum += texture2D(tex, uv + vec2(0.0, halfpixel.y * 2.0) * radius);
    sum += texture2D(tex, uv + vec2(halfpixel.x, halfpixel.y) * radius) * 2.0;
    sum += texture2D(tex, uv + vec2(halfpixel.x * 2.0, 0.0) * radius);
    sum += texture2D(tex, uv + vec2(halfpixel.x, -halfpixel.y) * radius) * 2.0;
    sum += texture2D(tex, uv + vec2(0.0, -halfpixel.y * 2.0) * radius);
    sum += texture2D(tex, uv + vec2(-halfpixel.x, -halfpixel.y) * radius) * 2.0;

    gl_FragColor = sum / 12.0;
}
)#";

inline const std::string FRAGBLURPREPARE = R"#(
precision         highp float;
varying vec2      v_texcoord; // is in 0-1
uniform sampler2D tex;

uniform float     contrast;
uniform float     brightness;

float gain(float x, float k) {
    float a = 0.5 * pow(2.0 * ((x < 0.5) ? x : 1.0 - x), k);
    return (x < 0.5) ? a : 1.0 - a;
}

void main() {
    vec4 pixColor = texture2D(tex, v_texcoord);

    // contrast
    if (contrast != 1.0) {
        pixColor.r = gain(pixColor.r, contrast);
        pixColor.g = gain(pixColor.g, contrast);
        pixColor.b = gain(pixColor.b, contrast);
    }

    // brightness
    if (brightness > 1.0) {
        pixColor.rgb *= brightness;
    }

    gl_FragColor = pixColor;
}
)#";

inline const std::string FRAGBLURFINISH = R"#(
precision         highp float;
varying vec2      v_texcoord; // is in 0-1
uniform sampler2D tex;

uniform float     noise;
uniform float     brightness;

uniform int       colorize;
uniform vec3      colorizeTint;
uniform float     boostA;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 pixColor = texture2D(tex, v_texcoord);

    // noise
    float noiseHash   = hash(v_texcoord);
    float noiseAmount = (mod(noiseHash, 1.0) - 0.5);
    pixColor.rgb += noiseAmount * noise;

    // brightness
    if (brightness < 1.0) {
        pixColor.rgb *= brightness;
    }

    pixColor.a *= boostA;

    if (colorize == 1) {
        gl_FragColor = vec4(colorizeTint.r * pixColor.a, colorizeTint.g * pixColor.a, colorizeTint.b * pixColor.a, pixColor.a);
        return;
    }

    gl_FragColor = pixColor;
}
)#";

// makes a stencil without corners
inline const std::string FRAGBORDER = R"#(
precision highp float;
varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 topLeft;
uniform vec2 fullSize;
uniform vec2 fullSizeUntransformed;
uniform float radius;
uniform float radiusOuter;
uniform float thick;

// Gradients are in OkLabA!!!! {l, a, b, alpha}
uniform vec4 gradient[10];
uniform vec4 gradient2[10];
uniform int gradientLength;
uniform int gradient2Length;
uniform float angle;
uniform float angle2;
uniform float gradientLerp;
uniform float alpha;

float linearToGamma(float x) {
    return x >= 0.0031308 ? 1.055 * pow(x, 0.416666666) - 0.055 : 12.92 * x;
}

vec4 okLabAToSrgb(vec4 lab) {
    float l = pow(lab[0] + lab[1] * 0.3963377774 + lab[2] * 0.2158037573, 3.0);
    float m = pow(lab[0] + lab[1] * (-0.1055613458) + lab[2] * (-0.0638541728), 3.0);
    float s = pow(lab[0] + lab[1] * (-0.0894841775) + lab[2] * (-1.2914855480), 3.0);

    return vec4(linearToGamma(l * 4.0767416621 + m * -3.3077115913 + s * 0.2309699292), 
                linearToGamma(l * (-1.2684380046) + m * 2.6097574011 + s * (-0.3413193965)),
                linearToGamma(l * (-0.0041960863) + m * (-0.7034186147) + s * 1.7076147010),
                lab[3]);
}

vec4 getOkColorForCoordArray1(vec2 normalizedCoord) {
    if (gradientLength < 2)
        return gradient[0];

    float finalAng = 0.0;

    if (angle > 4.71 /* 270 deg */) {
        normalizedCoord[1] = 1.0 - normalizedCoord[1];
        finalAng = 6.28 - angle;
    } else if (angle > 3.14 /* 180 deg */) {
        normalizedCoord[0] = 1.0 - normalizedCoord[0];
        normalizedCoord[1] = 1.0 - normalizedCoord[1];
        finalAng = angle - 3.14;
    } else if (angle > 1.57 /* 90 deg */) {
        normalizedCoord[0] = 1.0 - normalizedCoord[0];
        finalAng = 3.14 - angle;
    } else {
        finalAng = angle;
    }

    float sine = sin(finalAng);

    float progress = (normalizedCoord[1] * sine + normalizedCoord[0] * (1.0 - sine)) * float(gradientLength - 1);
    int bottom = int(floor(progress));
    int top = bottom + 1;

    return gradient[top] * (progress - float(bottom)) + gradient[bottom] * (float(top) - progress);
}

vec4 getOkColorForCoordArray2(vec2 normalizedCoord) {
    if (gradient2Length < 2)
        return gradient2[0];

    float finalAng = 0.0;

    if (angle2 > 4.71 /* 270 deg */) {
        normalizedCoord[1] = 1.0 - normalizedCoord[1];
        finalAng = 6.28 - angle;
    } else if (angle2 > 3.14 /* 180 deg */) {
        normalizedCoord[0] = 1.0 - normalizedCoord[0];
        normalizedCoord[1] = 1.0 - normalizedCoord[1];
        finalAng = angle - 3.14;
    } else if (angle2 > 1.57 /* 90 deg */) {
        normalizedCoord[0] = 1.0 - normalizedCoord[0];
        finalAng = 3.14 - angle2;
    } else {
        finalAng = angle2;
    }

    float sine = sin(finalAng);

    float progress = (normalizedCoord[1] * sine + normalizedCoord[0] * (1.0 - sine)) * float(gradient2Length - 1);
    int bottom = int(floor(progress));
    int top = bottom + 1;

    return gradient2[top] * (progress - float(bottom)) + gradient2[bottom] * (float(top) - progress);
}

vec4 getColorForCoord(vec2 normalizedCoord) {
    vec4 result1 = getOkColorForCoordArray1(normalizedCoord);

    if (gradient2Length <= 0)
        return okLabAToSrgb(result1);

    vec4 result2 = getOkColorForCoordArray2(normalizedCoord);

    return okLabAToSrgb(mix(result1, result2, gradientLerp));
}

void main() {

    highp vec2 pixCoord = vec2(gl_FragCoord);
    highp vec2 pixCoordOuter = pixCoord;
    highp vec2 originalPixCoord = v_texcoord;
    originalPixCoord *= fullSizeUntransformed;
    float additionalAlpha = 1.0;

    vec4 pixColor = vec4(1.0, 1.0, 1.0, 1.0);

    bool done = false;

    pixCoord -= topLeft + fullSize * 0.5;
    pixCoord *= vec2(lessThan(pixCoord, vec2(0.0))) * -2.0 + 1.0;
    pixCoordOuter = pixCoord;
    pixCoord -= fullSize * 0.5 - radius;
    pixCoordOuter -= fullSize * 0.5 - radiusOuter;

    // center the pixes dont make it top-left
    pixCoord += vec2(1.0, 1.0) / fullSize;
    pixCoordOuter += vec2(1.0, 1.0) / fullSize;

    if (min(pixCoord.x, pixCoord.y) > 0.0 && radius > 0.0) {
        // smoothing constant for the edge: more = blurrier, but smoother
        const float SMOOTHING_CONSTANT = )#" +
    std::format("{:.7f}", SHADER_ROUNDED_SMOOTHING_FACTOR) + R"#(;

	    float dist = length(pixCoord);
	    float distOuter = length(pixCoordOuter);
        float h = (thick / 2.0);

	    if (dist < radius - h) {
            // lower
            float normalized = smoothstep(0.0, 1.0, (dist - radius + thick + SMOOTHING_CONSTANT) / (SMOOTHING_CONSTANT * 2.0));
            additionalAlpha *= normalized;
            done = true;
        } else if (min(pixCoordOuter.x, pixCoordOuter.y) > 0.0) {
            // higher
            float normalized = 1.0 - smoothstep(0.0, 1.0, (distOuter - radiusOuter + SMOOTHING_CONSTANT) / (SMOOTHING_CONSTANT * 2.0));
            additionalAlpha *= normalized;
            done = true;
        } else if (distOuter < radiusOuter - h) {
            additionalAlpha = 1.0;
            done = true;
        }
    }

    // now check for other shit
    if (!done) {
        // distance to all straight bb borders
        float distanceT = originalPixCoord[1];
        float distanceB = fullSizeUntransformed[1] - originalPixCoord[1];
        float distanceL = originalPixCoord[0];
        float distanceR = fullSizeUntransformed[0] - originalPixCoord[0];

        // get the smallest
        float smallest = min(min(distanceT, distanceB), min(distanceL, distanceR));

        if (smallest > thick)
            discard;
    }

    if (additionalAlpha == 0.0)
        discard;

    pixColor = getColorForCoord(v_texcoord);
    pixColor.rgb *= pixColor[3];

    pixColor *= alpha * additionalAlpha;

    gl_FragColor = pixColor;
}
)#";