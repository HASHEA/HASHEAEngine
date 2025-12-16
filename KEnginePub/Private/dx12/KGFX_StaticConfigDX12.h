#pragma once
namespace gfx
{
    /**
     * 会开启每次复制描述符时判断是否一致从而减少拷贝（有可能导致数据错误的bug）
     */
    constexpr bool bOpenEveryDescriptorCopyOptimization = true;


    /**
     * 开启之后会给所有DX12资源设置debug名字
     */
    constexpr bool bOpenSetDx12ResourceName = true;
}


