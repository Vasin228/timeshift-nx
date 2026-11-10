"""Preserve original TimeShift dirty state and defer both UP data streams."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
gpu=ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu'
def edit(name,replacements):
    p=gpu/name;original=s=p.read_text()
    for old,new in replacements:
        if new in s:continue
        assert s.count(old)==1,(name,old)
        s=s.replace(old,new)
    if s!=original:p.write_text(s,newline='\n')
edit('backend/queued.h',[
 ('void QueueDeferredDraw(u32 device_va, const DrawCall &call, const u8 *scratch, u32 bytes);',
  '''void QueueDeferredDraw(u32 device_va, const DrawCall &call, const u8 *scratch, u32 bytes,
                       const u8 *index_scratch = nullptr, u32 index_bytes = 0);''')])
edit('backend/queued.cpp',[
 ('  u32 bytes = 0;\n};\nstd::mutex g_deferred_mutex;',
  '''  u32 bytes = 0;
  const u8 *index_scratch = nullptr;
  u32 index_bytes = 0;
};
std::mutex g_deferred_mutex;'''),
 ('  ClearPending(device);\n  DrawCapture *capture = g_capture_pool.Take();',
  '''#if !defined(TIMESHIFT_RECOMP)
  ClearPending(device);
#endif  // Original TimeShift draw calls still consume these dirty tags.
  DrawCapture *capture = g_capture_pool.Take();'''),
 ('void QueueDeferredDraw(u32 device_va, const DrawCall &call, const u8 *scratch, u32 bytes) {',
  '''void QueueDeferredDraw(u32 device_va, const DrawCall &call, const u8 *scratch, u32 bytes,
                       const u8 *index_scratch, u32 index_bytes) {'''),
 ('g_deferred[device_va] = {delta, scratch, bytes};',
  'g_deferred[device_va] = {delta, scratch, bytes, index_scratch, index_bytes};'),
 ('  slot = {capture, scratch, bytes};','  slot = {capture, scratch, bytes, index_scratch, index_bytes};'),
 ('  deferred.capture->vertices.assign(deferred.scratch, deferred.scratch + deferred.bytes);\n  QueueCapture',
  '''  deferred.capture->vertices.assign(deferred.scratch, deferred.scratch + deferred.bytes);
  if (deferred.index_scratch)
    deferred.capture->indices.assign(deferred.index_scratch, deferred.index_scratch + deferred.index_bytes);
  QueueCapture''')])
print('TimeShift queue preserves original dirty tags; deferred vertices and indices copied together')
