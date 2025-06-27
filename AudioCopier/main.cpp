#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <iostream>
#include <functiondiscoverykeys_devpkey.h>
#include <fcntl.h>
#include <io.h>

#include <Audioclient.h>

using namespace std;

struct PlaybackContext
{
	IAudioClient* client;
	IAudioRenderClient* renderClient;
	WAVEFORMATEX* format;
	UINT32 bufferFrameCnt;
};

void InitLoopbackCapture(IMMDevice*, IMMDevice*);

bool InitPlayback(IMMDevice*, PlaybackContext&, WAVEFORMATEX*);

void Resample48kto44k1(const int16_t* src, int srcFrames, int16_t* dst, int dstFrames)
{
	static double frac = 0.0;
	const double ratio = 44100.0 / 48000.0;	// ~=0.91875

	int produced = 0;
	for (int i = 0; i < srcFrames; i++)
	{
		if (frac <= 1.0)
		{
			// copy current sample
			dst[produced * 2] = src[i *2];
			dst[produced * 2 + 1] = src[i *2 + 1];
			++produced;
			frac += ratio;
		}
		while (frac > 1.0)
			frac -= 1.0;
	}

	dstFrames = produced;
}

int main() {
	// set to support unicode
	_setmode(_fileno(stdout), _O_U16TEXT);

	CoInitialize(nullptr);

	IMMDeviceEnumerator* enumerator = nullptr;
	IMMDeviceCollection* deviceCollection = nullptr;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumerator);

	if (FAILED(hr)) {
	std::cerr << "Failed to create device enumerator\n";
		return 1;
	}

	// enumerate all audio device
	enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection);

	UINT cnt;
	deviceCollection->GetCount(&cnt);

	std::wcout << L"Active render devices: " << cnt << endl;

	for (UINT i = 0; i < cnt; ++i) {
		IMMDevice* imd = nullptr;
		deviceCollection->Item(i, &imd);

		IPropertyStore* pptyStore = nullptr;
		imd->OpenPropertyStore(STGM_READ, &pptyStore);

		PROPVARIANT name;
		PropVariantInit(&name);
		pptyStore->GetValue(PKEY_Device_FriendlyName, &name);

		wcout << L"[" << i << L"] " << name.pwszVal << endl;
		 //std::wcout << L"[" << i << L"] " << name.pwszVal << std::endl;
		//cout << name.pwszVal << endl;
		//wcout << L"z" << endl;

		PropVariantClear(&name);
		pptyStore->Release();
		imd->Release();
	}

	//return 0;
	int targeIdx = 0, srcIdx = 1;
	IMMDevice* src_dev = nullptr;
	IMMDevice* dst_dev = nullptr;
	PlaybackContext dst_ctx;
	deviceCollection->Item(srcIdx, &src_dev);
	deviceCollection->Item(targeIdx, &dst_dev);
	
	//if (dst_ctx)
	InitLoopbackCapture(src_dev, dst_dev);

	deviceCollection->Release();
	enumerator->Release();
	CoUninitialize();

	return 0;
}

void InitLoopbackCapture(IMMDevice* src_dev, IMMDevice* dst_dev) {
	IAudioClient* client = nullptr;
	IAudioCaptureClient* capClient = nullptr;

	// get audio client
	HRESULT hr = src_dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client);
	if (FAILED(hr)) {
		wcerr << "Failed to active IAudioClient\n";
		return;
	}

	// get device format
	WAVEFORMATEX* waveFormat = nullptr;
	hr = client->GetMixFormat(&waveFormat);
	if (FAILED(hr)) {
		wcerr << "Failed to get wave format\n";
		return;
	}

	// initialize client using loopback mode
	hr = client->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK,
		1000000, 0, // buffer time : 1 sec
		waveFormat,
		nullptr
	);
	if (FAILED(hr)) {
		wcerr << "Failed to initialize IAudioClient\n";
		return;
	}

	// get capture client
	hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&capClient);
	if (FAILED(hr)) {
		wcerr << "Failed to get service of IAudioCaptureClient\n";
		return;
	}

	// Check the wave format of src and dst is the same.
	//WAVEFORMATEX* closet = nullptr;
	//hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, waveFormat, &closet);

	PlaybackContext ctx;

	if (!InitPlayback(dst_dev, ctx, waveFormat))
	{
		wcerr << "Failed to initialize target.\n";
		return;
	}

	// recording
	client->Start();
	wcout << L"start to recording...\n";
	UINT32 blockAlign = waveFormat->nBlockAlign;
	int i = 10000;
	while (i-- > 0) {
		Sleep(10); // wait for buffer accumulate

		UINT32 pktLen = 0;
		//hr = capClient->GetNextPacketSize(&pktLen);
		if (FAILED(capClient->GetNextPacketSize(&pktLen))) continue;

		while (pktLen != 0) {
			BYTE* data;
			UINT32 numFrames;
			DWORD flags;

			hr = capClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
			if (SUCCEEDED(hr)) {
				// TODO: here we copy data to both audio device
				BYTE* playbackData;
				//double framesNeed = 441.0 / 480.0 + 2;
				//int frames = (numFrames * 44100 + 47999) / 48000;
				int frames = numFrames;
				hr = ctx.renderClient->GetBuffer(frames , &playbackData);

				if (SUCCEEDED(hr)) {
					//memcpy(playbackData, data, frames * blockAlign);
					int16_t* intSrc = reinterpret_cast<int16_t*>(data);
					int16_t* intDst = reinterpret_cast<int16_t*>(playbackData);
					int dstFrames = 0;
					Resample48kto44k1(intSrc, frames, intDst, dstFrames);
					
					//wcout << L"Catch" << dstFrames << L"frame audio\n";
					//short* sampleData = reinterpret_cast<short*>(playbackData);
					//std::wcout << L"Sample[0]: " << sampleData[0] << L"\n";
					
					ctx.renderClient->ReleaseBuffer(frames, 0);
				}


				//wcout << L"Catch" << numFrames << L"frame audio\n";
				//short* sampleData = reinterpret_cast<short*>(data);
				//std::wcout << L"Sample[0]: " << sampleData[0] << L"\n";
				// release buffer
				capClient->ReleaseBuffer(numFrames);
			}

			capClient->GetNextPacketSize(&pktLen);
		}
	}
}

bool InitPlayback(IMMDevice* dev, PlaybackContext &ctx, WAVEFORMATEX* fmt) {
	REFERENCE_TIME dur = 200000;
	HRESULT hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ctx.client);
	if (FAILED(hr)) return false;

	hr = ctx.client->GetMixFormat(&ctx.format);
	if (FAILED(hr)) return false;

	hr = ctx.client->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		0,
		1000000,
		0,
		ctx.format,
		//fmt,
		nullptr
	);
	if (FAILED(hr)) return false;

	// check if the client is reneder client
	EDataFlow flow = eAll;
	IMMEndpoint* ep = nullptr;
	dev->QueryInterface(__uuidof(IMMEndpoint), (void**)&ep);
	ep->GetDataFlow(&flow);
std:wcerr << L"flow = " << (flow == eRender ? L"Render" : L"Capture") << L"\n";
	ep->Release();

	hr = ctx.client->GetService(__uuidof(IAudioRenderClient), (void**)&ctx.renderClient);
	if (FAILED(hr)) return false;

	hr = ctx.client->GetBufferSize(&ctx.bufferFrameCnt);
	if (FAILED(hr)) return false;

	// clear the buffer to avoid peak of audio
	BYTE* pData = nullptr;
	hr = ctx.renderClient->GetBuffer(ctx.bufferFrameCnt, &pData);
	if (SUCCEEDED(hr)) {
		memset(pData, 0, ctx.bufferFrameCnt * ctx.format->nBlockAlign);
		ctx.renderClient->ReleaseBuffer(ctx.bufferFrameCnt, 0);
	}

	hr = ctx.client->Start();
	return SUCCEEDED(hr);
}