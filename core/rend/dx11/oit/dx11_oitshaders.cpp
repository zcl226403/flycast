/*
	Copyright 2021 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "dx11_oitshaders.h"
#include "../dx11context.h"
#include "../dx11_naomi2.h"

const char * const VertexShader = R"(
#if pp_Gouraud == 1
#define INTERPOLATION
#else
#define INTERPOLATION nointerpolation
#endif

struct VertexIn
{
	float4 pos : POSITION;
	float4 col : COLOR0;
	float4 spec : COLOR1;
	float2 uv : TEXCOORD0;
	float4 col1 : COLOR2;
	float4 spec1 : COLOR3;
	float2 uv1 : TEXCOORD1;
	float3 normal: NORMAL; // unused
	uint vertexId : SV_VertexID;
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
	INTERPOLATION float4 col : COLOR0;
	INTERPOLATION float4 spec : COLOR1;
	float2 uv1 : TEXCOORD1;
	INTERPOLATION float4 col1 : COLOR2;
	INTERPOLATION float4 spec1 : COLOR3;
	nointerpolation uint index : BLENDINDICES0;
};

cbuffer shaderConstants : register(b0)
{
	float4x4 transMatrix;
	float4 leftPlane;
	float4 topPlane;
	float4 rightPlane;
	float4 bottomPlane;
};

cbuffer polyConstants : register(b1)
{
	int polyNumber;
};

[clipplanes(leftPlane, topPlane, rightPlane, bottomPlane)]
VertexOut main(in VertexIn vin)
{
	VertexOut vo;
	vo.pos = mul(transMatrix, float4(vin.pos.xyz, 1.f));
#if pp_Gouraud == 1
	vo.col = vin.col * vo.pos.z;
	vo.spec = vin.spec * vo.pos.z;
	vo.col1 = vin.col1 * vo.pos.z;
	vo.spec1 = vin.spec1 * vo.pos.z;
#else
	// flat shading: no interpolation
	vo.col = vin.col;
	vo.spec = vin.spec;
	vo.col1 = vin.col1;
	vo.spec1 = vin.spec1;
#endif
	vo.uv = float4(vin.uv * vo.pos.z, 0.f, vo.pos.z);
	vo.uv1 = vin.uv1 * vo.pos.z;
	vo.index = (uint(polyNumber) << 18) + vin.vertexId;

	vo.pos.w = 1.f;
	vo.pos.z = 0.f;

	return vo;
}

)";

extern const char *ModVolVertexShader;

static const char OITShaderHeader[] = R"(

cbuffer constantBuffer : register(b0)
{
	float4 colorClampMin;
	float4 colorClampMax;
	float4 FOG_COL_VERT;
	float4 FOG_COL_RAM;
	float fogDensity;
	float shadowScale;
	float alphaTestValue;
};

RWTexture2D<uint> abufferPointers : register(u2);

struct Pixel {
	uint color;
	float depth;
	uint seq_num;
	uint next;
};
#define EOL 0xFFFFFFFFu

#define ZERO				0
#define ONE					1
#define OTHER_COLOR			2
#define INVERSE_OTHER_COLOR	3
#define SRC_ALPHA			4
#define INVERSE_SRC_ALPHA	5
#define DST_ALPHA			6
#define INVERSE_DST_ALPHA	7

float getFragDepth(float z)
{
	float w = 100000.0 * z;
	return log2(1.0 + w) / 34.0;
}

struct PolyParam {
	int tsp_isp_pcw;
	int tsp1;
};

#define GET_TSP_FOR_AREA int tsp = area1 ? pp.tsp1 : pp.tsp_isp_pcw;

int getSrcBlendFunc(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return (tsp >> 29) & 7;
}

int getDstBlendFunc(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return (tsp >> 26) & 7;
}

bool getSrcSelect(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return ((tsp >> 25) & 1) != 0;
}

bool getDstSelect(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return ((tsp >> 24) & 1) != 0;
}

int getFogControl(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return (tsp >> 22) & 3;
}

bool getUseAlpha(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return ((tsp >> 20) & 1) != 0;
}

bool getIgnoreTexAlpha(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return ((tsp >> 19) & 1) != 0;
}

int getShadingInstruction(in PolyParam pp, in bool area1)
{
	GET_TSP_FOR_AREA
	return (tsp >> 6) & 3;
}

int getDepthFunc(in PolyParam pp)
{
	return (pp.tsp_isp_pcw >> 13) & 7;
}

bool getDepthMask(in PolyParam pp)
{
	return ((pp.tsp_isp_pcw >> 10) & 1) != 1;
}

bool getShadowEnable(in PolyParam pp)
{
	return (pp.tsp_isp_pcw & 1) != 0;
}

uint getPolyIndex(in Pixel pixel)
{
	return pixel.seq_num & 0x3FFFFFFFu;
}

uint getPolyNumber(in Pixel pixel)
{
	return (pixel.seq_num & 0x3FFFFFFFu) >> 18;
}

#define SHADOW_STENCIL 0x40000000u
#define SHADOW_ACC	   0x80000000u

bool isShadowed(in Pixel pixel)
{
	return (pixel.seq_num & SHADOW_ACC) == SHADOW_ACC;
}

bool isTwoVolumes(in PolyParam pp)
{
	return pp.tsp1 != -1;
}

uint packColors(in float4 v)
{
	return (uint(round(v.r * 255.f)) << 24) | (uint(round(v.g * 255.f)) << 16) | (uint(round(v.b * 255.f)) << 8) | uint(round(v.a * 255.f));
}

float4 unpackColors(in uint u)
{
	return float4(float((u >> 24) & 255u) / 255.f, float((u >> 16) & 255u) / 255.f, float((u >> 8) & 255u) / 255.f, float(u & 255u) / 255.f);
}

RWStructuredBuffer<Pixel> Pixels : register(u1);

uint getNextPixelIndex()
{
	uint index = Pixels.IncrementCounter();
	uint count;
	uint stride;
	Pixels.GetDimensions(count, stride);
	if (index >= count)
		// Buffer overflow
		discard;
	
	return index;
}

StructuredBuffer<PolyParam> tr_poly_params : register(t5);

)";

const char * const PixelShader = R"(

#include "oit_header.hlsl"

#if pp_Gouraud == 1
#define INTERPOLATION
#else
#define INTERPOLATION nointerpolation
#endif

#define PI 3.1415926f
#define PASS_DEPTH 0
#define PASS_COLOR 1
#define PASS_OIT 2

#if pp_TwoVolumes == 1
#define IF(x) if (x)
#else
#define IF(x)
#endif

struct VertexIn 
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
	INTERPOLATION float4 col : COLOR0;
	INTERPOLATION float4 spec : COLOR1;
	float2 uv1 : TEXCOORD1;
	INTERPOLATION float4 col1 : COLOR2;
	INTERPOLATION float4 spec1 : COLOR3;
	nointerpolation uint index : BLENDINDICES0;
};

Texture2D texture0 : register(t0);
sampler sampler0 : register(s0);
Texture2D texture1 : register(t3);
sampler sampler1 : register(s3);

Texture2D paletteTexture : register(t1);
sampler paletteSampler : register(s1);

Texture2D fogTexture : register(t2);
sampler fogSampler : register(s2);

#if PASS == PASS_COLOR
Texture2D<uint2> stencilTexture : register(t4);
#endif
#if PASS == PASS_OIT
Texture2D<float> depthTexture : register(t4);
#endif

cbuffer polyConstantBuffer : register(b1)
{
	float4 clipTest;
	int2 blend_mode0;
	int2 blend_mode1;
	float paletteIndex;
	float trilinearAlpha;

	// two volume mode
	int shading_instr0;
	int shading_instr1;
	int fog_control0;
	int fog_control1;
	int use_alpha0;
	int use_alpha1;
	int ignore_tex_alpha0;
	int ignore_tex_alpha1;
};

float fog_mode2(float w)
{
	float z = clamp(w * fogDensity, 1.0f, 255.9999f);
	float exp = floor(log2(z));
	float m = z * 16.0f / pow(2.0, exp) - 16.0f;
	float idx = floor(m) + exp * 16.0f + 0.5f;
	float4 fogCoef = fogTexture.Sample(fogSampler, float2(idx / 128.0f, 0.75f - (m - floor(m)) / 2.0f));
	return fogCoef.a;
}

float4 clampColor(float4 color)
{
#if FogClamping == 1
	return clamp(color, colorClampMin, colorClampMax);
#else
	return color;
#endif
}

#if pp_Palette == 1

float4 palettePixel(Texture2D tex, sampler texSampler, float2 coords)
{
	uint colorIdx = int(floor(tex.Sample(texSampler, coords).a * 255.0f + 0.5f) + paletteIndex);
    float2 c = float2((fmod(float(colorIdx), 32.0f) * 2.0f + 1.0f) / 64.0f, (float(colorIdx / 32) * 2.0f + 1.0f) / 64.0f);
	return paletteTexture.Sample(paletteSampler, c);
}

#endif

struct PSO
{
	float4 col : SV_TARGET;
	float z : SV_DEPTH;
};

PSO main(in VertexIn inpix)
{
#if pp_ClipInside == 1
	// Clip inside the box
	if (inpix.pos.x >= clipTest.x && inpix.pos.x <= clipTest.z
			&& inpix.pos.y >= clipTest.y && inpix.pos.y <= clipTest.w)
		discard;
#endif
	PSO pso;
	pso.z = getFragDepth(inpix.uv.w);
	#if PASS == PASS_OIT
		// Manual depth testing
		float frontDepth = depthTexture[uint2(inpix.pos.xy)].r;
		if (pso.z < frontDepth)
			discard;
	#endif

	float4 color = inpix.col;
	float4 specular = inpix.spec;
	bool area1 = false;
	int2 cur_blend_mode = blend_mode0.xy;
	
	#if pp_TwoVolumes == 1
		bool cur_use_alpha = use_alpha0 != 0;
		bool cur_ignore_tex_alpha = ignore_tex_alpha0 != 0;
		int cur_shading_instr = shading_instr0;
		int cur_fog_control = fog_control0;
		#if PASS == PASS_COLOR
			uint stencil = stencilTexture[uint2(inpix.pos.xy)].g;
			if (stencil == 0x81u) {
				color = inpix.col1;
				specular = inpix.spec1;
				area1 = true;
				cur_blend_mode = blend_mode1.xy;
				cur_use_alpha = use_alpha1 != 0;
				cur_ignore_tex_alpha = ignore_tex_alpha1 != 0;
				cur_shading_instr = shading_instr1;
				cur_fog_control = fog_control1;
			}
		#endif
	#endif
	#if pp_Gouraud == 1
		color /= inpix.uv.w;
		specular /= inpix.uv.w;
	#endif
	#if pp_UseAlpha == 0 || pp_TwoVolumes == 1
		IF (!cur_use_alpha)
			color.a = 1.0;
	#endif
	#if pp_FogCtrl == 3 || pp_TwoVolumes == 1 // LUT Mode 2
		IF (cur_fog_control == 3)
			color = float4(FOG_COL_RAM.rgb, fog_mode2(inpix.uv.w));
	#endif
	#if pp_Texture==1
	{
		float2 uv;
		#if pp_TwoVolumes == 1
			if (area1)
				uv = inpix.uv1 / inpix.uv.w;
			else
		#endif
				uv = inpix.uv.xy / inpix.uv.w;
		#if NearestWrapFix == 1
			uv = min(fmod(uv, 1.f), 0.9997f);
		#endif
		float4 texcol;
		#if pp_TwoVolumes == 1
			if (area1)
				#if pp_Palette == 0
					texcol = texture1.Sample(sampler1, uv);
				#else
					texcol = palettePixel(texture1, sampler1, uv);
				#endif
			else
		#endif
		#if pp_Palette == 0
				texcol = texture0.Sample(sampler0, uv);
		#else
				texcol = palettePixel(texture0, sampler0, uv);
		#endif
		#if pp_BumpMap == 1
			float s = PI / 2.0f * (texcol.a * 15.0f * 16.0f + texcol.r * 15.0f) / 255.0f;
			float r = 2.0f * PI * (texcol.g * 15.0f * 16.0f + texcol.b * 15.0f) / 255.0f;
			texcol[3] = clamp(specular.a + specular.r * sin(s) + specular.g * cos(s) * cos(r - 2.0f * PI * specular.b), 0.0f, 1.0f);
			texcol.rgb = float3(1.0f, 1.0f, 1.0f);	
		#else
			#if pp_IgnoreTexA==1 || pp_TwoVolumes == 1
				IF(cur_ignore_tex_alpha)
					texcol.a = 1.0f;
			#endif
			#if cp_AlphaTest == 1
				if (alphaTestValue > texcol.a)
					discard;
				texcol.a = 1.0f;
			#endif
		#endif
		#if pp_ShadInstr == 0 || pp_TwoVolumes == 1 // DECAL
		IF(cur_shading_instr == 0)
			color = texcol;
		#endif
		#if pp_ShadInstr == 1 || pp_TwoVolumes == 1 // MODULATE
		IF(cur_shading_instr == 1)
			color.rgb *= texcol.rgb;
			color.a = texcol.a;
		#endif
		#if pp_ShadInstr == 2 || pp_TwoVolumes == 1 // DECAL ALPHA
		IF(cur_shading_instr == 2)
			color.rgb = lerp(color.rgb, texcol.rgb, texcol.a);
		#endif
		#if pp_ShadInstr == 3 || pp_TwoVolumes == 1 // MODULATE ALPHA
		IF(cur_shading_instr == 3)
			color *= texcol;
		#endif
		
		#if pp_Offset == 1 && pp_BumpMap == 0
			color.rgb += specular.rgb;
		#endif
	}
	#endif
	#if PASS == PASS_COLOR && pp_TwoVolumes == 0
		uint stencil = stencilTexture[uint2(inpix.pos.xy)].g;
		if (stencil == 0x81u)
			color.rgb *= shadowScale;
	#endif

	color = clampColor(color);
	
	#if pp_FogCtrl == 0 || pp_TwoVolumes == 1 // LUT
		IF(cur_fog_control == 0)
			color.rgb = lerp(color.rgb, FOG_COL_RAM.rgb, fog_mode2(inpix.uv.w)); 
	#endif
	#if pp_Offset == 1 && pp_BumpMap == 0 && (pp_FogCtrl == 1 || pp_TwoVolumes == 1)  // Per vertex
		IF(cur_fog_control == 1)
			color.rgb = lerp(color.rgb, FOG_COL_VERT.rgb, specular.a);
	#endif
	
	color *= trilinearAlpha;

	#if PASS == PASS_COLOR 
		pso.col = color;
	#elif PASS == PASS_OIT
		// Discard as many pixels as possible
		switch (cur_blend_mode.y) // DST
		{
		case ONE:
			switch (cur_blend_mode.x) // SRC
			{
				case ZERO:
					discard;
					break;
				case ONE:
				case OTHER_COLOR:
				case INVERSE_OTHER_COLOR:
					if (all(color == 0.f))
						discard;
					break;
				case SRC_ALPHA:
					if (color.a == 0.f || all(color.rgb == 0.f))
						discard;
					break;
				case INVERSE_SRC_ALPHA:
					if (color.a == 1.0 || all(color.rgb == 0.f))
						discard;
					break;
			}
			break;
		case OTHER_COLOR:
			if (cur_blend_mode.x == ZERO && all(color == 1.f))
				discard;
			break;
		case INVERSE_OTHER_COLOR:
			if (cur_blend_mode.x <= SRC_ALPHA && all(color == 0.f))
				discard;
			break;
		case SRC_ALPHA:
			if ((cur_blend_mode.x == ZERO || cur_blend_mode.x == INVERSE_SRC_ALPHA) && color.a == 1.f)
				discard;
			break;
		case INVERSE_SRC_ALPHA:
			switch (cur_blend_mode.x) // SRC
			{
				case ZERO:
				case SRC_ALPHA:
					if (color.a == 0.f)
						discard;
					break;
				case ONE:
				case OTHER_COLOR:
				case INVERSE_OTHER_COLOR:
					if (all(color == 0.f))
						discard;
					break;
			}
			break;
		}
		
		uint2 coords = uint2(inpix.pos.xy);
		uint idx =  getNextPixelIndex();
		
		Pixel pixel;
		pixel.color = packColors(clamp(color, 0.f, 1.f));
		pixel.depth = inpix.uv.w;
		pixel.seq_num = inpix.index;
		InterlockedExchange(abufferPointers[coords], idx, pixel.next);
		Pixels[idx] = pixel;
		
		pso.col = 0.f;
	#endif

	return pso;
}

struct MVPixel 
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
};

float modifierVolume(in MVPixel inpix) : SV_Depth
{
	return getFragDepth(inpix.uv.w);
}
)";

const char *MAX_PIXELS_PER_FRAGMENT = "32";

static const char OITFinalShaderSource[] = R"(
#include "oit_header.hlsl"

Texture2D opaqueTex : register(t0);
sampler opaqueSampler : register(s0);

int fillAndSortFragmentArray(in uint2 coords, out uint pixel_list[MAX_PIXELS_PER_FRAGMENT])
{
	uint idx = abufferPointers[coords];
	if (idx == EOL)
		return 0;
	int count = 1;
	pixel_list[0] = idx;
	idx = Pixels[idx].next;
	for (; idx != EOL && count < MAX_PIXELS_PER_FRAGMENT; count++)
	{
		int j = count - 1;
		uint jIdx = pixel_list[j];
		while (j >= 0
			   && (Pixels[jIdx].depth > Pixels[idx].depth
				   || (Pixels[jIdx].depth == Pixels[idx].depth && getPolyIndex(Pixels[jIdx]) > getPolyIndex(Pixels[idx]))))
		{
			pixel_list[j + 1] = pixel_list[j];
			j--;
			if (j >= 0)
				jIdx = pixel_list[j];
		}
		pixel_list[j + 1] = idx;
		idx = Pixels[idx].next;
	}
	// Reset pointer
	abufferPointers[coords] = EOL;

	return count;
}

// Blend fragments back-to-front
float4 resolveAlphaBlend(in float2 pos)
{
	// Copy and sort indexes into a local array
	uint2 coords = uint2(pos);
	uint pixel_list[MAX_PIXELS_PER_FRAGMENT];
	int num_frag = fillAndSortFragmentArray(coords, pixel_list);
	
	float2 dim;
	opaqueTex.GetDimensions(dim.x, dim.y);
	float4 finalColor = opaqueTex.Sample(opaqueSampler, pos / dim);
	float4 secondaryBuffer = 0.f; // Secondary accumulation buffer
	
	for (int i = 0; i < num_frag; i++)
	{
		uint pixIdx = pixel_list[i];
		const Pixel pixel = Pixels[pixIdx];
		const PolyParam pp = tr_poly_params[getPolyNumber(pixel)];
		bool area1 = false;
		bool shadowed = false;
		if (isShadowed(pixel))
		{
			if (isTwoVolumes(pp))
				area1 = true;
			else
				shadowed = true;
		}
		float4 srcColor;
		if (getSrcSelect(pp, area1))
			srcColor = secondaryBuffer;
		else
		{
			srcColor = unpackColors(pixel.color);
			if (shadowed)
				srcColor.rgb *= shadowScale;
		}
		float4 dstColor = getDstSelect(pp, area1) ? secondaryBuffer : finalColor;
		float4 srcCoef;
		float4 dstCoef;
		
		int srcBlend = getSrcBlendFunc(pp, area1);
		switch (srcBlend)
		{
			case ZERO:
				srcCoef = 0.f;
				break;
			case ONE:
				srcCoef = 1.f;
				break;
			case OTHER_COLOR:
				srcCoef = finalColor;
				break;
			case INVERSE_OTHER_COLOR:
				srcCoef = 1.f - dstColor;
				break;
			case SRC_ALPHA:
				srcCoef = srcColor.a;
				break;
			case INVERSE_SRC_ALPHA:
				srcCoef = 1.f - srcColor.a;
				break;
			case DST_ALPHA:
				srcCoef = dstColor.a;
				break;
			case INVERSE_DST_ALPHA:
				srcCoef = 1.f - dstColor.a;
				break;
		}
		int dstBlend = getDstBlendFunc(pp, area1);
		switch (dstBlend)
		{
			case ZERO:
				dstCoef = 0.f;
				break;
			case ONE:
				dstCoef = 1.f;
				break;
			case OTHER_COLOR:
				dstCoef = srcColor;
				break;
			case INVERSE_OTHER_COLOR:
				dstCoef = 1.f - srcColor;
				break;
			case SRC_ALPHA:
				dstCoef = srcColor.a;
				break;
			case INVERSE_SRC_ALPHA:
				dstCoef = 1.f - srcColor.a;
				break;
			case DST_ALPHA:
				dstCoef = dstColor.a;
				break;
			case INVERSE_DST_ALPHA:
				dstCoef = 1.f - dstColor.a;
				break;
		}
		const float4 result = clamp(dstColor * dstCoef + srcColor * srcCoef, 0.f, 1.f);
		if (getDstSelect(pp, area1))
			secondaryBuffer = result;
		else
			finalColor = result;
	}

	return finalColor;
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	// Visualize the number of layers in use
	//uint pixel_list[MAX_PIXELS_PER_FRAGMENT];
	//return float4(float(fillAndSortFragmentArray(uint2(pos.xy), pixel_list)) / MAX_PIXELS_PER_FRAGMENT * 8.f, 0.f, 0.f, 1.f);

	// Compute and output final color for the frame buffer
	return resolveAlphaBlend(pos.xy);
}
)";

static const char OITFinalVertexShaderSource[] = R"(

float4 main(uint vertexId : SV_VertexID) : SV_Position
{
	float4 output;
    
	if (vertexId == 0)
		output = float4(-1.f, -1.f, 0.f, 1.f);
	else if (vertexId == 1)
		output = float4(-1.f, 1.f, 0.f, 1.f);
	else if (vertexId == 2)
		output = float4(1.f, -1.f, 0.f, 1.f);
	else
		output = float4(1.f, 1.f, 0.f, 1.f);
    
	return output;
}
)";

static const char OITTranslucentModvolShaderSource[] = R"(
#include "oit_header.hlsl"

struct MVPixel 
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
};

// Must match ModifierVolumeMode enum values
#define MV_XOR		 0
#define MV_OR		 1
#define MV_INCLUSION 2
#define MV_EXCLUSION 3

void main(in MVPixel inpix)
{
	uint2 coords = uint2(inpix.pos.xy);
	
	uint idx = abufferPointers[coords];
	int list_len = 0;
	while (idx != EOL && list_len < MAX_PIXELS_PER_FRAGMENT)
	{
		const Pixel pixel = Pixels[idx];
		const PolyParam pp = tr_poly_params[getPolyNumber(pixel)];
		if (getShadowEnable(pp))
		{
			uint prev_val;
#if MV_MODE == MV_XOR
			if (inpix.uv.w >= pixel.depth)
				InterlockedXor(Pixels[idx].seq_num, SHADOW_STENCIL, prev_val);
#elif MV_MODE == MV_OR
			if (inpix.uv.w >= pixel.depth)
				InterlockedOr(Pixels[idx].seq_num, SHADOW_STENCIL, prev_val);
#elif MV_MODE == MV_INCLUSION
			InterlockedAnd(Pixels[idx].seq_num, ~(SHADOW_STENCIL), prev_val);
			if ((prev_val & (SHADOW_STENCIL|SHADOW_ACC)) == SHADOW_STENCIL)
				InterlockedOr(Pixels[idx].seq_num, SHADOW_ACC, prev_val);
#elif MV_MODE == MV_EXCLUSION
			InterlockedAnd(Pixels[idx].seq_num, ~(SHADOW_STENCIL|SHADOW_ACC), prev_val);
			if ((prev_val & (SHADOW_STENCIL|SHADOW_ACC)) == SHADOW_ACC)
				InterlockedOr(Pixels[idx].seq_num, SHADOW_ACC, prev_val);
#endif
		}
		idx = pixel.next;
		list_len++;
	}
}
)";

struct IncludeManager : public ID3DInclude
{
	HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) override
	{
		if (!strcmp(pFileName, "oit_header.hlsl"))
		{
			*ppData = OITShaderHeader;
			*pBytes = (UINT)strlen(OITShaderHeader);
			return S_OK;
		}
		return E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE Close(LPCVOID pData) override {
		return S_OK;
	}
};

const char * const MacroValues[] { "0", "1", "2", "3" };

static D3D_SHADER_MACRO VertexMacros[]
{
	{ "pp_Gouraud", "1" },
	{ "POSITION_ONLY", "0" },
	{ "pp_TwoVolumes", "0" },
	{ "LIGHT_ON", "1" },
	{ nullptr, nullptr }
};

enum PixelMacroEnum {
	MacroGouraud,
	MacroTexture,
	MacroUseAlpha,
	MacroIgnoreTexA,
	MacroShadInstr,
	MacroOffset,
	MacroFogCtrl,
	MacroBumpMap,
	MacroFogClamping,
	MacroPalette,
	MacroAlphaTest,
	MacroClipInside,
	MacroNearestWrapFix,
	MacroTwoVolumes,
	MacroPass
};

static D3D_SHADER_MACRO PixelMacros[]
{
	{ "pp_Gouraud", "1" },
	{ "pp_Texture", "0" },
	{ "pp_UseAlpha", "0" },
	{ "pp_IgnoreTexA", "0" },
	{ "pp_ShadInstr", "0" },
	{ "pp_Offset", "0" },
	{ "pp_FogCtrl", "0" },
	{ "pp_BumpMap", "0" },
	{ "FogClamping", "0" },
	{ "pp_Palette", "0" },
	{ "cp_AlphaTest", "0" },
	{ "pp_ClipInside", "0" },
	{ "NearestWrapFix", "0" },
	{ "pp_TwoVolumes", "0" },
	{ "PASS", "0" },
	{ nullptr, nullptr }
};

const ComPtr<ID3D11PixelShader>& DX11OITShaders::getShader(bool pp_Texture, bool pp_UseAlpha, bool pp_IgnoreTexA, u32 pp_ShadInstr,
		bool pp_Offset, u32 pp_FogCtrl, bool pp_BumpMap, bool fog_clamping,
		bool palette, bool gouraud, bool alphaTest, bool clipInside, bool nearestWrapFix, bool twoVolumes, Pass pass)
{
	const u32 hash = (int)pp_Texture
			| (pp_UseAlpha << 1)
			| (pp_IgnoreTexA << 2)
			| (pp_ShadInstr << 3)
			| (pp_Offset << 5)
			| (pp_FogCtrl << 6)
			| (pp_BumpMap << 8)
			| (fog_clamping << 9)
			| (palette << 10)
			| (gouraud << 11)
			| (alphaTest << 12)
			| (clipInside << 13)
			| (nearestWrapFix << 14)
			| (twoVolumes << 15)
			| (pass << 16);
	auto& shader = shaders[hash];
	if (shader == nullptr)
	{
		verify(pp_ShadInstr < ARRAY_SIZE(MacroValues));
		verify(pp_FogCtrl < ARRAY_SIZE(MacroValues));
		verify(pass < (int)ARRAY_SIZE(MacroValues));
		PixelMacros[MacroGouraud].Definition = MacroValues[gouraud];
		PixelMacros[MacroTexture].Definition = MacroValues[pp_Texture];
		PixelMacros[MacroUseAlpha].Definition = MacroValues[pp_UseAlpha];
		PixelMacros[MacroIgnoreTexA].Definition = MacroValues[pp_IgnoreTexA];
		PixelMacros[MacroShadInstr].Definition = MacroValues[pp_ShadInstr];
		PixelMacros[MacroOffset].Definition = MacroValues[pp_Offset];
		PixelMacros[MacroFogCtrl].Definition = MacroValues[pp_FogCtrl];
		PixelMacros[MacroBumpMap].Definition = MacroValues[pp_BumpMap];
		PixelMacros[MacroFogClamping].Definition = MacroValues[fog_clamping];
		PixelMacros[MacroPalette].Definition = MacroValues[palette];
		PixelMacros[MacroAlphaTest].Definition = MacroValues[alphaTest];
		PixelMacros[MacroClipInside].Definition = MacroValues[clipInside];
		PixelMacros[MacroNearestWrapFix].Definition = MacroValues[nearestWrapFix];
		PixelMacros[MacroTwoVolumes].Definition = MacroValues[twoVolumes];
		PixelMacros[MacroPass].Definition = MacroValues[pass];

		shader = compilePS(PixelShader, "main", PixelMacros);
		verify(shader != nullptr);
	}
	return shader;
}

const ComPtr<ID3D11VertexShader>& DX11OITShaders::getVertexShader(bool gouraud, bool naomi2, bool positionOnly, bool lightOn, bool twoVolumes)
{
	const u32 hash = (int)gouraud
			| ((int)naomi2 << 1)
			| ((int)positionOnly << 2)
			| ((int)lightOn << 3)
			| ((int)twoVolumes << 4);
	auto& shader = vertexShaders[hash];
	if (shader == nullptr)
	{
		VertexMacros[0].Definition = MacroValues[gouraud];
		if (!naomi2)
		{
			shader = compileVS(VertexShader, "main", VertexMacros);
		}
		else
		{
			VertexMacros[1].Definition = MacroValues[positionOnly];
			VertexMacros[2].Definition = MacroValues[twoVolumes];
			VertexMacros[3].Definition = MacroValues[lightOn];
			std::string source(DX11N2VertexShader);
			if (!positionOnly && lightOn)
				source += std::string("\n") + DX11N2ColorShader;
			shader = compileVS(source.c_str(), "main", VertexMacros);
		}
	}

	return shader;
}

const ComPtr<ID3D11VertexShader>& DX11OITShaders::getMVVertexShader(bool naomi2)
{
	if (!modVolVertexShaders[naomi2])
	{
		if (!naomi2)
			modVolVertexShaders[0] = compileVS(ModVolVertexShader, "main", nullptr);
		else
		{
			VertexMacros[0].Definition = MacroValues[false];
			VertexMacros[1].Definition = MacroValues[true];
			VertexMacros[2].Definition = MacroValues[false];
			VertexMacros[3].Definition = MacroValues[false];
			modVolVertexShaders[1] = compileVS(DX11N2VertexShader, "main", VertexMacros);
		}
	}

	return modVolVertexShaders[naomi2];
}

const ComPtr<ID3D11PixelShader>& DX11OITShaders::getModVolShader()
{
	if (!modVolShader)
		modVolShader = compilePS(PixelShader, "modifierVolume", PixelMacros);

	return modVolShader;
}

const ComPtr<ID3D11PixelShader>& DX11OITShaders::getFinalShader()
{
	if (!finalShader)
	{
		D3D_SHADER_MACRO macros[]
		{
			{ "MAX_PIXELS_PER_FRAGMENT", MAX_PIXELS_PER_FRAGMENT },
			{ }
		};
		finalShader = compilePS(OITFinalShaderSource, "main", macros);
	}
	return finalShader;
}

const ComPtr<ID3D11VertexShader>& DX11OITShaders::getFinalVertexShader()
{
	if (!finalVertexShader)
		finalVertexShader = compileVS(OITFinalVertexShaderSource, "main", nullptr);

	return finalVertexShader;
}

const ComPtr<ID3D11PixelShader>& DX11OITShaders::getTrModVolShader(int type)
{
	ComPtr<ID3D11PixelShader>& shader = trModVolShaders[type];
	if (!shader)
	{
		D3D_SHADER_MACRO macros[]
		{
			{ "MV_MODE", MacroValues[type] },
			{ "MAX_PIXELS_PER_FRAGMENT", MAX_PIXELS_PER_FRAGMENT },
			{ }
		};
		shader = compilePS(OITTranslucentModvolShaderSource, "main", macros);
	}
	return shader;
}

ComPtr<ID3DBlob> DX11OITShaders::compileShader(const char* source, const char* function, const char* profile, const D3D_SHADER_MACRO *pDefines)
{
	// add the include file even if not included
	u64 hash = hashShader(source, function, profile, pDefines, OITShaderHeader);

	ComPtr<ID3DBlob> shaderBlob;
	if (!lookupShader(hash, shaderBlob))
	{
		ComPtr<ID3DBlob> errorBlob;
		IncludeManager includeManager;

		if (FAILED(this->D3DCompile(source, strlen(source), nullptr, pDefines, &includeManager, function, profile, 0, 0, &shaderBlob.get(), &errorBlob.get())))
			ERROR_LOG(RENDERER, "Shader compilation failed: %s", errorBlob->GetBufferPointer());
		else
			cacheShader(hash, shaderBlob);
	}

	return shaderBlob;
}

ComPtr<ID3D11VertexShader> DX11OITShaders::compileVS(const char* source, const char* function, const D3D_SHADER_MACRO *pDefines)
{
	ComPtr<ID3DBlob> blob = compileShader(source, function, "vs_5_0", pDefines);
	ComPtr<ID3D11VertexShader> shader;
	if (blob)
	{
		if (FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader.get())))
			ERROR_LOG(RENDERER, "Vertex shader creation failed");
	}

	return shader;
}

ComPtr<ID3D11PixelShader> DX11OITShaders::compilePS(const char* source, const char* function, const D3D_SHADER_MACRO *pDefines)
{
	ComPtr<ID3DBlob> blob = compileShader(source, function, "ps_5_0", pDefines);
	ComPtr<ID3D11PixelShader> shader;
	if (blob)
	{
		if (device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader.get()) != S_OK)
			ERROR_LOG(RENDERER, "Pixel shader creation failed");
	}

	return shader;
}

ComPtr<ID3DBlob> DX11OITShaders::getVertexShaderBlob()
{
	VertexMacros[0].Definition = MacroValues[true];
	// FIXME code dup
	VertexMacros[1].Definition = MacroValues[false];
	VertexMacros[2].Definition = MacroValues[true];
	std::string source(DX11N2VertexShader);
	source += std::string("\n") + DX11N2ColorShader;
	return compileShader(source.c_str(), "main", "vs_5_0", VertexMacros);
}

ComPtr<ID3DBlob> DX11OITShaders::getMVVertexShaderBlob()
{
	// FIXME code dup
	VertexMacros[0].Definition = MacroValues[false];
	VertexMacros[1].Definition = MacroValues[true];
	VertexMacros[2].Definition = MacroValues[false];
	return compileShader(DX11N2VertexShader, "main", "vs_5_0", VertexMacros);
}

ComPtr<ID3DBlob> DX11OITShaders::getFinalVertexShaderBlob()
{
	return compileShader(OITFinalVertexShaderSource, "main", "vs_5_0", nullptr);
}

void DX11OITShaders::init(const ComPtr<ID3D11Device>& device, pD3DCompile D3DCompile)
{
	this->device = device;
	this->D3DCompile = D3DCompile;
	enableCache(!theDX11Context.hasShaderCache());
	loadCache(CacheFile);
}
