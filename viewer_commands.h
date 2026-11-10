// Included inside diskview::detail. UI commands share one eligibility path.
#pragma once
struct CommandItem {Command id;const wchar_t* zh;const wchar_t* en;const wchar_t* key;};
inline const std::vector<CommandItem>& commandItems() {
    static const std::vector<CommandItem> items={
        {Command::Enter,L"进入目录 / 打开文件",L"Enter / open",L"Enter"},
        {Command::Copy,L"复制完整路径",L"Copy full path",L"Ctrl+C"},
        {Command::CopyFile,L"复制文件",L"Copy item",L"Ctrl+Shift+C"},
        {Command::CutFile,L"剪切文件",L"Cut item",L"Ctrl+Shift+X"},
        {Command::Locate,L"定位到所在目录",L"Locate parent",L""},
        {Command::Explorer,L"在资源管理器中显示",L"Show in Explorer",L""},
        {Command::Properties,L"属性",L"Properties",L"Alt+Enter"},
        {Command::Recycle,L"移至回收站…",L"Move to Recycle Bin…",L"Delete"},
        {Command::DeletePermanently,L"永久删除…",L"Delete permanently…",L""},
        {Command::Parent,L"返回父级",L"Parent folder",L"Alt+Up"},
        {Command::Ancestors,L"全部上级",L"All ancestors",L""},
        {Command::Rescan,L"扫描设置 / 重扫此目录",L"Scan settings",L"F5"},
        {Command::Exclude,L"切换下轮排除",L"Toggle exclusion",L""},
        {Command::Help,L"快捷键帮助",L"Keyboard shortcuts",L"F1"},
        {Command::Commands,L"搜索操作",L"Search actions",L"Ctrl+K"},
        {Command::FocusChart,L"聚焦空间图",L"Focus chart",L"F6"},
        {Command::FocusList,L"聚焦目录列表",L"Focus list",L"Shift+F6"},
        {Command::Fit,L"适合窗口",L"Fit chart",L"0"},
        {Command::Diagnostics,L"扫描诊断",L"Scan diagnostics",L""},
        {Command::Issues,L"读取问题与覆盖范围",L"Read errors and coverage",L""},
        {Command::Accounting,L"容量统计口径",L"Size accounting",L""}
    };return items;
}
inline std::wstring actionPath(const ViewerState& s) {return s.selected?s.selectedPath:s.viewRoot?nodePath(s,s.viewRoot):s.lastPath;}
inline bool protectedPath(const std::wstring& raw) {
    const auto path=normalizePath(raw,nullptr);
    if(path.empty()||path.size()<=3||path.rfind(L"\\\\",0)==0)return true;
    wchar_t system[MAX_PATH]{};::GetWindowsDirectoryW(system,MAX_PATH);
    if(sameOrBelow(path,system))return true;
    for(const auto name:{L"ProgramFiles",L"ProgramFiles(x86)",L"ProgramData"}) {
        wchar_t value[32768]{};const auto count=::GetEnvironmentVariableW(name,value,32768);
        if(count&&count<32768&&sameOrBelow(path,value))return true;
    }
    const auto name=lower(fileNameOf(path));if(name==L"pagefile.sys"||name==L"hiberfil.sys"||name==L"swapfile.sys"||name==L"system volume information")return true;
    if(::GetDriveTypeW(path.substr(0,3).c_str())!=DRIVE_FIXED)return true;
    const auto attrs=::GetFileAttributesW(path.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_REPARSE_POINT)))return true;
    for(auto end=path.find(L'\\',3);end!=std::wstring::npos;end=path.find(L'\\',end+1)) {
        const auto parent=::GetFileAttributesW(path.substr(0,end).c_str());
        if(parent==INVALID_FILE_ATTRIBUTES||(parent&FILE_ATTRIBUTE_REPARSE_POINT))return true;
    }
    return false;
}
inline bool commandEnabled(const ViewerState& s,Command command) {
    switch(command) {
    case Command::Parent:return s.viewRoot&&s.viewRoot!=s.tree.get();
    case Command::Ancestors:return s.viewRoot&&s.viewRoot!=s.tree.get();
    case Command::Enter:case Command::Locate:case Command::Copy:case Command::CopyFile:case Command::CutFile:case Command::Explorer:case Command::Properties:return s.selected!=nullptr;
    case Command::Exclude:return s.selected&&s.selected!=s.tree.get();
    // An incomplete snapshot says nothing about whether the file on disk can
    // be deleted; only a running scan does. Protected paths are refused.
    case Command::Recycle:case Command::DeletePermanently:
        return s.selected&&s.selected!=s.tree.get()&&!s.scanning&&!protectedPath(s.selectedPath);
    case Command::Rescan:return !s.scanning;
    case Command::Fit:case Command::FocusChart:case Command::FocusList:return s.tree!=nullptr;
    default:return true;
    }
}
inline HMENU makeContextMenu(const ViewerState& s) {
    const auto menu=::CreatePopupMenu();
    const auto header=[&](const wchar_t* zh,const wchar_t* en){::AppendMenuW(menu,MF_STRING|MF_DISABLED|MF_GRAYED,0,s.spec.zh?zh:en);};
    for(const auto& item:commandItems()) {
        if(item.id==Command::FocusChart)break;
        if(item.id==Command::Recycle||item.id==Command::Parent||item.id==Command::Rescan)::AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        if(item.id==Command::Enter)header(L"文件操作",L"File actions");
        if(item.id==Command::Parent)header(L"导航",L"Navigation");
        if(item.id==Command::Rescan)header(L"扫描",L"Scanning");
        std::wstring title=s.spec.zh?item.zh:item.en;
        if(item.id==Command::Enter)title=s.selected&&s.selected->dir?(s.spec.zh?L"进入目录":L"Enter folder"):(s.spec.zh?L"打开文件":L"Open file");
        if(item.id==Command::Exclude)title=std::any_of(s.scanOptions.excludedPaths.begin(),s.scanOptions.excludedPaths.end(),[&](const auto& p){return lower(p)==lower(s.selectedPath);})?(s.spec.zh?L"取消下轮排除":L"Remove exclusion"):(s.spec.zh?L"下轮扫描排除此项":L"Exclude from next scan");
        if(*item.key)title+=L"\t"+std::wstring(item.key);
        ::AppendMenuW(menu,MF_STRING|(commandEnabled(s,item.id)?0:MF_GRAYED),UINT(item.id),title.c_str());
    }
    return menu;
}
inline void contextMenu(ViewerState& s,POINT screen) {
    clearHover(s);const auto path=s.selectedPath;const auto menu=makeContextMenu(s);
    const auto command=::TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,s.hwnd,nullptr);::DestroyMenu(menu);
    // A posted scan result may have replaced every node while the menu was open.
    s.selected=descend(s,path,nullptr);s.selectedPath=s.selected?path:L"";
    if(command)performCommand(s,static_cast<Command>(command));
}
inline void showAncestors(ViewerState& s) {
    if(!commandEnabled(s,Command::Parent))return;
    std::vector<std::wstring> paths{s.tree->name};std::wstring path=s.tree->name;
    for(auto* n:s.drill){path=childPath(path,n->name);paths.push_back(path);}
    HMENU menu=::CreatePopupMenu();for(std::size_t i=paths.size();i-->0;)::AppendMenuW(menu,MF_STRING,100+i,paths[i].c_str());
    POINT p{s.rcParent.left,s.rcParent.bottom};::ClientToScreen(s.hwnd,&p);
    const auto choice=::TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,s.hwnd,nullptr);::DestroyMenu(menu);
    if(choice>=100&&choice<100+paths.size())navigateTo(s,paths[choice-100]);
}
inline void focusPane(ViewerState& s,bool list) {
    s.focusList=list;s.mobileList=list;computeLayout(s);relayoutView(s);s.keyboardFocus=false;
    if(!s.selected&&!s.rows.empty())selectNode(s,s.rows.front().node,s.rows.front().path);
    if(s.hwnd)::SetFocus(s.hwnd);::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline void ensureSelectedVisible(ViewerState& s) {
    if(s.focusList) {
        for(int i=0;i<listCount(s);++i)if(s.rows[i].node==s.selected){if(i<s.topScroll)s.topScroll=i;if(i>=s.topScroll+maxRows(s))s.topScroll=std::max(0,i-maxRows(s)+1);}
    } else if(s.zoom>1) {
        double x=0,y=0;bool found=false;
        if(s.sunburst){for(const auto& r:s.slices)if(r.node==s.selected){const auto a=(r.a0+r.a1)/2,rad=(r.r0+r.r1)/2;x=(s.rcChart.left+s.rcChart.right)/2.0+std::cos(a)*rad;y=(s.rcChart.top+s.rcChart.bottom)/2.0+std::sin(a)*rad;found=true;break;}}
        else for(const auto& r:s.tiles)if(r.node==s.selected){x=r.rect.x+r.rect.w/2;y=r.rect.y+r.rect.h/2;found=true;break;}
        if(found){auto p=chartRect(s,{x,y,0,0});const int margin=S(18,s);s.panX+=std::clamp(p.x,double(s.rcChart.left+margin),double(s.rcChart.right-margin))-p.x;s.panY+=std::clamp(p.y,double(s.rcChart.top+margin),double(s.rcChart.bottom-margin))-p.y;clampPan(s);}
    }
}
inline void directionSelect(ViewerState& s,UINT key) {
    s.keyboardFocus=false;
    if(s.focusList) {
        if(key==VK_LEFT){goUp(s);return;}if(key==VK_RIGHT){if(s.selected&&s.selected->dir)navigateTo(s,s.selectedPath);return;}
        int index=-1;for(int i=0;i<listCount(s);++i)if(s.rows[i].node==s.selected)index=i;
        if(listCount(s)){index=std::clamp(index+(key==VK_DOWN?1:-1),0,listCount(s)-1);selectNode(s,s.rows[index].node,s.rows[index].path);}
    } else {
        struct Point {const Node* node;std::wstring path;double x,y;int depth;};std::vector<Point> points;
        if(s.sunburst)for(const auto& r:s.slices){const auto a=(r.a0+r.a1)/2,rad=(r.r0+r.r1)/2;points.push_back({r.node,r.path,std::cos(a)*rad,std::sin(a)*rad,r.depth});}
        else for(const auto& r:s.tiles)points.push_back({r.node,r.path,r.rect.x+r.rect.w/2,r.rect.y+r.rect.h/2,r.depth});
        if(points.empty())return;
        auto at=std::find_if(points.begin(),points.end(),[&](const auto& r){return r.node==s.selected;});
        if(at==points.end()){selectNode(s,points.front().node,points.front().path);return;}
        const double dx=key==VK_LEFT?-1:key==VK_RIGHT?1:0,dy=key==VK_UP?-1:key==VK_DOWN?1:0;
        const Point* best=nullptr;double score=1e100;
        for(const auto& p:points)if(p.node!=at->node&&p.depth==at->depth){const auto vx=p.x-at->x,vy=p.y-at->y,along=vx*dx+vy*dy,side=std::abs(vx*dy-vy*dx);if(along>1&&along+side*2.5<score){score=along+side*2.5;best=&p;}}
        if(best)selectNode(s,best->node,best->path);
    }
    ensureSelectedVisible(s);if(s.hwnd)::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline bool copyText(HWND owner,const std::wstring& text) {
    if(!::OpenClipboard(owner))return false;
    bool ok=false;const auto bytes=(text.size()+1)*sizeof(wchar_t);const auto memory=::GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(memory){if(auto* dest=::GlobalLock(memory)){std::memcpy(dest,text.c_str(),bytes);::GlobalUnlock(memory);::EmptyClipboard();ok=::SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;}if(!ok)::GlobalFree(memory);}
    ::CloseClipboard();return ok;
}
inline std::wstring errorText(HRESULT hr) {return L"Windows error / HRESULT: "+std::to_wstring(static_cast<unsigned long>(hr));}
class RecycleSink final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IFileOperationProgressSink> {
public:
    HRESULT itemResult=E_PENDING;
    bool completed=false,recycled=false;
    // When false the delete is allowed to bypass the Recycle Bin (permanent).
    bool requireRecycle=true;
    Microsoft::WRL::ComPtr<IShellItem> destination;
    IFACEMETHODIMP StartOperations() override {return S_OK;}
    IFACEMETHODIMP FinishOperations(HRESULT) override {return S_OK;}
    IFACEMETHODIMP PreRenameItem(DWORD,IShellItem*,LPCWSTR) override {return E_ABORT;}
    IFACEMETHODIMP PostRenameItem(DWORD,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override {return S_OK;}
    IFACEMETHODIMP PreMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override {return E_ABORT;}
    IFACEMETHODIMP PostMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override {return S_OK;}
    IFACEMETHODIMP PreCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override {return E_ABORT;}
    IFACEMETHODIMP PostCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override {return S_OK;}
    IFACEMETHODIMP PreDeleteItem(DWORD flags,IShellItem*) override {
        // Refuse a non-recycle retry unless the caller asked for a permanent
        // delete, even if the shell could provide one.
        return (!requireRecycle||(flags&TSF_DELETE_RECYCLE_IF_POSSIBLE))?S_OK:E_ABORT;
    }
    IFACEMETHODIMP PostDeleteItem(DWORD,IShellItem*,HRESULT hr,IShellItem* inRecycleBin) override {
        completed=true;itemResult=hr;recycled=SUCCEEDED(hr)&&hr!=S_FALSE&&inRecycleBin;
        destination=inRecycleBin;return S_OK;
    }
    IFACEMETHODIMP PreNewItem(DWORD,IShellItem*,LPCWSTR) override {return E_ABORT;}
    IFACEMETHODIMP PostNewItem(DWORD,IShellItem*,LPCWSTR,LPCWSTR,DWORD,HRESULT,IShellItem*) override {return S_OK;}
    IFACEMETHODIMP UpdateProgress(UINT,UINT) override {return S_OK;}
    IFACEMETHODIMP ResetTimer() override {return S_OK;}
    IFACEMETHODIMP PauseTimer() override {return S_OK;}
    IFACEMETHODIMP ResumeTimer() override {return S_OK;}
};
struct DeleteResult {
    HRESULT error=E_FAIL;
    bool recycled=false,deleted=false;
    Microsoft::WRL::ComPtr<IShellItem> destination;
};
// Deletes one path through the shell. With `recycle` the item must land in
// the Recycle Bin; without it the delete is permanent and nothing else about
// the call changes. No elevation prompt, no shell error UI: the caller owns
// confirmation and reports failures.
inline DeleteResult deletePath(HWND owner,const std::wstring& path,bool recycle) {
    DeleteResult result;
    if(protectedPath(path)){result.error=E_ACCESSDENIED;return result;}
    Microsoft::WRL::ComPtr<IFileOperation> operation;Microsoft::WRL::ComPtr<IShellItem> item;
    auto sink=Microsoft::WRL::Make<RecycleSink>();
    sink->requireRecycle=recycle;
    HRESULT hr=::CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(operation.GetAddressOf()));
    if(SUCCEEDED(hr))hr=::SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(item.GetAddressOf()));
    if(SUCCEEDED(hr))hr=operation->SetOwnerWindow(owner);
    if(SUCCEEDED(hr))hr=operation->SetOperationFlags(recycle
        ?(FOF_ALLOWUNDO|FOFX_RECYCLEONDELETE|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOFX_EARLYFAILURE)
        :(FOF_NOCONFIRMATION|FOF_NOERRORUI|FOFX_EARLYFAILURE));
    if(SUCCEEDED(hr))hr=operation->DeleteItem(item.Get(),sink.Get());
    if(SUCCEEDED(hr))hr=operation->PerformOperations();
    BOOL aborted=TRUE;if(SUCCEEDED(hr))hr=operation->GetAnyOperationsAborted(&aborted);
    result.recycled=SUCCEEDED(hr)&&!aborted&&sink->completed&&sink->recycled;
    result.deleted=recycle?result.recycled:(SUCCEEDED(hr)&&!aborted&&sink->completed&&SUCCEEDED(sink->itemResult));
    result.error=FAILED(hr)?hr:aborted?HRESULT_FROM_WIN32(ERROR_CANCELLED):sink->itemResult;
    if(!result.deleted&&SUCCEEDED(result.error))result.error=E_FAIL;
    result.destination=sink->destination;return result;
}
inline DeleteResult recyclePath(HWND owner,const std::wstring& path) {return deletePath(owner,path,true);}
// The file itself on the clipboard, so Explorer can paste it. Cutting adds
// the Preferred DropEffect (move); without it pasting copies.
inline bool putFilesOnClipboard(HWND owner,const std::wstring& path,bool cut) {
    std::wstring list=path;list.push_back(L'\0');list.push_back(L'\0');
    const SIZE_T bytes=sizeof(DROPFILES)+list.size()*sizeof(wchar_t);
    HGLOBAL drop=::GlobalAlloc(GMEM_MOVEABLE,bytes);
    // The preferred effect is written for both copy and cut: Explorer only
    // treats an absent effect as a copy, and being explicit is testable.
    HGLOBAL effect=::GlobalAlloc(GMEM_MOVEABLE,sizeof(DWORD));
    if(!drop||!effect){if(drop)::GlobalFree(drop);if(effect)::GlobalFree(effect);return false;}
    if(auto* header=static_cast<DROPFILES*>(::GlobalLock(drop))) {
        header->pFiles=sizeof(DROPFILES);header->fWide=TRUE;
        std::memcpy(reinterpret_cast<char*>(header)+sizeof(DROPFILES),list.c_str(),list.size()*sizeof(wchar_t));
        ::GlobalUnlock(drop);
    }
    if(effect)if(auto* value=static_cast<DWORD*>(::GlobalLock(effect))) {
        *value=cut?static_cast<DWORD>(DROPEFFECT_MOVE):static_cast<DWORD>(DROPEFFECT_COPY);
        ::GlobalUnlock(effect);
    }
    if(!::OpenClipboard(owner)) {::GlobalFree(drop);if(effect)::GlobalFree(effect);return false;}
    bool ok=false;
    ::EmptyClipboard();
    if(::SetClipboardData(CF_HDROP,drop))ok=true;else ::GlobalFree(drop);
    if(effect) {
        const UINT format=::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        if(!(ok&&format&&::SetClipboardData(format,effect)))::GlobalFree(effect);
    }
    ::CloseClipboard();
    return ok;
}
inline bool removeSnapshotItem(ScanResult& result,const std::wstring& path) {
    if(!sameOrBelow(path,result.root.name)||lower(path)==lower(result.root.name))return false;
    const auto walk=[&](const auto& self,Node& n,const std::wstring& base)->bool {
        for(auto it=n.children.begin();it!=n.children.end();++it){const auto full=childPath(base,it->name);if(lower(full)==lower(path)){n.skippedChildren-=it->skippedChildren;n.children.erase(it);return true;}if(it->dir&&sameOrBelow(path,full)){const int before=it->skippedChildren;const bool removed=self(self,*it,full);n.skippedChildren-=before-it->skippedChildren;return removed;}}return false;
    };
    if(!walk(walk,result.root,result.root.name))return false;
    parallel_detail::summarize(result.root,true);parallel_detail::rank(result);result.files=result.root.files;result.dirs=result.root.dirs;return true;
}
inline void recycleSelected(ViewerState& s) {
    if(!commandEnabled(s,Command::Recycle))return;
    const auto path=s.selectedPath;
    const auto warning=path+L"\r\n\r\n"+sizeLabel(s,*s.selected)+(s.spec.zh?L"（扫描时）\r\n移至系统回收站？可在回收站还原。":L" (at scan time)\r\nMove to the system Recycle Bin?");
    if(::MessageBoxW(s.hwnd,warning.c_str(),s.spec.zh?L"确认移至回收站":L"Move to Recycle Bin",MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2)!=IDOK)return;
    if(!commandEnabled(s,Command::Recycle)||s.selectedPath!=path)return;
    const auto original=s.completeResult;
    const auto result=recyclePath(s.hwnd,path);
    if(!result.recycled){showTools(s,1,(s.spec.zh?L"未确认回收成功，统计保持不变；可重新扫描核对。\r\n":L"Recycle was not confirmed. Snapshot retained; rescan to verify.\r\n")+errorText(result.error));return;}
    if(original&&s.completeResult==original){auto updated=std::make_shared<ScanResult>(*original);if(removeSnapshotItem(*updated,path)){s.completeResult=updated;adoptResult(s,updated,false);}}
    setNotice(s,s.spec.zh?L"已移至回收站；还原后按 F5 重新扫描。":L"Moved to Recycle Bin. Rescan after restoring.");
    computeLayout(s);relayoutView(s);::InvalidateRect(s.hwnd,nullptr,FALSE);
}

// The permanent delete has no undo, so confirming takes two steps: a warning,
// then the item's own name typed back. The dialog is small and modal.
struct ConfirmState {ViewerState* s;std::wstring expected;bool confirmed=false;HBRUSH brush=nullptr;};
inline LRESULT CALLBACK confirmProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* ctx=reinterpret_cast<ConfirmState*>(::GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    switch(msg) {
    case WM_COMMAND:
        if(LOWORD(wp)==1&&ctx) {
            wchar_t typed[256]{};
            ::GetDlgItemTextW(hwnd,101,typed,256);
            if(lower(typed)==lower(ctx->expected)) {ctx->confirmed=true;::DestroyWindow(hwnd);}
            else if(HWND hint=::GetDlgItem(hwnd,104))::SetWindowTextW(hint,ctx->s->spec.zh?L"名称不一致；请照原样输入。":L"The name does not match; type it exactly.");
            return 0;
        }
        if(LOWORD(wp)==2){::DestroyWindow(hwnd);return 0;}
        break;
    case WM_CLOSE: ::DestroyWindow(hwnd);return 0;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:
        if(ctx&&ctx->s) {
            if(msg==WM_CTLCOLORSTATIC&&HWND(::GetDlgItem(hwnd,100))==reinterpret_cast<HWND>(lp))
                ::SetTextColor(reinterpret_cast<HDC>(wp),ctx->s->palette.danger);
            else ::SetTextColor(reinterpret_cast<HDC>(wp),ctx->s->palette.text);
            ::SetBkColor(reinterpret_cast<HDC>(wp),ctx->s->palette.panel);
            if(ctx->brush)::DeleteObject(ctx->brush);
            ctx->brush=::CreateSolidBrush(ctx->s->palette.panel);
            return reinterpret_cast<LRESULT>(ctx->brush);
        }
        break;
    case WM_DESTROY:
        if(ctx&&ctx->brush){::DeleteObject(ctx->brush);ctx->brush=nullptr;}
        return 0;
    }
    return ::DefWindowProcW(hwnd,msg,wp,lp);
}
inline bool confirmPermanentDelete(ViewerState& s,const std::wstring& path) {
    const std::wstring name=fileNameOf(path);
    ConfirmState ctx{&s,s.spec.zh?name:L"delete "+name,false};
    WNDCLASSW wc{};
    wc.lpfnWndProc=confirmProc;wc.hInstance=::GetModuleHandleW(nullptr);
    wc.hCursor=::LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=HBRUSH(COLOR_WINDOW+1);
    wc.lpszClassName=L"DiskViewConfirm";::RegisterClassW(&wc);
    const int w=S(440,s),h=S(210,s);
    const auto dialog=::CreateWindowExW(0,wc.lpszClassName,s.spec.zh?L"永久删除":L"Delete permanently",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,w,h,s.hwnd,nullptr,wc.hInstance,nullptr);
    if(!dialog)return false;
    ::SetWindowLongPtrW(dialog,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&ctx));
    const auto child=[&](const wchar_t* type,const std::wstring& label,DWORD style,int id,int x,int y,int cw,int ch) {
        auto* c=::CreateWindowExW(0,type,label.c_str(),WS_CHILD|WS_VISIBLE|style,S(x,s),S(y,s),S(cw,s),S(ch,s),
            dialog,reinterpret_cast<HMENU>(INT_PTR(id)),wc.hInstance,nullptr);
        ::SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(s.font),TRUE);return c;
    };
    child(L"STATIC",s.spec.zh?L"此操作不可恢复，条目不会进入回收站。":L"This cannot be undone; the item will not go to the Recycle Bin.",0,100,12,12,w-24,20);
    child(L"STATIC",path,0,102,12,S(36,s),w-24,20);
    child(L"STATIC",s.spec.zh?L"输入「"+name+L"」以确认：":L"Type \""+name+L"\" to confirm:",0,103,12,S(60,s),w-24,20);
    child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,101,12,S(82,s),w-24-S(100,s),24);
    child(L"BUTTON",s.spec.zh?L"永久删除":L"Delete",WS_TABSTOP|BS_DEFPUSHBUTTON,1,w-S(206,s),S(80,s),S(92,s),26);
    child(L"BUTTON",s.spec.zh?L"取消":L"Cancel",WS_TABSTOP,2,w-S(106,s),S(80,s),S(92,s),26);
    child(L"STATIC",L"",0,104,12,S(112,s),w-24,20);
    RECT owner{},self{};::GetWindowRect(s.hwnd,&owner);
    ::GetWindowRect(dialog,&self);
    ::SetWindowPos(dialog,nullptr,(owner.left+owner.right-self.right+self.left)/2,(owner.top+owner.bottom-self.bottom+self.top)/2,0,0,SWP_NOZORDER|SWP_NOSIZE);
    ::EnableWindow(s.hwnd,FALSE);
    ::ShowWindow(dialog,SW_SHOW);
    MSG msg;
    while(::GetMessageW(&msg,nullptr,0,0)>0) {
        if(msg.message==WM_QUIT){::PostQuitMessage(int(msg.wParam));break;}
        ::TranslateMessage(&msg);::DispatchMessageW(&msg);
        if(!::IsWindow(dialog))break;
    }
    ::EnableWindow(s.hwnd,TRUE);
    if(::IsWindow(dialog))::DestroyWindow(dialog);
    ::SetFocus(s.hwnd);
    return ctx.confirmed;
}
inline void deletePermanentlySelected(ViewerState& s) {
    if(!commandEnabled(s,Command::DeletePermanently))return;
    const auto path=s.selectedPath;
    const auto warning=path+L"\r\n\r\n"+sizeLabel(s,*s.selected)+(s.spec.zh?L"（扫描时）\r\n此操作不可恢复，条目不会进入回收站。":L" (at scan time)\r\nThis cannot be undone; the item will not go to the Recycle Bin.");
    if(::MessageBoxW(s.hwnd,warning.c_str(),s.spec.zh?L"永久删除":L"Delete permanently",MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2)!=IDOK)return;
    if(!confirmPermanentDelete(s,path))return;
    if(!commandEnabled(s,Command::DeletePermanently)||s.selectedPath!=path)return;
    const auto original=s.completeResult;
    const auto result=deletePath(s.hwnd,path,false);
    if(!result.deleted){showTools(s,1,(s.spec.zh?L"未确认删除成功，统计保持不变；可重新扫描核对。\r\n":L"The delete was not confirmed. Snapshot retained; rescan to verify.\r\n")+errorText(result.error));return;}
    if(original&&s.completeResult==original){auto updated=std::make_shared<ScanResult>(*original);if(removeSnapshotItem(*updated,path)){s.completeResult=updated;adoptResult(s,updated,false);}}
    setNotice(s,s.spec.zh?L"已永久删除。":L"Deleted permanently.");
    computeLayout(s);relayoutView(s);::InvalidateRect(s.hwnd,nullptr,FALSE);
}
inline void fillCommandList(ViewerState& s) {
    if(!s.toolList)return;const auto query=lower(editText(s.toolInput));::SendMessageW(s.toolList,LB_RESETCONTENT,0,0);
    for(const auto& item:commandItems()) {
        std::wstring label=s.spec.zh?item.zh:item.en;if(*item.key)label+=L"  "+std::wstring(item.key);
        if(!query.empty()&&lower(label).find(query)==std::wstring::npos)continue;
        if(!commandEnabled(s,item.id))continue;
        const auto index=::SendMessageW(s.toolList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));::SendMessageW(s.toolList,LB_SETITEMDATA,index,UINT(item.id));
    }
    ::SendMessageW(s.toolList,LB_SETCURSEL,0,0);
}
inline int toolScale(const ViewerState& s,int px) {return ::MulDiv(px,s.toolDpi,96);}
inline void layoutTools(ViewerState& s,HWND hwnd) {
    RECT r{};::GetClientRect(hwnd,&r);const int w=r.right,h=r.bottom;
    const auto scale=[&](int px){return toolScale(s,px);};
    const auto move=[&](int id,int x,int y,int width,int height){if(auto c=::GetDlgItem(hwnd,id))::MoveWindow(c,x,y,std::max(1,width),std::max(1,height),TRUE);};
    const int pad=scale(12),content=w-pad*2;
    if(s.toolPage==3){move(401,pad,pad,content,scale(30));move(402,pad,scale(52),content,h-scale(108));}
    else if(s.toolPage==4) {
        move(420,pad,pad,content,scale(24));move(410,pad,scale(38),content,scale(30));
        for(int i=0;i<4;++i){move(421+i,pad,scale(88+i*48),scale(192),scale(24));move(411+i,scale(210),scale(85+i*48),w-scale(222),scale(220));}
        move(425,pad,scale(280),content,scale(108));move(426,pad,scale(395),content,scale(24));
        move(415,pad,scale(424),content,h-scale(480));
    } else move(401,pad,pad,content,h-scale(68));
    move(IDOK,w-scale(208),h-scale(42),scale(94),scale(30));move(IDCANCEL,w-scale(108),h-scale(42),scale(94),scale(30));
}
inline void updateToolFont(ViewerState& s) {
    const auto old=s.toolFont;
    s.toolFont=::CreateFontW(-toolScale(s,14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(auto child=::GetWindow(s.toolWindow,GW_CHILD);child;child=::GetWindow(child,GW_HWNDNEXT))::SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(s.toolFont),TRUE);
    if(old)::DeleteObject(old);
}
inline std::vector<std::wstring> exclusionLines(const std::wstring& text) {
    std::vector<std::wstring> result;
    for(std::size_t begin=0;begin<text.size();) {
        auto end=text.find_first_of(L"\r\n",begin);if(end==std::wstring::npos)end=text.size();
        auto value=text.substr(begin,end-begin);const auto first=value.find_first_not_of(L" \t"),last=value.find_last_not_of(L" \t");
        if(first!=std::wstring::npos){value=normalizePath(value.substr(first,last-first+1),nullptr);if(!value.empty()&&std::none_of(result.begin(),result.end(),[&](const auto& p){return lower(p)==lower(value);}))result.push_back(value);}
        begin=end+1;
    }
    return result;
}
inline bool toolMessage(ViewerState& s,const MSG& msg) {
    if(s.toolPage==3&&msg.hwnd==s.toolInput&&msg.message==WM_KEYDOWN&&(msg.wParam==VK_UP||msg.wParam==VK_DOWN)) {
        const auto count=::SendMessageW(s.toolList,LB_GETCOUNT,0,0),selected=::SendMessageW(s.toolList,LB_GETCURSEL,0,0);
        if(count>0)::SendMessageW(s.toolList,LB_SETCURSEL,std::clamp<LRESULT>(selected+(msg.wParam==VK_DOWN?1:-1),0,count-1),0);
        return true;
    }
    return false;
}
inline LRESULT CALLBACK toolProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto& s=state();
    switch(msg) {
    case DM_GETDEFID:return MAKELONG(IDOK,DC_HASDEFID);
    case WM_SIZE:layoutTools(s,hwnd);return 0;
    case WM_GETMINMAXINFO:{auto* limits=reinterpret_cast<MINMAXINFO*>(lp);limits->ptMinTrackSize={toolScale(s,s.toolPage==4?600:380),toolScale(s,s.toolPage==4?600:300)};return 0;}
    case WM_DPICHANGED:{s.toolDpi=HIWORD(wp);updateToolFont(s);const auto* r=reinterpret_cast<RECT*>(lp);::SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layoutTools(s,hwnd);return 0;}
    case WM_PRINTCLIENT:case WM_ERASEBKGND:{RECT r{};::GetClientRect(hwnd,&r);fillWith(reinterpret_cast<HDC>(wp),r,s.palette.panel);return 1;}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX:{
        if(s.toolBrush)::DeleteObject(s.toolBrush);s.toolBrush=::CreateSolidBrush(s.palette.panel);
        ::SetTextColor(reinterpret_cast<HDC>(wp),s.palette.text);::SetBkColor(reinterpret_cast<HDC>(wp),s.palette.panel);return reinterpret_cast<LRESULT>(s.toolBrush);}
    case WM_COMMAND:
        if(LOWORD(wp)==IDCANCEL){::DestroyWindow(hwnd);return 0;}
        if(s.toolPage==3) {
            if(HIWORD(wp)==EN_CHANGE){fillCommandList(s);return 0;}
            if(LOWORD(wp)==IDOK||HIWORD(wp)==LBN_DBLCLK){const auto selected=::SendMessageW(s.toolList,LB_GETCURSEL,0,0);if(selected!=LB_ERR){const auto command=::SendMessageW(s.toolList,LB_GETITEMDATA,selected,0);::DestroyWindow(hwnd);performCommand(s,static_cast<Command>(command));}return 0;}
        }
        if(s.toolPage==4&&LOWORD(wp)==IDOK) {
            if(s.scanning)return 0;
            const auto path=editText(::GetDlgItem(hwnd,410));
            s.scanMode=int(::SendDlgItemMessageW(hwnd,411,CB_GETCURSEL,0,0));
            const int worker=int(::SendDlgItemMessageW(hwnd,412,CB_GETCURSEL,0,0));const unsigned limits[]={0,1,2,4,8};s.scanOptions.workers=limits[std::clamp(worker,0,4)];
            s.scanOptions.media=static_cast<ScanOptions::Media>(::SendDlgItemMessageW(hwnd,413,CB_GETCURSEL,0,0));
            s.scanOptions.previewIntervalMs=::SendDlgItemMessageW(hwnd,414,CB_GETCURSEL,0,0)==0?1500:5000;
            s.scanOptions.excludedPaths=exclusionLines(editText(::GetDlgItem(hwnd,415)));
            ::DestroyWindow(hwnd);
            // Persist the chosen settings now: a scan that fails or is cancelled
            // must not silently discard what the user picked.
            saveConfigNow(s);
            startScan(s,path);return 0;
        }
        if(LOWORD(wp)==IDOK){::DestroyWindow(hwnd);return 0;}break;
    case WM_CLOSE: ::DestroyWindow(hwnd);return 0;
    case WM_DESTROY:s.toolWindow=s.toolInput=s.toolList=nullptr;if(s.toolFont){::DeleteObject(s.toolFont);s.toolFont=nullptr;}if(s.toolBrush){::DeleteObject(s.toolBrush);s.toolBrush=nullptr;}::SetFocus(s.hwnd);return 0;
    }
    return ::DefWindowProcW(hwnd,msg,wp,lp);
}
inline void showTools(ViewerState& s,int page,const std::wstring& text) {
    if(s.toolWindow)::DestroyWindow(s.toolWindow);s.toolPage=page;s.toolDpi=s.dpi;clearHover(s);
    WNDCLASSW wc{};wc.lpfnWndProc=toolProc;wc.hInstance=::GetModuleHandleW(nullptr);wc.hCursor=::LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=HBRUSH(COLOR_WINDOW+1);wc.lpszClassName=L"DiskViewTools";::RegisterClassW(&wc);
    const auto title=page==4?(s.spec.zh?L"扫描设置":L"Scan settings"):page==3?(s.spec.zh?L"全部操作":L"All actions"):page==2?(s.spec.zh?L"快捷键与焦点":L"Keyboard shortcuts"):(s.spec.zh?L"Disk View · 信息":L"Disk View · Information");
    s.toolWindow=::CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,title,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,CW_USEDEFAULT,CW_USEDEFAULT,S(620,s),S(page==4?600:500,s),s.hwnd,nullptr,wc.hInstance,nullptr);
    const auto window=s.toolWindow;if(!window)return;
    const auto child=[&](const wchar_t* type,const std::wstring& label,DWORD style,int id,int x,int y,int w,int h){const auto c=::CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,label.c_str(),WS_CHILD|WS_VISIBLE|style,S(x,s),S(y,s),S(w,s),S(h,s),window,reinterpret_cast<HMENU>(INT_PTR(id)),wc.hInstance,nullptr);::SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(s.font),TRUE);return c;};
    if(page==3) {
        s.toolInput=child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,401,12,12,560,30);
        s.toolList=child(L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,402,12,52,560,320);fillCommandList(s);
    } else if(page==4) {
        child(L"STATIC",s.spec.zh?L"扫描范围（可编辑路径）":L"Scan path",0,420,12,12,550,24);
        s.toolInput=child(L"EDIT",s.toolPath.empty()?s.lastPath:s.toolPath,WS_TABSTOP|ES_AUTOHSCROLL,410,12,38,560,30);
        const auto combo=[&](int id,int y,const std::wstring& label,const std::vector<std::wstring>& choices,int selected){child(L"STATIC",label,0,id+10,12,y,200,24);auto c=child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,id,210,y-3,360,220);for(const auto& option:choices)::SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option.c_str()));::SendMessageW(c,CB_SETCURSEL,selected,0);};
        combo(411,88,s.spec.zh?L"扫描方式":L"Scan mode",{s.spec.zh?L"自动（推荐）":L"Automatic",L"NTFS / MFT",s.spec.zh?L"普通扫描":L"Ordinary"},s.scanMode);
        const unsigned limits[]={0,1,2,4,8};const int wi=int(std::find(std::begin(limits),std::end(limits),s.scanOptions.workers)-std::begin(limits));
        combo(412,136,s.spec.zh?L"普通扫描并发上限":L"Ordinary worker limit",{s.spec.zh?L"自动":L"Automatic",L"1",L"2",L"4",L"8"},std::min(4,wi));
        combo(413,184,s.spec.zh?L"介质策略":L"Media strategy",{s.spec.zh?L"自动检测":L"Detect device",L"SSD / NVMe",s.spec.zh?L"机械硬盘":L"HDD",s.spec.zh?L"网络目录":L"Network"},int(s.scanOptions.media));
        combo(414,232,s.spec.zh?L"预览发布":L"Preview publishing",{s.spec.zh?L"平衡":L"Balanced",s.spec.zh?L"更少更新":L"Less frequent"},s.scanOptions.previewIntervalMs>1500?1:0);
        child(L"STATIC",s.spec.zh?L"NTFS 整盘可使用 MFT。仅扫描助手按需申请管理员权限，拒绝授权自动回退普通扫描。主界面保持普通权限。\r\n\r\n并发越高不一定越快；自动从 SSD 4、机械盘 1、网络 2 开始，未知设备从 1 开始。":L"NTFS whole volumes can use MFT. Only the helper requests elevation; declining falls back to ordinary scanning.\r\n\r\nMore workers may not be faster. Automatic starts at SSD 4, HDD 1, network 2, unknown 1.",0,425,12,280,560,108);
        child(L"STATIC",s.spec.zh?L"下轮排除（每行一个完整路径；留空清除）":L"Next-scan exclusions (one full path per line; empty clears)",0,426,12,395,560,24);
        std::wstring exclusions;for(const auto& p:s.scanOptions.excludedPaths)exclusions+=p+L"\r\n";
        child(L"EDIT",exclusions,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,415,12,424,560,70);
    } else {
        s.toolInput=child(L"EDIT",text,WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,401,12,12,560,350);
    }
    child(L"BUTTON",page==4?(s.spec.zh?L"开始扫描":L"Start"):page==3?(s.spec.zh?L"执行":L"Run"):(s.spec.zh?L"完成":L"Done"),WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,350,420,94,30);
    child(L"BUTTON",s.spec.zh?L"关闭":L"Close",WS_TABSTOP,IDCANCEL,454,420,94,30);
    updateToolFont(s);layoutTools(s,window);
    ::SendMessageW(window,WM_SETICON,ICON_SMALL,::SendMessageW(s.hwnd,WM_GETICON,ICON_SMALL,0));
    ::ShowWindow(window,SW_SHOW);::SetFocus(s.toolInput);
}
inline void performCommand(ViewerState& s,Command command) {
    if(!commandEnabled(s,command))return;
    const auto path=actionPath(s);
    switch(command) {
    case Command::Parent:goUp(s);break;
    case Command::Ancestors:showAncestors(s);break;
    case Command::Enter:if(s.selected->dir)navigateTo(s,path);else if(reinterpret_cast<INT_PTR>(::ShellExecuteW(s.hwnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)showTools(s,1,s.spec.zh?L"无法使用默认程序打开此文件。":L"Unable to open with the default application.");break;
    case Command::Locate:locateSelected(s);s.focusList=true;s.mobileList=true;computeLayout(s);relayoutView(s);ensureSelectedVisible(s);break;
    case Command::Copy:setNotice(s,copyText(s.hwnd,path)?(s.spec.zh?L"已复制完整路径。":L"Path copied."):(s.spec.zh?L"无法打开剪贴板，请重试。":L"Clipboard unavailable."));break;
    case Command::CopyFile:case Command::CutFile:
        setNotice(s,putFilesOnClipboard(s.hwnd,path,command==Command::CutFile)
            ?(command==Command::CutFile?(s.spec.zh?L"已剪切，到资源管理器粘贴即移动。":L"Cut. Paste in Explorer to move.")
                                       :(s.spec.zh?L"已复制，到资源管理器粘贴即复制。":L"Copied. Paste in Explorer to copy."))
            :(s.spec.zh?L"无法写入剪贴板，请重试。":L"Clipboard unavailable."));
        break;
    case Command::Explorer:{PIDLIST_ABSOLUTE full=nullptr;auto hr=::SHParseDisplayName(path.c_str(),nullptr,&full,0,nullptr);if(SUCCEEDED(hr)){auto parent=::ILCloneFull(full);if(parent){::ILRemoveLastID(parent);PCUITEMID_CHILD child=::ILFindLastID(full);hr=::SHOpenFolderAndSelectItems(parent,1,&child,0);::CoTaskMemFree(parent);}::CoTaskMemFree(full);}if(FAILED(hr))showTools(s,1,errorText(hr));break;}
    case Command::Properties:{SHELLEXECUTEINFOW info{sizeof info};info.fMask=SEE_MASK_INVOKEIDLIST;info.hwnd=s.hwnd;info.lpVerb=L"properties";info.lpFile=path.c_str();info.nShow=SW_SHOWNORMAL;if(!::ShellExecuteExW(&info))showTools(s,1,errorText(HRESULT_FROM_WIN32(::GetLastError())));break;}
    case Command::Rescan:{const auto slash=path.find_last_of(L'\\');s.toolPath=s.selected&&!s.selected->dir?(slash==2?path.substr(0,3):path.substr(0,slash)):path;showTools(s,4);break;}
    case Command::Exclude:{auto& paths=s.scanOptions.excludedPaths;auto it=std::find_if(paths.begin(),paths.end(),[&](const auto& p){return lower(p)==lower(path);});if(it==paths.end())paths.push_back(path);else paths.erase(it);saveConfigNow(s);setNotice(s,s.spec.zh?L"已更新下轮排除范围，当前统计保持不变。":L"Next-scan exclusions updated; current snapshot unchanged.");break;}
    case Command::Recycle:recycleSelected(s);break;
    case Command::DeletePermanently:deletePermanentlySelected(s);break;
    case Command::Commands:showTools(s,3);break;
    case Command::FocusChart:focusPane(s,false);break;
    case Command::FocusList:focusPane(s,true);break;
    case Command::Fit:chartZoom(s,0,true);break;
    case Command::Issues:showIssues(s);break;
    case Command::Accounting:showTools(s,1,s.spec.zh?L"图表使用逻辑大小（1024 进制）。硬链接按路径计数，目录重解析点不跟随。\r\n\r\n按簇取整只是近似占用，不等于稀疏、压缩文件的实际占用。文件总计可能不含无法读取区域或系统元数据，因此不等于磁盘已用空间。\r\n\r\n局部结果占比只以当前已发现内容计算。此窗口不阻塞主窗口导航。":L"Charts use logical bytes (base 1024). Hard links count per path; directory reparse points are not followed.\r\n\r\nCluster-rounded allocation is approximate. Unreadable areas and system metadata may be missing. Partial shares only cover discovered entries.");break;
    case Command::Diagnostics:{const auto r=s.resultIncomplete?s.partialResult:s.completeResult;std::wstring text=s.spec.zh?L"阶段计时（毫秒）\r\n":L"Stage timings (milliseconds)\r\n";if(r)text+=L"Total: "+std::to_wstring(r->elapsedMs)+L"\r\nWorkers: "+std::to_wstring(r->workers)+L"\r\nWorker time (sum, includes pause): "+std::to_wstring(r->workerMs)+L"\r\nMerge: "+std::to_wstring(r->mergeMs)+L"\r\nPreview: "+std::to_wstring(r->snapshotMs);text+=s.spec.zh?L"\r\n\r\n并发累计工作时间不等于总耗时，不可相加比较。MFT 读取方式保持不变。":L"\r\n\r\nWorker times overlap and cannot be added to elapsed time. MFT reading is unchanged.";showTools(s,1,text);break;}
    case Command::Help:showTools(s,2,s.spec.zh?L"图表 ↑↓←→：选择邻近条目\r\n列表 ↑↓：上一项 / 下一项\r\n列表 ← / →：返回父级 / 进入目录\r\nEnter：进入目录 / 打开文件\r\nAlt+↑ / Backspace：返回父级\r\nAlt+← / Alt+→：后退 / 前进\r\nF6 / Shift+F6：切换图表和列表\r\nShift+F10 / 菜单键：右键菜单\r\nCtrl+L：编辑路径\r\nCtrl+F：筛选列表\r\nCtrl+K：搜索操作\r\nCtrl+C：复制完整路径\r\nCtrl+Shift+C：复制文件\r\nCtrl+Shift+X：剪切文件\r\nAlt+Enter：属性\r\nF5：扫描设置\r\n+ / − / 0：缩放 / 适合窗口\r\nCtrl+滚轮：图表缩放\r\n空格+拖动 / 中键拖动：图表平移\r\nDelete：确认移至回收站\r\nEsc：关闭工具或退出编辑\r\n\r\n输入框里的方向键、退格和 Ctrl+C 用于编辑文字。\r\n永久删除没有快捷键：在右键菜单里选择，并输入条目名确认。":L"Chart arrows: nearest item\r\nList Up/Down: previous/next\r\nList Left/Right: parent/enter\r\nEnter: enter/open\r\nAlt+Up / Backspace: parent\r\nAlt+Left/Right: history\r\nF6 / Shift+F6: chart/list focus\r\nShift+F10: context menu\r\nCtrl+L: address\r\nCtrl+F: filter\r\nCtrl+K: commands\r\nCtrl+C: copy path\r\nCtrl+Shift+C: copy item\r\nCtrl+Shift+X: cut item\r\nAlt+Enter: properties\r\nF5: scan settings\r\n+ / - / 0: zoom/fit\r\nCtrl+wheel: zoom\r\nSpace+drag / middle drag: pan\r\nDelete: recycle confirmation\r\nEsc: close/exit editing\r\n\r\nText inputs keep native editing shortcuts.\r\nPermanent delete has no shortcut: pick it in the context menu and confirm by typing the name.");break;
    }
    if(s.hwnd){computeLayout(s);::InvalidateRect(s.hwnd,nullptr,FALSE);}
}
