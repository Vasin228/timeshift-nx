"""Expose an actual GPU-idle barrier and conditional video-device retirement."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1];up=ROOT/'upstream/sa2-nx'
revision=subprocess.run(['git','rev-parse','HEAD'],cwd=up,capture_output=True,text=True,check=True).stdout.strip()
assert revision=='32a81a83aafd83e25d3a3cccae34f567766402c6'
changes={
 'app/src/renut_engine/gpu/backend/video.h':('  static bool EnsureCommandListLocked();','''  static bool EnsureCommandListLocked();
#if defined(TIMESHIFT_RECOMP)
  // Device retirement only: submit current work and wait actual GPU fences.
  // Source-read completion/renderer Drain are insufficient for this barrier.
  static bool QuiesceGuestWorkLocked();
  static void ClearGuestDeviceIf(uint32_t expected);
#endif'''),
 'app/src/renut_engine/gpu/backend/video.cpp':('''plume::RenderDevice *Video::Device() {
  return State().device.get();
}''','''plume::RenderDevice *Video::Device() {
  return State().device.get();
}

#if defined(TIMESHIFT_RECOMP)
bool Video::QuiesceGuestWorkLocked() {
  auto &s = State();
  if (!s.host_ready || !s.queue || s.shutting_down.load(std::memory_order_acquire))
    return false;
  detail::WaitAllSubmitted(s);
  detail::SubmitAndWaitLocked(s);
  return !s.shutting_down.load(std::memory_order_acquire);
}
void Video::ClearGuestDeviceIf(uint32_t expected) {
  auto &s = State();
  std::lock_guard lock(s.mutex);
  if (s.guest_device.load(std::memory_order_acquire) == expected) {
    s.guest_device.store(0, std::memory_order_release);
    s.guest_ready = false;
  }
}
#endif''')}
for name,(old,new) in changes.items():
 original=subprocess.run(['git','show','HEAD:'+name],cwd=up,capture_output=True,text=True,check=True).stdout
 assert original.count(old)==1,name
 expected=original.replace(old,new);path=up/name;current=path.read_text()
 assert current in (original,expected),'refuse unrelated renderer edits: '+name
 if current!=expected:path.write_text(expected,newline='\n')
print('TimeShift GPU-idle barrier/conditional video retirement synchronized; no production activation')
