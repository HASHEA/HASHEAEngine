#pragma once

#include "KEnginePub/Public/IGFX_Public.h"
#include <mutex>
#include "Engine/KGLog.h"
#include "KDelayReleaseQueue.h"

////////////////////////////////////////////////////////
#include "KEnginePub/Public/KProfileTools.h"

namespace gfx
{
	class KGFX_GraphicDevice : public IKGFX_GraphicDevice
	{
        //公共实现
    public:
        KGFX_GraphicDevice();
        ~KGFX_GraphicDevice();
        void UnInit() override;
        void FrameMove(BOOL bFrameRendered) override;

        BOOL GC_DelayReleaseObject(KGFX_DelayReleaseObject* pDeviceObj,const std::function<void()>& pfunSyncReleaseCall = nullptr);
        void ReleaseAllDeviceObjects() override;
        void ReleaseAllProgram() override;

		BOOL DestroyProgram(IKGFX_Program*& pProgram) override;
        bool CreateStaticConstBuf(IKGFX_ConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName = nullptr) override;

	private:
        TDelayReleaseQueue<KGFX_DelayReleaseObject> m_DelayRelase_Objects;
        TDelayReleaseQueue<IKGFX_Program> m_DelayRelase_Program;
	};
}
