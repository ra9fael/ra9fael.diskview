// In-process, read-only MFT scan using the token supplied by the host.
#pragma once
#include <atomic>
#include <chrono>
#include <deque>
#include <thread>
#include "mft_scan.h"

namespace diskview::fastscan {
enum class Outcome { Complete, Cancelled, Fallback };

inline bool processElevated() {
    HANDLE token=nullptr;
    if(!::OpenProcessToken(::GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    struct Guard {HANDLE token;~Guard(){if(token)::CloseHandle(token);}} guard{token};
    TOKEN_ELEVATION elevation{};
    DWORD size=0;
    return ::GetTokenInformation(token,TokenElevation,&elevation,sizeof elevation,&size)&&elevation.TokenIsElevated;
}

// Segmented storage never allocates old+new contiguous Entry buffers during
// growth. Batches transfer their names rather than copying them. In particular,
// the sink must be mutable: std::move(const Entry&) still copies the string.
inline void appendBatch(mft::Batch& batch, std::deque<mft::Entry>& entries,
                        std::uint64_t& files, std::uint64_t& bytes) {
    for(auto& entry:batch.entries) {
        if(!entry.directory&&!entry.name.empty()) {
            ++files;
            if(entry.known)bytes+=entry.bytes;
        }
        entries.push_back(std::move(entry));
    }
    batch.entries.clear();
}

inline Outcome scanDirect(const std::wstring& path,const ProgressFn& progress,
                          const SnapshotFn& snapshot,const std::atomic<bool>& cancel,
                          ScanResult& result,std::wstring& reason,const ScanOptions& options={}) {
    (void)snapshot;
    if(cancel)return Outcome::Cancelled;
    if(!mft::eligible(path)){reason=L"Fast scan requires a fixed NTFS drive root";return Outcome::Fallback;}
    const auto start=::GetTickCount64();
    ws::ntfs::MftReader reader(path.substr(0,2));
    if(!reader.valid()){reason=reader.error();return Outcome::Fallback;}
    std::deque<mft::Entry> entries;
    std::uint32_t cluster=reader.geometry().clusterSize;
    std::uint64_t files=0,bytes=0;
    std::wstring error;
    bool stopped=false;
    // Only counters are published during the fast read. A background tree
    // preview duplicates both metadata and names while the main read grows;
    // the old 500k check also failed to cancel an already-running preview.
    const bool ok=mft::read(reader,cluster,[&](mft::Batch& batch) {
        while(options.paused&&*options.paused&&!cancel)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(cancel)return false;
        cluster=batch.cluster;
        const auto done=batch.done,total=batch.total;
        appendBatch(batch,entries,files,bytes);
        ScanProgress pg;pg.files=files;pg.bytes=bytes;pg.current=path;
        pg.fast=true;pg.recordsDone=done;pg.recordsTotal=total;
        if(progress&&!progress(pg)){stopped=true;return false;}
        return true;
    },error);
    if(cancel||stopped)return Outcome::Cancelled;
    if(!ok){reason=error.empty()?L"MFT scan failed":error;return Outcome::Fallback;}
    const auto mergeStart=::GetTickCount64();
    mft::build(path,entries,cluster,result,true,options,&cancel,&entries);
    result.mergeMs=::GetTickCount64()-mergeStart;
    result.elapsedMs=::GetTickCount64()-start;
    return result.cancelled?Outcome::Cancelled:Outcome::Complete;
}
} // namespace diskview::fastscan
