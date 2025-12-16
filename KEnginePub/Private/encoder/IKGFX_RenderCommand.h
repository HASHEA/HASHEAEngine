////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IKGFX_RenderCommand.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "KBase/ecs/ecs.h"

namespace gfx
{
    enum class ERenderCommand : int
    {
        Invalid = 0,
        
		Draw,
        DrawIndexed,
        DrawIndirect,
        DrawIndexedIndirect,
        //DrawIndirectCount,
        //DrawIndexedIndirectCount,
        Dispatch,
        DispatchIndirect,
        /*
        DispatchRays,
        DispatchRaysIndirect,
        DispatchMesh,
        DispatchMeshIndirect,
        DispatchMeshRays,
        DispatchMeshRaysIndirect,
        DispatchMeshTasks,
        DispatchMeshTasksIndirect,
        DispatchMeshTasksRays,
        DispatchMeshTasksRaysIndirect,
        */

        End
    };

    class KRenderCommand : public NSECS::Object
    {
        KECS_OBJECT_DECLARE(KRenderCommand, NSECS::Object);

    protected:
        virtual uint32_t SystemCapacity() override { return 128; }

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;

    protected:
        ERenderCommand m_eRC_Type{ERenderCommand::Invalid};

    public:
        ERenderCommand RC_GetType() const { return m_eRC_Type; }
    };

    class KRenderCommandDraw : public KRenderCommand
    {
        KECS_OBJECT_DECLARE(KRenderCommandDraw, KRenderCommand);

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;
    };

    class KRenderCommandDrawIndexed : public KRenderCommand
    {
        KECS_OBJECT_DECLARE(KRenderCommandDrawIndexed, KRenderCommand);

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;
    };

    class KRenderCommandDrawIndirect : public KRenderCommand
    {
        KECS_OBJECT_DECLARE(KRenderCommandDrawIndirect, KRenderCommand);

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;
    };

    class KRenderCommandDrawIndexedIndirect : public KRenderCommand
    {
        KECS_OBJECT_DECLARE(KRenderCommandDrawIndexedIndirect, KRenderCommand);

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;
    };

    class KRenderCommandDispatch : public KRenderCommand
    {
        KECS_OBJECT_DECLARE(KRenderCommandDispatch, KRenderCommand);

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;
    };

    class KRenderCommandDispatchIndirect : public KRenderCommand
    {
        KECS_OBJECT_DECLARE(KRenderCommandDispatchIndirect, KRenderCommand);

    public:
        virtual void OnConstruct() override; // called after new
        virtual void OnRelease() override;
    };
} // namespace gfx
