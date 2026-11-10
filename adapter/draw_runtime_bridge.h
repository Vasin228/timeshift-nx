#pragma once
#include <cstdint>
#include "resolve_parameters.h"
namespace timeshift::renderer {
bool DrawHooksEnabled();
void FlushPendingDraw(uint32_t device);
void DrawIndexed(uint32_t device,uint32_t primitive,int32_t base,uint32_t start,uint32_t count);
void DrawVerticesUP(uint32_t device,uint32_t primitive,uint32_t count,uint32_t data,uint32_t stride);
void DrawIndexedUP(uint32_t device,uint32_t primitive,uint32_t minimum,uint32_t vertices,uint32_t count,
                   uint32_t indices,uint32_t format,uint32_t data,uint32_t stride);
void BeginVerticesCompleted(uint32_t device,uint32_t primitive,uint32_t count,uint32_t stride,uint32_t data);
void BeginIndexedCompleted(uint32_t device,uint32_t primitive,int32_t base,uint32_t vertices,uint32_t count,
                           uint32_t format,uint32_t stride,uint32_t data,uint32_t indices);
void ClearFrame(uint32_t device,uint32_t flags,uint32_t rectangle,uint32_t color,float depth,uint32_t stencil);
void ResolveFrame(uint32_t device,uint32_t flags,uint32_t rectangle,uint32_t destination,uint32_t point,
                  uint32_t level,uint32_t slice,uint32_t color,float depth,uint32_t stencil,uint32_t parameters);
void DrawPrimitive(uint32_t device,uint32_t primitive,uint32_t start,uint32_t count);
}
