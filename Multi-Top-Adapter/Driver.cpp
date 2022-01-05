// Multi-Top (c) Albert Bregonia 2021

// Much of this code is borrows from https://github.com/microsoft/Windows-driver-samples/blob/master/video/IndirectDisplay/
// It has merely been modified with my code style, comments, and supported resolutions/refresh rates

#include "Driver.h"
#include "Driver.tmh"

using namespace std;
using namespace VirtualMonitor;
using namespace Microsoft::WRL;

#pragma region Constants

static constexpr const Resolution resolutions[] = {
    {3840, 2160},
    {3440, 1440},
    {2560, 1440},
    {2560, 1080},
    {2048, 1152},
    {1920, 1200},
    {1920, 1080},
    {1680, 1050},
    {1600, 900},
    {1536, 864},
    {1440, 900},
    {1366, 768},
    {1280, 1024},
    {1280, 800},
    {1280, 720},
    {1024, 768},
    {800, 600},
    {640, 360},
};

static constexpr DWORD 
    refreshRates[] = { 60, 75, 120, 144, 240 },
    MONITOR_COUNT = 1; //EDID-less monitor(s)

static constexpr UINT 
    resCount = sizeof(resolutions) / sizeof(Resolution),
    rateCount = sizeof(refreshRates) / sizeof(DWORD);

extern "C" DRIVER_INITIALIZE DriverEntry;

//standard driver functions
EVT_WDF_DRIVER_DEVICE_ADD VirtualMonitorAdd;
EVT_WDF_DEVICE_D0_ENTRY VirtualMonitorD0Entry;

//display adapter to connect the virtual monitor
EVT_IDD_CX_ADAPTER_INIT_FINISHED VMAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES VMAdapterCommitModes;

//required monitor handlers
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION VMParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES VMGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES VMQueryTargetModes;

EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN VMAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN VMUnassignSwapChain;

struct IndirectDeviceContextWrapper {
    IndirectDeviceContext* context;
    void Cleanup() {
        delete context;
        context = nullptr;
    }
};

struct IndirectMonitorContextWrapper {
    IndirectMonitorContext* context;
    void Cleanup() {
        delete context;
        context = nullptr;
    }
};

// This macro creates the methods for accessing an IndirectDeviceContextWrapper as a context for a WDF object
WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);
WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContextWrapper);

#pragma endregion

#pragma region Driver Initialization

//https://docs.microsoft.com/en-us/windows/win32/dlls/dllmain - unused
 extern "C" bool WINAPI DllMain(_In_ HINSTANCE hInstance, _In_ UINT dwReason, _In_opt_ LPVOID lpReserved) {
     UNREFERENCED_PARAMETER(hInstance);
     UNREFERENCED_PARAMETER(lpReserved);
     UNREFERENCED_PARAMETER(dwReason);
     return true;
 }

_Use_decl_annotations_ //driver version of main()
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, VirtualMonitorAdd); //register entry point for adding a virtual monitor and the display adapter
    NTSTATUS status = WdfDriverCreate(driverObject, registryPath, &attributes, &config, WDF_NO_HANDLE); //create driver object
    if(!NT_SUCCESS(status))
        return status;
    return status;
}

_Use_decl_annotations_ //create virtual monitor and display adapter with the corresponding event handlers
NTSTATUS VirtualMonitorAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    NTSTATUS status = STATUS_SUCCESS;
    
    WDF_PNPPOWER_EVENT_CALLBACKS powerHandler; //power event handler
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&powerHandler); //power-on is the only event handler needed
    powerHandler.EvtDeviceD0Entry = VirtualMonitorD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &powerHandler);
    
    IDD_CX_CLIENT_CONFIG config; //register monitor and display adapter event handlers
    IDD_CX_CLIENT_CONFIG_INIT(&config);
    config.EvtIddCxAdapterInitFinished = VMAdapterInitFinished;
    config.EvtIddCxParseMonitorDescription = VMParseMonitorDescription;
    config.EvtIddCxMonitorGetDefaultDescriptionModes = VMGetDefaultModes;
    config.EvtIddCxMonitorQueryTargetModes = VMQueryTargetModes;
    config.EvtIddCxAdapterCommitModes = VMAdapterCommitModes;
    config.EvtIddCxMonitorAssignSwapChain = VMAssignSwapChain;
    config.EvtIddCxMonitorUnassignSwapChain = VMUnassignSwapChain;
    status = IddCxDeviceInitConfig(deviceInit, &config);
    if(!NT_SUCCESS(status)) //if registration fails, return the error
        return status;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, IndirectDeviceContextWrapper);
    attributes.EvtCleanupCallback = [](WDFOBJECT object) {
        auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(object);
        if(wrapper) //destructor
            wrapper->Cleanup();
    };

    WDFDEVICE device = nullptr;
    status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    if(!NT_SUCCESS(status))
        return status;

    status = IddCxDeviceInitialize(device); //create context object, attach it to the WDF device object
    WdfObjectGet_IndirectDeviceContextWrapper(device)->context = new IndirectDeviceContext(device);
    return status;
}

_Use_decl_annotations_  //WDF starts the device in the fully-on power state.
NTSTATUS VirtualMonitorD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE state) {
    UNREFERENCED_PARAMETER(state);
    WdfObjectGet_IndirectDeviceContextWrapper(device)->context->InitializeAdapter();
    return STATUS_SUCCESS;
}

#pragma endregion

#pragma region Event Handlers

_Use_decl_annotations_ //reconfigure device to commit new modes - unused
NTSTATUS VMAdapterCommitModes(IDDCX_ADAPTER adapter, const IDARG_IN_COMMITMODES* inArgs) {
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(inArgs);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ //generate monitor modes for an EDID by parsing it - unused
NTSTATUS VMParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* inArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* outArgs) {
    UNREFERENCED_PARAMETER(inArgs);
    UNREFERENCED_PARAMETER(outArgs);
    return STATUS_SUCCESS; //i create EDID-less monitors only so this function does nothing
}

_Use_decl_annotations_ //set up monitors if the adapter was setup
NTSTATUS VMAdapterInitFinished(IDDCX_ADAPTER adapter, const IDARG_IN_ADAPTER_INIT_FINISHED* inArgs) {
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(adapter);
    if (NT_SUCCESS(inArgs->AdapterInitStatus))
        for(int i=0; i<MONITOR_COUNT; i++)
            wrapper->context->FinishInitialization(i);
    return STATUS_SUCCESS;
}

static inline void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& mode, DWORD width, DWORD height, DWORD vsync, bool vsyncFreqDivider) {
    mode.totalSize.cx = mode.activeSize.cx = width;
    mode.totalSize.cy = mode.activeSize.cy = height;
    //https://docs.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_video_signal_info
    mode.AdditionalSignalInfo.vSyncFreqDivider = vsyncFreqDivider ? 0 : 1;
    mode.AdditionalSignalInfo.videoStandard = 255;
    mode.vSyncFreq.Numerator = vsync;
    mode.hSyncFreq.Numerator = vsync * height;
    mode.hSyncFreq.Denominator = mode.vSyncFreq.Denominator = 1;
    mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    mode.pixelRate = ((UINT64)vsync) * ((UINT64)width) * ((UINT64)height);
}

static IDDCX_MONITOR_MODE CreateMonitorMode(DWORD width, DWORD height, DWORD vsync, IDDCX_MONITOR_MODE_ORIGIN origin=IDDCX_MONITOR_MODE_ORIGIN_DRIVER) {
    IDDCX_MONITOR_MODE mode = {};
    mode.Size = sizeof(mode);
    mode.Origin = origin;
    FillSignalInfo(mode.MonitorVideoSignalInfo, width, height, vsync, true);
    return mode;
}

static IDDCX_TARGET_MODE CreateTargetMode(DWORD width, DWORD height, DWORD vsync) {
    IDDCX_TARGET_MODE mode = {};
    mode.Size = sizeof(mode);
    FillSignalInfo(mode.TargetVideoSignalInfo.targetVideoSignalInfo, width, height, vsync, false);
    return mode;
}

_Use_decl_annotations_ //generate monitor modes for a monitor with no EDID.
NTSTATUS VMGetDefaultModes(IDDCX_MONITOR monitor, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* inArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* outArgs) {
    UNREFERENCED_PARAMETER(monitor);
    outArgs->DefaultMonitorModeBufferOutputCount = resCount * rateCount;
    if(inArgs->DefaultMonitorModeBufferInputCount > 0) {
        for(UINT i=0; i<resCount; i++) //create valid monitor modes
            for(UINT z=0; z<rateCount; z++)
                inArgs->pDefaultMonitorModes[(rateCount * i) + z] =
                    CreateMonitorMode(
                        resolutions[i].width,
                        resolutions[i].height,
                        refreshRates[z],
                        IDDCX_MONITOR_MODE_ORIGIN_DRIVER
                    );
        outArgs->PreferredMonitorModeIdx = 0;
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VMQueryTargetModes(IDDCX_MONITOR monitor, const IDARG_IN_QUERYTARGETMODES* inArgs, IDARG_OUT_QUERYTARGETMODES* outArgs) {
    UNREFERENCED_PARAMETER(monitor);
    outArgs->TargetModeBufferOutputCount = resCount * rateCount;
    if(inArgs->TargetModeBufferInputCount >= resCount)
        for(UINT i=0; i<resCount; i++)
            for(UINT z=0; z<rateCount; z++)
                inArgs->pTargetModes[(rateCount * i) + z] =
                    CreateTargetMode(resolutions[i].width, resolutions[i].height, refreshRates[z]);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VMAssignSwapChain(IDDCX_MONITOR monitor, const IDARG_IN_SETSWAPCHAIN* inArgs) {
    WdfObjectGet_IndirectMonitorContextWrapper(monitor)->context->
        AssignSwapChain(inArgs->hSwapChain, inArgs->RenderAdapterLuid, inArgs->hNextSurfaceAvailable);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VMUnassignSwapChain(IDDCX_MONITOR monitor) {
    WdfObjectGet_IndirectMonitorContextWrapper(monitor)->context->UnassignSwapChain();
    return STATUS_SUCCESS;
}

#pragma endregion

#pragma region IndirectDeviceContext

IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE wdfDevice) : m_WdfDevice(wdfDevice) { m_Adapter = {}; }

//Adapter info
void IndirectDeviceContext::InitializeAdapter() {
    IDDCX_ADAPTER_CAPS adapterInfo = {};
    adapterInfo.Size = sizeof(adapterInfo);
    //required info
    //basic feature support for the adapter
    adapterInfo.MaxMonitorsSupported = MONITOR_COUNT;
    adapterInfo.EndPointDiagnostics.Size = sizeof(adapterInfo.EndPointDiagnostics);
    adapterInfo.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    adapterInfo.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    //device strings for telemetry (required)
    adapterInfo.EndPointDiagnostics.pEndPointFriendlyName = L"Multi-Top Virtual Monitor";
    adapterInfo.EndPointDiagnostics.pEndPointManufacturerName = L"Albert Bregonia";
    adapterInfo.EndPointDiagnostics.pEndPointModelName = L"Multi-Top Virtual Monitor";
    //hardware and firmware versions 
    IDDCX_ENDPOINT_VERSION version = {};
    version.Size = sizeof(version);
    version.MajorVer = 1;
    adapterInfo.EndPointDiagnostics.pFirmwareVersion = &version;
    adapterInfo.EndPointDiagnostics.pHardwareVersion = &version;

    // Initialize a WDF context that can store a pointer to the device context object
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, IndirectDeviceContextWrapper);

    IDARG_IN_ADAPTER_INIT adapterInit = {};
    adapterInit.WdfDevice = m_WdfDevice;
    adapterInit.pCaps = &adapterInfo;
    adapterInit.ObjectAttributes = &attributes;

    //initialize the adapter, which will trigger the AdapterFinishInit callback later
    IDARG_OUT_ADAPTER_INIT adapterInitOut;
    if(FAILED(IddCxAdapterInitAsync(&adapterInit, &adapterInitOut)))
        return;
    m_Adapter = adapterInitOut.AdapterObject; //store a reference to the WDF adapter handle
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(adapterInitOut.AdapterObject);
    wrapper->context = this; //store the device context in the wrapper
}

void IndirectDeviceContext::FinishInitialization(UINT connectorIndex) {
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, IndirectMonitorContextWrapper);

    IDDCX_MONITOR_INFO info = {};
    info.Size = sizeof(info);
    info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED;
    info.ConnectorIndex = connectorIndex; //report a monitor right away
    info.MonitorDescription.Size = sizeof(info.MonitorDescription);
    info.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    info.MonitorDescription.DataSize = 0; //no EDID
    info.MonitorDescription.pData = nullptr;

    //container ID should be distinct from "this" device's container ID if the monitor is not permanently attached to the adapter
    //as the latter is false, it doesn't matter and a random container ID GUID is generated
    CoCreateGuid(&info.MonitorContainerId);
    IDARG_IN_MONITORCREATE monitorCreate = {};
    monitorCreate.ObjectAttributes = &attributes;
    monitorCreate.pMonitorInfo = &info;

    //create a monitor object with the specified monitor descriptor
    IDARG_OUT_MONITORCREATE monitorCreateOut;
    if(FAILED(IddCxMonitorCreate(m_Adapter, &monitorCreate, &monitorCreateOut)))
        return;
    //create a monitor and attach to the IDD
    auto* wrapper = WdfObjectGet_IndirectMonitorContextWrapper(monitorCreateOut.MonitorObject);
    wrapper->context = new IndirectMonitorContext(monitorCreateOut.MonitorObject);
    IDARG_OUT_MONITORARRIVAL pluggedInEvent; //tell the OS that the monitor has been plugged in
    IddCxMonitorArrival(monitorCreateOut.MonitorObject, &pluggedInEvent);
}

#pragma endregion

#pragma region IndirectMonitor

IndirectMonitorContext::IndirectMonitorContext(_In_ IDDCX_MONITOR monitor) : m_Monitor(monitor) {}
IndirectMonitorContext::~IndirectMonitorContext() { m_ProcessingThread.reset(); }

void IndirectMonitorContext::AssignSwapChain(IDDCX_SWAPCHAIN swapChain, LUID renderAdapter, HANDLE frameEvent) {
    m_ProcessingThread.reset();
    auto device = make_shared<Direct3DDevice>(renderAdapter);
    if(FAILED(device->Init())) //delete the swap-chain if D3D initialization fails, so that the OS knows to generate a new swap-chain and try again.
        WdfObjectDelete(swapChain);
    else // Create a new swap-chain processing thread
        m_ProcessingThread.reset(new SwapChainProcessor(swapChain, device, frameEvent));
}

// Stop processing the last swap-chain
void IndirectMonitorContext::UnassignSwapChain() { m_ProcessingThread.reset(); }

#pragma endregion

//code that handles Windows compatibility like DirectX and thread handling

#pragma region SwapChainProcessor

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, shared_ptr<Direct3DDevice> device, HANDLE frameEvent) : 
m_hSwapChain(hSwapChain), m_Device(device), m_hAvailableBufferEvent(frameEvent) {
    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    // Immediately create and run the swap-chain processing thread, passing 'this' as the thread parameter
    m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
}

SwapChainProcessor::~SwapChainProcessor() {
    // Alert the swap-chain processing thread to terminate
    SetEvent(m_hTerminateEvent.Get());
    if(m_hThread.Get()) //block thread
        WaitForSingleObject(m_hThread.Get(), INFINITE);
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID arg) {
    reinterpret_cast<SwapChainProcessor*>(arg)->Run();
    return 0;
}

//Multimedia Class Scheduler Service should be used for better performance but we only need the bare minimum
void SwapChainProcessor::Run() {
    DWORD avTask = 0;
    HANDLE avTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &avTask);
    WdfObjectDelete((WDFOBJECT)m_hSwapChain); //delete swap chain when it ends to get a new one
    m_hSwapChain = nullptr;
    AvRevertMmThreadCharacteristics(avTaskHandle);
}

#pragma endregion

#pragma region Direct3DDevice

Direct3DDevice::Direct3DDevice(LUID adapterLUID) : adapterLUID(adapterLUID) {}
Direct3DDevice::Direct3DDevice() : adapterLUID(LUID{}) {}
HRESULT Direct3DDevice::Init() {
    HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
    if(FAILED(result))
        return result;
    result = dxgiFactory->EnumAdapterByLuid(adapterLUID, IID_PPV_ARGS(&adapter));
    if(FAILED(result))
        return result;
    // Create a D3D device using the render adapter. BGRA support is required by the WHQL test suite.
    result = D3D11CreateDevice(
        adapter.Get(), 
        D3D_DRIVER_TYPE_UNKNOWN, 
        nullptr, 
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, 
        nullptr, 
        0, 
        D3D11_SDK_VERSION, 
        &device, 
        nullptr, 
        &deviceContext
    );
    if(FAILED(result)) //system is in a transient state or the render GPU was lost
        return result;
    return S_OK;
}

#pragma endregion