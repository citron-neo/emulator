# SPDX-License-Identifier: GPL-2.0-or-later
"""Production CPU CopyBlock/WalkBlock with fixture translations, no concurrent remaps."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
p.add_argument('--revision', help='Extract an older revision for the failing control')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/cpu-copy-block-tests'
out.mkdir(parents=True, exist_ok=True)
source = (subprocess.check_output(['git', 'show', a.revision + ':src/core/memory.cpp'],
                                  cwd=root, text=True) if a.revision else
          (root / 'src/core/memory.cpp').read_text())


def method(marker):
    start = source.index(marker)
    return source[start:source.index('\n    }', start) + 6] + '\n'


head = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>
using u8=uint8_t; using u64=uint64_t;
bool fail_allocation=false;
void* operator new(std::size_t n) {
    if(fail_allocation) { fail_allocation=false; throw std::bad_alloc(); }
    if(auto p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
u64 GetInteger(u64 x) { return x; }
#define LOG_ERROR(...) ((void)0)
constexpr unsigned CITRON_PAGEBITS=2;
constexpr u64 CITRON_PAGESIZE=4, CITRON_PAGEMASK=3;
namespace Common {
using ProcessAddress=u64;
enum class PageType {Unmapped,Memory,DebugMemory,RasterizerCachedMemory};
struct PageTable {
    struct Pointer {
        uintptr_t pointer{}; PageType type=PageType::Unmapped;
        auto PointerType() const { return std::pair{pointer,type}; }
    };
    struct Entry { Pointer pointer; size_t physical{}; };
    std::array<Entry,16> entries;
    unsigned GetAddressSpaceBits() const { return 6; }
};
}
bool AddressSpaceContains(const Common::PageTable& t,u64 a,size_t n) {
    const u64 max=1ULL<<t.GetAddressSpaceBits(); return a+n>=a && a+n<=max;
}
struct Fixture {
    std::array<u8,64> backing;
    Common::PageTable table; Common::PageTable* current_page_table=&table;
    int writes=0,reads=0,fail_write_at=0;
    Fixture() { for(size_t i=0;i<backing.size();++i) backing[i]=u8(i+1); }
    void map(size_t v,size_t physical,bool cached=true) {
        table.entries[v]={{{},cached?Common::PageType::RasterizerCachedMemory:Common::PageType::Memory},physical};
        table.entries[v].pointer.pointer=reinterpret_cast<uintptr_t>(backing.data()+physical)-(v<<2);
    }
    u8* GetPointerFromRasterizerCachedMemory(u64 v) { return backing.data()+table.entries[v>>2].physical+(v&3); }
    u8* GetPointerFromDebugMemory(u64 v) { return GetPointerFromRasterizerCachedMemory(v); }
    void HandleRasterizerDownload(u64,size_t) { ++reads; }
    void HandleRasterizerWrite(u64,size_t) {
        if(++writes==fail_write_at) throw std::runtime_error("injected preservation failure");
    }
'''
body = ''.join(method(s) for s in [
    '    bool WalkBlock(',
    '    template <bool UNSAFE>\n    bool ReadBlockImpl(',
    '    template <bool UNSAFE>\n    bool WriteBlockImpl(',
    '    bool ZeroBlock(',
    '    bool CopyBlock('])
tail = r'''
};
void require(bool x) { if(!x) throw std::runtime_error("copy assertion failed"); }
void overlap(bool reverse,bool cached) {
    Fixture f; for(size_t i=0;i<8;++i) f.map(i,i*4,cached);
    // Disjoint VAs, physically overlapping source/destination across page chunks.
    f.map(8,reverse?4:0,cached); f.map(9,reverse?8:4,cached); f.map(10,reverse?12:8,cached);
    f.map(12,reverse?0:4,cached); f.map(13,reverse?4:8,cached); f.map(14,reverse?8:12,cached);
    auto expected=f.backing;
    std::memmove(expected.data()+(reverse?0:4),expected.data()+(reverse?4:0),12);
    require(f.CopyBlock(48,32,12)); require(f.backing==expected);
}
int main() {
 try {
    for(bool reverse:{false,true}) for(bool cached:{false,true}) overlap(reverse,cached);
    Fixture distinct; distinct.map(1,0); distinct.map(2,4); distinct.map(4,16); distinct.map(5,20);
    auto expected=distinct.backing; std::memcpy(expected.data()+16,expected.data(),8);
    require(distinct.CopyBlock(16,4,8)); require(distinct.backing==expected);
    // A source hole must not zero/write destination; snapshot must finish first.
    Fixture hole; hole.map(1,0); hole.map(4,16); hole.map(5,20);
    auto original=hole.backing; require(!hole.CopyBlock(16,4,8)); require(hole.backing==original);
    // Destination preflight rejects a hole before any destination notification.
    Fixture dest; dest.map(1,0); dest.map(2,4); dest.map(4,16);
    original=dest.backing; require(!dest.CopyBlock(16,4,8)); require(dest.backing==original && dest.writes==0);
    // Preservation can still throw after a completed prefix. Do not return success.
    Fixture failure; failure.map(1,0); failure.map(2,4); failure.map(4,16); failure.map(5,20);
    failure.fail_write_at=2; original=failure.backing;
    bool threw=false; try { (void)failure.CopyBlock(16,4,8); } catch(const std::runtime_error&) { threw=true; }
    require(threw && failure.backing[16]==original[0] && failure.backing[20]==original[20]);
    Fixture oom;oom.map(1,0);oom.map(4,16);original=oom.backing;
    fail_allocation=true;require(!oom.CopyBlock(16,4,4));require(oom.backing==original && oom.writes==0);
    // Repeated destination aliases use logical write order from an owned source.
    Fixture repeated;repeated.map(1,0);repeated.map(2,4);repeated.map(4,4);repeated.map(5,4);
    original=repeated.backing;require(repeated.CopyBlock(16,4,8));
    require(std::equal(repeated.backing.begin()+4,repeated.backing.begin()+8,original.begin()+4));
    // Unaligned ranges crossing both source and destination page boundaries.
    Fixture partial;for(size_t i=0;i<8;++i) partial.map(i,i*4);
    original=partial.backing;std::memmove(original.data()+3,original.data()+1,9);
    require(partial.CopyBlock(3,1,9));require(partial.backing==original);
    Fixture empty; require(empty.CopyBlock(0,0,0));
    require(!empty.CopyBlock(60,0,8));
    std::cout<<"PASS CPU CopyBlock: source snapshot, both physical overlap directions, cached/ordinary, holes, prefix failure, bounds\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
}
'''
cpp = out / 'copy.cpp'
cpp.write_text(head + body + tail)
exe = out / ('copy.exe' if os.name == 'nt' else 'copy')
subprocess.run([a.cxx, '-std=c++23', '-O0', '-I', str(root / 'src'), str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True, timeout=20)
