// Included by the test translation unit, which provides TempDir and CHECK.
static void testParallelScan() {
    using namespace diskview;
    TempDir tmp;
    for(int d=0;d<16;++d){auto dir=tmp.path()/std::to_wstring(d);fs::create_directories(dir/L"nested");for(int f=0;f<8;++f)writeFile(dir,std::to_wstring(f),100+d*8+f);writeFile(dir/L"nested",L"unicode-文件",42);}
    std::atomic<bool> cancel=false;ScanResult serial;scanPathSerial(tmp.path().wstring(),{},cancel,&serial);
    for(unsigned workers:{1u,2u,4u,8u}) {
        ScanOptions options;options.workers=workers;ScanResult actual;scanPath(tmp.path().wstring(),{},cancel,&actual,{},options);
        CHECK(actual.error.empty()&&!actual.cancelled);CHECK(actual.files==serial.files);CHECK(actual.dirs==serial.dirs);
        CHECK(actual.root.logical==serial.root.logical);CHECK(actual.root.allocApprox==serial.root.allocApprox);CHECK(actual.largest.size()==serial.largest.size());
        CHECK(actual.workers==workers);
    }
    ScanOptions excluded;excluded.workers=4;excluded.excludedPaths={ (tmp.path()/L"0").wstring() };ScanResult pruned;
    scanPath(tmp.path().wstring(),{},cancel,&pruned,{},excluded);CHECK(!findChild(pruned.root,L"0"));CHECK(findChild(pruned.root,L"10"));CHECK(pruned.excludedItems==1);CHECK(pruned.files==serial.files-9);
    CHECK(sameOrBelow(L"C:\\folder\\nested",L"c:\\folder"));CHECK(!sameOrBelow(L"C:\\folder-other",L"C:\\folder"));
    CHECK(!sameOrBelow(L"C:\\folder",L""));CHECK(!sameOrBelow(L"",L""));
    excluded.excludedPaths={tmp.path().wstring()};scanPath(tmp.path().wstring(),{},cancel,&pruned,{},excluded);CHECK(pruned.files==0&&pruned.excludedItems==1);
    std::atomic<bool> paused=true,finished=false;ScanOptions options;options.workers=4;options.paused=&paused;ScanResult pausedResult;
    std::thread worker([&]{scanPath(tmp.path().wstring(),{},cancel,&pausedResult,{},options);finished=true;});
    ::Sleep(40);CHECK(!finished);paused=false;worker.join();CHECK(finished&&pausedResult.files==serial.files);
    paused=true;cancel=false;finished=false;
    std::thread cancelling([&]{scanPath(tmp.path().wstring(),{},cancel,&pausedResult,{},options);finished=true;});
    ::Sleep(25);const auto tick=::GetTickCount64();cancel=true;cancelling.join();CHECK(pausedResult.cancelled);CHECK(::GetTickCount64()-tick<2000);
    cancel=false;bool threw=false;
    try{scanPath(tmp.path().wstring(),[](const auto&)->bool{throw std::runtime_error("test callback");},cancel,&pausedResult);}catch(const std::runtime_error&){threw=true;}CHECK(threw);
}
static void testPendingMftRecords() {
    using namespace diskview;
    std::vector<mft::Entry> entries={{5,0,0,FILE_ATTRIBUTE_DIRECTORY,true,true,L""},{100,99,12,0,false,true,L"late.txt"}};
    ScanResult part,full;mft::build(L"C:\\",entries,4096,part,false);mft::build(L"C:\\",entries,4096,full,true);
    CHECK(part.pendingRecords==1&&part.orphanRecords==0);CHECK(full.pendingRecords==0&&full.orphanRecords==1);
    entries.push_back({99,5,0,FILE_ATTRIBUTE_DIRECTORY,true,true,L"parent"});mft::build(L"C:\\",entries,4096,full,true);
    CHECK(full.orphanRecords==0&&full.root.logical==12);
    ScanOptions options;options.excludedPaths={L"c:\\parent"};mft::build(L"C:\\",entries,4096,full,true,options);CHECK(full.root.logical==0&&full.excludedItems==1);
    entries.push_back({101,99,0,0,false,false,L"unknown.bin"});mft::build(L"C:\\",entries,4096,full,true,options);CHECK(full.unknownSizes==0&&full.orphanRecords==0&&full.skippedList.empty());
    options.excludedPaths={L"C:\\"};mft::build(L"C:\\",entries,4096,full,true,options);CHECK(full.files==0&&full.excludedItems==1);
    std::atomic<bool> cancel=true;mft::build(L"C:\\",entries,4096,full,true,{},&cancel);CHECK(full.cancelled);
}
static void testConfigRoundTrip() {
    using namespace diskview;
    TempDir tmp;
    const auto dir = winshun::utf8(tmp.path().wstring());
    // A missing file keeps the defaults: automatic mode, device-chosen workers.
    const auto empty = loadViewConfig(dir);
    CHECK(!empty.hasFile && empty.scanMode == 0 && empty.workers == 0 && empty.excludedPaths.empty());
    ViewConfig saved;
    saved.lastPath = L"D:\\工作";saved.scanMode = 2;saved.workers = 4;saved.media = 1;
    saved.previewIntervalMs = 5000;saved.excludedPaths = {L"D:\\工作\\node_modules", L"D:\\工作\\build"};saved.hasFile = true;
    saveViewConfig(dir, saved);
    const auto back = loadViewConfig(dir);
    CHECK(back.hasFile && back.lastPath == saved.lastPath && back.scanMode == 2 && back.workers == 4);
    CHECK(back.media == 1 && back.previewIntervalMs == 5000 && back.excludedPaths == saved.excludedPaths);
    // Out-of-range values from a hand-edited file are clamped, not trusted.
    {
        std::ofstream f(configPath(dir).c_str(), std::ios::binary | std::ios::trunc);
        f << R"({"path":"C:\\","scanMode":99,"workers":64,"media":-3,"previewIntervalMs":7,"excludedPaths":["C:\\x",42]})";
    }
    const auto clamped = loadViewConfig(dir);
    CHECK(clamped.scanMode == 2 && clamped.workers == 8 && clamped.media == 0 && clamped.previewIntervalMs == 1500);
    CHECK(clamped.excludedPaths.size() == 1 && clamped.excludedPaths.front() == L"C:\\x");
}
static void testCommandModel() {    using namespace diskview;using namespace diskview::detail;
    ViewerState s;s.tree=std::make_shared<Node>(makeDirOf(L"C:\\",{makeDirOf(L"one",{makeDir(L"two",{{L"a",10}})}),makeFile(L"b",20)}));s.viewRoot=s.tree.get();s.rcChart={0,0,500,400};s.rcTopList={500,0,800,400};
    CHECK(!commandEnabled(s,Command::Parent));CHECK(!commandEnabled(s,Command::Copy));CHECK(protectedPath(L"C:\\"));
    navigateTo(s,L"C:\\one\\two");goUp(s);CHECK(s.viewRoot->name==L"one"&&s.selected->name==L"two");
    CHECK(commandEnabled(s,Command::Parent)&&commandEnabled(s,Command::Copy));
    const auto menu=makeContextMenu(s);CHECK(::GetMenuState(menu,UINT(Command::Parent),MF_BYCOMMAND)!=UINT(-1));::DestroyMenu(menu);
    goUp(s);CHECK(s.selected->name==L"one");goUp(s);CHECK(s.viewRoot==s.tree.get());
    ScanResult r;r.root=*s.tree;r.displayRoot=r.root.name;
    CHECK(removeSnapshotItem(r,L"C:\\one\\two\\a"));CHECK(r.root.logical==20&&r.root.files==1);CHECK(!removeSnapshotItem(r,L"C:\\"));
    auto sink=Microsoft::WRL::Make<RecycleSink>();CHECK(FAILED(sink->PreDeleteItem(0,nullptr)));CHECK(SUCCEEDED(sink->PreDeleteItem(TSF_DELETE_RECYCLE_IF_POSSIBLE,nullptr)));
    sink->PostDeleteItem(0,nullptr,S_OK,nullptr);CHECK(sink->completed&&!sink->recycled);
    sink->PostDeleteItem(0,nullptr,E_ACCESSDENIED,nullptr);CHECK(!sink->recycled&&sink->itemResult==E_ACCESSDENIED);
    auto permanent=Microsoft::WRL::Make<RecycleSink>();permanent->requireRecycle=false;
    CHECK(SUCCEEDED(permanent->PreDeleteItem(0,nullptr)));
    // An incomplete snapshot no longer blocks the destructive commands; a
    // running scan or a protected path still does. protectedPath reads the
    // real filesystem, so the selection must point at a real temp file.
    s.resultIncomplete=true;
    {
        TempDir tmp;writeFile(tmp.path(),L"deletable.bin",5);
        const auto keep=s.selectedPath;
        s.selectedPath=tmp.path()/L"deletable.bin";
        CHECK(commandEnabled(s,Command::Recycle)&&commandEnabled(s,Command::DeletePermanently));
        s.resultIncomplete=false;s.scanning=true;
        CHECK(!commandEnabled(s,Command::Recycle)&&!commandEnabled(s,Command::DeletePermanently));
        s.scanning=false;
        CHECK(commandEnabled(s,Command::CopyFile)&&commandEnabled(s,Command::CutFile));
        s.selectedPath=keep;
    }
    s.selected=nullptr;
    CHECK(!commandEnabled(s,Command::Recycle)&&!commandEnabled(s,Command::CopyFile)&&!commandEnabled(s,Command::DeletePermanently));
    CHECK(exclusionLines(L"C:\\one\r\nc:\\one\r\n  C:\\two  \r\n").size()==2);
}
static int verifyRecycle() {
    using namespace diskview::detail;
    const auto com=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);CHECK(SUCCEEDED(com));
    {
        TempDir tmp;writeFile(tmp.path(),L"recycle-check.txt",37);
        fs::create_directories(tmp.path()/L"recycle-folder");writeFile(tmp.path()/L"recycle-folder",L"child.txt",59);
        for(const auto name:{L"recycle-check.txt",L"recycle-folder"}) {
            const auto path=tmp.path()/name;CHECK(!protectedPath(path.wstring()));
            const auto recycled=recyclePath(nullptr,path.wstring());
            std::printf("recycle: success=%d HRESULT=0x%08lx\n",recycled.recycled,static_cast<unsigned long>(recycled.error));
            CHECK(recycled.recycled&&recycled.destination&&!fs::exists(path));
            if(recycled.recycled&&recycled.destination) {
                Microsoft::WRL::ComPtr<IFileOperation> restore;Microsoft::WRL::ComPtr<IShellItem> parent;
                auto hr=::CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(restore.GetAddressOf()));
                if(SUCCEEDED(hr))hr=::SHCreateItemFromParsingName(tmp.path().c_str(),nullptr,IID_PPV_ARGS(parent.GetAddressOf()));
                if(SUCCEEDED(hr))hr=restore->SetOperationFlags(FOF_NOCONFIRMATION|FOF_NOERRORUI|FOFX_EARLYFAILURE);
                if(SUCCEEDED(hr))hr=restore->MoveItem(recycled.destination.Get(),parent.Get(),name,nullptr);
                if(SUCCEEDED(hr))hr=restore->PerformOperations();BOOL aborted=TRUE;if(SUCCEEDED(hr))hr=restore->GetAnyOperationsAborted(&aborted);
                CHECK(SUCCEEDED(hr)&&!aborted&&fs::exists(path));
                if(fs::exists(path))CHECK(fs::file_size(fs::is_directory(path)?path/L"child.txt":path)==(fs::is_directory(path)?59u:37u));
                std::printf("restore: success=%d HRESULT=0x%08lx\n",fs::exists(path),static_cast<unsigned long>(hr));
            }
        }
    }
    // A permanent delete removes the file for real and never reports it as
    // recycled; a protected path is refused before touching anything.
    {
        TempDir tmp;writeFile(tmp.path(),L"permanent-check.txt",73);
        const auto path=tmp.path()/L"permanent-check.txt";
        const auto result=deletePath(nullptr,path.wstring(),false);
        std::printf("permanent: deleted=%d HRESULT=0x%08lx\n",result.deleted,static_cast<unsigned long>(result.error));
        CHECK(result.deleted&&!fs::exists(path)&&!result.recycled);
        CHECK(deletePath(nullptr,L"C:\\Windows",false).error==E_ACCESSDENIED);
    }
    // The clipboard round-trip: CF_HDROP the shell can paste, with the
    // preferred effect telling copy from cut. Opt-in like the rest of this
    // harness because it replaces the user's clipboard.
    {
        TempDir tmp;writeFile(tmp.path(),L"clipboard-check.txt",11);
        const auto path=tmp.path()/L"clipboard-check.txt";
        for(const bool cut:{false,true}) {
            CHECK(putFilesOnClipboard(nullptr,path.wstring(),cut));
            const UINT effectFormat=::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            CHECK(::IsClipboardFormatAvailable(CF_HDROP));
            CHECK(effectFormat&&::IsClipboardFormatAvailable(effectFormat));
            CHECK(::OpenClipboard(nullptr));
            const auto drop=static_cast<HDROP>(::GetClipboardData(CF_HDROP));
            CHECK(drop);
            if(drop) {
                wchar_t name[MAX_PATH]{};
                CHECK(::DragQueryFileW(drop,0,name,MAX_PATH)>0);
                CHECK(lower(name)==lower(path.wstring()));
            }
            DWORD effect=0;
            if(effectFormat)if(const auto data=static_cast<const DWORD*>(::GetClipboardData(effectFormat)))effect=*data;
            ::CloseClipboard();
            std::printf("clipboard: cut=%d effect=%lu\n",cut,static_cast<unsigned long>(effect));
            CHECK(effect==(cut?DROPEFFECT_MOVE:DROPEFFECT_COPY));
        }
    }
    if(SUCCEEDED(com))::CoUninitialize();return g_failures?1:0;
}
static int benchmarkOrdinary(const std::wstring& path) {
    using namespace diskview;
    using Clock=std::chrono::steady_clock;
    std::atomic<bool> cancel=false;ScanResult baseline;const auto baselineStart=Clock::now();scanPathSerial(path,{},cancel,&baseline);
    std::printf("baseline_ms=%.3f files=%u dirs=%u bytes=%llu\n",std::chrono::duration<double,std::milli>(Clock::now()-baselineStart).count(),baseline.files,baseline.dirs,static_cast<unsigned long long>(baseline.root.logical));
    for(int repeat=0;repeat<3;++repeat)for(unsigned workers:{1u,2u,4u,8u}) {
        ScanOptions options;options.workers=workers;ScanResult r;std::uint64_t firstPreview=0;const auto start=::GetTickCount64();const auto wallStart=Clock::now();
        scanPath(path,{},cancel,&r,[&](auto){if(!firstPreview)firstPreview=::GetTickCount64()-start;},options);
        CHECK(r.error.empty()&&r.files==baseline.files&&r.dirs==baseline.dirs&&r.root.logical==baseline.root.logical&&r.root.allocApprox==baseline.root.allocApprox);
        std::printf("repeat=%d workers=%u elapsed_ms=%.3f worker_sum_ms=%llu merge_ms=%llu preview_ms=%llu first_preview_ms=%llu\n",repeat+1,workers,std::chrono::duration<double,std::milli>(Clock::now()-wallStart).count(),static_cast<unsigned long long>(r.workerMs),static_cast<unsigned long long>(r.mergeMs),static_cast<unsigned long long>(r.snapshotMs),static_cast<unsigned long long>(firstPreview));
    }
    return g_failures?1:0;
}
static void testNewWindowControls() {
    using namespace diskview;using namespace diskview::detail;
    auto& s=state();
    // Runs inside testViewerMessages while its hidden real window exists.
    s.dpi=96;s.spec.zh=true;s.palette=lightPalette();s.notice.clear();s.scanning=false;s.mobileList=false;s.depth=1;s.focusList=false;
    ::SetWindowPos(s.hwnd,nullptr,0,0,1200,940,SWP_NOMOVE|SWP_NOZORDER);makeFonts(s);
    navigateTo(s,L"C:\\Projects\\Disk View");computeLayout(s);relayoutView(s);
    CHECK(s.rcParentRow.right>s.rcParentRow.left);
    activateAt(s,s.rcParent.left+4,s.rcParent.top+4);CHECK(s.viewRoot->name==L"Projects"&&s.selected->name==L"Disk View");
    ::SendMessageW(s.hwnd,WM_KEYDOWN,VK_BACK,0);CHECK(s.viewRoot==s.tree.get()&&s.selected->name==L"Projects");
    navigateTo(s,L"C:\\Projects\\Disk View");activateAt(s,s.rcParentRow.left+4,s.rcParentRow.top+4);CHECK(s.viewRoot->name==L"Projects");
    navigateTo(s,L"C:\\Projects\\Disk View");setViewMode(s,true);
    const int cx=(s.rcChart.left+s.rcChart.right)/2,cy=(s.rcChart.top+s.rcChart.bottom)/2;
    CHECK(hitCenter(s,cx,cy));activateAt(s,cx,cy);CHECK(s.viewRoot->name==L"Projects");
    navigateTo(s,L"C:\\");setViewMode(s,false);s.selected=nullptr;directionSelect(s,VK_RIGHT);CHECK(s.selected!=nullptr);
    const auto first=s.selected;directionSelect(s,VK_RIGHT);CHECK(s.selected!=nullptr);(void)first;
    focusPane(s,true);CHECK(s.focusList);::SendMessageW(s.hwnd,WM_KEYDOWN,VK_F6,0);CHECK(!s.focusList);
    chartZoom(s,1);CHECK(s.zoom==2);
    const auto tile=s.tiles.front();const auto rect=chartRect(s,tile.rect);const int x=int(rect.x+rect.w/2),y=int(rect.y+rect.h/2);
    if(inRect(s.rcChart,x,y))CHECK(hitTile(s,x,y)>=0);
    s.panX=100000;s.panY=-100000;clampPan(s);CHECK(s.panX<100000&&s.panY>-100000);
    chartZoom(s,0,true);CHECK(s.zoom==1&&s.panX==0&&s.panY==0);
    setViewMode(s,true);renderViewer("controls-rings-light");CHECK(s.smoothRendering);
    navigateTo(s,L"C:\\Projects\\Disk View");renderViewer("controls-parent-light");
    for(int dpi:{96,120,144,192}){s.dpi=dpi;makeFonts(s);::SetWindowPos(s.hwnd,nullptr,0,0,MulDiv(1200,dpi,96),MulDiv(940,dpi,96),SWP_NOMOVE|SWP_NOZORDER);computeLayout(s);relayoutView(s);renderViewer(("controls-rings-dpi-"+std::to_string(dpi)).c_str());CHECK(s.smoothRendering);}
    s.dpi=96;makeFonts(s);::SetWindowPos(s.hwnd,nullptr,0,0,1200,940,SWP_NOMOVE|SWP_NOZORDER);computeLayout(s);relayoutView(s);
    performCommand(s,Command::Accounting);CHECK(s.toolWindow&&::IsWindowEnabled(s.hwnd));goUp(s);CHECK(s.viewRoot->name==L"Projects");::DestroyWindow(s.toolWindow);
    performCommand(s,Command::Help);CHECK(editText(s.toolInput).find(L"Backspace")!=std::wstring::npos);renderViewer("controls-help-light",s.toolWindow);::DestroyWindow(s.toolWindow);
    performCommand(s,Command::Commands);::SetWindowTextW(s.toolInput,L"返回父级");CHECK(::SendMessageW(s.toolList,LB_GETCOUNT,0,0)==1);::SendMessageW(s.toolWindow,WM_COMMAND,IDOK,0);CHECK(s.viewRoot==s.tree.get());
    performCommand(s,Command::Commands);MSG commandDown{};commandDown.hwnd=s.toolInput;commandDown.message=WM_KEYDOWN;commandDown.wParam=VK_DOWN;
    CHECK(toolMessage(s,commandDown)&&::SendMessageW(s.toolList,LB_GETCURSEL,0,0)==1);renderViewer("controls-commands-light",s.toolWindow);::DestroyWindow(s.toolWindow);
    for(int dpi:{96,120,144,192}) {
        s.dpi=dpi;makeFonts(s);performCommand(s,Command::Rescan);CHECK(::GetDlgItem(s.toolWindow,412)!=nullptr);
        renderViewer(("controls-settings-dpi-"+std::to_string(dpi)).c_str(),s.toolWindow);
        RECT exclusions{},button{};::GetWindowRect(::GetDlgItem(s.toolWindow,415),&exclusions);::GetWindowRect(::GetDlgItem(s.toolWindow,IDOK),&button);CHECK(exclusions.bottom<button.top);
        ::DestroyWindow(s.toolWindow);
    }
    // The panel must open on the settings that were persisted, not on defaults.
    s.scanMode=2;s.scanOptions.workers=4;s.scanOptions.media=ScanOptions::Media::Ssd;s.scanOptions.previewIntervalMs=5000;
    s.scanOptions.excludedPaths={L"C:\\excluded"};
    performCommand(s,Command::Rescan);
    CHECK(::SendDlgItemMessageW(s.toolWindow,411,CB_GETCURSEL,0,0)==2);
    CHECK(::SendDlgItemMessageW(s.toolWindow,412,CB_GETCURSEL,0,0)==3); // the "4" entry
    CHECK(::SendDlgItemMessageW(s.toolWindow,413,CB_GETCURSEL,0,0)==1);
    CHECK(::SendDlgItemMessageW(s.toolWindow,414,CB_GETCURSEL,0,0)==1);
    CHECK(editText(::GetDlgItem(s.toolWindow,415)).find(L"C:\\excluded")!=std::wstring::npos);
    ::DestroyWindow(s.toolWindow);
    s.scanMode=0;s.scanOptions.workers=0;s.scanOptions.media=ScanOptions::Media::Automatic;
    s.scanOptions.previewIntervalMs=1500;s.scanOptions.excludedPaths.clear();
    s.dpi=96;makeFonts(s);
    editAddress(s);::SetWindowTextW(s.edit,L"C:\\typing");const auto before=s.viewRoot;::SendMessageW(s.edit,WM_KEYDOWN,VK_BACK,0);CHECK(s.viewRoot==before);
    auto copy=std::make_shared<ScanResult>(*s.completeResult);adoptResult(s,copy,false);CHECK(s.editingAddress&&editText(s.edit)==L"C:\\typing");::SendMessageW(s.edit,WM_KEYDOWN,VK_ESCAPE,0);
    s.palette=darkPalette();renderViewer("controls-rings-dark");
    performCommand(s,Command::Rescan);renderViewer("controls-settings-dark",s.toolWindow);::DestroyWindow(s.toolWindow);
    ::SetWindowPos(s.hwnd,nullptr,0,0,410,1100,SWP_NOMOVE|SWP_NOZORDER);s.mobileList=false;computeLayout(s);relayoutView(s);renderViewer("controls-narrow-chart");
    // The compact layout has no in-pane zoom or pan buttons: the chart fills its pane.
    CHECK(s.rcZoomIn.right<=s.rcZoomIn.left&&s.rcPanUp.right<=s.rcPanUp.left);
    CHECK(s.rcChart.top>=s.rcChartBar.bottom&&s.rcChart.bottom<=s.rcStatus.top);
    s.mobileList=true;computeLayout(s);relayoutView(s);renderViewer("controls-narrow-list");CHECK(s.rcTopList.bottom<s.rcStatus.top);}
