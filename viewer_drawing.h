// Included inside diskview::detail after the GDI drawing helpers.
#pragma once
inline D2D1_COLOR_F d2color(COLORREF c) {return D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f);}
inline bool smoothSlices(ViewerState& s,HDC dc) {
    using Microsoft::WRL::ComPtr;
    if(!s.d2dFactory&&FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,s.d2dFactory.GetAddressOf())))return false;
    if(!s.d2dTarget) {
        const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        if(FAILED(s.d2dFactory->CreateDCRenderTarget(&props,s.d2dTarget.GetAddressOf())))return false;
    }
    auto* target=s.d2dTarget.Get();if(FAILED(target->BindDC(dc,&s.rcChart)))return false;
    ComPtr<ID2D1SolidColorBrush> brush;if(FAILED(target->CreateSolidColorBrush(d2color(s.palette.panel),brush.GetAddressOf())))return false;
    target->BeginDraw();target->Clear(d2color(s.palette.panel));target->SetTransform(D2D1::Matrix3x2F::Translation(float(-s.rcChart.left),float(-s.rcChart.top)));target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->PushAxisAlignedClip(D2D1::RectF(float(s.rcChart.left),float(s.rcChart.top),float(s.rcChart.right),float(s.rcChart.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);
    const double cx=(s.rcChart.left+s.rcChart.right)/2.0+s.panX,cy=(s.rcChart.top+s.rcChart.bottom)/2.0+s.panY;
    for(const auto& sl:s.slices) {
        ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(s.d2dFactory->CreatePathGeometry(geometry.GetAddressOf()))||FAILED(geometry->Open(sink.GetAddressOf())))continue;
        const auto point=[&](double r,double a){return D2D1::Point2F(float(cx+r*s.zoom*std::cos(a)),float(cy+r*s.zoom*std::sin(a)));};
        // Two arcs support a complete ring without coincident start/end ambiguity.
        const auto arc=[&](double r,double a,bool clockwise,double span){return D2D1::ArcSegment(point(r,a),D2D1::SizeF(float(r*s.zoom),float(r*s.zoom)),0,clockwise?D2D1_SWEEP_DIRECTION_CLOCKWISE:D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,span>3.141592653589793?D2D1_ARC_SIZE_LARGE:D2D1_ARC_SIZE_SMALL);};
        const double mid=(sl.a0+sl.a1)/2,half=(sl.a1-sl.a0)/2;
        sink->BeginFigure(point(sl.r1,sl.a0),D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(arc(sl.r1,mid,true,half));sink->AddArc(arc(sl.r1,sl.a1,true,half));
        sink->AddLine(point(sl.r0,sl.a1));sink->AddArc(arc(sl.r0,mid,false,half));sink->AddArc(arc(sl.r0,sl.a0,false,half));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);if(FAILED(sink->Close()))continue;
        auto fill=sl.dir?dirColor(sl.depth,s.palette):fileColor(sl.node->name,sl.depth,s.palette);
        const bool active=sl.node==s.hovered||sl.node==s.selected;if(active)fill=lighten(fill,s.palette.dark?22:-18);
        brush->SetColor(d2color(fill));target->FillGeometry(geometry.Get(),brush.Get());
        brush->SetColor(d2color(active?s.palette.accent:s.palette.panel));target->DrawGeometry(geometry.Get(),brush.Get(),active?1.5f:1.f);
    }
    target->PopAxisAlignedClip();const auto hr=target->EndDraw();if(FAILED(hr)){s.d2dTarget.Reset();return false;}return true;
}
inline bool hitCenter(const ViewerState& s,int x,int y) {
    if(!s.sunburst||s.slices.empty()||!inRect(s.rcChart,x,y))return false;
    const auto p=chartInverse(s,x,y);const double dx=p.x-(s.rcChart.left+s.rcChart.right)/2.0,dy=p.y-(s.rcChart.top+s.rcChart.bottom)/2.0;
    return dx*dx+dy*dy<s.slices.front().r0*s.slices.front().r0;
}
inline void drawSlices(ViewerState& s,HDC dc) {
    if(s.slices.empty())return;
    s.smoothRendering=smoothSlices(s,dc);
    if(!s.smoothRendering)drawSlicesFallback(s,dc);
    const double cx=(s.rcChart.left+s.rcChart.right)/2.0+s.panX,cy=(s.rcChart.top+s.rcChart.bottom)/2.0+s.panY;
    const bool hasParent=s.viewRoot!=s.tree.get();
    const Node* parent=s.drill.empty()?s.tree.get():s.drill.back();
    const int inner=int(std::min(s.slices.front().r0*s.zoom*.88,double(S(100,s))));
    if(inner>S(30,s)) {
        RECT r{LONG(cx-inner),LONG(cy-S(23,s)),LONG(cx+inner),LONG(cy)};
        drawTextIn(dc,hasParent?(s.spec.zh?L"↑ 返回上一级":L"↑ Parent folder"):(s.spec.zh?L"扫描根目录":L"Scan root"),r,s.palette.accent,DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);
        r.top=r.bottom;r.bottom+=S(23,s);drawTextIn(dc,hasParent?parent->name:winshun::wide(formatBytes(s.viewRoot->logical,s.spec.zh)),r,s.palette.muted,DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);
    }
    const auto old=::SelectObject(dc,s.fontSmall);
    for(const auto& sl:s.slices)if((sl.a1-sl.a0)>.35&&(sl.r1-sl.r0)*s.zoom>S(26,s)) {
        const double a=(sl.a0+sl.a1)/2,r=(sl.r0+sl.r1)*s.zoom/2;
        const int half=int(std::min<double>(S(50,s),(sl.r1-sl.r0)*s.zoom*.42));
        RECT label{LONG(cx+std::cos(a)*r-half),LONG(cy+std::sin(a)*r-S(10,s)),LONG(cx+std::cos(a)*r+half),LONG(cy+std::sin(a)*r+S(10,s))};
        const auto fill=sl.dir?dirColor(sl.depth,s.palette):fileColor(sl.node->name,sl.depth,s.palette);
        drawTextIn(dc,sl.dir?sl.node->name:winshun::wide(formatBytes(sl.node->logical,s.spec.zh)),label,tileText(fill),DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);
    }
    ::SelectObject(dc,old);
}
