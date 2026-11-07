// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include <thread>
#include "video_core/renderer_vulkan/vk_private_producer_guard.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <csignal>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/wait.h>
namespace Vulkan {
class PrivateProducerTestAccess {
public:
    static auto Make(){auto s=PrivateProducerGuard::Create(64);assert(s);s->count=2;s->identities={reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),nullptr};return s;}
    static auto Acquire(const auto& s){return PrivateProducerGuard::Acquire(s);}
    static bool Ready(const auto& g){return g->Ready();}
    static bool Finish(const auto& g){return g->CompleteChecked();} // Injected unit certificate, NOT Vulkan proof.
    static void Unknown(const auto& g){g->Quarantine();}
    static void Epoch(const auto& s){s->epoch->generation.fetch_add(2);}
    static void* Page(const auto& s){return s->page;}
};
}
void Probe(void* page,bool writable) {
    const auto child=fork();assert(child>=0);
    if(!child){
        const rlimit no_core{0,0};(void)setrlimit(RLIMIT_CORE,&no_core);
        (void)signal(SIGSEGV,SIG_DFL);
        *static_cast<volatile unsigned char*>(page)=37; // Actual native instruction, no provider callback.
        _exit(0);
    }
    int status{};assert(waitpid(child,&status,0)==child);
    assert(writable?(WIFEXITED(status)&&WEXITSTATUS(status)==0):(WIFSIGNALED(status)&&WTERMSIG(status)==SIGSEGV));
    unsigned char byte=99;ssize_t written{};
    std::thread cpu([&]{iovec local{&byte,1},remote{page,1};written=process_vm_writev(getpid(),&local,1,&remote,1,0);});cpu.join();
    assert(writable?written==1:written==-1); // Live same-process competing thread, kernel enforces page permission.
}
int main(){
    using A=Vulkan::PrivateProducerTestAccess;
    {auto s=A::Make();Probe(A::Page(s),true);auto g=A::Acquire(s);assert(g&&A::Ready(g));
     Probe(A::Page(s),false);assert(!A::Acquire(s));g.reset();Probe(A::Page(s),false);assert(!A::Acquire(s));}
    {auto s=A::Make();auto a=A::Acquire(s);assert(a);assert(A::Finish(a));Probe(A::Page(s),true);
     auto b=A::Acquire(s);assert(b);assert(A::Finish(a));Probe(A::Page(s),false);assert(A::Finish(b));Probe(A::Page(s),true);}
    {auto s=A::Make();auto g=A::Acquire(s);A::Unknown(g);assert(!A::Finish(g));Probe(A::Page(s),false);assert(!A::Acquire(s));}
    {auto s=A::Make();auto g=A::Acquire(s);A::Epoch(s);assert(!A::Ready(g));assert(!A::Finish(g));Probe(A::Page(s),false);}
    std::cout<<"PASS Linux real native-store fault and concurrent kernel write probes; completion certificates injected in this kernel-only unit\n";
}
#else
int main(){std::cout<<"SKIP unsupported OS: no private producer closure proof\n";return 77;}
#endif
