// editor_test.mm  (macOS only)
// Loads the built plugin, opens its panel in an offscreen window the way a host
// does, renders screenshots, drives the controls with synthetic mouse events,
// checks host automation messages, and closes it again.
//
//   editor_test <plugin binary> <output dir for PNGs>

#import <Cocoa/Cocoa.h>

#include "../Source/VST2/vst2_abi.h"
#include "../Source/VST2/Params.h"
#include "../Source/VST2/Editor.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static int automateCalls = 0, beginCalls = 0, endCalls = 0;
static VstIntPtr VST_CALL hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    switch (opcode)
    {
        case audioMasterVersion:   return 2400;
        case audioMasterAutomate:  ++automateCalls; return 0;
        case audioMasterBeginEdit: ++beginCalls; return 1;
        case audioMasterEndEdit:   ++endCalls; return 1;
        default:                   return 0;
    }
}

typedef AEffect* (*EntryProc) (audioMasterCallback);

static VstIntPtr call (AEffect* e, VstInt32 op, VstInt32 idx = 0, VstIntPtr val = 0, void* ptr = 0, float opt = 0.0f)
{
    return e->dispatcher (e, op, idx, val, ptr, opt);
}

static NSData* renderPNG (NSView* v, const std::string& path)
{
    const NSRect b = [v bounds];
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect: b];
    [v cacheDisplayInRect: b toBitmapImageRep: rep];
    NSData* png = [rep representationUsingType: NSPNGFileType properties: [NSDictionary dictionary]];
    [png writeToFile: [NSString stringWithUTF8String: path.c_str()] atomically: YES];
    std::printf ("  wrote %s (%lu bytes)\n", path.c_str(), (unsigned long) [png length]);
    return png;
}

// Send a mouse event at a point in the panel's (flipped) coordinates.
static void mouse (NSWindow* w, NSView* v, NSEventType type, NSPoint panelPt, int clicks = 1)
{
    const NSPoint wp = [v convertPoint: panelPt toView: nil];
    NSEvent* e = [NSEvent mouseEventWithType: type location: wp modifierFlags: 0 timestamp: 0
                                windowNumber: [w windowNumber] context: nil eventNumber: 0
                                  clickCount: clicks pressure: 1.0f];
    if (type == NSLeftMouseDown) [v mouseDown: e];
    else if (type == NSLeftMouseDragged) [v mouseDragged: e];
    else [v mouseUp: e];
}

static void pump (double secs)
{
    [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: secs]];
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: %s <plugin binary> <png dir>\n", argv[0]); return 2; }
    setvbuf (stdout, 0, _IONBF, 0);   // unbuffered, so a crash still shows how far it got
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const std::string dir = argv[2];

    // Keep the test's presets away from any real ones
    const std::string presetFile = dir + "/test_presets/Presets.txt";
    setenv ("TB117_PRESET_FILE", presetFile.c_str(), 1);
    std::remove (presetFile.c_str());

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    EntryProc entry = (EntryProc) dlsym (lib, "VSTPluginMain");
    if (! entry) { std::printf ("no entry point\n"); return 1; }
    AEffect* e = entry (hostCallback);
    call (e, effOpen);
    call (e, effSetSampleRate, 0, 0, 0, 44100.0f);
    call (e, effMainsChanged, 0, 1);

    using namespace tb117;
    std::printf ("Editor\n");
    check ((e->flags & effFlagsHasEditor) != 0, "plugin reports a custom editor");
    ERect* r = 0;
    call (e, effEditGetRect, 0, 0, &r);
    check (r && r->right - r->left == kEditorWidth && r->bottom - r->top == kEditorHeight,
           "editor size as declared");

    NSWindow* win = [[NSWindow alloc] initWithContentRect: NSMakeRect (0, 0, kEditorWidth, kEditorHeight)
                                                styleMask: NSBorderlessWindowMask
                                                  backing: NSBackingStoreBuffered defer: NO];
    NSView* parent = [win contentView];
    check (call (e, effEditOpen, 0, 0, parent) == 1, "effEditOpen succeeds");
    check ([[parent subviews] count] == 1, "panel view added to the host's view");
    NSView* panel = [[parent subviews] count] ? [[parent subviews] objectAtIndex: 0] : nil;
    if (! panel) return 1;
    check (NSEqualSizes ([panel frame].size, NSMakeSize (kEditorWidth, kEditorHeight)), "panel has the declared size");

    NSData* pngDefault = renderPNG (panel, dir + "/panel_default.png");
    check ([pngDefault length] > 10000, "panel renders");

    std::printf ("\nControls\n");
    // Drag the WIDTH knob up 50 px: +0.25
    e->setParameter (e, kWidth, 0.5f);
    const float g0 = e->getParameter (e, kWidth);
    automateCalls = beginCalls = endCalls = 0;
    mouse (win, panel, NSLeftMouseDown,    NSMakePoint (247, 114));
    mouse (win, panel, NSLeftMouseDragged, NSMakePoint (247, 89));
    mouse (win, panel, NSLeftMouseDragged, NSMakePoint (247, 64));
    mouse (win, panel, NSLeftMouseUp,      NSMakePoint (247, 64));
    const float g1 = e->getParameter (e, kWidth);
    std::printf ("  Width %.2f -> %.2f\n", g0, g1);
    check (std::fabs (g1 - std::min (1.0f, g0 + 0.25f)) < 0.02f, "dragging a knob up turns it up");
    check (automateCalls > 0 && beginCalls == 1 && endCalls == 1, "host gets begin / automate / end");

    mouse (win, panel, NSLeftMouseDown, NSMakePoint (247, 114), 2);
    mouse (win, panel, NSLeftMouseUp,   NSMakePoint (247, 114), 2);
    check (std::fabs (e->getParameter (e, kWidth) - kDefaults[kWidth]) < 1e-6f, "double-click resets a knob");

    // Each knob answers where it is drawn
    {
        const int knobs[6] = { kManual, kWidth, kSpeed, kRegen, kInput, kOutput };
        const CGFloat kx[6] = { 98, 247, 393, 540, 46, 132 };
        const CGFloat ky[6] = { 114, 114, 114, 114, 534, 534 };
        bool all = true;
        for (int k = 0; k < 6; ++k)
        {
            const float v0 = e->getParameter (e, knobs[k]);
            mouse (win, panel, NSLeftMouseDown,    NSMakePoint (kx[k], ky[k]));
            mouse (win, panel, NSLeftMouseDragged, NSMakePoint (kx[k], ky[k] + 20));
            mouse (win, panel, NSLeftMouseUp,      NSMakePoint (kx[k], ky[k] + 20));
            if (! (e->getParameter (e, knobs[k]) < v0 - 0.05f || v0 < 0.05f)) all = false;
            e->setParameter (e, knobs[k], kDefaults[knobs[k]]);
        }
        check (all, "all six knobs turn");
    }

    // Stomp switch: each press toggles
    mouse (win, panel, NSLeftMouseDown, NSMakePoint (320, 355));
    mouse (win, panel, NSLeftMouseUp,   NSMakePoint (320, 355));
    check (e->getParameter (e, kEffect) < 0.5f, "footswitch switches the effect out");
    pump (0.1);
    renderPNG (panel, dir + "/panel_bypassed.png");
    mouse (win, panel, NSLeftMouseDown, NSMakePoint (320, 355));
    mouse (win, panel, NSLeftMouseUp,   NSMakePoint (320, 355));
    check (e->getParameter (e, kEffect) >= 0.5f, "and back in");

    // Strip knob turned up shows in the readout
    const float a0 = e->getParameter (e, kOutput);
    mouse (win, panel, NSLeftMouseDown,    NSMakePoint (132, 534));
    mouse (win, panel, NSLeftMouseDragged, NSMakePoint (132, 514));
    mouse (win, panel, NSLeftMouseUp,      NSMakePoint (132, 514));
    check (e->getParameter (e, kOutput) > a0 + 0.05f, "strip knobs turn");
    renderPNG (panel, dir + "/panel_readout.png");
    e->setParameter (e, kOutput, 0.5f);

    std::printf ("\nPresets\n");
    #define SLOT(k) NSMakePoint (310.0 + (k) * 39.0 + 17.0, 533.0)
    e->setParameter (e, kRegen, 0.3f);
    e->setParameter (e, kEffect, 0.0f);
    pump (0.1);
    mouse (win, panel, NSLeftMouseDown, NSMakePoint (276, 533));    // SAVE
    mouse (win, panel, NSLeftMouseUp,   NSMakePoint (276, 533));
    mouse (win, panel, NSLeftMouseDown, SLOT (2));                   // slot 3
    mouse (win, panel, NSLeftMouseUp,   SLOT (2));
    FILE* pf = std::fopen (presetFile.c_str(), "r");
    check (pf != 0, "SAVE then a slot writes the preset file");
    if (pf) std::fclose (pf);
    renderPNG (panel, dir + "/panel_preset_saved.png");

    e->setParameter (e, kRegen, 0.9f);
    e->setParameter (e, kEffect, 1.0f);
    pump (0.1);
    automateCalls = 0;
    mouse (win, panel, NSLeftMouseDown, SLOT (2));
    mouse (win, panel, NSLeftMouseUp,   SLOT (2));
    check (std::fabs (e->getParameter (e, kRegen) - 0.3f) < 1e-5f
           && e->getParameter (e, kEffect) < 0.5f, "clicking the slot recalls the saved settings");
    check (automateCalls >= kNumParams, "recall tells the host about every parameter");

    const float before = e->getParameter (e, kRegen);
    mouse (win, panel, NSLeftMouseDown, SLOT (5));                   // empty slot 6
    mouse (win, panel, NSLeftMouseUp,   SLOT (5));
    check (e->getParameter (e, kRegen) == before, "an empty slot changes nothing");

    std::printf ("\nHost automation shows on the panel\n");
    NSData* pngBefore = renderPNG (panel, dir + "/panel_before_host.png");
    e->setParameter (e, kSpeed, 0.9f);
    e->setParameter (e, kEffect, 1.0f);
    pump (0.3);
    NSData* pngAfter = renderPNG (panel, dir + "/panel_changed.png");
    check (! [pngAfter isEqualToData: pngBefore], "panel redraws after host changes");

    // A showcase render
    e->setParameter (e, kManual, 0.62f);
    e->setParameter (e, kWidth, 0.68f);
    e->setParameter (e, kSpeed, 0.72f);
    e->setParameter (e, kRegen, 0.92f);
    pump (1.0);
    renderPNG (panel, dir + "/panel_showcase.png");

    std::printf ("\nPCB trims board\n");
    {
        // Click TRIMS: the board drops down over the face
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (212, 533));
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (212, 533));
        pump (0.12);
        NSData* mid = renderPNG (panel, dir + "/panel_trims_dropping.png");
        pump (0.6);
        NSData* down = renderPNG (panel, dir + "/panel_trims.png");
        check (! [mid isEqualToData: down], "the board animates down");

        // LEVEL trim: drag up 40 px -> +0.2
        e->setParameter (e, kLevelTrim, 0.5f);
        pump (0.1);
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (320, 132));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (320, 112));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (320, 92));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (320, 92));
        const float lv = e->getParameter (e, kLevelTrim);
        std::printf ("  LEVEL trim 0.50 -> %.2f\n", lv);
        check (std::fabs (lv - 0.7f) < 0.02f, "trims turn with a drag");
        // The others answer where they are drawn
        const float b0 = e->getParameter (e, kBiasTrim), c0 = e->getParameter (e, kClockTrim);
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (164, 132));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (164, 152));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (164, 152));
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (476, 132));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (476, 152));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (476, 152));
        check (e->getParameter (e, kBiasTrim) < b0 - 0.05f && e->getParameter (e, kClockTrim) < c0 - 0.05f,
               "BIAS and CLOCK trims turn");
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (164, 132), 2);
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (164, 132), 2);
        check (std::fabs (e->getParameter (e, kBiasTrim) - kDefaults[kBiasTrim]) < 1e-6f, "double-click resets a trim");
        e->setParameter (e, kClockTrim, kDefaults[kClockTrim]);
        e->setParameter (e, kLevelTrim, 0.82f);

        // While the board is down, the knob under it doesn't move
        const float m0 = e->getParameter (e, kManual);
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (98, 114));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (98, 74));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (98, 74));
        check (e->getParameter (e, kManual) == m0, "the board covers the knobs beneath it");
        pump (0.2);
        renderPNG (panel, dir + "/panel_trims_set.png");

        // Click TRIMS again: the board goes away and the knobs work again
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (212, 533));
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (212, 533));
        pump (0.6);
        NSData* up = renderPNG (panel, dir + "/panel_trims_away.png");
        check (! [up isEqualToData: down], "the board goes back up");
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (98, 114));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (98, 94));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (98, 94));
        check (e->getParameter (e, kManual) > m0 + 0.05f || m0 > 0.95f, "knobs answer again");
        e->setParameter (e, kLevelTrim, kDefaults[kLevelTrim]);
        e->setParameter (e, kManual, 0.62f);
    }

    std::printf ("\nClose\n");
    call (e, effEditClose);
    check ([[parent subviews] count] == 0, "effEditClose removes the panel");
    check (call (e, effEditOpen, 0, 0, parent) == 1 && [[parent subviews] count] == 1, "reopens");
    call (e, effEditClose);
    call (e, effMainsChanged, 0, 0);
    call (e, effClose);
    [win release];
    [pool release];      // drain before unloading the plugin's code
    dlclose (lib);

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL EDITOR CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
