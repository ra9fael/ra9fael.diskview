// Bounded directory dispatch; only the coordinator owns and mutates the tree.
#pragma once
#include "scan.h"
#include <winioctl.h>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace diskview {
inline bool sameOrBelow(const std::wstring& path,const std::wstring& base) {
    if(base.empty())return false;
    if(path.size()<base.size()||::CompareStringOrdinal(path.c_str(),int(base.size()),base.c_str(),int(base.size()),TRUE)!=CSTR_EQUAL)return false;
    return path.size()==base.size()||base.back()==L'\\'||path[base.size()]==L'\\';
}
inline unsigned ordinaryWorkers(const std::wstring& path,const ScanOptions& options) {
    if(options.workers)return std::clamp(options.workers,1u,8u);
    if(options.media==ScanOptions::Media::Ssd)return 4;
    if(options.media==ScanOptions::Media::Hdd)return 1;
    if(options.media==ScanOptions::Media::Network||path.rfind(L"\\\\",0)==0)return 2;
    if(path.size()>2&&path[1]==L':') {
        const auto volume=L"\\\\.\\"+path.substr(0,2);
        const HANDLE h=::CreateFileW(volume.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(h!=INVALID_HANDLE_VALUE) {
            STORAGE_PROPERTY_QUERY q{};q.PropertyId=StorageDeviceSeekPenaltyProperty;q.QueryType=PropertyStandardQuery;
            DEVICE_SEEK_PENALTY_DESCRIPTOR d{};DWORD returned=0;
            const bool ok=::DeviceIoControl(h,IOCTL_STORAGE_QUERY_PROPERTY,&q,sizeof q,&d,sizeof d,&returned,nullptr)!=FALSE;
            ::CloseHandle(h);
            if(ok&&returned>=sizeof d)return d.IncursSeekPenalty?1:4;
        }
    }
    return 1; // Unknown devices start serially; users can opt in to more workers.
}
namespace parallel_detail {
struct Task { Node* target=nullptr;std::wstring path;unsigned depth=0; };
struct Batch { Task task;std::vector<Node> children;std::vector<std::size_t> dirs;DWORD error=0;unsigned links=0,excluded=0;std::uint64_t ms=0; };
struct Frontier { Node* node;std::wstring path;unsigned depth;std::vector<std::size_t> dirs;std::size_t next=0; };
inline void summarize(Node& n,bool sort,bool aggregateErrors=false) {
    if(!n.dir)return;
    n.logical=n.allocApprox=0;n.files=n.dirs=0;
    for(auto& c:n.children) {
        summarize(c,sort,aggregateErrors);n.logical+=c.logical;n.allocApprox+=c.allocApprox;
        if(aggregateErrors)n.skippedChildren+=c.skippedChildren;
        n.files+=c.dir?c.files:1;n.dirs+=c.dir?c.dirs+1:0;
    }
    if(sort)std::stable_sort(n.children.begin(),n.children.end(),[](const auto& a,const auto& b){return a.logical>b.logical;});
}
inline void rank(ScanResult& r) {
    detail::Ctx ctx;
    const auto walk=[&](const auto& self,const Node& n,const std::wstring& path)->void {
        for(const auto& c:n.children)if(c.dir)self(self,c,childPath(path,c.name));else detail::considerLarge(ctx,c.logical,path,c.name.c_str());
    };
    walk(walk,r.root,r.displayRoot);r.largest.clear();
    while(!ctx.top.empty()){r.largest.push_back(ctx.top.top());ctx.top.pop();}
    std::sort(r.largest.begin(),r.largest.end(),std::greater<>());
}
}
inline void scanPath(const std::wstring& path,const ProgressFn& progress,const std::atomic<bool>& cancel,
                     ScanResult* result,const SnapshotFn& snapshot={},const ScanOptions& options={}) {
    using namespace parallel_detail;
    *result=ScanResult{};const auto start=::GetTickCount64();
    result->displayRoot=normalizePath(path,nullptr);result->root.name=result->displayRoot;result->root.dir=true;
    if(result->displayRoot.empty()){result->error="路径为空 / the path is empty";return;}
    result->workers=ordinaryWorkers(result->displayRoot,options);
    wchar_t volume[MAX_PATH]{};DWORD sectors=0,bytes=0,free=0,total=0;
    if(::GetVolumePathNameW(result->displayRoot.c_str(),volume,MAX_PATH)&&::GetDiskFreeSpaceW(volume,&sectors,&bytes,&free,&total))result->clusterBytes=std::uint64_t(sectors)*bytes;
    struct Shared {
        std::mutex mutex;std::condition_variable cv;std::deque<Task> jobs;std::deque<Batch> results;
        std::atomic<bool> stop{false};std::atomic<std::uint64_t> files{0},dirs{0},bytes{0};
        std::exception_ptr failure;
    } shared;
    const std::size_t capacity=result->workers*2;
    std::vector<std::thread> workers;
    // Runs before workers are destroyed even if a callback or allocation throws.
    struct Join { Shared& s;std::vector<std::thread>& threads;~Join(){s.stop=true;s.cv.notify_all();for(auto& t:threads)if(t.joinable())t.join();} } join{shared,workers};
    const auto stopped=[&]{return cancel.load()||shared.stop.load();};
    const auto waitPause=[&]{while(options.paused&&options.paused->load()&&!stopped())std::this_thread::sleep_for(std::chrono::milliseconds(15));return !stopped();};
    const auto excluded=[&](const std::wstring& p){return std::any_of(options.excludedPaths.begin(),options.excludedPaths.end(),[&](const auto& x){return !x.empty()&&sameOrBelow(p,x);});};
    const auto worker=[&] {
        try {
            for(;;) {
                Task task;
                {std::unique_lock lock(shared.mutex);shared.cv.wait_for(lock,std::chrono::milliseconds(20),[&]{return stopped()||!shared.jobs.empty();});
                 if(stopped())return;if(shared.jobs.empty())continue;task=std::move(shared.jobs.front());shared.jobs.pop_front();}
                if(!waitPause())return;
                Batch batch;batch.task=std::move(task);const auto tick=::GetTickCount64();
                if(excluded(batch.task.path))batch.excluded=1;
                else if(batch.task.depth>detail::kMaxDepth)batch.error=ERROR_CANT_ACCESS_FILE;
                else {
                    std::wstring prefixed;normalizePath(batch.task.path,&prefixed);
                    WIN32_FIND_DATAW fd{};const HANDLE h=::FindFirstFileExW(childPath(prefixed,L"*").c_str(),FindExInfoBasic,&fd,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
                    if(h==INVALID_HANDLE_VALUE)batch.error=::GetLastError();
                    else {
                        detail::FindHandle handle(h);
                        for(;;) {
                            if(!waitPause())break;
                            if(wcscmp(fd.cFileName,L".")&&wcscmp(fd.cFileName,L"..")) {
                                const auto full=childPath(batch.task.path,fd.cFileName);
                                if(excluded(full))++batch.excluded;
                                else {
                                    Node n;n.name=fd.cFileName;n.dir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
                                    if(n.dir){++shared.dirs;if(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)++batch.links;else batch.dirs.push_back(batch.children.size());}
                                    else {n.logical=(std::uint64_t(fd.nFileSizeHigh)<<32)|fd.nFileSizeLow;n.allocApprox=detail::clusterRound(n.logical,result->clusterBytes);++shared.files;shared.bytes+=n.logical;}
                                    batch.children.push_back(std::move(n));
                                }
                            }
                            if(!::FindNextFileW(h,&fd)){const auto e=::GetLastError();if(e!=ERROR_NO_MORE_FILES)batch.error=e;break;}
                        }
                    }
                }
                batch.ms=::GetTickCount64()-tick;
                {std::unique_lock lock(shared.mutex);shared.cv.wait(lock,[&]{return shared.stop||shared.results.size()<capacity;});
                 if(shared.stop)return;shared.results.push_back(std::move(batch));shared.cv.notify_all();}
            }
        } catch(...) {std::lock_guard lock(shared.mutex);shared.failure=std::current_exception();shared.stop=true;shared.cv.notify_all();}
    };
    shared.jobs.push_back({&result->root,result->displayRoot,0});
    for(unsigned i=0;i<result->workers;++i)workers.emplace_back(worker);
    std::size_t outstanding=1;std::vector<Frontier> frontier;
    auto nextPreview=start+options.previewIntervalMs;auto nextProgress=start;std::wstring current=result->displayRoot;
    bool callbackCancelled=false;
    while(outstanding||!frontier.empty()) {
        if(cancel||shared.stop||callbackCancelled)break;
        if(!(options.paused&&*options.paused)) {
            std::lock_guard lock(shared.mutex);
            while(!frontier.empty()&&shared.jobs.size()<capacity&&outstanding<capacity+result->workers) {
                auto& f=frontier.back();if(f.next>=f.dirs.size()){frontier.pop_back();continue;}
                auto& n=f.node->children[f.dirs[f.next++]];
                shared.jobs.push_back({&n,childPath(f.path,n.name),f.depth+1});++outstanding;
            }
            shared.cv.notify_all();
        }
        std::deque<Batch> ready;
        {std::unique_lock lock(shared.mutex);if(shared.results.empty())shared.cv.wait_for(lock,std::chrono::milliseconds(20));ready.swap(shared.results);shared.cv.notify_all();}
        const auto mergeStart=::GetTickCount64();
        for(auto& b:ready) {
            --outstanding;current=b.task.path;result->workerMs+=b.ms;result->skippedLinks+=b.links;result->excludedItems+=b.excluded;
            auto& n=*b.task.target;n.children=std::move(b.children);
            if(b.error){++n.skippedChildren;++result->skippedDirs;if(result->skippedList.size()<detail::kMaxSkippedList)result->skippedList.push_back({b.task.path,b.error});if(!b.task.depth)result->error="无法读取扫描根 / Cannot read scan root. Windows error "+std::to_string(b.error);}
            if(!b.dirs.empty())frontier.push_back({&n,std::move(b.task.path),b.task.depth,std::move(b.dirs)});
        }
        result->mergeMs+=::GetTickCount64()-mergeStart;
        auto now=::GetTickCount64();
        if(progress&&now>=nextProgress) {
            ScanProgress pg;pg.files=shared.files;pg.dirs=shared.dirs;pg.bytes=shared.bytes;pg.current=current;
            callbackCancelled=!progress(pg);nextProgress=now+100;
        }
        now=::GetTickCount64();
        if(snapshot&&now>=nextPreview&&!ready.empty()&&!stopped()&&!callbackCancelled) {
            const auto tick=::GetTickCount64();auto copy=std::make_shared<ScanResult>(*result);
            summarize(copy->root,true,true);copy->files=copy->root.files;copy->dirs=copy->root.dirs;rank(*copy);copy->elapsedMs=now-start;
            snapshot(std::move(copy));const auto cost=::GetTickCount64()-tick;result->snapshotMs+=cost;
            nextPreview=::GetTickCount64()+std::max<std::uint64_t>(options.previewIntervalMs,cost*10);
        }
    }
    // Stop/join before any final tree sorting can invalidate worker task pointers.
    shared.stop=true;shared.cv.notify_all();for(auto& t:workers)if(t.joinable())t.join();
    if(shared.failure)std::rethrow_exception(shared.failure);
    // Completed batches that raced cancellation are still safe to retain.
    for(auto& b:shared.results){b.task.target->children=std::move(b.children);result->workerMs+=b.ms;result->skippedLinks+=b.links;result->excludedItems+=b.excluded;
        if(b.error){++b.task.target->skippedChildren;++result->skippedDirs;if(result->skippedList.size()<detail::kMaxSkippedList)result->skippedList.push_back({b.task.path,b.error});}}
    result->cancelled=cancel||callbackCancelled;
    const auto finish=::GetTickCount64();summarize(result->root,true,true);rank(*result);result->mergeMs+=::GetTickCount64()-finish;
    result->files=result->root.files;result->dirs=result->root.dirs;result->elapsedMs=::GetTickCount64()-start;
}
}
