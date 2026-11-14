// The NTFS MFT fast scan, run inside the plugin's own process.
//
// It used to be a short-lived elevated helper started with "runas": the GUI
// process popped its own UAC prompt and read the MFT through a pipe. That
// bypassed the host's permission model, which reserves elevation for Win顺
// itself (Elevation.h): a plugin that declares `elevated` and that the user
// allowed follows an elevated Win顺 through its elevated runner, so this
// process already holds an administrator token when it may read the MFT at
// all. A normal Win顺 start has no such token, and the fast scan is simply
// not available; the scan then reports why and walks the tree the ordinary
// way. No helper process, no pipe, no consent prompt of its own.
//
// The native (GDI) edition keeps its own runas helper in scan_helper.h; this
// is the QML edition's replacement, not shared with it.
#pragma once
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include "mft_scan.h"

namespace diskview::fastscan {

enum class Outcome { Complete, Cancelled, Fallback };

// Whether this process carries an administrator token. Only used to phrase
// the fallback reason: the MFT reader itself decides by opening the volume,
// so a token that is nominally elevated but lacks the rights still falls
// back with the reader's own error.
inline bool processElevated() {
    HANDLE token=nullptr;
    if(!::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    struct Guard {HANDLE token;~Guard(){if(token)::CloseHandle(token);}} guard{token};
    TOKEN_ELEVATION elevation{};
    DWORD size=0;
    return ::GetTokenInformation(token,TokenElevation,&elevation,sizeof elevation,&size)&&elevation.TokenIsElevated;
}

// One pass over the MFT, in this process. The batches arrive through
// mft::read's sink; the tree is assembled once at the end, with previews
// built in the background while the records keep coming in.
inline Outcome scanDirect(const std::wstring& path,const ProgressFn& progress,const SnapshotFn& snapshot,const std::atomic<bool>& cancel,ScanResult& result,std::wstring& reason,const ScanOptions& options={}) {
    if(cancel)return Outcome::Cancelled;
    if(!mft::eligible(path)){reason=L"Fast scan requires a fixed NTFS drive root";return Outcome::Fallback;}
    const auto start=::GetTickCount64();
    ws::ntfs::MftReader reader(path.substr(0,2));
    if(!reader.valid()){reason=reader.error();return Outcome::Fallback;}
    std::vector<mft::Entry> entries;
    std::uint32_t cluster=reader.geometry().clusterSize;
    std::uint64_t files=0,bytes=0;
    std::wstring error;
    auto nextPublish=::GetTickCount64()+options.previewIntervalMs;
    std::atomic<bool> stopPreview=false;
    std::future<std::shared_ptr<ScanResult>> preview;
    // Cancel construction before the future joins on every exit, including
    // reader failure and exceptions. The stop flag outlives the future.
    struct StopPreview {std::atomic<bool>& flag;~StopPreview(){flag=true;}} stop{stopPreview};
    std::uint64_t previewMs=0;
    const bool ok=mft::read(reader,cluster,[&](const mft::Batch& batch) {
        while(options.paused&&*options.paused&&!cancel)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(cancel)return false;
        if(preview.valid()&&preview.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            auto part=preview.get();previewMs+=part->snapshotMs;if(snapshot)snapshot(std::move(part));
        }
        cluster=batch.cluster;
        for(auto& e:batch.entries){if(!e.directory&&!e.name.empty()){++files;if(e.known)bytes+=e.bytes;}entries.push_back(std::move(e));}
        ScanProgress pg;pg.files=files;pg.bytes=bytes;pg.current=path;pg.fast=true;pg.recordsDone=batch.done;pg.recordsTotal=batch.total;
        if(progress&&!progress(pg))return false;
        const auto now=::GetTickCount64();
        // A preview duplicates the entry names as well as constructing a
        // second tree. Stop publishing large previews under the host job cap.
        if(snapshot&&entries.size()<=500000&&now>=nextPublish&&!preview.valid()) {
            // One background build at a time. Accumulate the next batches
            // while it associates records and sorts the immutable preview
            // tree.
            auto copy=entries;
            preview=std::async(std::launch::async,[path,cluster,options,start,&stopPreview,copy=std::move(copy)] {
                const auto tick=::GetTickCount64();auto part=std::make_shared<ScanResult>();mft::build(path,copy,cluster,*part,false,options,&stopPreview);
                part->snapshotMs=::GetTickCount64()-tick;part->elapsedMs=::GetTickCount64()-start;return part;
            });
            nextPublish=::GetTickCount64()+std::max<std::uint64_t>(options.previewIntervalMs,previewMs*9);
        }
        return true;
    },error);
    stopPreview=true;
    if(preview.valid()){auto discarded=preview.get();previewMs+=discarded->snapshotMs;}
    if(cancel)return Outcome::Cancelled;
    if(!ok){reason=error.empty()?L"MFT scan failed":error;return Outcome::Fallback;}
    const auto mergeStart=::GetTickCount64();mft::build(path,entries,cluster,result,true,options,&cancel,&entries);result.mergeMs=::GetTickCount64()-mergeStart;result.snapshotMs=previewMs;result.elapsedMs=::GetTickCount64()-start;
    return result.cancelled?Outcome::Cancelled:Outcome::Complete;
}

} // namespace diskview::fastscan
