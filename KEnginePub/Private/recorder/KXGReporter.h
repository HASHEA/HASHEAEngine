////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : KXGReporter.h
//  Creator     : HuaFei
//  Create Date : 2022
//  Purpose     : 西瓜SDK数据上报
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "KGBaseDef/Public/core_base_macro.h"
// #include "xgsdk/XGSDK_interface.h"
#include <string>

class IXGSDKInterface;
class KXGReporter
{
private:
    IXGSDKInterface* m_piXGSDK = nullptr;

public:
    bool IsValid() { return m_piXGSDK != nullptr; }

    void SetXGSDK(IXGSDKInterface* piXGSDK) { m_piXGSDK = piXGSDK; }

public:
    void TrackShaderVSFSCompileEvent(int nPlatform, const char* pcszShaderSource, const char* pcszIncludedShaderSource, const char* pcszShaderDef, const char* pcszMacro, uint32_t uHashVs, uint32_t uHashFs);
    void TrackShaderCSCompileEvent(int nPlatform, const char* pcszShaderSource, const char* pcszIncludedShaderSource, const char* pcszShaderDef, const char* pcszMacro, uint32_t uHashCS);

private:
    bool _EncodeShaderCmd(const char* pcszShaderCompileCmd, std::string& strResult);
    bool _DecodeShaderCmd(const char* pcszShaderCompileCmd, std::string& strResult);
};

extern KXGReporter g_sX3DXGSDKReporter;
