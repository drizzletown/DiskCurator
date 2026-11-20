#ifdef _WIN32
#include "core.hpp"
#include <windows.h>
#include <wincodec.h>
#include <propidl.h>
#include <array>
#include <string>
#include <algorithm>

namespace dc {
template<class T> struct ComPtr {
    T* p=nullptr;
    ~ComPtr(){if(p)p->Release();}
    T** out(){return &p;}
    T* operator->() const{return p;}
    explicit operator bool() const{return p!=nullptr;}
    ComPtr()=default;ComPtr(const ComPtr&)=delete;ComPtr& operator=(const ComPtr&)=delete;
};
static bool get_prop(IWICMetadataQueryReader* q,const wchar_t* key, PROPVARIANT& v) {
    PropVariantInit(&v);return q&&SUCCEEDED(q->GetMetadataByName(key,&v));
}
static std::wstring prop_text(const PROPVARIANT& v){
    if(v.vt==VT_BSTR&&v.bstrVal)return v.bstrVal;
    if(v.vt==VT_LPWSTR&&v.pwszVal)return v.pwszVal;
    if(v.vt==VT_LPSTR&&v.pszVal){int n=MultiByteToWideChar(CP_ACP,0,v.pszVal,-1,nullptr,0);if(n>0){std::wstring w(size_t(n),L'\0');MultiByteToWideChar(CP_ACP,0,v.pszVal,-1,w.data(),n);w.pop_back();return w;}}
    return {};
}
static std::string exif_date(IWICMetadataQueryReader* q){
    const wchar_t* keys[]={L"/app1/ifd/exif/{ushort=36867}",L"/ifd/exif/{ushort=36867}",L"/app1/ifd/exif/{ushort=36868}",L"/app1/ifd/{ushort=306}",L"/ifd/{ushort=306}"};
    for(auto key:keys){PROPVARIANT v;if(!get_prop(q,key,v))continue;auto w=prop_text(v);PropVariantClear(&v);
        if(w.size()>=10&&w[4]==L':'&&w[7]==L':'&&w[0]>=L'1'&&w[0]<=L'2'){
            std::string s;for(int i=0;i<10;++i)s.push_back(w[i]==L':'?'-':char(w[i]));return s;}}
    return {};
}
static unsigned orientation(IWICMetadataQueryReader* q){
    for(auto key:{L"/app1/ifd/{ushort=274}",L"/ifd/{ushort=274}"}){
        PROPVARIANT v;if(!get_prop(q,key,v))continue;unsigned o=1;
        if(v.vt==VT_UI2)o=v.uiVal;else if(v.vt==VT_UI4)o=v.ulVal;PropVariantClear(&v);if(o>=1&&o<=8)return o;}
    return 1;
}
Photo probe_photo(const fs::path& path){
    Photo result;HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct ScopedCom { bool initialized; ~ScopedCom(){if(initialized)CoUninitialize();} } scoped{SUCCEEDED(hr)};
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE){result.error="COM initialization failed";return result;}
    ComPtr<IWICImagingFactory> factory;
    hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.out()));
    if(FAILED(hr)){result.error="WIC factory unavailable";return result;}
    ComPtr<IWICBitmapDecoder> decoder;
    hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.out());
    if(FAILED(hr)){result.error="Unsupported or unreadable image codec";return result;}
    ComPtr<IWICBitmapFrameDecode> frame;hr=decoder->GetFrame(0,frame.out());
    if(FAILED(hr)){result.error="Cannot decode image frame";return result;}
    ComPtr<IWICMetadataQueryReader> query;
    if(SUCCEEDED(frame->GetMetadataQueryReader(query.out()))){result.date=exif_date(query.p);result.exif_date=!result.date.empty();}
    IWICBitmapSource* source=frame.p;
    ComPtr<IWICBitmapFlipRotator> flip;
    WICBitmapTransformOptions transform=WICBitmapTransformRotate0;
    switch(orientation(query.p)){
        case 2:transform=WICBitmapTransformFlipHorizontal;break;
        case 3:transform=WICBitmapTransformRotate180;break;
        case 4:transform=WICBitmapTransformFlipVertical;break;
        case 5:transform=static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal);break;
        case 6:transform=WICBitmapTransformRotate90;break;
        case 7:transform=static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal);break;
        case 8:transform=WICBitmapTransformRotate270;break;
    }
    if(transform!=WICBitmapTransformRotate0&&SUCCEEDED(factory->CreateBitmapFlipRotator(flip.out()))&&SUCCEEDED(flip->Initialize(source,transform)))source=flip.p;
    ComPtr<IWICBitmapScaler> scaled;hr=factory->CreateBitmapScaler(scaled.out());
    if(SUCCEEDED(hr))hr=scaled->Initialize(source,9,8,WICBitmapInterpolationModeFant);
    ComPtr<IWICFormatConverter> convert;
    if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(convert.out());
    if(SUCCEEDED(hr))hr=convert->Initialize(scaled.p,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    std::array<BYTE,9*8*4> pixels{};
    if(SUCCEEDED(hr))hr=convert->CopyPixels(nullptr,9*4,UINT(pixels.size()),pixels.data());
    if(FAILED(hr)){result.error="Image scaling/conversion failed";return result;}
    std::array<unsigned,9*8> gray{};
    for(size_t i=0;i<gray.size();++i){auto b=&pixels[i*4];gray[i]=(299u*b[0]+587u*b[1]+114u*b[2])/1000u;}
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(gray[y*9+x]>gray[y*9+x+1])result.dhash|=U64(1)<<(y*8+x);
    result.valid=true;return result;
}
}
#endif
