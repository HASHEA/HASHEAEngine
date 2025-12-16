//
// Created by Ant on 2024/8/19.
//
#pragma once

class KCameraBackground
{
public:
    KCameraBackground() = default;
    virtual ~KCameraBackground() = default;

    virtual BOOL Init() = 0;
    virtual BOOL Render(const KCameraRenderParam& pParam) = 0;
    virtual void UnInit() = 0;
    virtual void Recreate() = 0;
};
