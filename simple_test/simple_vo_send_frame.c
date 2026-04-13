#include "rk_type.h"
#include "rk_mpi_vo.h"
#include "rk_comm_vo.h"
#include "rk_mpi_mb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RV1126B
#define RK356X_VO_DEV_HD0 0
#define RK356X_VOP_LAYER_CLUSTER_0 0
#define RK356X_VOP_LAYER_ESMART_0 1
#elif RK3588
#define RK356X_VO_DEV_HD0 3
#define RK356X_VOP_LAYER_CLUSTER_0 0
#define RK356X_VOP_LAYER_ESMART_0 7
#else
#define RK356X_VO_DEV_HD0 1
#define RK356X_VOP_LAYER_CLUSTER_0 0
#define RK356X_VOP_LAYER_ESMART_0 5
#endif

// 测试函数：初始化VO设备并进行基本配置
int testVOInitAndConfig() {
    // 初始化相关变量
    VO_PUB_ATTR_S VoPubAttr;
    VO_VIDEO_LAYER_ATTR_S stLayerAttr;
    VO_LAYER VoLayer = RK356X_VOP_LAYER_CLUSTER_0;
    VO_DEV VoDev = RK356X_VO_DEV_HD0;
    RK_S32 s32Ret;

    // 绑定图层到设备
    s32Ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_VIDEO);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_BindLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }
    printf("Layer %d is successfully bound to device %d.\n", VoLayer, VoDev);

    // 初始化VO设备公共属性结构体
    memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
    VoPubAttr.enIntfType = VO_INTF_MIPI;
    VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

    // 设置VO设备公共属性
    s32Ret = RK_MPI_VO_SetPubAttr(VoDev, &VoPubAttr);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_SetPubAttr failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 启用VO设备
    s32Ret = RK_MPI_VO_Enable(VoDev);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_Enable failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 初始化图层属性结构体
    memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
    stLayerAttr.enPixFormat = RK_FMT_YUV420SP;
    stLayerAttr.stDispRect.s32X = 0;
    stLayerAttr.stDispRect.s32Y = 0;
    stLayerAttr.stDispRect.u32Width = 240;
    stLayerAttr.stDispRect.u32Height = 320;
    stLayerAttr.stImageSize.u32Width = 1920;
    stLayerAttr.stImageSize.u32Height = 1080;
    stLayerAttr.u32DispFrmRt = 25;
    stLayerAttr.enCompressMode = COMPRESS_AFBC_16x16;

    // 设置图层属性
    s32Ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_SetLayerAttr failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 使能图层
    s32Ret = RK_MPI_VO_EnableLayer(VoLayer);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_EnableLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }
    sleep(1);
    // 禁用图层
    s32Ret = RK_MPI_VO_DisableLayer(VoLayer);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_DisableLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 解绑图层
    s32Ret = RK_MPI_VO_UnBindLayer(VoLayer, VoDev);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_UnBindLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }
    printf("Layer %d is successfully unbound from device %d.\n", VoLayer, VoDev);

    // 禁用VO设备
    s32Ret = RK_MPI_VO_Disable(VoDev);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_Disable failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    return RK_SUCCESS;
}

// 测试函数：进行UI绘制测试
int testUIDrawing() {
    // 初始化相关变量
    VO_PUB_ATTR_S stVoPubAttr;
    VO_VIDEO_LAYER_ATTR_S stLayerAttr;
    VIDEO_FRAME_INFO_S stVFrame;
    VO_LAYER VoLayer = RK356X_VOP_LAYER_ESMART_0;
    VO_DEV VoDev = RK356X_VO_DEV_HD0;
    RK_VOID *pMblk = RK_NULL;
    RK_U32 s32Ret;
    RK_U32 u32BuffSize;
    RK_U8 *pData;

    // 申请单buffer用于UI绘制
    u32BuffSize = RK_MPI_VO_CreateGraphicsFrameBuffer(1080, 1920, RK_FMT_RGBA8888, &pMblk);
    if (u32BuffSize == 0) {
        printf("RK_MPI_VO_CreateGraphicsFrameBuffer failed\n");
        return RK_FAILURE;
    }

    // 绑定图层到设备
    s32Ret = RK_MPI_VO_BindLayer(VoLayer, VoDev, VO_LAYER_MODE_GRAPHIC);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_BindLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }
    printf("Layer %d is successfully bound to device %d.\n", VoLayer, VoDev);

    // 初始化VO设备公共属性结构体
    memset(&stVoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
    stVoPubAttr.enIntfType = VO_INTF_MIPI;
    // stVoPubAttr.enIntfSync = VO_OUTPUT_1080P60;
    stVoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

    // 设置VO设备公共属性
    s32Ret = RK_MPI_VO_SetPubAttr(VoDev, &stVoPubAttr);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_SetPubAttr failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 启用VO设备
    s32Ret = RK_MPI_VO_Enable(VoDev);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_Enable failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 初始化图层属性结构体
    memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
    stLayerAttr.enPixFormat = RK_FMT_RGBA8888;
    stLayerAttr.stDispRect.s32X = 0;
    stLayerAttr.stDispRect.s32Y = 0;
    stLayerAttr.stDispRect.u32Width = 1080;
    stLayerAttr.stDispRect.u32Height = 1920;
    stLayerAttr.stImageSize.u32Width = 1080;
    stLayerAttr.stImageSize.u32Height = 1920;

    // 设置图层属性
    s32Ret = RK_MPI_VO_SetLayerAttr(VoLayer, &stLayerAttr);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_SetLayerAttr failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 使能图层
    s32Ret = RK_MPI_VO_EnableLayer(VoLayer);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_EnableLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 设置图像数据到stVFrame
    pData = (RK_U8 *)RK_MPI_MB_Handle2VirAddr(pMblk);
    memset(pData, 0xaa, u32BuffSize / 2);
    stVFrame.stVFrame.pMbBlk = pMblk;
    stVFrame.stVFrame.u32Width = 1080;
    stVFrame.stVFrame.u32Height = 1920;
    stVFrame.stVFrame.u32VirWidth = 1080;
    stVFrame.stVFrame.u32VirHeight = 1920;
    stVFrame.stVFrame.enPixelFormat = RK_FMT_RGBA8888;
    // 这里可添加填充图像数据的代码，例如memset(stVFrame.pData, 0xff, stVFrame.u32Size);

    // 发送图像到图层显示
    s32Ret = RK_MPI_VO_SendLayerFrame(VoLayer, &stVFrame);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_SendLayerFrame failed with error code: %x\n", s32Ret);
        return s32Ret;
    }
    sleep(5);

    // 释放buffer
    RK_MPI_VO_DestroyGraphicsFrameBuffer(pMblk);

    // 禁用图层
    s32Ret = RK_MPI_VO_DisableLayer(VoLayer);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_DisableLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 禁用VO设备
    s32Ret = RK_MPI_VO_Disable(VoDev);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_Disable failed with error code: %x\n", s32Ret);
        return s32Ret;
    }

    // 解绑图层
    s32Ret = RK_MPI_VO_UnBindLayer(VoLayer, VoDev);
    if (s32Ret != RK_SUCCESS) {
        printf("RK_MPI_VO_UnBindLayer failed with error code: %x\n", s32Ret);
        return s32Ret;
    }
    printf("Layer %d is successfully unbound from device %d.\n", VoLayer, VoDev);

    return RK_SUCCESS;
}

int main() {
    // 初始化RK MPI系统
    RK_S32 ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        printf("RK_MPI_SYS_Init failed with error code: %x\n", ret);
        return ret;
    }

    // 执行VO初始化和配置测试
    ret = testVOInitAndConfig();
    if (ret != RK_SUCCESS) {
        printf("VO initialization and configuration test failed\n");
        goto cleanup;
    }

    // 执行UI绘制测试
    ret = testUIDrawing();
    if (ret != RK_SUCCESS) {
        printf("UI drawing test failed\n");
        goto cleanup;
    }

    printf("All VO tests passed\n");

cleanup:
    // 反初始化RK MPI系统
    RK_MPI_SYS_Exit();
    return ret;
}