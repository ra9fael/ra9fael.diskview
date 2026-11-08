// Short-lived, read-only elevated helper. Only a local NTFS drive letter and
// a single private pipe are accepted; the GUI process never elevates.
#pragma once
#include "mft_scan.h"
#include <shellapi.h>
#include <sddl.h>
#include <objbase.h>
#include <limits>
#include <stdexcept>

namespace diskview::helper {
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){}
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)::CloseHandle(h);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
constexpr std::uint32_t kMagic=0x32564453, kMaxPacket=2*1024*1024;
struct Header {std::uint32_t magic=kMagic,type=0,bytes=0;};
struct Row {std::uint64_t id=0,parent=0,bytes=0;std::uint32_t attributes=0;std::uint16_t chars=0;std::uint8_t directory=0,known=0;};
struct Progress {std::uint64_t done=0,total=0;std::uint32_t cluster=4096;};
template<class T> inline void append(std::vector<std::byte>& out,const T& value) {
    auto start=out.size();out.resize(start+sizeof(T));std::memcpy(out.data()+start,&value,sizeof(T));
}
inline bool writeAll(HANDLE pipe,const void* buffer,DWORD bytes) {
    auto* at=static_cast<const std::byte*>(buffer);
    while(bytes){DWORD done=0;if(!::WriteFile(pipe,at,bytes,&done,nullptr)||!done)return false;bytes-=done;at+=done;}return true;
}
inline bool send(HANDLE pipe,std::uint32_t type,const void* data,std::uint32_t bytes) {
    Header h{kMagic,type,bytes};return bytes<=kMaxPacket&&writeAll(pipe,&h,sizeof h)&&(!bytes||writeAll(pipe,data,bytes));
}
inline std::vector<std::byte> encode(const mft::Batch& batch) {
    std::vector<std::byte> data;data.reserve(batch.entries.size()*96);
    append(data,Progress{batch.done,batch.total,batch.cluster});
    for(const auto& e:batch.entries) {
        if(e.name.size()>255)throw std::runtime_error("Invalid MFT name length");
        append(data,Row{e.id,e.parent,e.bytes,e.attributes,static_cast<std::uint16_t>(e.name.size()),static_cast<std::uint8_t>(e.directory),static_cast<std::uint8_t>(e.known)});
        const auto start=data.size();data.resize(start+e.name.size()*sizeof(wchar_t));std::memcpy(data.data()+start,e.name.data(),e.name.size()*sizeof(wchar_t));
    }
    return data;
}
inline bool decode(const std::vector<std::byte>& data,mft::Batch& batch) {
    batch={};if(data.size()<sizeof(Progress))return false;
    auto progress=mft::load<Progress>(data.data());batch.done=progress.done;batch.total=progress.total;batch.cluster=progress.cluster;
    if(!batch.cluster||batch.cluster>(16u<<20)||batch.done>batch.total)return false;
    std::size_t at=sizeof(Progress);
    while(at<data.size()) {
        if(data.size()-at<sizeof(Row))return false;
        const auto row=mft::load<Row>(data.data()+at);at+=sizeof(Row);
        if(row.chars>255||row.directory>1||row.known>1||data.size()-at<row.chars*sizeof(wchar_t))return false;
        std::wstring name(row.chars,L'\0');std::memcpy(name.data(),data.data()+at,row.chars*sizeof(wchar_t));at+=row.chars*sizeof(wchar_t);
        if(name.find_first_of(L"\\/:")!=std::wstring::npos||name.find(L'\0')!=std::wstring::npos)return false;
        batch.entries.push_back({row.id,row.parent,row.bytes,row.attributes,row.directory!=0,row.known!=0,std::move(name)});
    }
    return true;
}
inline bool pipeNameValid(const std::wstring& name) {
    const std::wstring prefix=L"\\\\.\\pipe\\DiskView.Scan.";
    if(name.rfind(prefix,0)!=0||name.size()!=prefix.size()+38)return false;
    return name.substr(prefix.size()).find_first_not_of(L"{}0123456789abcdefABCDEF-")==std::wstring::npos;
}
inline int run(const std::wstring& drive,const std::wstring& pipeName,DWORD parentPid) {
    if(!mft::eligible(drive)||!pipeNameValid(pipeName)||!parentPid)return 2;
    Handle pipe(::CreateFileW(pipeName.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));
    if(pipe.h==INVALID_HANDLE_VALUE)return 3;
    ULONG server=0;if(!::GetNamedPipeServerProcessId(pipe.h,&server)||server!=parentPid)return 4;
    try {
        ws::ntfs::MftReader reader(drive.substr(0,2));std::wstring error;
        bool ok=false;
        if(reader.valid())ok=mft::read(reader,reader.geometry().clusterSize,[&](const mft::Batch& batch){const auto bytes=encode(batch);return send(pipe.h,1,bytes.data(),static_cast<std::uint32_t>(bytes.size()));},error);
        else error=reader.error();
        if(!ok){if(error.empty())return 5;send(pipe.h,3,error.data(),static_cast<std::uint32_t>(error.size()*sizeof(wchar_t)));return 5;}
        return send(pipe.h,2,nullptr,0)?0:6;
    } catch(const std::exception& e) {const auto error=winshun::wide(e.what());send(pipe.h,3,error.data(),static_cast<std::uint32_t>(error.size()*sizeof(wchar_t)));return 7;}
}
// Server reads are overlapped so cancel/close never waits for the helper's I/O.
inline bool readAll(HANDLE pipe,HANDLE child,const std::atomic<bool>& cancel,void* data,DWORD bytes) {
    auto* at=static_cast<std::byte*>(data);Handle event(::CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.h)return false;
    while(bytes&&!cancel) {
        OVERLAPPED ov{};ov.hEvent=event.h;::ResetEvent(event.h);DWORD got=0;
        if(!::ReadFile(pipe,at,bytes,&got,&ov)) {
            if(::GetLastError()!=ERROR_IO_PENDING)return false;
            for(;;) {
                if(cancel){::CancelIoEx(pipe,&ov);::GetOverlappedResult(pipe,&ov,&got,TRUE);return false;}
                const DWORD wait=::WaitForSingleObject(event.h,50);
                if(wait==WAIT_OBJECT_0)break;
                if(wait==WAIT_FAILED){::CancelIoEx(pipe,&ov);::GetOverlappedResult(pipe,&ov,&got,TRUE);return false;}
                if(::WaitForSingleObject(child,0)==WAIT_OBJECT_0){::CancelIoEx(pipe,&ov);::GetOverlappedResult(pipe,&ov,&got,TRUE);return false;}
            }
            if(!::GetOverlappedResult(pipe,&ov,&got,FALSE))return false;
        }
        if(!got)return false;at+=got;bytes-=got;
    }
    return bytes==0;
}
enum class Outcome {Complete,Cancelled,Fallback};
inline Outcome scan(HWND owner,const std::wstring& path,const ProgressFn& progress,const SnapshotFn& snapshot,const std::atomic<bool>& cancel,ScanResult& result,std::wstring& reason) {
    GUID guid{};if(FAILED(::CoCreateGuid(&guid))){reason=L"Cannot create scan session";return Outcome::Fallback;}
    wchar_t id[40]{};::StringFromGUID2(guid,id,40);const auto pipeName=std::wstring(L"\\\\.\\pipe\\DiskView.Scan.")+id;
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!::ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)",SDDL_REVISION_1,&descriptor,nullptr)){reason=L"Cannot secure scan session";return Outcome::Fallback;}
    SECURITY_ATTRIBUTES sa{sizeof sa,descriptor,FALSE};
    Handle pipe(::CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,0,kMaxPacket,0,&sa));::LocalFree(descriptor);
    if(pipe.h==INVALID_HANDLE_VALUE){reason=L"Cannot open scan session";return Outcome::Fallback;}
    Handle connected(::CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED connection{};connection.hEvent=connected.h;
    const BOOL immediate=::ConnectNamedPipe(pipe.h,&connection);
    if(!immediate&&::GetLastError()!=ERROR_IO_PENDING){reason=L"Cannot connect scan session";return Outcome::Fallback;}
    wchar_t exe[32768]{};::GetModuleFileNameW(nullptr,exe,32768);
    const std::wstring args=L"--scan-helper "+path.substr(0,2)+L" "+pipeName+L" "+std::to_wstring(::GetCurrentProcessId());
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof info;info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;info.hwnd=owner;info.lpVerb=L"runas";info.lpFile=exe;info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(!::ShellExecuteExW(&info)){const auto error=::GetLastError();::CancelIoEx(pipe.h,&connection);DWORD ignored=0;::GetOverlappedResult(pipe.h,&connection,&ignored,TRUE);reason=error==ERROR_CANCELLED?L"Authorization declined":L"Scan helper could not start ("+std::to_wstring(error)+L")";return Outcome::Fallback;}
    Handle child(info.hProcess);
    const auto start=::GetTickCount64();bool linked=immediate!=FALSE;
    while(!linked&&!cancel&&::GetTickCount64()-start<15000) {
        if(::WaitForSingleObject(connected.h,50)==WAIT_OBJECT_0){DWORD ignored=0;linked=::GetOverlappedResult(pipe.h,&connection,&ignored,FALSE)!=FALSE;break;}
        if(::WaitForSingleObject(child.h,0)==WAIT_OBJECT_0)break;
    }
    if(!linked){::CancelIoEx(pipe.h,&connection);DWORD ignored=0;::GetOverlappedResult(pipe.h,&connection,&ignored,TRUE);reason=L"Scan helper connection failed";return cancel?Outcome::Cancelled:Outcome::Fallback;}
    ULONG client=0;
    if(!::GetNamedPipeClientProcessId(pipe.h,&client)||client!=::GetProcessId(child.h)){reason=L"Unexpected scan helper";return Outcome::Fallback;}
    std::vector<mft::Entry> entries;std::uint32_t cluster=4096;std::uint64_t files=0,bytes=0;
    auto nextPublish=::GetTickCount64()+1500;
    for(;;) {
        Header h;
        if(!readAll(pipe.h,child.h,cancel,&h,sizeof h)){reason=L"Scan helper disconnected";return cancel?Outcome::Cancelled:Outcome::Fallback;}
        if(h.magic!=kMagic||h.bytes>kMaxPacket||h.type<1||h.type>3){reason=L"Invalid scan helper response";return Outcome::Fallback;}
        std::vector<std::byte> payload(h.bytes);
        if(h.bytes&&!readAll(pipe.h,child.h,cancel,payload.data(),h.bytes))return cancel?Outcome::Cancelled:Outcome::Fallback;
        if(h.type==3){if(h.bytes%2){reason=L"Invalid scan error";return Outcome::Fallback;}reason.resize(h.bytes/2);std::memcpy(reason.data(),payload.data(),h.bytes);return Outcome::Fallback;}
        if(h.type==2) {
            if(h.bytes){reason=L"Invalid scan completion";return Outcome::Fallback;}
            mft::build(path,entries,cluster,result);result.elapsedMs=::GetTickCount64()-start;
            return Outcome::Complete;
        }
        mft::Batch batch;if(!decode(payload,batch)){reason=L"Invalid file metadata";return Outcome::Fallback;}cluster=batch.cluster;
        for(auto& e:batch.entries){if(!e.directory&&!e.name.empty()){++files;if(e.known)bytes+=e.bytes;}entries.push_back(std::move(e));}
        ScanProgress pg;pg.files=files;pg.bytes=bytes;pg.current=path;pg.fast=true;pg.recordsDone=batch.done;pg.recordsTotal=batch.total;
        if(progress&&!progress(pg))return Outcome::Cancelled;
        const auto now=::GetTickCount64();
        if(snapshot&&now>=nextPublish){auto part=std::make_shared<ScanResult>();mft::build(path,entries,cluster,*part);snapshot(std::move(part));nextPublish=::GetTickCount64()+std::max<std::uint64_t>(1500,9*(::GetTickCount64()-now));}
    }
}
} // namespace diskview::helper
