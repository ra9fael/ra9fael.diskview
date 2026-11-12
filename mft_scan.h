// Read-only NTFS metadata adapter. MFT names and unnamed $DATA sizes travel
// together; USN name records alone are insufficient for a space analyzer.
#pragma once
#include "scan.h"
#include "vendor/winshun/Ntfs.h"
#include <unordered_map>
#include <unordered_set>
#include <cstring>

namespace diskview::mft {
struct Entry {
    std::uint64_t id=0,parent=0,bytes=0;
    std::uint32_t attributes=0;
    bool directory=false,known=false;
    std::wstring name;
    std::uint64_t modified=0; // FILETIME, from $STANDARD_INFORMATION
};
struct Batch {
    std::vector<Entry> entries;
    std::uint64_t done=0,total=0;
    std::uint32_t cluster=4096;
};
using Sink=std::function<bool(const Batch&)>;
inline bool eligible(const std::wstring& path) {
    if(path.size()!=3||path[1]!=L':'||path[2]!=L'\\'||::GetDriveTypeW(path.c_str())!=DRIVE_FIXED)return false;
    wchar_t fs[32]{};
    return ::GetVolumeInformationW(path.c_str(),nullptr,0,nullptr,nullptr,nullptr,fs,32)&&_wcsicmp(fs,L"NTFS")==0;
}
template<class T> T load(const std::byte* data) {T value{};std::memcpy(&value,data,sizeof value);return value;}
inline bool read(ws::ntfs::RecordSource& reader,std::uint32_t cluster,const Sink& sink,std::wstring& error) {
    Batch batch;batch.cluster=cluster;batch.total=reader.bytes();
    std::uint64_t rejected=0;
    while(reader.next()) {
        for(std::size_t i=0;i<reader.blockRecords();++i) {
            auto* diskReader=dynamic_cast<ws::ntfs::MftReader*>(&reader);
            const auto raw=diskReader?diskReader->recordBytes(i):std::span<std::byte>{};
            ws::ntfs::FileRecord record;
            if(!reader.parse(i,record)) {
                // Free slots may contain zeros, so only damaged allocated
                // records count as a scan error.
                if(raw.size()>0x18 && load<std::uint32_t>(raw.data())==0x454c4946 && (load<std::uint16_t>(raw.data()+0x16)&1))++rejected;
                continue;
            }
            if(!record.inUse)continue;
            const auto id=(raw.size()>0x12?std::uint64_t(load<std::uint16_t>(raw.data()+0x10))<<48:0)|(reader.firstRecord()+i);
            const auto base=raw.size()>0x28?load<std::uint64_t>(raw.data()+0x20):record.baseRecord;
            if(record.baseRecord) {
                if(record.sizeKnown)batch.entries.push_back({base,0,record.size,0,false,true,L""});
                // Names can live in extension records too.
                for(const auto& n:record.names)batch.entries.push_back({base,n.parent,record.size,record.attributes,record.directory,record.sizeKnown,std::wstring(n.name.begin(),n.name.end()),std::uint64_t(record.modified)});
            } else {
                batch.entries.push_back({id,0,record.size,record.attributes,record.directory,record.sizeKnown,L"",std::uint64_t(record.modified)});
                for(const auto& n:record.names)batch.entries.push_back({id,n.parent,record.size,record.attributes,record.directory,record.sizeKnown,std::wstring(n.name.begin(),n.name.end()),std::uint64_t(record.modified)});
            }
            if(batch.entries.size()>=2048){batch.done=reader.bytesDone();if(!sink(batch))return false;batch.entries.clear();}
        }
        batch.done=reader.bytesDone();if(!sink(batch))return false;batch.entries.clear();
    }
    if(!reader.valid()){error=reader.error();return false;}
    if(rejected) {error=L"MFT contains unreadable records ("+std::to_wstring(rejected)+L"); use ordinary enumeration.";return false;}
    return true;
}
inline void build(const std::wstring& root,const std::vector<Entry>& entries,std::uint32_t cluster,ScanResult& out,bool complete=true,const ScanOptions& options={},const std::atomic<bool>* cancel=nullptr) {
    out=ScanResult{};out.displayRoot=root;out.clusterBytes=cluster;out.root.name=root;out.root.dir=true;out.fast=true;
    const auto cancelled=[&]{return cancel&&cancel->load();};
    const auto excluded=[&](const std::wstring& p){return std::any_of(options.excludedPaths.begin(),options.excludedPaths.end(),[&](const auto& base){return sameOrBelow(p,base);});};
    if(excluded(root)){out.excludedItems=1;return;}
    std::unordered_map<std::uint64_t,std::uint64_t> sizes;
    std::unordered_map<std::uint64_t,std::uint64_t> modifieds;
    std::unordered_map<std::uint64_t,std::vector<std::size_t>> children;
    std::unordered_map<std::uint64_t,std::uint32_t> directoryAttributes;
    std::uint64_t rootId=ws::ntfs::kRootRecord;
    for(std::size_t i=0;i<entries.size();++i) {
        if(cancelled()){out.cancelled=true;return;}
        const auto& e=entries[i];if(e.known)sizes[e.id]=e.bytes;
        if(e.modified)modifieds[e.id]=e.modified;
        // Extension records need not repeat STANDARD_INFORMATION. Preserve
        // reparse attributes from the base record when merging their names.
        if(e.directory)directoryAttributes[e.id]|=e.attributes;
        if(ws::ntfs::recordOf(e.id)==ws::ntfs::kRootRecord)rootId=e.id;
        if(!e.name.empty()&&ws::ntfs::recordOf(e.id)!=ws::ntfs::kRootRecord)children[e.parent].push_back(i);
    }
    detail::Ctx ranking;std::unordered_set<std::uint64_t> active;
    std::unordered_set<std::size_t> visited;
    // Excluded descendants are accounted for as excluded scope, never as
    // unreadable/orphan records. An iterative walk also handles metadata cycles.
    std::unordered_set<std::uint64_t> ignored;
    const auto ignore=[&](std::uint64_t id) {
        std::vector<std::uint64_t> pending{id};
        while(!pending.empty()&&!cancelled()) {
            const auto current=pending.back();pending.pop_back();if(!ignored.insert(current).second)continue;
            const auto found=children.find(current);if(found==children.end())continue;
            for(const auto index:found->second){visited.insert(index);if(directoryAttributes.count(entries[index].id))pending.push_back(entries[index].id);}
        }
    };
    const auto visit=[&](const auto& self,std::uint64_t id,Node& dir,const std::wstring& path,int depth)->void {
        if(depth>detail::kMaxDepth||!active.insert(id).second){++dir.skippedChildren;++out.skippedDirs;return;}
        const auto found=children.find(id);
        if(found!=children.end())for(const auto index:found->second) {
            if(cancelled())break;
            if(!visited.insert(index).second)continue;
            const auto& e=entries[index];Node n;n.name=e.name;n.dir=directoryAttributes.count(e.id)!=0;
            // The metadata is on the base record; the name may come from an
            // extension record that repeats less of it.
            n.attributes=n.dir?directoryAttributes[e.id]:e.attributes;
            const auto stamp=modifieds.find(e.id);
            if(stamp!=modifieds.end())n.modified=stamp->second;
            const auto full=childPath(path,e.name);
            if(excluded(full)){++out.excludedItems;if(n.dir)ignore(e.id);continue;}
            if(n.dir) {
                ++out.dirs;
                if(directoryAttributes[e.id]&FILE_ATTRIBUTE_REPARSE_POINT)++out.skippedLinks;
                else self(self,e.id,n,full,depth+1);
                dir.dirs+=1+n.dirs;dir.files+=n.files;
            } else {
                ++out.files;++dir.files;
                const auto size=sizes.find(e.id);
                if(size==sizes.end()) {
                    n.skippedChildren=1;++out.unknownSizes;
                    if(out.skippedList.size()<detail::kMaxSkippedList)out.skippedList.push_back({full,ERROR_NOT_FOUND});
                } else {n.logical=size->second;n.allocApprox=detail::clusterRound(n.logical,cluster);detail::considerLarge(ranking,n.logical,path,e.name.c_str());}
            }
            dir.logical+=n.logical;dir.allocApprox+=n.allocApprox;dir.skippedChildren+=n.skippedChildren;dir.children.push_back(std::move(n));
        }
        std::stable_sort(dir.children.begin(),dir.children.end(),[](const auto& a,const auto& b){return a.logical>b.logical;});active.erase(id);
    };
    visit(visit,rootId,out.root,root,0);
    if(cancelled()){out.cancelled=true;return;}
    while(!ranking.top.empty()){out.largest.push_back(ranking.top.top());ranking.top.pop();}
    std::sort(out.largest.begin(),out.largest.end(),std::greater<>());
    for(std::size_t i=0;i<entries.size();++i)if(!entries[i].name.empty()&&ws::ntfs::recordOf(entries[i].id)!=ws::ntfs::kRootRecord&&!visited.count(i))++out.orphanRecords;
    if(!complete){out.pendingRecords=out.orphanRecords;out.orphanRecords=0;}
}
} // namespace diskview::mft
