#ifdef __MACOS__
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#else
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#endif

//https://github.com/KhronosGroup/MoltenVK/issues/78
//https://github.com/ikryukov/QtVulkan
//https://www.qt.io/blog/2018/05/30/vulkan-for-qt-on-macos

static CALayer* orilayer;

void MakeViewMetalCompatible(void* handle)
{
#ifdef __MACOS__
    NSView* view = (__bridge NSView*)handle;
    [view setNeedsDisplay:YES];
    assert([view isKindOfClass:[NSView class]]);

    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        CAMetalLayer *metalLayer = [CAMetalLayer new]; // Calls alloc + init

        [view setWantsLayer : YES];
        [view setLayer : metalLayer];
        
        metalLayer.contentsScale = [view.window backingScaleFactor];
    }
    
    
#else
    UIView* view = (__bridge  UIView*)handle;
    assert([view isKindOfClass:[UIView class]]);

    CAMetalLayer *metalLayer = [CAMetalLayer new]; // Calls alloc + init
    //UIView uiView = ...; // From swapchain create info

    CGSize viewSize = view.frame.size;
    metalLayer.frame = view.frame;
    metalLayer.opaque = true;
    metalLayer.framebufferOnly = true;
    metalLayer.drawableSize = viewSize;
    metalLayer.pixelFormat = (MTLPixelFormat)80;//BGRA8Unorm==80
    [view.layer addSublayer:metalLayer];
#endif
}

//void MakeViewMetalCompatible(void* handle)
//{
//  NSView* view = (NSView*)handle;
//  assert([view isKindOfClass:[NSView class]]);
//
//  if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
//    orilayer = [view layer];
//    [view setLayer:[CAMetalLayer layer]];
//    [view setWantsLayer:NO];
//  }
//}

void UnMakeViewMetalCompatible(void* handle)
{
#ifdef __MACOS__
    NSView* view = (__bridge NSView*)handle;
    assert([view isKindOfClass:[NSView class]]);
#else
    UIView* view = (__bridge UIView*)handle;
    assert([view isKindOfClass:[UIView class]]);
#endif

    if ([view.layer isKindOfClass:[CAMetalLayer class]])
    {
       // [view setLayer:orilayer];
    }
}
