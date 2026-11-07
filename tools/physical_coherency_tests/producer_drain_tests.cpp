// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
#include "common/producer_drain.h"
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

using Gate=Common::ProducerDrain;
using R=Gate::Result;
int main(){
    {Gate g;auto active=g.TryEnter();assert(active);auto close=g.TryClose(8);assert(close);
     assert(!g.TryEnter());assert(!g.TryClose());assert(close->Poll()==R::Pending);
     assert(!close->ResumeNoEffects());active.reset();assert(close->Poll()==R::DrainedSubset);
     assert(close->ResumeNoEffects());assert(g.TryEnter());assert(!close->ResumeNoEffects());}
    {Gate g;std::promise<void> entered,finish;auto wait=finish.get_future();unsigned bytes=0;
     std::thread cpu([&]{auto active=g.TryEnter();assert(active);entered.set_value();wait.wait();bytes=77;});
     entered.get_future().wait();auto close=g.TryClose(8);assert(close);assert(close->Poll()==R::Pending);
     assert(!close->ResumeNoEffects());finish.set_value();cpu.join();
     assert(close->Poll()==R::DrainedSubset);assert(bytes==77);assert(close->ResumeNoEffects());}
    {Gate g;auto active=g.TryEnter();assert(active);auto close=g.TryClose(2);assert(close);
     // Caller may itself own the entry: bounded polling must not wait on itself.
     assert(close->Poll()==R::Pending);assert(close->Poll()==R::Unknown);active.reset();
     assert(close->Poll()==R::Unknown);assert(!close->ResumeNoEffects());assert(!g.TryEnter());}
    {Gate g;{auto close=g.TryClose();assert(close);}assert(!g.TryEnter());assert(!g.TryClose());}
    {Gate g;auto close=g.TryClose();assert(close);assert(close->Poll()==R::DrainedSubset);
     close->Quarantine();assert(close->Poll()==R::Unknown);assert(!close->ResumeNoEffects());assert(!g.TryEnter());}
    {auto g=std::make_unique<Gate>();auto entry=g->TryEnter();auto close=g->TryClose();assert(entry&&close);
     g.reset();entry.reset();assert(close->Poll()==R::Unknown);assert(!close->ResumeNoEffects());}
    {Gate g;auto first=g.TryClose();assert(first&&first->ResumeNoEffects());auto second=g.TryClose();assert(second);
     assert(!first->ResumeNoEffects());assert(!g.TryEnter());assert(second->ResumeNoEffects());}
    assert(Gate::GuestBackingAdmission()==R::Unknown); // No owner/alias declaration is a certificate.
    {Gate g;auto original=g.TryClose();assert(original);
     auto moved=std::move(*original);
     assert(original->Poll()==R::Unknown);assert(!original->ResumeNoEffects());
     original->Quarantine();assert(moved.Poll()==R::DrainedSubset);
     assert(moved.ResumeNoEffects());auto next=g.TryClose();assert(next);
     original->Quarantine();assert(!original->ResumeNoEffects());
     assert(!g.TryEnter());assert(next->ResumeNoEffects());}
#if defined(__linux__)
    { // Actual missed-alias counterexample, NOT a successful exclusion experiment.
     const int fd=memfd_create("drain-alias-negative",MFD_CLOEXEC);assert(fd>=0);assert(ftruncate(fd,4096)==0);
     void* a=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
     void* b=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);assert(a!=MAP_FAILED&&b!=MAP_FAILED);
     Gate g;auto close=g.TryClose();assert(close);assert(close->Poll()==R::DrainedSubset);
     assert(mprotect(a,4096,PROT_READ)==0);
     std::thread omitted([&]{*static_cast<volatile unsigned char*>(b)=99;});omitted.join();
     assert(*static_cast<const unsigned char*>(a)==99); // A second writable VMA bypassed both.
     assert(Gate::GuestBackingAdmission()==R::Unknown);
     assert(munmap(a,4096)==0&&munmap(b,4096)==0);assert(::close(fd)==0);
     std::cout<<"PASS Linux omitted writable alias counterexample; full guest coverage Unknown\n";}
#endif
    std::cout<<"PASS real managed writer close/drain: in-flight store, early resume, self-dependency budget, drop, retirement, stale release; no guest/GPU admission\n";
}
