#pragma once
#include <atlcomcli.h>
#include "KGFX_GraphiceDeviceDx12.h"
#include "Engine/Utf8AndWideChar.h"
#include "KLog/Public/KLog.h"
#include "nvapi.h"
namespace gfx
{
    static void DumpDred(ID3D12Device* device)
    {
        if (!device)
            return;

        HRESULT reason = device->GetDeviceRemovedReason();
        KGLogPrintf(KGLOG_ERR, "[DRED] DeviceRemovedReason=0x%08X", reason);

        CComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred1))))
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 ab{};
            D3D12_DRED_PAGE_FAULT_OUTPUT1 pf{};
            dred1->GetAutoBreadcrumbsOutput1(&ab);
            dred1->GetPageFaultAllocationOutput1(&pf);

            for (const D3D12_AUTO_BREADCRUMB_NODE1* node = ab.pHeadAutoBreadcrumbNode; node; node = node->pNext)
            {
                const char* clName = "";
                const char* cqName = "";
                char clNameU8[256] = {};
                char cqNameU8[256] = {};

                if (node->pCommandListDebugNameW)
                {
                    WideCharToUtf8(clNameU8, 256, node->pCommandListDebugNameW);
                    clName = clNameU8;
                }

                if (node->pCommandQueueDebugNameW)
                {
                    WideCharToUtf8(cqNameU8, 256, node->pCommandQueueDebugNameW);
                    cqName = cqNameU8;
                }

                uint32_t last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;

                KGLogPrintf(KGLOG_ERR, "[DRED][AB] CQ:%s CL:%s Last:%u Count:%u",cqName, clName, last, node->BreadcrumbCount);

                if (node->pCommandHistory && node->BreadcrumbCount)
                {
                    const UINT idx = (last < node->BreadcrumbCount) ? last : (node->BreadcrumbCount - 1);
                    auto lastOp = node->pCommandHistory[idx];
                    KGLogPrintf(KGLOG_ERR, "[DRED][AB] LastOp(enum)=%u", (uint32_t)lastOp);
                }

                for (UINT i = 0; i < node->BreadcrumbContextsCount; ++i)
                {
                    const auto& ctx = node->pBreadcrumbContexts[i];
                    WideCharToUtf8(cqNameU8, 256, ctx.pContextString);
                    KGLogPrintf(KGLOG_ERR, "[DRED][CTX] Index=%u Text=%s", ctx.BreadcrumbIndex, cqNameU8);
                }
            }

            if (pf.PageFaultVA)
            {
                KGLogPrintf(KGLOG_ERR, "[DRED][PF] VA=0x%p", (void*)pf.PageFaultVA);
                for (const D3D12_DRED_ALLOCATION_NODE1* n = pf.pHeadExistingAllocationNode; n; n = n->pNext)
                {
                    const char* nm = "";
                    char nmU8[256] = {};
                    if (n->ObjectNameW)
                    {
                        WideCharToUtf8(nmU8, 256, n->ObjectNameW);
                        nm = nmU8;
                    }
                    KGLogPrintf(KGLOG_ERR, "[DRED][PF][Alive] Type=%d Name=%s", (int)n->AllocationType, nm);
                }
                for (const D3D12_DRED_ALLOCATION_NODE1* n = pf.pHeadRecentFreedAllocationNode; n; n = n->pNext)
                {
                    const char* nm = "";
                    char nmU8[256] = {};
                    if (n->ObjectNameW) { WideCharToUtf8(nmU8, 256, n->ObjectNameW); nm = nmU8; }
                    KGLogPrintf(KGLOG_ERR, "[DRED][PF][Freed] Type=%d Name=%s", (int)n->AllocationType, nm);
                }
            }
            return;
        }

    }

    /**
     * 会打印设备丢失时候的堆栈信息，可以在常见的GPU发生处加上调用
     * @param hrRes 
     */
    static void CheckDeviceRemoveReason(HRESULT hrRes)
    {
        if (hrRes == DXGI_ERROR_DEVICE_REMOVED || hrRes == DXGI_ERROR_DEVICE_RESET)
        {
            KGFX_GraphicDeviceDx12* gfxDevice = gfx::KGFX_GetGraphicDeviceDx12Internal();
            ID3D12Device* pDevice = gfxDevice->GetDXDevice();
            DumpDred(pDevice);
            /// 打印崩溃日志完成
            if (DrvOption::bEnableRayTracing && DrvOption::bEnableRayTracingValidation)
            {
                ID3D12Device5* m_pD3dDevice5 = nullptr;
                pDevice->QueryInterface(IID_PPV_ARGS(&m_pD3dDevice5));
                NvAPI_D3D12_FlushRaytracingValidationMessages(m_pD3dDevice5);
                SAFE_RELEASE(m_pD3dDevice5);
            }    
        }
    }

} // namespace
