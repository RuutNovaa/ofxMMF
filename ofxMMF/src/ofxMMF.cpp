#include "ofxMMF.h"

ofxMMF::ofxMMF() {

	InitializeCriticalSection(&criticalSection);
	isSetup = false;

	mmfCamParameterGroup.setName("Camera not available");
}
ofxMMF::~ofxMMF() {
	if (wSymbolicLink) {
		delete wSymbolicLink;
		wSymbolicLink = NULL;
	}
	EnterCriticalSection(&criticalSection);

	if (sourceReader) {
		sourceReader -> Release();
		sourceReader = NULL;
	}

	if (rawData) {
		delete rawData;
		rawData = NULL;
	}

	CoTaskMemFree(wSymbolicLink);
	wSymbolicLink = NULL;
	cchSymbolicLink = 0;

	LeaveCriticalSection(&criticalSection);
	DeleteCriticalSection(&criticalSection);
}


void ofxMMF::update() {
	if (isSetup) {

		//-- TODO: make sure update first checks video stream type and process accordingly (avoid bad allocation!)
		//-- TODO: offload decoding operations to thread!
		unsigned char * rgbData = new unsigned char[width * height * 3]();

		try {
			nv12_to_rgb(rgbData, rawData, width, height);
		} catch (const std::exception & e) {

			std::cout << "Error: " << e.what() << std::endl;
		}

		try {
			camTex.loadData((unsigned char *)rgbData, width, height, GL_RGB);

		} catch (const std::exception & e) {

			std::cout << "Error: " << e.what() << std::endl;
		}
		//std::cout << nPix.getPixelFormat() << std::endl;

		delete[] rgbData;
	} else {

		//std::cout << "Camera is not setup!" << std::endl;
	
	}
}


std::string ofxMMF::ConvertWCharToString(WCHAR* in) {

	std::wstring ws(in);
	std::string str(ws.begin(), ws.end());
	return str;

}

//From IUnknown
STDMETHODIMP ofxMMF::QueryInterface(REFIID riid, void ** ppvObject) {
	static const QITAB qit[] = {
		QITABENT(ofxMMF, IMFSourceReaderCallback),
		{ 0 },
	};
	return QISearch(this, qit, riid, ppvObject);
}
//From IUnknown
ULONG ofxMMF::Release() {
	ULONG count = InterlockedDecrement(&referenceCount);
	if (count == 0)
		delete this;
	// For thread safety
	return count;
}

//From IUnknown
ULONG ofxMMF::AddRef() {
	return InterlockedIncrement(&referenceCount);
}

//Method from IMFSourceReaderCallback
STDMETHODIMP ofxMMF::OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }

//Method from IMFSourceReaderCallback
STDMETHODIMP ofxMMF::OnFlush(DWORD) { return S_OK; }

HRESULT ofxMMF::CreateCaptureDevice(int rw, int rh, std::string searchName) {
	
	reqW = rw;
	reqH = rh;

	HRESULT hr = S_OK;

	//this is important!!
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);

	UINT32 count = 0;
	IMFAttributes * attributes = NULL;
	IMFActivate ** devices = NULL;

	if (FAILED(hr)) {
		std::string errString = "Unknown error";
		if (hr == S_FALSE) {
			errString = "Library already initialized on thread";
		}
		if (hr == RPC_E_CHANGED_MODE) {
			errString = "Wrong concurency mode!";
		}
		std::cout << "Failed CoinitializeEx!: " << hr << ": " << errString << std::endl;
		CLEAN_ATTRIBUTES()
	}
	// Create an attribute store to specify enumeration parameters.
	hr = MFCreateAttributes(&attributes, 1);

	if (FAILED(hr)) {
		CLEAN_ATTRIBUTES()
	}

	//The attribute to be requested is devices that can capture video
	hr = attributes -> SetGUID( MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,	MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

	if (FAILED(hr)) {
		CLEAN_ATTRIBUTES()
	}
	//Enummerate the video capture devices
	hr = MFEnumDeviceSources(attributes, &devices, &count);

	if (FAILED(hr)) {
		CLEAN_ATTRIBUTES();
		std::cout << "ofxMMF: Device enumeration failed" << std::endl;
	}
	//if there are any available devices
	if (count > 0) {

		std::cout << "Found: " << count << " devices compatible for streaming" << std::endl;

		int selectedDev = -1;

		for (int it = 0; it < count; it++) {

			WCHAR devName[255];
			devices[it]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, devName, 255, NULL);
			std::cout << it << ": " << ConvertWCharToString(devName) << std::endl;

			if (ConvertWCharToString(devName).find(searchName) != std::string::npos) {
				std::cout << "Found device: " << searchName << " selected: " << it << std::endl;
				selectedDev = it;
				break;
			}
			
		}

		WCHAR * nameString = NULL;
		UINT32 cchName;

		//Set sourceReader needs to be called last as this gives the update about finding the correct
		//Media type (streaming profile), if this failed do not allocate memory!
		//--- TODO: make sure allocation is suitable for YUY2, RGB24/32 as well.
		if (selectedDev != -1) {
			
			// Get the human-friendly name of the device
			hr = devices[selectedDev]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nameString, &cchName);
			hr = SetSourceReader(devices[selectedDev]);
			
			std::cout << "Set source reader: " << hr << std::endl;
		}
		else {
			//Get a source reader from the first available device
			// Get the human-friendly name of the device
			hr = devices[0]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nameString, &cchName);
			hr = SetSourceReader(devices[0]);
			std::cout << "Set source reader: " << hr << std::endl;
		}
		
		

		if (SUCCEEDED(hr)) {
			//allocate a byte buffer for the raw pixel data
			bytesPerPixel = abs(stride) / width;
			rawData = new BYTE[((float)width * (float)height * (float)(3.0f / 2.0f))];
			wcscpy_s(deviceNameString, nameString);
			mmfCamParameterGroup.setName(ConvertWCharToString(nameString));
		}
		CoTaskMemFree(nameString);
	}

	if (SUCCEEDED(hr)) {
		isSetup = true;
	} 
	else {
		isSetup = false;	
	}

	//clean
	CLEAN_ATTRIBUTES()
}

HRESULT ofxMMF::SetSourceReader(IMFActivate * device) {
	HRESULT hr = S_OK;

	IMFMediaSource * source = NULL;
	IMFAttributes * attributes = NULL;
	

	//std::cout << "Pass 2a" << std::endl;

	EnterCriticalSection(&criticalSection);
	//std::cout << "Pass 2b" << std::endl;
	hr = device -> ActivateObject(__uuidof(IMFMediaSource), (void **)&source);
	
	//get symbolic link for the device
	if (SUCCEEDED(hr))
		hr = device ->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &wSymbolicLink, &cchSymbolicLink);
	//Allocate attributes
	if (SUCCEEDED(hr))
		hr = MFCreateAttributes(&attributes, 2);
	//get attributes
	if (SUCCEEDED(hr))
		hr = attributes -> SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
	// Set the callback pointer.
	if (SUCCEEDED(hr))
		hr = attributes -> SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
	//Create the source reader
	if (SUCCEEDED(hr))
		hr = MFCreateSourceReaderFromMediaSource(source, attributes, &sourceReader);
		EnumerateCaptureFormats(source);
	if (SUCCEEDED(hr))
		hr = device->ActivateObject(IID_IAMCameraControl, (void **)&camControl);
	std::cout << "Get camControl: " << hr << std::endl;

	//Exposure settings
	HRESULT camControlVal=0;
	HRESULT camControlFlags=0;
	hr = camControl->Get(CameraControlProperty::CameraControl_Exposure,&camControlVal,&camControlFlags);

	if (SUCCEEDED(hr)) {
		std::cout << "Cam control: \tExposure\t val:" << camControlVal << "\tflag:" << camControlFlags << std::endl;
		camControl->Set(CameraControlProperty::CameraControl_Exposure, 0, 0x01);
		HRESULT rMin, rMax, rStep, rDefault, rFlag;
		camControl->GetRange(CameraControlProperty::CameraControl_Exposure, &rMin, &rMax, &rStep, &rDefault, &rFlag);
		std::cout << "Exposure \tmin: " << rMin << " \tmax:" << rMax << std::endl;

		camControl->Get(CameraControlProperty::CameraControl_Exposure, &camControlVal, &camControlFlags);
		std::cout << "Current exposure: " << camControlVal << ", flag: " << camControlFlags << std::endl;

		camControl->Set(CameraControlProperty::CameraControl_Exposure, -2, 0x01);
		mmfCamParameterGroup.add(exposureSetting.set("Exposure", camControlVal, rMin, rMax));

		exposureSetting.addListener(this, &ofxMMF::changeExposureSetting);
	}
	//White balance settings



	bool foundCorrectMediaType = false;

	// Try to find a suitable output type.
	if (SUCCEEDED(hr)) {
		HRESULT nativeTypeErrorCode = S_OK;
		DWORD count = 0;
		UINT32 streamIndex = 0;
		//2688 x 1512
		UINT32 requiredWidth = reqW;
		UINT32 requiredheight = reqH;

		

		while (nativeTypeErrorCode == S_OK) {
			IMFMediaType * nativeType = NULL;
			nativeTypeErrorCode = sourceReader->GetNativeMediaType(streamIndex, count, &nativeType);
			if (nativeTypeErrorCode != S_OK) continue;

			GUID nativeGuid = { 0 };
			hr = nativeType->GetGUID(MF_MT_SUBTYPE, &nativeGuid);

			if (FAILED(hr)) return hr;

			UINT32 cwidth, cheight;
			hr = MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &cwidth, &cheight);
			if (FAILED(hr)) return hr;

			UINT32 yuv_video_matrix = 0;
			yuv_video_matrix = MFGetAttributeUINT32(nativeType, MF_MT_YUV_MATRIX, yuv_video_matrix);

			UINT32 sample_size = 0;
			sample_size = MFGetAttributeUINT32(nativeType, MF_MT_SAMPLE_SIZE, sample_size);

			UINT32 curstride = 0;
			curstride = MFGetAttributeUINT32(nativeType, MF_MT_DEFAULT_STRIDE, curstride);

			GUID subtype = GUID_NULL;
			LONG tempStride = 0;
			nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
			MFGetStrideForBitmapInfoHeader(subtype.Data1, cwidth, &tempStride);
			
			WCHAR * pGuidValName;
			hr = GetGUIDName(nativeGuid, &pGuidValName);
			
			
			std::cout << count << ":  " << ConvertWCharToString(pGuidValName) << "\t" << cwidth << " x " << cheight << " : yuv_decode: " << yuv_video_matrix << " : sample size: " << sample_size << ": stride: " << curstride << ": tmpstride: " << tempStride << std::endl;

			if (nativeGuid == MFVideoFormat_NV12 && cwidth == requiredWidth && cheight == requiredheight) {
				// found native config, set it

				hr = IsMediaTypeSupported(nativeType);
				if (FAILED(hr)) {
					std::cout << "Media type is not supported" << std::endl;
					//return hr;
				}

				hr = sourceReader->SetCurrentMediaType(streamIndex, NULL, nativeType);
				if (FAILED(hr)) {
					std::cout << "Failed to set Media type" << std::endl;
					//return hr;
				}

				MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &width, &height);
				
				foundCorrectMediaType = true;
				break;
			}

			
			count++;
		}

	}

	if (!foundCorrectMediaType) {
		hr = E_FAIL;
		std::cout << "Cant find appropriate mediatype,exiting" << std::endl;
	}

	if (SUCCEEDED(hr)) {
		// Ask for the first sample.
		

		hr = sourceReader -> ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, NULL, NULL, NULL);
	}

	if (FAILED(hr)) {
		if (source) {
			source -> Shutdown();
		}
		if (camControl) {
			camControl->Release();
		}
		//Close();
	}
	if (source) {
		source -> Release();
		source = NULL;
	}
	if (attributes) {
		attributes -> Release();
		attributes = NULL;
	}
	

	LeaveCriticalSection(&criticalSection);
	return hr;
}

void ofxMMF::changeExposureSetting(int &setting) {
	if (camControl) {



		HRESULT camControlVal = 0;
		HRESULT camControlFlags = 0;
		camControl->Get(CameraControlProperty::CameraControl_Exposure, &camControlVal, &camControlFlags);

		HRESULT rMin, rMax, rStep, rDefault, rFlag;
		camControl->GetRange(CameraControlProperty::CameraControl_Exposure, &rMin, &rMax, &rStep, &rDefault, &rFlag);

		if ((setting >= rMin && setting <= rMax)) {
			//std::cout << "valstep: " << rStep << std::endl;
			//std::cout << "Set exposure" << std::endl;
			HRESULT hr = camControl->Set(CameraControlProperty::CameraControl_Exposure,(long)setting,0x02);
			//std::cout << "success" << hr << std::endl;
		}
	}
}

HRESULT ofxMMF::IsMediaTypeSupported(IMFMediaType * pType) {
	HRESULT hr = S_OK;

	BOOL bFound = FALSE;
	GUID subtype = { 0 };

	//Get the stride for this format so we can calculate the number of bytes per pixel
	GetDefaultStride(pType, &stride);

	if (FAILED(hr)) {
		return hr;
	}
	hr = pType ->GetGUID(MF_MT_SUBTYPE, &subtype);

	videoFormat = subtype;

	if (FAILED(hr)) {
		return hr;
	}

	if (subtype == MFVideoFormat_MJPG || subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_RGB24 || subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_NV12)
		return S_OK;
	else
		return S_FALSE;

	return hr;
}

HRESULT ofxMMF::GetDefaultStride(IMFMediaType * type, LONG * stride) {
	LONG tempStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = type -> GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&tempStride);
	if (FAILED(hr)) {
		//Setting this atribute to NULL we can obtain the default stride
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Obtain the subtype
		hr = type ->GetGUID(MF_MT_SUBTYPE, &subtype);
		//obtain the width and height


		if (SUCCEEDED(hr))
			hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
		//Calculate the stride based on the subtype and width
		if (SUCCEEDED(hr)) {
			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &tempStride);
			std::cout << "Found: " << tempStride << std::endl;
		}
		// set the attribute so it can be read
		if (SUCCEEDED(hr))
			(void)type -> SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(tempStride));
	}

	if (SUCCEEDED(hr))
		*stride = tempStride;
	return hr;
}

//Method from IMFSourceReaderCallback
HRESULT ofxMMF::OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample * sample) {
	HRESULT hr = S_OK;
	IMFMediaBuffer * mediaBuffer = NULL;

	EnterCriticalSection(&criticalSection);

	if (FAILED(status))
		hr = status;

	if (SUCCEEDED(hr)) {
		if (sample) { // Get the video frame buffer from the sample.
			hr = sample ->GetBufferByIndex(0, &mediaBuffer);
			// Draw the frame.
			if (SUCCEEDED(hr)) {
				BYTE * data;
				DWORD bufferLength=0;
				mediaBuffer->GetCurrentLength(&bufferLength);


				
				mediaBuffer ->Lock(&data, NULL, NULL);
				
				//This is a good place to perform color conversion and drawing
				//ColorConversion(data);
				//Draw(data)
				//Instead we're copying the data to a buffer
				CopyMemory(rawData, data, ((float)width * (float)height * (float)(3.0f / 2.0f)));
			}
		}
	}
	// Request the next frame.
	if (SUCCEEDED(hr))
		hr = sourceReader ->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, NULL, NULL, NULL);

	if (FAILED(hr)) {
		//Notify there was an error
		printf("Error HRESULT = 0x%d", hr);
		PostMessage(NULL, 1, (WPARAM)hr, 0L);
	}
	if (mediaBuffer) {
		mediaBuffer ->Release();
		mediaBuffer = NULL;
	}

	LeaveCriticalSection(&criticalSection);
	return hr;
}

HRESULT ofxMMF::Close() {
	EnterCriticalSection(&criticalSection);
	if (sourceReader) {
		sourceReader -> Release();
		sourceReader = NULL;
	}

	CoTaskMemFree(wSymbolicLink);
	wSymbolicLink = NULL;
	cchSymbolicLink = 0;

	LeaveCriticalSection(&criticalSection);
	return S_OK;
}


HRESULT ofxMMF::EnumerateCaptureFormats(IMFMediaSource * pSource) {
	
	IMFPresentationDescriptor * pPD = NULL;
	IMFStreamDescriptor * pSD = NULL;
	IMFMediaTypeHandler * pHandler = NULL;
	IMFMediaType * pType = NULL;

	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr)) {
		//goto done;
		std::cout << "Failed to get presentation descriptor" << std::endl;
	}

	BOOL fSelected;
	hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
	if (FAILED(hr)) {
		//goto done;
		std::cout << "Failed to get Stream descriptor" << std::endl;
	}

	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr)) {
		//goto done;
		std::cout << "Failed to get Media Type Handler" << std::endl;
	}

	DWORD cTypes = 0;
	hr = pHandler->GetMediaTypeCount(&cTypes);
	if (FAILED(hr)) {
		//goto done;
		std::cout << "Failed to get Media Type count" << std::endl;
	}

	for (DWORD i = 0; i < cTypes; i++) {
		hr = pHandler->GetMediaTypeByIndex(i, &pType);
		if (FAILED(hr)) {
			std::cout << "failed to get media type by index" << std::endl;
		}

		LogMediaType(pType);
		//OutputDebugString(L"\n");

		
	}

	
	return hr;
}














HRESULT ofxMMF::LogMediaType(IMFMediaType * pType) {
	UINT32 count = 0;

	HRESULT hr = pType->GetCount(&count);
	if (FAILED(hr)) {
		return hr;
	}

	if (count == 0) {
		std::cout << "empty media type." << std::endl;
	}

	for (UINT32 i = 0; i < count; i++) {
		hr = LogAttributeValueByIndex(pType, i);
		//std::cout << "__________________________________________________" << std::endl;
		if (FAILED(hr)) {
			break;
		}
	}
	return hr;
}

HRESULT ofxMMF::LogAttributeValueByIndex(IMFAttributes * pAttr, DWORD index) {
	WCHAR * pGuidName = NULL;
	WCHAR * pGuidValName = NULL;

	GUID guid = { 0 };

	PROPVARIANT var;
	PropVariantInit(&var);

	HRESULT hr = pAttr->GetItemByIndex(index, &guid, &var);
	if (FAILED(hr)) {
		std::cout << "GetItemByIndex: couldnt get name by index" << std::endl;
	}

	hr = GetGUIDName(guid, &pGuidName);
	if (FAILED(hr)) {
		std::cout << "GetItemByIndex: couldnt retrieve GUIDName" << std::endl;
	}

	//std::cout << ConvertWCharToString(pGuidName) << "\t:";

	hr = SpecialCaseAttributeValue(guid, var);
	if (FAILED(hr)) {
		
	}
	if (hr == S_FALSE) {
		switch (var.vt) {
		case VT_UI4:
			
			//std::cout << (long)var.ulVal;
			break;

		case VT_UI8:
			//std::cout << var.uhVal.LowPart << " x " << var.uhVal.HighPart;
			break;

		case VT_R8:
			//std::cout << var.dblVal;
			break;

		case VT_CLSID:
			hr = GetGUIDName(*var.puuid, &pGuidValName);
			if (SUCCEEDED(hr)) {
				//std::cout << ConvertWCharToString(pGuidValName) << "flag";
			}
			break;

		case VT_LPWSTR:
			//std::cout << ConvertWCharToString(var.pwszVal);
			break;

		case VT_VECTOR | VT_UI1:
			//std::cout << "byte array! ";
			break;

		case VT_UNKNOWN:
			//std::cout << "Unknown";
			break;
		case VT_ARRAY:
			//std::cout << "array";
		case VT_BLOB:
			//std::cout << "blob";
		case VT_DECIMAL: 
			//std::cout << "dec";
		default:
			//DBGMSG(L"Unexpected attribute type (vt = %d)", var.vt);
			//std::cout << "Unexpected";
			break;
		}

		//std::cout << std::endl;
	}


	//DBGMSG(L"\n");
	CoTaskMemFree(pGuidName);
	CoTaskMemFree(pGuidValName);
	PropVariantClear(&var);
	return hr;
}

HRESULT ofxMMF::GetGUIDName(const GUID & guid, WCHAR ** ppwsz) {
	HRESULT hr = S_OK;
	WCHAR * pName = NULL;

	LPCWSTR pcwsz = GetGUIDNameConst(guid);
	if (pcwsz) {
		size_t cchLength = 0;

		hr = StringCchLength(pcwsz, STRSAFE_MAX_CCH, &cchLength);
		if (FAILED(hr)) {
			goto done;
		}

		pName = (WCHAR *)CoTaskMemAlloc((cchLength + 1) * sizeof(WCHAR));

		if (pName == NULL) {
			hr = E_OUTOFMEMORY;
			goto done;
		}

		hr = StringCchCopy(pName, cchLength + 1, pcwsz);
		if (FAILED(hr)) {
			goto done;
		}
	} else {
		hr = StringFromCLSID(guid, &pName);
	}

done:
	if (FAILED(hr)) {
		*ppwsz = NULL;
		CoTaskMemFree(pName);
	} else {
		*ppwsz = pName;
	}
	return hr;
}

void ofxMMF::LogUINT32AsUINT64(const PROPVARIANT & var) {
	UINT32 uHigh = 0, uLow = 0;
	Unpack2UINT32AsUINT64(var.uhVal.QuadPart, &uHigh, &uLow);
	//std::cout << uHigh << " x " << uLow;
}

float ofxMMF::OffsetToFloat(const MFOffset & offset) {
	return offset.value + (static_cast<float>(offset.fract) / 65536.0f);
}

HRESULT ofxMMF::LogVideoArea(const PROPVARIANT & var) {
	if (var.caub.cElems < sizeof(MFVideoArea)) {
		return MF_E_BUFFERTOOSMALL;
	}

	MFVideoArea * pArea = (MFVideoArea *)var.caub.pElems;

	//DBGMSG(L"(%f,%f) (%d,%d)", OffsetToFloat(pArea->OffsetX), OffsetToFloat(pArea->OffsetY),
		//pArea->Area.cx, pArea->Area.cy);

	
	std::cout << OffsetToFloat(pArea->OffsetX) << " x " << OffsetToFloat(pArea->OffsetY);
	return S_OK;
}

// Handle certain known special cases.
HRESULT ofxMMF::SpecialCaseAttributeValue(GUID guid, const PROPVARIANT & var) {
	if ((guid == MF_MT_FRAME_RATE) || (guid == MF_MT_FRAME_RATE_RANGE_MAX) || (guid == MF_MT_FRAME_RATE_RANGE_MIN) || (guid == MF_MT_FRAME_SIZE) || (guid == MF_MT_PIXEL_ASPECT_RATIO)) {
		// Attributes that contain two packed 32-bit values.
		LogUINT32AsUINT64(var);
	} else if ((guid == MF_MT_GEOMETRIC_APERTURE) || (guid == MF_MT_MINIMUM_DISPLAY_APERTURE) || (guid == MF_MT_PAN_SCAN_APERTURE)) {
		// Attributes that an MFVideoArea structure.
		return LogVideoArea(var);
	} else {
		return S_FALSE;
	}
	return S_OK;
}



#ifndef IF_EQUAL_RETURN
	#define IF_EQUAL_RETURN(param, val) \
		if (val == param) return L#val
#endif

LPCWSTR ofxMMF::GetGUIDNameConst(const GUID & guid) {
	IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_SUBTYPE);
	IF_EQUAL_RETURN(guid, MF_MT_ALL_SAMPLES_INDEPENDENT);
	IF_EQUAL_RETURN(guid, MF_MT_FIXED_SIZE_SAMPLES);
	IF_EQUAL_RETURN(guid, MF_MT_COMPRESSED);
	IF_EQUAL_RETURN(guid, MF_MT_SAMPLE_SIZE);
	IF_EQUAL_RETURN(guid, MF_MT_WRAPPED_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_NUM_CHANNELS);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_AVG_BYTES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BLOCK_ALIGNMENT);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BITS_PER_SAMPLE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_VALID_BITS_PER_SAMPLE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_BLOCK);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_CHANNEL_MASK);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FOLDDOWN_MATRIX);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKREF);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKTARGET);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGREF);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGTARGET);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_PREFER_WAVEFORMATEX);
	IF_EQUAL_RETURN(guid, MF_MT_AAC_PAYLOAD_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_SIZE);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MAX);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MIN);
	IF_EQUAL_RETURN(guid, MF_MT_PIXEL_ASPECT_RATIO);
	IF_EQUAL_RETURN(guid, MF_MT_DRM_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_PAD_CONTROL_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_SOURCE_CONTENT_HINT);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_CHROMA_SITING);
	IF_EQUAL_RETURN(guid, MF_MT_INTERLACE_MODE);
	IF_EQUAL_RETURN(guid, MF_MT_TRANSFER_FUNCTION);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_PRIMARIES);
	IF_EQUAL_RETURN(guid, MF_MT_CUSTOM_VIDEO_PRIMARIES);
	IF_EQUAL_RETURN(guid, MF_MT_YUV_MATRIX);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_LIGHTING);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_NOMINAL_RANGE);
	IF_EQUAL_RETURN(guid, MF_MT_GEOMETRIC_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_MINIMUM_DISPLAY_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_ENABLED);
	IF_EQUAL_RETURN(guid, MF_MT_AVG_BITRATE);
	IF_EQUAL_RETURN(guid, MF_MT_AVG_BIT_ERROR_RATE);
	IF_EQUAL_RETURN(guid, MF_MT_MAX_KEYFRAME_SPACING);
	IF_EQUAL_RETURN(guid, MF_MT_DEFAULT_STRIDE);
	IF_EQUAL_RETURN(guid, MF_MT_PALETTE);
	IF_EQUAL_RETURN(guid, MF_MT_USER_DATA);
	IF_EQUAL_RETURN(guid, MF_MT_AM_FORMAT_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG_START_TIME_CODE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_PROFILE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_LEVEL);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG_SEQUENCE_HEADER);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_0);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_0);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_1);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_1);
	IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_SRC_PACK);
	IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_CTRL_PACK);
	IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_HEADER);
	IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_FORMAT);
	IF_EQUAL_RETURN(guid, MF_MT_IMAGE_LOSS_TOLERANT);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG4_SAMPLE_DESCRIPTION);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG4_CURRENT_SAMPLE_ENTRY);
	IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_4CC);
	IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_WAVE_FORMAT_TAG);

	//Added config values for video streams;
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_ROTATION);
	
	

	// Media types

	IF_EQUAL_RETURN(guid, MFMediaType_Audio);
	IF_EQUAL_RETURN(guid, MFMediaType_Video);
	IF_EQUAL_RETURN(guid, MFMediaType_Protected);
	IF_EQUAL_RETURN(guid, MFMediaType_SAMI);
	IF_EQUAL_RETURN(guid, MFMediaType_Script);
	IF_EQUAL_RETURN(guid, MFMediaType_Image);
	IF_EQUAL_RETURN(guid, MFMediaType_HTML);
	IF_EQUAL_RETURN(guid, MFMediaType_Binary);
	IF_EQUAL_RETURN(guid, MFMediaType_FileTransfer);

	IF_EQUAL_RETURN(guid, MFVideoFormat_AI44); //     FCC('AI44')
	IF_EQUAL_RETURN(guid, MFVideoFormat_ARGB32); //   D3DFMT_A8R8G8B8
	IF_EQUAL_RETURN(guid, MFVideoFormat_AYUV); //     FCC('AYUV')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DV25); //     FCC('dv25')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DV50); //     FCC('dv50')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVH1); //     FCC('dvh1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVSD); //     FCC('dvsd')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVSL); //     FCC('dvsl')
	IF_EQUAL_RETURN(guid, MFVideoFormat_H264); //     FCC('H264')
	IF_EQUAL_RETURN(guid, MFVideoFormat_I420); //     FCC('I420')
	IF_EQUAL_RETURN(guid, MFVideoFormat_IYUV); //     FCC('IYUV')
	IF_EQUAL_RETURN(guid, MFVideoFormat_M4S2); //     FCC('M4S2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MJPG);
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP43); //     FCC('MP43')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP4S); //     FCC('MP4S')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP4V); //     FCC('MP4V')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MPG1); //     FCC('MPG1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MSS1); //     FCC('MSS1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MSS2); //     FCC('MSS2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_NV11); //     FCC('NV11')
	IF_EQUAL_RETURN(guid, MFVideoFormat_NV12); //     FCC('NV12')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P010); //     FCC('P010')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P016); //     FCC('P016')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P210); //     FCC('P210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P216); //     FCC('P216')
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB24); //    D3DFMT_R8G8B8
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB32); //    D3DFMT_X8R8G8B8
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB555); //   D3DFMT_X1R5G5B5
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB565); //   D3DFMT_R5G6B5
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB8);
	IF_EQUAL_RETURN(guid, MFVideoFormat_UYVY); //     FCC('UYVY')
	IF_EQUAL_RETURN(guid, MFVideoFormat_v210); //     FCC('v210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_v410); //     FCC('v410')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV1); //     FCC('WMV1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV2); //     FCC('WMV2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV3); //     FCC('WMV3')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WVC1); //     FCC('WVC1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y210); //     FCC('Y210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y216); //     FCC('Y216')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y410); //     FCC('Y410')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y416); //     FCC('Y416')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y41P);
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y41T);
	IF_EQUAL_RETURN(guid, MFVideoFormat_YUY2); //     FCC('YUY2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_YV12); //     FCC('YV12')
	IF_EQUAL_RETURN(guid, MFVideoFormat_YVYU);

	IF_EQUAL_RETURN(guid, MFAudioFormat_PCM); //              WAVE_FORMAT_PCM
	IF_EQUAL_RETURN(guid, MFAudioFormat_Float); //            WAVE_FORMAT_IEEE_FLOAT
	IF_EQUAL_RETURN(guid, MFAudioFormat_DTS); //              WAVE_FORMAT_DTS
	IF_EQUAL_RETURN(guid, MFAudioFormat_Dolby_AC3_SPDIF); //  WAVE_FORMAT_DOLBY_AC3_SPDIF
	IF_EQUAL_RETURN(guid, MFAudioFormat_DRM); //              WAVE_FORMAT_DRM
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV8); //        WAVE_FORMAT_WMAUDIO2
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV9); //        WAVE_FORMAT_WMAUDIO3
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudio_Lossless); // WAVE_FORMAT_WMAUDIO_LOSSLESS
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMASPDIF); //         WAVE_FORMAT_WMASPDIF
	IF_EQUAL_RETURN(guid, MFAudioFormat_MSP1); //             WAVE_FORMAT_WMAVOICE9
	IF_EQUAL_RETURN(guid, MFAudioFormat_MP3); //              WAVE_FORMAT_MPEGLAYER3
	IF_EQUAL_RETURN(guid, MFAudioFormat_MPEG); //             WAVE_FORMAT_MPEG
	IF_EQUAL_RETURN(guid, MFAudioFormat_AAC); //              WAVE_FORMAT_MPEG_HEAAC
	IF_EQUAL_RETURN(guid, MFAudioFormat_ADTS); //             WAVE_FORMAT_MPEG_ADTS_AAC

	return NULL;
}