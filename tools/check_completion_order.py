"""Execute ReNut's actual completion bodies with a controlled FIFO and SDK words."""
from pathlib import Path
import json,subprocess
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu/backend/render_thread.cpp').read_text()
out=ROOT/'analysis/completion-fixture';out.mkdir(exist_ok=True)
def function(signature):
    start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
names=['void WriteFence(u32 completion_va','void QueueFenceCompletion(u32 completion_va',
       'void QueueCallbackMarker(uint64_t seq)','uint64_t CallbacksReached()',
       'void CallbacksRun(uint64_t seq)']
bodies='\n'.join(function(n) for n in names)
(out/'bodies.inc').write_text(bodies+'\n')
source=out/'fixture.cpp'
source.write_text('''#include <rex/types.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <cstdio>
std::mutex g_mutex;std::condition_variable g_done_cv;
uint64_t g_callbacks_run=0,g_callbacks_reached=0,g_fences_done=0;
unsigned g_done_waiters=0;bool DoneNotifyAll(){return true;}
struct PendingFence{u32 completion_va,fence;uint64_t after_callback;};
std::vector<PendingFence>g_pending_fences;
be_u32 words[2];
template<class T>T *GuestPtr(u32 at){return at==0x100||at==0x104?reinterpret_cast<T*>(&words[(at-0x100)/4]):nullptr;}
std::deque<std::function<void()>>fifo;
void QueueUnlocked(std::function<void()>work){fifo.push_back(std::move(work));}
#include "bodies.inc"
void Check(bool v,const char*s){if(!v)throw std::runtime_error(s);}
void Next(){auto work=std::move(fifo.front());fifo.pop_front();work();}
int main(){try{
  words[0]=0;words[1]=0;
  std::vector<u8>source(32,0xA5),owned;
  QueueUnlocked([&]{owned=source;});
  QueueCallbackMarker(1);QueueFenceCompletion(0x100,1,1);
  QueueCallbackMarker(2);QueueFenceCompletion(0x104,7,2);QueueFenceCompletion(0x100,2,2);
  Check(u32(words[0])==0&&owned.empty(),"unread work completed early");
  Next();Next();Next();
  Check(CallbacksReached()==1&&u32(words[0])==0&&g_pending_fences.size()==1,"fence preceded its callback acknowledgement");
  std::fill(source.begin(),source.end(),0x5A);Check(owned[0]==0xA5,"source bytes were not owned at read completion");
  while(!fifo.empty())Next();
  Check(CallbacksReached()==2&&u32(words[0])==0&&u32(words[1])==0,"queued callback marker treated as callback execution");
  CallbacksRun(1);
  Check(u32(words[0])==1&&u32(words[1])==0&&g_pending_fences.size()==2,"first callback did not release only its prefix");
  CallbacksRun(2);
  Check(u32(words[0])==2&&u32(words[1])==7&&g_pending_fences.empty(),"device completion addresses/order lost");
  Check(g_fences_done==5,"completion progress counter");
  auto bytes=reinterpret_cast<const u8*>(&words[0]);Check(bytes[0]==0&&bytes[3]==2,"guest completion endian store");
  CallbacksRun(1);Check(g_callbacks_run==2&&u32(words[0])==2,"stale acknowledgement regressed progress");
  QueueFenceCompletion(0x100,3,2);Next();Check(u32(words[0])==3,"already acknowledged callback stalled fence");
  std::puts("PASS: original completion bodies wait for host reads AND guest callbacks, preserve FIFO/device addresses and SDK endian stores");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\\n",e.what());return 1;}}
''')
exe=out/'completion_fixture'
subprocess.run(['clang++','-std=c++23','-O1','-I'+str(ROOT/'private/sdk/include'),str(source),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True);assert result.returncode==0,(result.stdout,result.stderr)
print(result.stdout.strip())
(out/'report.json').write_text(json.dumps(dict(success=True,original_completion_bodies=5,
 sdk_endian_words=True,host_read_and_callback_order=True,
 limitation='Controlled single-thread FIFO and callback acknowledgements; no actual queue thread, guest callback, allocation owner or GPU fence'),indent=2)+'\n')
