// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "video_core/renderer_vulkan/vk_native_source_retainer.h"
using namespace Vulkan;
void Check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::_Exit(91);}}
struct Receipt{NativeSourceStatus state=NativeSourceStatus::Pending;};
int main(int argc,char** argv){
    const std::string mode=argc>1?argv[1]:"normal";
    using R=std::shared_ptr<Receipt>;std::vector<R> receipts;
    NativeSourceRetainer<R> queue{[&]{auto r=std::make_shared<Receipt>();receipts.push_back(r);return r;}};
    auto owner=std::make_shared<int>(1);std::weak_ptr<int> weak=owner;
    auto collect=[&]{queue.Collect([](const R& r){return r->state;});};
    {
        auto draw=queue.Begin();queue.Retain(owner);owner.reset();
        if(mode=="pending"){
            collect();Check(!weak.expired(),"pending source owner released");
        }else if(mode=="split"){
            auto first=queue.TakeSubmission();first->state=NativeSourceStatus::Complete;collect();
            Check(!weak.expired(),"active draw lost owner after internal submission");
            queue.BeforeRecord();Check(receipts.size()==2,"later recording did not renew owner batch");
        }else if(mode=="unknown"){
            auto r=queue.TakeSubmission();r->state=NativeSourceStatus::Unknown;collect();
            Check(!weak.expired(),"unknown submission released source");
        }else if(mode=="idle-active"){
            Check(!queue.RetireAfterIdle(),"active draw authorized idle retirement");
        }else if(mode=="nested"){
            {auto inner=queue.Begin();queue.Retain(std::make_shared<int>(2));}
            queue.TakeSubmission();queue.BeforeRecord();
        }else if(mode=="record-throw"){
            try{queue.BeforeRecord();throw 7;}catch(int){}
        }
    }
    collect();Check(!weak.expired(),"scope end released unconfirmed source");
    if(mode=="normal"){
        auto r=queue.TakeSubmission();r->state=NativeSourceStatus::Complete;collect();
        Check(weak.expired(),"completed source owner retained indefinitely");
    }else{
        Check(queue.RetireAfterIdle(),"closed draw refused checked-idle retirement");
        Check(weak.expired(),"idle retirement retained source");
    }
    std::cout<<"PASS native source protocol "<<mode<<'\n';
}
