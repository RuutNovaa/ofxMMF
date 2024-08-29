#include "ofMain.h"

#include <windows.h>
#include <shobjidl.h> 
#include <shlwapi.h>
#include <assert.h>
#include <tchar.h>
#include <strsafe.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <windows.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <strmif.h>

#include "yuv2rgb.h"

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "strmiids")


#define CLEAN_ATTRIBUTES() if (attributes) { attributes->Release(); attributes = NULL; } for (DWORD i = 0; i < count; i++){if (devices[i]) { devices[i]->Release(); devices[i] = NULL; }}CoTaskMemFree(devices); return hr;

class ofxMMF : public IMFSourceReaderCallback {


	CRITICAL_SECTION criticalSection;
	long referenceCount;
	WCHAR * wSymbolicLink;
	UINT32 cchSymbolicLink;
	IMFSourceReader * sourceReader;
	IAMCameraControl * camControl = NULL;

public:
	LONG stride;
	int bytesPerPixel;
	GUID videoFormat;
	UINT height;
	UINT width;
	WCHAR deviceNameString[2048];
	BYTE * rawData;

	
	HRESULT CreateCaptureDevice(int rw, int rh, std::string searchName = "HD USB Camaer 4K");
	HRESULT SetSourceReader(IMFActivate * device);
	HRESULT IsMediaTypeSupported(IMFMediaType * type);
	HRESULT GetDefaultStride(IMFMediaType * pType, LONG * plStride);
	HRESULT Close();

	HRESULT EnumerateCaptureFormats(IMFMediaSource * pSource);

	LPCWSTR GetGUIDNameConst(const GUID & guid);
	HRESULT GetGUIDName(const GUID & guid, WCHAR ** ppwsz);

	HRESULT LogAttributeValueByIndex(IMFAttributes * pAttr, DWORD index);
	HRESULT SpecialCaseAttributeValue(GUID guid, const PROPVARIANT & var);

	

	HRESULT LogMediaType(IMFMediaType * pType);
	void LogUINT32AsUINT64(const PROPVARIANT & var);
	float OffsetToFloat(const MFOffset & offset);
	HRESULT LogVideoArea(const PROPVARIANT & var);

	

	void update();

	ofxMMF();
	~ofxMMF();

	// the class must implement the methods from IUnknown
	STDMETHODIMP QueryInterface(REFIID iid, void ** ppv);
	STDMETHODIMP_(ULONG)
	AddRef();
	STDMETHODIMP_(ULONG)
	Release();

	//  the class must implement the methods from IMFSourceReaderCallback
	STDMETHODIMP OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample * sample);
	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *);
	STDMETHODIMP OnFlush(DWORD);

	void changeExposureSetting(int &setting);

	ofTexture camTex;

	ofParameterGroup mmfCamParameterGroup;
	ofParameter<int> exposureSetting;

	private:
	bool isSetup;
	std::string ConvertWCharToString(WCHAR * in);
	int reqW, reqH;


};