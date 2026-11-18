#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Video filter modes. New filters can be added as embedded/external RetroArch
// .glsl presets are added to shaders/.
enum class VideoFilter
{
    None = 0,
    Bilinear,
    LcdGrid,
    Crt,
    Scanlines,
    Count
};

const char* videoFilterName(int filter);
int         videoFilterCount();

// Called once after the OpenGL context is created.
void videoFiltersInit();
void videoFiltersShutdown();

// Returns true if the given filter uses the post-processing FBO pipeline.
bool videoFilterUsesPipeline(int filter);

// Render srcTex (srcW x srcH) filtered to outW x outH and return the texture to
// display. If the filter does not use the pipeline, srcTex is returned as-is so
// the caller can draw it directly (texture filtering already set by the caller).
uint32_t videoFilterRender(uint32_t srcTex, int srcW, int srcH,
                           int outW, int outH, int filter);