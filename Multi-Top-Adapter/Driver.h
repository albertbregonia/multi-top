#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>

#include "Trace.h"

namespace Microsoft {
    namespace WRL {
        namespace Wrappers {
            // Adds a wrapper for thread handles to the existing set of WRL handle wrapper classes
            typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
        }
    }
}

namespace VirtualMonitor {
    
    const struct Resolution {
        DWORD width;
        DWORD height;
    };

    //Manages the creation and lifetime of a Direct3D render device.
    struct Direct3DDevice {
        Direct3DDevice(LUID adapterLUID);
        Direct3DDevice();
        HRESULT Init();

        LUID adapterLUID;
        Microsoft::WRL::ComPtr<IDXGIFactory5> dxgiFactory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    };

    //Manages a thread that consumes buffers from an indirect display swap-chain object.
    class SwapChainProcessor {
        private:
            static DWORD CALLBACK RunThread(LPVOID arg);
            void Run();
            IDDCX_SWAPCHAIN m_hSwapChain;
            std::shared_ptr<Direct3DDevice> m_Device;
            HANDLE m_hAvailableBufferEvent;
            Microsoft::WRL::Wrappers::Thread m_hThread;
            Microsoft::WRL::Wrappers::Event m_hTerminateEvent;
        public:
            SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> device, HANDLE frameEvent);
            ~SwapChainProcessor();
    };

    // Provides a sample implementation of an indirect display driver.
    class IndirectDeviceContext {
        protected:
            WDFDEVICE m_WdfDevice;
            IDDCX_ADAPTER m_Adapter;
        public:
            IndirectDeviceContext(_In_ WDFDEVICE wdfDevice);

            void InitializeAdapter();
            void FinishInitialization(UINT connectorIndex);
    };

    class IndirectMonitorContext {
        private:
            IDDCX_MONITOR m_Monitor;
            std::unique_ptr<SwapChainProcessor> m_ProcessingThread;
        public:
            IndirectMonitorContext(_In_ IDDCX_MONITOR monitor);
            virtual ~IndirectMonitorContext();

            void AssignSwapChain(IDDCX_SWAPCHAIN swapChain, LUID renderAdapter, HANDLE frameEvent);
            void UnassignSwapChain();
    };
}
