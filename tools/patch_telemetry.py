import os

ROOT = r'D:\Anti\timeshift-renut'

# 1. Patch draw_hooks.cpp
path_dh = os.path.join(ROOT, 'adapter', 'draw_hooks.cpp')
with open(path_dh, 'r', encoding='utf-8') as f:
    text_dh = f.read()

text_dh = text_dh.replace('#include "device_registry.h"', '#include "device_registry.h"\n#include "render_drop_telemetry.h"')
old_putword = "void PutWord(uint8_t *base, uint32_t address, uint32_t value) {\n  if (!address) return;"
new_putword = """void PutWord(uint8_t *base, uint32_t address, uint32_t value) {
  if (!address) {
    timeshift::telemetry::g_drop_draw_null_address.fetch_add(1, std::memory_order_relaxed);
    return;
  }"""
if old_putword not in text_dh:
    # try CRLF
    old_putword = old_putword.replace('\n', '\r\n')
    new_putword = new_putword.replace('\n', '\r\n')
assert old_putword in text_dh, 'old_putword not found'
text_dh = text_dh.replace(old_putword, new_putword, 1)

with open(path_dh, 'w', encoding='utf-8') as f:
    f.write(text_dh)
print('Patched draw_hooks.cpp')

# 2. Patch draw_runtime_bridge.cpp
path_drb = os.path.join(ROOT, 'adapter', 'draw_runtime_bridge.cpp')
with open(path_drb, 'r', encoding='utf-8') as f:
    text_drb = f.read()

text_drb = text_drb.replace('#include "draw_runtime_bridge.h"', '#include "draw_runtime_bridge.h"\n#include "render_drop_telemetry.h"')

old_clear = "if(rect.x2<=rect.x1||rect.y2<=rect.y1)return;"
new_clear = """if(rect.x2<=rect.x1||rect.y2<=rect.y1) {
    timeshift::telemetry::g_drop_clear_empty_rect.fetch_add(1, std::memory_order_relaxed);
    return;
  }"""
assert old_clear in text_drb, 'old_clear not found'
text_drb = text_drb.replace(old_clear, new_clear, 1)

old_dest = "if(!destination)return;"
new_dest = """if(!destination) {
    timeshift::telemetry::g_drop_resolve_invalid.fetch_add(1, std::memory_order_relaxed);
    return;
  }"""
assert old_dest in text_drb, 'old_dest not found'
text_drb = text_drb.replace(old_dest, new_dest, 1)

# Second rect check in ResolveFrame
old_resolve_rect = "if(rect.x2<=rect.x1||rect.y2<=rect.y1)return;"
new_resolve_rect = """if(rect.x2<=rect.x1||rect.y2<=rect.y1) {
      timeshift::telemetry::g_drop_resolve_invalid.fetch_add(1, std::memory_order_relaxed);
      return;
    }"""
assert old_resolve_rect in text_drb, 'old_resolve_rect not found'
text_drb = text_drb.replace(old_resolve_rect, new_resolve_rect, 1)

with open(path_drb, 'w', encoding='utf-8') as f:
    f.write(text_drb)
print('Patched draw_runtime_bridge.cpp')

# 3. Patch shader_resource_hooks.cpp
path_srh = os.path.join(ROOT, 'adapter', 'shader_resource_hooks.cpp')
with open(path_srh, 'r', encoding='utf-8') as f:
    text_srh = f.read()

text_srh = text_srh.replace('#include "device_registry.h"', '#include "device_registry.h"\n#include "render_drop_telemetry.h"')

old_word = "uint32_t Word(uint8_t *base, uint32_t address) {\n  if (!base) return 0;"
new_word = """uint32_t Word(uint8_t *base, uint32_t address) {
  if (!base) {
    timeshift::telemetry::g_drop_texture_null_base.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }"""
if old_word not in text_srh:
    old_word = old_word.replace('\n', '\r\n')
    new_word = new_word.replace('\n', '\r\n')
assert old_word in text_srh, 'old_word not found'
text_srh = text_srh.replace(old_word, new_word, 1)

with open(path_srh, 'w', encoding='utf-8') as f:
    f.write(text_srh)
print('Patched shader_resource_hooks.cpp')

# 4. Patch presentation_hooks.cpp to call CheckAndReportDrops
path_ph = os.path.join(ROOT, 'adapter', 'presentation_hooks.cpp')
with open(path_ph, 'r', encoding='utf-8') as f:
    text_ph = f.read()

text_ph = text_ph.replace('#include "timeshift_device_layout.h"', '#include "timeshift_device_layout.h"\n#include "render_drop_telemetry.h"')

target_swap = "const uint64_t call_idx = g_swap_calls.fetch_add(1, std::memory_order_relaxed) + 1;"
addition_swap = """const uint64_t call_idx = g_swap_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (call_idx % 300 == 0) {
      timeshift::telemetry::CheckAndReportDrops(call_idx);
    }"""
assert target_swap in text_ph, 'target_swap not found'
text_ph = text_ph.replace(target_swap, addition_swap, 1)

with open(path_ph, 'w', encoding='utf-8') as f:
    f.write(text_ph)
print('Patched presentation_hooks.cpp')
