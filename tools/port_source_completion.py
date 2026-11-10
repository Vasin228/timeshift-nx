"""Synchronize TimeShift's completion reads with ReNut's existing fence writes."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
base=ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu/backend'
for name,old,new in [
 ('render_thread.h','void QueueFenceCompletion(u32 completion_va, u32 fence, uint64_t after_callback);',
  '''void QueueFenceCompletion(u32 completion_va, u32 fence, uint64_t after_callback);
#if defined(TIMESHIFT_RECOMP)
// Shares the writer's mutex: source ownership must not race a be_u32 store.
u32 ReadFenceCompletion(u32 completion_va);
#endif'''),
 ('render_thread.cpp','void QueueCallbackMarker(uint64_t seq) {',
  '''#if defined(TIMESHIFT_RECOMP)
u32 ReadFenceCompletion(u32 completion_va) {
  std::lock_guard lock(g_mutex);
  const auto *word = GuestPtr<const be_u32>(completion_va);
  return word ? u32(*word) : 0;
}
#endif

void QueueCallbackMarker(uint64_t seq) {''')]:
    p=base/name;s=p.read_text()
    if new in s:continue
    assert s.count(old)==1,name
    p.write_text(s.replace(old,new),newline='\n')
print('TimeShift completion reads share the ReNut writer mutex; no hooks activated')
