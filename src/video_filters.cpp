#include "video_filters.h"
#include "gl_loader.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>


static const char* kLcdGridSrc = R"RA(// Parameter lines go here:
#pragma parameter GRID_STRENGTH "LCD Grid Strength" 0.05 0.0 1.0 0.01
#pragma parameter gamma "LCD Input Gamma" 2.2 1.0 5.0 0.1

#if defined(VERTEX)

#if __VERSION__ >= 130
#define COMPAT_VARYING out
#define COMPAT_ATTRIBUTE in
#define COMPAT_TEXTURE texture
#else
#define COMPAT_VARYING varying 
#define COMPAT_ATTRIBUTE attribute 
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#define COMPAT_PRECISION highp
#else
#define COMPAT_PRECISION
#endif

COMPAT_ATTRIBUTE vec4 VertexCoord;
COMPAT_ATTRIBUTE vec4 COLOR;
COMPAT_ATTRIBUTE vec4 TexCoord;
COMPAT_VARYING vec4 COL0;
COMPAT_VARYING vec4 TEX0;

uniform mat4 MVPMatrix;
uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    COL0 = COLOR;
    TEX0.xy = TexCoord.xy;
}

#elif defined(FRAGMENT)

#if __VERSION__ >= 130
#define COMPAT_VARYING in
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define COMPAT_VARYING varying
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define COMPAT_PRECISION highp
#else
#define COMPAT_PRECISION
#endif

uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform COMPAT_PRECISION vec2 OrigInputSize;
uniform sampler2D Texture;
COMPAT_VARYING vec4 TEX0;

// compatibility #defines
#define Source Texture
#define vTexCoord TEX0.xy

#define SourceSize vec4(TextureSize, 1.0 / TextureSize) //either TextureSize or InputSize
#define outsize vec4(OutputSize, 1.0 / OutputSize)

#ifdef PARAMETER_UNIFORM
// All parameter floats need to have COMPAT_PRECISION in front of them
uniform COMPAT_PRECISION float GRID_STRENGTH;
uniform COMPAT_PRECISION float gamma;
#else
#define GRID_STRENGTH 0.05
#define gamma 2.2
#endif

float intsmear_func(float z)
{
  float z2 = z*z;
  float z4 = z2*z2;
  float z8 = z4*z4;
  return z - 2.0/3.0*z*z2 - 1.0/5.0*z*z4 + 4.0/7.0*z*z2*z4 - 1.0/9.0*z*z8
    - 2.0/11.0*z*z2*z8 + 1.0/13.0*z*z4*z8;
}

float intsmear(float x, float dx)
{
  const float d = 1.5;
  float zl = clamp((x-dx)/d,-1.0,1.0);
  float zh = clamp((x+dx)/d,-1.0,1.0);
  return d * ( intsmear_func(zh) - intsmear_func(zl) )/(2.0*dx);
}

#define round(x) floor( (x) + 0.5 )
#define TEX2D(c) pow(COMPAT_TEXTURE(Source, (c)), vec4(gamma))
#define OriginalTexelSize (InputSize.xy / (TextureSize.xy * OrigInputSize.xy))

void main()
{
  vec2 texelSize = OriginalTexelSize;
  vec2 subtexelSize = texelSize / vec2(3.0,1.0);
  vec2 range;
  range = InputSize.xy / (OutputSize.xy * TextureSize.xy);
  
  float left   = vTexCoord.x - texelSize.x*0.4999;
  float top    = vTexCoord.y + range.y;
  float right  = vTexCoord.x + texelSize.x*0.4999;
  float bottom = vTexCoord.y - range.y;
  
  vec4 lcol, rcol;
  float subpix = mod(vTexCoord.x/subtexelSize.x+1.5,3.0);
  float rsubpix = range.x/subtexelSize.x;
  lcol = vec4(intsmear(subpix+1.0,rsubpix),intsmear(subpix    ,rsubpix),
	          intsmear(subpix-1.0,rsubpix),0.0);
  rcol = vec4(intsmear(subpix-2.0,rsubpix),intsmear(subpix-3.0,rsubpix),
	          intsmear(subpix-4.0,rsubpix),0.0);
			  
  vec4 topLeftColor     = TEX2D((floor(vec2(left, top)     / texelSize) + 0.4999) * texelSize) * lcol;
  vec4 bottomRightColor = TEX2D((floor(vec2(right, bottom) / texelSize) + 0.4999) * texelSize) * rcol;
  vec4 bottomLeftColor  = TEX2D((floor(vec2(left, bottom)  / texelSize) + 0.4999) * texelSize) * lcol;
  vec4 topRightColor    = TEX2D((floor(vec2(right, top)    / texelSize) + 0.4999) * texelSize) * rcol;
  
  vec2 border = round(vTexCoord.st/subtexelSize);
  vec2 bordert = clamp((border+vec2(0.0,+GRID_STRENGTH)) * subtexelSize,
		       vec2(left, bottom), vec2(right, top));
  vec2 borderb = clamp((border+vec2(0.0,-GRID_STRENGTH)) * subtexelSize,
		       vec2(left, bottom), vec2(right, top));
  float totalArea = 2.0 * range.y;  

   vec4 averageColor;
  averageColor  = ((top - bordert.y)    / totalArea) * topLeftColor;
  averageColor += ((borderb.y - bottom) / totalArea) * bottomRightColor;
  averageColor += ((borderb.y - bottom) / totalArea) * bottomLeftColor;
  averageColor += ((top - bordert.y)    / totalArea) * topRightColor;
  
   FragColor = pow(averageColor,vec4(1.0/gamma));
#ifdef GL_ES
   // fix broken clamp behavior used in console-border shaders
   if (vTexCoord.x > 0.0007 && vTexCoord.x < 0.9999 && vTexCoord.y > 0.0007 && vTexCoord.y < 0.9999)
      FragColor = FragColor;
   else
      FragColor = vec4(0.0);
#endif
} 
#endif
)RA";
static const char* kCrtGeomSrc = R"RA(/*
    CRT-interlaced

    Copyright (C) 2010-2012 cgwg, Themaister and DOLLS

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version.

    (cgwg gave their consent to have the original version of this shader
    distributed under the GPL in this message:

        http://board.byuu.org/viewtopic.php?p=26075#p26075

        "Feel free to distribute my shaders under the GPL. After all, the
        barrel distortion code was taken from the Curvature shader, which is
        under the GPL."
    )
	This shader variant is pre-configured with screen curvature
*/

#pragma parameter CRTgamma "CRTGeom Target Gamma" 2.4 0.1 5.0 0.1
#pragma parameter INV "Inverse Gamma/CRT-Geom Gamma out" 0.0 0.0 1.0 1.0
#pragma parameter monitorgamma "CRTGeom Monitor Gamma" 2.2 0.1 5.0 0.1
#pragma parameter d "CRTGeom Distance" 1.6 0.1 3.0 0.1
#pragma parameter CURVATURE "CRTGeom Curvature Toggle" 1.0 0.0 1.0 1.0
#pragma parameter R "CRTGeom Curvature Radius" 2.0 0.1 10.0 0.1
#pragma parameter cornersize "CRTGeom Corner Size" 0.03 0.001 1.0 0.005
#pragma parameter cornersmooth "CRTGeom Corner Smoothness" 1000.0 80.0 2000.0 100.0
#pragma parameter x_tilt "CRTGeom Horizontal Tilt" 0.0 -0.5 0.5 0.05
#pragma parameter y_tilt "CRTGeom Vertical Tilt" 0.0 -0.5 0.5 0.05
#pragma parameter overscan_x "CRTGeom Horiz. Overscan %" 100.0 -125.0 125.0 1.0
#pragma parameter overscan_y "CRTGeom Vert. Overscan %" 100.0 -125.0 125.0 1.0
#pragma parameter DOTMASK "CRTGeom Dot Mask Strength" 0.3 0.0 1.0 0.1
#pragma parameter SHARPER "CRTGeom Sharpness" 1.0 1.0 3.0 1.0
#pragma parameter scanline_weight "CRTGeom Scanline Weight" 0.3 0.1 0.5 0.05
#pragma parameter lum "CRTGeom Luminance" 0.0 0.0 1.0 0.01
#pragma parameter interlace_detect "CRTGeom Interlacing Simulation" 1.0 0.0 1.0 1.0
#pragma parameter SATURATION "CRTGeom Saturation" 1.0 0.0 2.0 0.05

#ifndef PARAMETER_UNIFORM
#define CRTgamma 2.4
#define monitorgamma 2.2
#define d 1.6
#define CURVATURE 1.0
#define R 2.0
#define cornersize 0.03
#define cornersmooth 1000.0
#define x_tilt 0.0
#define y_tilt 0.0
#define overscan_x 100.0
#define overscan_y 100.0
#define DOTMASK 0.3
#define SHARPER 1.0
#define scanline_weight 0.3
#define lum 0.0
#define interlace_detect 1.0
#define SATURATION 1.0
#define INV 1.0
#endif

#if defined(VERTEX)

#if __VERSION__ >= 130
#define COMPAT_VARYING out
#define COMPAT_ATTRIBUTE in
#define COMPAT_TEXTURE texture
#else
#define COMPAT_VARYING varying 
#define COMPAT_ATTRIBUTE attribute 
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

COMPAT_ATTRIBUTE vec4 VertexCoord;
COMPAT_ATTRIBUTE vec4 COLOR;
COMPAT_ATTRIBUTE vec4 TexCoord;
COMPAT_VARYING vec4 COL0;
COMPAT_VARYING vec4 TEX0;

vec4 _oPosition1; 
uniform mat4 MVPMatrix;
uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;

COMPAT_VARYING vec2 overscan;
COMPAT_VARYING vec2 aspect;
COMPAT_VARYING vec3 stretch;
COMPAT_VARYING vec2 sinangle;
COMPAT_VARYING vec2 cosangle;
COMPAT_VARYING vec2 one;
COMPAT_VARYING float mod_factor;
COMPAT_VARYING vec2 ilfac;

#ifdef PARAMETER_UNIFORM
uniform COMPAT_PRECISION float CRTgamma;
uniform COMPAT_PRECISION float monitorgamma;
uniform COMPAT_PRECISION float d;
uniform COMPAT_PRECISION float CURVATURE;
uniform COMPAT_PRECISION float R;
uniform COMPAT_PRECISION float cornersize;
uniform COMPAT_PRECISION float cornersmooth;
uniform COMPAT_PRECISION float x_tilt;
uniform COMPAT_PRECISION float y_tilt;
uniform COMPAT_PRECISION float overscan_x;
uniform COMPAT_PRECISION float overscan_y;
uniform COMPAT_PRECISION float DOTMASK;
uniform COMPAT_PRECISION float SHARPER;
uniform COMPAT_PRECISION float scanline_weight;
uniform COMPAT_PRECISION float lum;
uniform COMPAT_PRECISION float interlace_detect;
uniform COMPAT_PRECISION float SATURATION;
#endif

#define FIX(c) max(abs(c), 1e-5);

float intersect(vec2 xy)
        {
	float A = dot(xy,xy)+d*d;
	float B = 2.0*(R*(dot(xy,sinangle)-d*cosangle.x*cosangle.y)-d*d);
	float C = d*d + 2.0*R*d*cosangle.x*cosangle.y;
	return (-B-sqrt(B*B-4.0*A*C))/(2.0*A);
        }

vec2 bkwtrans(vec2 xy)
        {
	float c = intersect(xy);
	vec2 point = vec2(c)*xy;
	point -= vec2(-R)*sinangle;
	point /= vec2(R);
	vec2 tang = sinangle/cosangle;
	vec2 poc = point/cosangle;
	float A = dot(tang,tang)+1.0;
	float B = -2.0*dot(poc,tang);
	float C = dot(poc,poc)-1.0;
	float a = (-B+sqrt(B*B-4.0*A*C))/(2.0*A);
	vec2 uv = (point-a*sinangle)/cosangle;
	float r = R*acos(a);
	return uv*r/sin(r/R);
        }

vec2 fwtrans(vec2 uv)
        {
	float r = FIX(sqrt(dot(uv,uv)));
	uv *= sin(r/R)/r;
	float x = 1.0-cos(r/R);
	float D = d/R + x*cosangle.x*cosangle.y+dot(uv,sinangle);
	return d*(uv*cosangle-x*sinangle)/D;
        }

vec3 maxscale()
        {
	vec2 c = bkwtrans(-R * sinangle / (1.0 + R/d*cosangle.x*cosangle.y));
	vec2 a = vec2(0.5,0.5)*aspect;
	vec2 lo = vec2(fwtrans(vec2(-a.x,c.y)).x, fwtrans(vec2(c.x,-a.y)).y)/aspect;
	vec2 hi = vec2(fwtrans(vec2(+a.x,c.y)).x, fwtrans(vec2(c.x,+a.y)).y)/aspect;
	return vec3((hi+lo)*aspect*0.5,max(hi.x-lo.x,hi.y-lo.y));
        }

void main()
{
// START of parameters

// gamma of simulated CRT
//	CRTgamma = 1.8;
// gamma of display monitor (typically 2.2 is correct)
//	monitorgamma = 2.2;
// overscan (e.g. 1.02 for 2% overscan)
	overscan = vec2(1.00,1.00);
// aspect ratio
	aspect = vec2(1.0, 0.75);
// lengths are measured in units of (approximately) the width
// of the monitor simulated distance from viewer to monitor
//	d = 2.0;
// radius of curvature
//	R = 1.5;
// tilt angle in radians
// (behavior might be a bit wrong if both components are
// nonzero)
	const vec2 angle = vec2(0.0,0.0);
// size of curved corners
//	cornersize = 0.03;
// border smoothness parameter
// decrease if borders are too aliased
//	cornersmooth = 1000.0;

// END of parameters

    vec4 _oColor;
    vec2 _otexCoord;
    gl_Position = VertexCoord.x * MVPMatrix[0] + VertexCoord.y * MVPMatrix[1] + VertexCoord.z * MVPMatrix[2] + VertexCoord.w * MVPMatrix[3];
    _oPosition1 = gl_Position;
    _oColor = COLOR;
    _otexCoord = TexCoord.xy*1.0001;
    COL0 = COLOR;
    TEX0.xy = TexCoord.xy*1.0001;

// Precalculate a bunch of useful values we'll need in the fragment
// shader.
	sinangle = sin(vec2(x_tilt, y_tilt)) + vec2(0.001);//sin(vec2(max(abs(x_tilt), 1e-3), max(abs(y_tilt), 1e-3)));
	cosangle = cos(vec2(x_tilt, y_tilt)) + vec2(0.001);//cos(vec2(max(abs(x_tilt), 1e-3), max(abs(y_tilt), 1e-3)));
	stretch = maxscale();

	ilfac = vec2(1.0,clamp(floor(InputSize.y/200.0), 1.0, 2.0));

// The size of one texel, in texture-coordinates.
	vec2 sharpTextureSize = vec2(SHARPER * TextureSize.x, TextureSize.y);
	one = ilfac / sharpTextureSize;

// Resulting X pixel-coordinate of the pixel we're drawing.
	mod_factor = TexCoord.x * TextureSize.x * OutputSize.x / InputSize.x;

}

#elif defined(FRAGMENT)

#if __VERSION__ >= 130
#define COMPAT_VARYING in
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define COMPAT_VARYING varying
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

struct output_dummy {
    vec4 _color;
};

uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform sampler2D Texture;
COMPAT_VARYING vec4 TEX0;

// Comment the next line to disable interpolation in linear gamma (and
// gain speed).
	#define LINEAR_PROCESSING

// Enable screen curvature.
//        #define CURVATURE

// Enable 3x oversampling of the beam profile
        #define OVERSAMPLE

// Use the older, purely gaussian beam profile
        //#define USEGAUSSIAN

// Macros.
#define FIX(c) max(abs(c), 1e-5);
#define PI 3.141592653589

#ifdef LINEAR_PROCESSING
#       define TEX2D(c) pow(COMPAT_TEXTURE(Texture, (c)), vec4(CRTgamma))
#else
#       define TEX2D(c) COMPAT_TEXTURE(Texture, (c))
#endif

COMPAT_VARYING vec2 one;
COMPAT_VARYING float mod_factor;
COMPAT_VARYING vec2 ilfac;
COMPAT_VARYING vec2 overscan;
COMPAT_VARYING vec2 aspect;
COMPAT_VARYING vec3 stretch;
COMPAT_VARYING vec2 sinangle;
COMPAT_VARYING vec2 cosangle;

#ifdef PARAMETER_UNIFORM
uniform COMPAT_PRECISION float CRTgamma;
uniform COMPAT_PRECISION float monitorgamma;
uniform COMPAT_PRECISION float d;
uniform COMPAT_PRECISION float CURVATURE;
uniform COMPAT_PRECISION float R;
uniform COMPAT_PRECISION float cornersize;
uniform COMPAT_PRECISION float cornersmooth;
uniform COMPAT_PRECISION float x_tilt;
uniform COMPAT_PRECISION float y_tilt;
uniform COMPAT_PRECISION float overscan_x;
uniform COMPAT_PRECISION float overscan_y;
uniform COMPAT_PRECISION float DOTMASK;
uniform COMPAT_PRECISION float SHARPER;
uniform COMPAT_PRECISION float scanline_weight;
uniform COMPAT_PRECISION float lum;
uniform COMPAT_PRECISION float interlace_detect;
uniform COMPAT_PRECISION float SATURATION;
uniform COMPAT_PRECISION float INV;
#endif

float intersect(vec2 xy)
        {
	float A = dot(xy,xy)+d*d;
	float B = 2.0*(R*(dot(xy,sinangle)-d*cosangle.x*cosangle.y)-d*d);
	float C = d*d + 2.0*R*d*cosangle.x*cosangle.y;
	return (-B-sqrt(B*B-4.0*A*C))/(2.0*A);
        }

vec2 bkwtrans(vec2 xy)
        {
	float c = intersect(xy);
	vec2 point = vec2(c)*xy;
	point -= vec2(-R)*sinangle;
	point /= vec2(R);
	vec2 tang = sinangle/cosangle;
	vec2 poc = point/cosangle;
	float A = dot(tang,tang)+1.0;
	float B = -2.0*dot(poc,tang);
	float C = dot(poc,poc)-1.0;
	float a = (-B+sqrt(B*B-4.0*A*C))/(2.0*A);
	vec2 uv = (point-a*sinangle)/cosangle;
	float r = FIX(R*acos(a));
	return uv*r/sin(r/R);
        }

vec2 transform(vec2 coord)
        {
	coord *= TextureSize / InputSize;
	coord = (coord-vec2(0.5))*aspect*stretch.z+stretch.xy;
	return (bkwtrans(coord)/vec2(overscan_x / 100.0, overscan_y / 100.0)/aspect+vec2(0.5)) * InputSize / TextureSize;
        }

float corner(vec2 coord)
        {
	coord *= TextureSize / InputSize;
	coord = (coord - vec2(0.5)) * vec2(overscan_x / 100.0, overscan_y / 100.0) + vec2(0.5);
	coord = min(coord, vec2(1.0)-coord) * aspect;
	vec2 cdist = vec2(cornersize);
	coord = (cdist - min(coord,cdist));
	float dist = sqrt(dot(coord,coord));
	return clamp((cdist.x-dist)*cornersmooth,0.0, 1.0)*1.0001;
        }

// Calculate the influence of a scanline on the current pixel.
//
// 'distance' is the distance in texture coordinates from the current
// pixel to the scanline in question.
// 'color' is the colour of the scanline at the horizontal location of
// the current pixel.
vec4 scanlineWeights(float distance, vec4 color)
        {
	// "wid" controls the width of the scanline beam, for each RGB
	// channel The "weights" lines basically specify the formula
	// that gives you the profile of the beam, i.e. the intensity as
	// a function of distance from the vertical center of the
	// scanline. In this case, it is gaussian if width=2, and
	// becomes nongaussian for larger widths. Ideally this should
	// be normalized so that the integral across the beam is
	// independent of its width. That is, for a narrower beam
	// "weights" should have a higher peak at the center of the
	// scanline than for a wider beam.
#ifdef USEGAUSSIAN
	vec4 wid = 0.3 + 0.1 * pow(color, vec4(3.0));
	vec4 weights = vec4(distance / wid);
	return (lum + 0.4) * exp(-weights * weights) / wid;
#else
	vec4 wid = 2.0 + 2.0 * pow(color, vec4(4.0));
	vec4 weights = vec4(distance / scanline_weight);
	return (lum + 1.4) * exp(-pow(weights * inversesqrt(0.5 * wid), wid)) / (0.6 + 0.2 * wid);
#endif
        }

vec3 saturation (vec3 textureColor)
{
    float lum=length(textureColor)*0.5775;

    vec3 luminanceWeighting = vec3(0.3,0.6,0.1);
    if (lum<0.5) luminanceWeighting.rgb=(luminanceWeighting.rgb*luminanceWeighting.rgb)+(luminanceWeighting.rgb*luminanceWeighting.rgb);

    float luminance = dot(textureColor, luminanceWeighting);
    vec3 greyScaleColor = vec3(luminance);

    vec3 res = vec3(mix(greyScaleColor, textureColor, SATURATION));
    return res;
}

#define pwr vec3(1.0/((-0.7*(1.0-scanline_weight)+1.0)*(-0.5*DOTMASK+1.0))-1.25)


// Returns gamma corrected output, compensated for scanline+mask embedded gamma
vec3 inv_gamma(vec3 col, vec3 power)
{
    vec3 cir  = col-1.0;
         cir *= cir;
         col  = mix(sqrt(col),sqrt(1.0-cir),power);
    return col;
}

void main()
{
// Here's a helpful diagram to keep in mind while trying to
// understand the code:
//
//  |      |      |      |      |
// -------------------------------
//  |      |      |      |      |
//  |  01  |  11  |  21  |  31  | <-- current scanline
//  |      | @    |      |      |
// -------------------------------
//  |      |      |      |      |
//  |  02  |  12  |  22  |  32  | <-- next scanline
//  |      |      |      |      |
// -------------------------------
//  |      |      |      |      |
//
// Each character-cell represents a pixel on the output
// surface, "@" represents the current pixel (always somewhere
// in the bottom half of the current scan-line, or the top-half
// of the next scanline). The grid of lines represents the
// edges of the texels of the underlying texture.

// Texture coordinates of the texel containing the active pixel.
	vec2 xy = (CURVATURE > 0.5) ? transform(TEX0.xy) : TEX0.xy;

	float cval = corner(xy);

// Of all the pixels that are mapped onto the texel we are
// currently rendering, which pixel are we currently rendering?
	vec2 ilvec = vec2(0.0,ilfac.y * interlace_detect > 1.5 ? mod(float(FrameCount),2.0) : 0.0);
	vec2 ratio_scale = (xy * TextureSize - vec2(0.5) + ilvec)/ilfac;
#ifdef OVERSAMPLE
	float filter_ = InputSize.y/OutputSize.y;//fwidth(ratio_scale.y);
#endif
	vec2 uv_ratio = fract(ratio_scale);

// Snap to the center of the underlying texel.
	xy = (floor(ratio_scale)*ilfac + vec2(0.5) - ilvec) / TextureSize;

// Calculate Lanczos scaling coefficients describing the effect
// of various neighbour texels in a scanline on the current
// pixel.
	vec4 coeffs = PI * vec4(1.0 + uv_ratio.x, uv_ratio.x, 1.0 - uv_ratio.x, 2.0 - uv_ratio.x);

// Prevent division by zero.
	coeffs = FIX(coeffs);

// Lanczos2 kernel.
	coeffs = 2.0 * sin(coeffs) * sin(coeffs / 2.0) / (coeffs * coeffs);

// Normalize.
	coeffs /= dot(coeffs, vec4(1.0));

// Calculate the effective colour of the current and next
// scanlines at the horizontal location of the current pixel,
// using the Lanczos coefficients above.
	vec4 col  = clamp(mat4(
                        TEX2D(xy + vec2(-one.x, 0.0)),
                        TEX2D(xy),
                        TEX2D(xy + vec2(one.x, 0.0)),
                        TEX2D(xy + vec2(2.0 * one.x, 0.0))) * coeffs,
                        0.0, 1.0);
        vec4 col2 = clamp(mat4(
                        TEX2D(xy + vec2(-one.x, one.y)),
                        TEX2D(xy + vec2(0.0, one.y)),
                        TEX2D(xy + one),
                        TEX2D(xy + vec2(2.0 * one.x, one.y))) * coeffs,
                        0.0, 1.0);

#ifndef LINEAR_PROCESSING
	col  = pow(col , vec4(CRTgamma));
	col2 = pow(col2, vec4(CRTgamma));
#endif

// Calculate the influence of the current and next scanlines on
// the current pixel.
	vec4 weights  = scanlineWeights(uv_ratio.y, col);
	vec4 weights2 = scanlineWeights(1.0 - uv_ratio.y, col2);
#ifdef OVERSAMPLE
	uv_ratio.y =uv_ratio.y+1.0/3.0*filter_;
	weights = (weights+scanlineWeights(uv_ratio.y, col))/3.0;
	weights2=(weights2+scanlineWeights(abs(1.0-uv_ratio.y), col2))/3.0;
	uv_ratio.y =uv_ratio.y-2.0/3.0*filter_;
	weights=weights+scanlineWeights(abs(uv_ratio.y), col)/3.0;
	weights2=weights2+scanlineWeights(abs(1.0-uv_ratio.y), col2)/3.0;
#endif

	vec3 mul_res  = (col * weights + col2 * weights2).rgb * vec3(cval);

// dot-mask emulation:
// Output pixels are alternately tinted green and magenta.
vec3 dotMaskWeights = mix(
	vec3(1.0, 1.0 - DOTMASK, 1.0),
	vec3(1.0 - DOTMASK, 1.0, 1.0 - DOTMASK),
	floor(mod(mod_factor, 2.0))
        );

	mul_res *= dotMaskWeights;

// Convert the image gamma for display on our output device.
if (INV == 1.0){ mul_res = inv_gamma(mul_res,pwr);} 
	else mul_res = pow(mul_res, vec3(1.0/monitorgamma));
        
        mul_res = saturation(mul_res);



// Color the texel.
    output_dummy _OUT;
    _OUT._color = vec4(mul_res, 1.0);
    FragColor = _OUT._color;
    return;
} 
#endif
)RA";
static const char* kScanSrc    = R"RA(// Parameter lines go here:
#pragma parameter SCANLINE_SINE_COMP_A "Grid Strength" 0.15 0.0 0.5 0.05
#pragma parameter SCANLINE_SINE_COMP_B "Scanline Strength" 0.5 0.0 1.0 0.05
#pragma parameter size "Grid size"  1.0 1.0 2.0 1.0
#define pi 3.141592654

#if defined(VERTEX)

#if __VERSION__ >= 130
#define COMPAT_VARYING out
#define COMPAT_ATTRIBUTE in
#define COMPAT_TEXTURE texture
#else
#define COMPAT_VARYING varying 
#define COMPAT_ATTRIBUTE attribute 
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

COMPAT_ATTRIBUTE vec4 VertexCoord;
COMPAT_ATTRIBUTE vec4 COLOR;
COMPAT_ATTRIBUTE vec4 TexCoord;
COMPAT_VARYING vec4 COL0;
COMPAT_VARYING vec4 TEX0;
COMPAT_VARYING vec2 omega;

vec4 _oPosition1; 
uniform mat4 MVPMatrix;
uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform COMPAT_PRECISION float size;

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    COL0 = COLOR;
    TEX0.xy = TexCoord.xy*1.0001;
    omega = vec2(TEX0.x*TextureSize.x/InputSize.x*OutputSize.x/size*pi, pi);
}

#elif defined(FRAGMENT)

#if __VERSION__ >= 130
#define COMPAT_VARYING in
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define COMPAT_VARYING varying
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform sampler2D Texture;
COMPAT_VARYING vec4 TEX0;
COMPAT_VARYING vec2 omega;

// compatibility #defines
#define Source Texture
#define vTexCoord TEX0.xy

#define SourceSize vec4(TextureSize, 1.0 / TextureSize) //either TextureSize or InputSize
#define OutputSize vec4(OutputSize, 1.0 / OutputSize)

#ifdef PARAMETER_UNIFORM
// All parameter floats need to have COMPAT_PRECISION in front of them
uniform COMPAT_PRECISION float SCANLINE_SINE_COMP_A;
uniform COMPAT_PRECISION float SCANLINE_SINE_COMP_B;
#else
#define SCANLINE_SINE_COMP_A 0.0
#define SCANLINE_SINE_COMP_B 0.15
#endif

void main()
{
   vec2 sine_comp = vec2(SCANLINE_SINE_COMP_A, SCANLINE_SINE_COMP_B);
   vec3 res = COMPAT_TEXTURE(Source, vTexCoord).xyz;

   float scanline = sine_comp.y * sin(fract(vTexCoord.y*TextureSize.y)*omega.y) + 1.0-sine_comp.y;
   float mask     = sine_comp.x * sin(omega.x)+1.0-sine_comp.x;
   res *= (scanline*mask);
   
   FragColor = vec4(res, 1.0);
} 
#endif
)RA";

struct ShaderParam { std::string name; float def; };

struct FilterProgram
{
    GLuint prog = 0;
    GLint  locMVP         = -1;
    GLint  locOutputSize  = -1;
    GLint  locTextureSize = -1;
    GLint  locInputSize   = -1;
    GLint  locOrigInput   = -1;
    GLint  locFrameCount  = -1;
    GLint  locFrameDir    = -1;
    GLint  locSampler     = -1;
    struct PLoc { std::string name; GLint loc; float def; };
    std::vector<PLoc> paramLocs;
};

static GLuint g_vao = 0, g_vbo = 0;
static GLuint g_fbo = 0, g_fboTex = 0;
static int    g_fboW = 0, g_fboH = 0;
static long   g_frame = 0;

static FilterProgram g_lcd, g_crt, g_scan;

static void parseParams(const std::string& src, std::vector<ShaderParam>& out)
{
    std::istringstream stream(src);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.rfind("#pragma parameter", 0) == 0)
        {
            std::istringstream ls(line);
            std::string pragma, tag, name, label;
            float def = 0.0f;
            ls >> pragma >> tag >> name;
            std::getline(ls, label, '"');
            std::getline(ls, label, '"');
            ls >> def;
            out.push_back({ name, def });
        }
    }
}

static std::string stripParamLines(const std::string& src)
{
    std::istringstream stream(src);
    std::string out, line;
    while (std::getline(stream, line))
    {
        if (line.rfind("#pragma parameter", 0) != 0)
        {
            out += line;
            out += "\n";
        }
    }
    return out;
}

static std::string buildPass(const std::string& stripped, bool fragment)
{
    std::string s("#version 130\n");
    s += fragment ? "#define FRAGMENT\n" : "#define VERTEX\n";
    s += "#define PARAMETER_UNIFORM\n";
    s += stripped;
    return s;
}

static GLuint compileShader(GLenum type, const std::string& src)
{
    GLuint sh = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096] = {0};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[filters] shader compile error:\n%s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static bool buildFilterProgram(FilterProgram& fp, const char* src)
{
    if (fp.prog) { glDeleteProgram(fp.prog); fp.prog = 0; }
    std::vector<ShaderParam> params;
    parseParams(src, params);
    std::string stripped = stripParamLines(src);

    std::string vs = buildPass(stripped, false);
    std::string fs = buildPass(stripped, true);
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return false;

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "VertexCoord");
    glBindAttribLocation(p, 1, "TexCoord");
    glBindAttribLocation(p, 2, "COLOR");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096] = {0};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[filters] program link error:\n%s\n", log);
        glDeleteProgram(p);
        return false;
    }

    fp.prog = p;
    fp.locMVP         = glGetUniformLocation(p, "MVPMatrix");
    fp.locOutputSize  = glGetUniformLocation(p, "OutputSize");
    fp.locTextureSize = glGetUniformLocation(p, "TextureSize");
    fp.locInputSize   = glGetUniformLocation(p, "InputSize");
    fp.locOrigInput   = glGetUniformLocation(p, "OrigInputSize");
    fp.locFrameCount  = glGetUniformLocation(p, "FrameCount");
    fp.locFrameDir    = glGetUniformLocation(p, "FrameDirection");
    fp.locSampler     = glGetUniformLocation(p, "Texture");
    for (const auto& prm : params)
    {
        GLint loc = glGetUniformLocation(p, prm.name.c_str());
        if (loc >= 0)
            fp.paramLocs.push_back({ prm.name, loc, prm.def });
    }
    return true;
}

const char* videoFilterName(int filter)
{
    switch (filter)
    {
        case 0:  return "None";
        case 1:  return "Bilinear";
        case 2:  return "LCD Grid";
        case 3:  return "CRT";
        case 4:  return "Scanlines";
        default: return "None";
    }
}

int videoFilterCount() { return 5; }

bool videoFilterUsesPipeline(int filter)
{
    return filter >= 2;
}

void videoFiltersInit()
{
    glLoaderInit();
    if (!g_vao)
    {
        float quad[48] = {
            -1, -1, 0, 1,   0, 0, 0, 1,   1, 1, 1, 1,
             1, -1, 0, 1,   1, 0, 0, 1,   1, 1, 1, 1,
            -1,  1, 0, 1,   0, 1, 0, 1,   1, 1, 1, 1,
             1,  1, 0, 1,   1, 1, 0, 1,   1, 1, 1, 1,
        };
        glGenVertexArrays(1, &g_vao);
        glBindVertexArray(g_vao);
        glGenBuffers(1, &g_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        GLsizei stride = 12 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    buildFilterProgram(g_lcd, kLcdGridSrc);
    buildFilterProgram(g_crt, kCrtGeomSrc);
    buildFilterProgram(g_scan, kScanSrc);
}

void videoFiltersShutdown()
{
    if (g_lcd.prog) glDeleteProgram(g_lcd.prog);
    if (g_crt.prog) glDeleteProgram(g_crt.prog);
    if (g_scan.prog) glDeleteProgram(g_scan.prog);
    g_lcd = {}; g_crt = {}; g_scan = {};
    if (g_fboTex) { glDeleteTextures(1, &g_fboTex); g_fboTex = 0; }
    if (g_fbo) { glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
}

static void ensureFbo(int w, int h)
{
    if (g_fboTex && g_fboW == w && g_fboH == h)
        return;

    if (g_fboTex) glDeleteTextures(1, &g_fboTex);
    if (g_fbo) glDeleteFramebuffers(1, &g_fbo);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

    glGenTextures(1, &g_fboTex);
    glBindTexture(GL_TEXTURE_2D, g_fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fboTex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "[filters] FBO incomplete: 0x%X\n", status);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_fboW = w; g_fboH = h;
}

static void renderProgram(FilterProgram& fp, GLuint srcTex, int srcW, int srcH,
                          int outW, int outH)
{
    if (!fp.prog)
        return;

    ensureFbo(outW, outH);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, outW, outH);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    { glClearColor(1.0f, 0.0f, 1.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); }
    glUseProgram(fp.prog);

    float ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    if (fp.locMVP >= 0) glUniformMatrix4fv(fp.locMVP, 1, GL_FALSE, ident);
    if (fp.locOutputSize >= 0) glUniform2f(fp.locOutputSize, (float)outW, (float)outH);
    if (fp.locTextureSize >= 0) glUniform2f(fp.locTextureSize, (float)srcW, (float)srcH);
    if (fp.locInputSize >= 0) glUniform2f(fp.locInputSize, (float)srcW, (float)srcH);
    if (fp.locOrigInput >= 0) glUniform2f(fp.locOrigInput, (float)srcW, (float)srcH);
    if (fp.locFrameCount >= 0) glUniform1i(fp.locFrameCount, (int)g_frame);
    if (fp.locFrameDir >= 0) glUniform1i(fp.locFrameDir, 0);
    if (fp.locSampler >= 0) glUniform1i(fp.locSampler, 0);

    for (const auto& pl : fp.paramLocs)
        glUniform1f(pl.loc, pl.def);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // Filter shaders write alpha=0 (they assume an opaque compositor); ImGui
    // composites by SRC_ALPHA, so force the FBO alpha channel to opaque while
    // preserving the rendered RGB.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

uint32_t videoFilterRender(uint32_t srcTex, int srcW, int srcH,
                           int outW, int outH, int filter)
{
    if (!videoFilterUsesPipeline(filter))
        return srcTex;

    FilterProgram* fp = nullptr;
    if (filter == 2) fp = &g_lcd;
    else if (filter == 3) fp = &g_crt;
    else if (filter == 4) fp = &g_scan;
    if (!fp || !fp->prog)
        return srcTex;

    ++g_frame;
    renderProgram(*fp, srcTex, srcW, srcH, outW, outH);
    return g_fboTex;
}