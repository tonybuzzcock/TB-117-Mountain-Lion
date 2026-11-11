// PanelView.mm
// The custom panel for TB 117 Flanger: an original design laid out like the
// user's MXR M117. A battleship-grey hammertone face with a thin white line
// round it (broken for POWER, OUTPUT and INPUT), four black fluted knobs with
// white pointer lines (MANUAL, WIDTH, SPEED, REGEN.) and white lettering, the
// red LED, the chrome stomp switch, the user's TB electronic logo where the
// maker's badge sits (with "electronic" in white on the grey) and "flanger" in
// plain lettering. A strip of plugin controls runs along the bottom (Input,
// Output, presets and the readout).
//
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.5.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Params.h"
#include "Presets.h"
#include "TBLogo.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tb117;

namespace
{

enum CtlType { kCtlKnob, kCtlFootswitch, kCtlSmallKnob };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy, r;
    const char* label;
    const char* posA;      // small knob left
    const char* posB;      // small knob right
};

// The pedal's face (from the photo, 1433 x 1064 scaled to 640 wide), then the strip
const CGFloat kStripTop = 476.0;
const NSRect kLine = { { 29.0, 30.0 }, { 582.0, 418.0 } };      // the thin white line
const CGFloat kLabelY = 200.0;
const NSPoint kLed = { 320.0, 241.0 };
const NSRect kLogoBox = { { 80.0, 318.0 }, { 156.0, 74.0 } };   // where the maker's badge is
const NSPoint kNameAt = { 482.0, 352.0 };

const Ctl kCtls[] = {
    { kManual,    kCtlKnob,        98, 114, 62, "MANUAL",  0, 0 },
    { kWidth,     kCtlKnob,       247, 114, 62, "WIDTH",   0, 0 },
    { kSpeed,     kCtlKnob,       393, 114, 62, "SPEED",   0, 0 },
    { kRegen,     kCtlKnob,       540, 114, 62, "REGEN.",  0, 0 },
    { kEffect,    kCtlFootswitch, 320, 355, 24, "EFFECT",  0, 0 },
    // Plugin strip
    { kInput,     kCtlSmallKnob,   60, 534, 19, "INPUT",   "-12", "+12" },
    { kOutput,    kCtlSmallKnob,  162, 534, 19, "OUTPUT",  "-24", "+24" },
};

// Preset section in the strip: a SAVE button and slots 1..8, then the readout
const NSRect kSaveRect = { { 236.0, 520.0 }, { 54.0, 26.0 } };
const CGFloat kSlotX0 = 300.0, kSlotPitch = 41.0, kSlotW = 35.0, kSlotY = 520.0, kSlotH = 26.0;
const CGFloat kPresetLabelY = 504.0;
const NSRect kReadoutRect = { { 20.0, 570.0 }, { 600.0, 54.0 } };

inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }

// Point at angle a (0 = up, clockwise) and radius r around (cx, cy) in flipped coordinates.
inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }
NSColor* paint() { return rgb (238, 238, 234); }     // the pedal's white lettering

NSFont* boldFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Bold" size: size];
    return f ? f : [NSFont boldSystemFontOfSize: size];
}

NSFont* plainFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica" size: size];
    return f ? f : [NSFont systemFontOfSize: size];
}

NSDictionary* textAttrs (NSFont* font, NSColor* color, CGFloat kern)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
              font, NSFontAttributeName, color, NSForegroundColorAttributeName,
              [NSNumber numberWithDouble: kern], NSKernAttributeName, nil];
}

void drawText (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy, CGFloat kern = 0.0)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = textAttrs (font, color, kern);
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (cx - (sz.width - kern) * 0.5, cy - sz.height * 0.5) withAttributes: attrs];
}

CGFloat textWidth (const char* s, NSFont* font, CGFloat kern)
{
    NSString* str = [NSString stringWithUTF8String: s];
    return [str sizeWithAttributes: textAttrs (font, [NSColor whiteColor], kern)].width - kern;
}

// Text turned a quarter turn, centred on (cx, cy): the jack labels on the sides.
// clockwise = reads top to bottom.
void drawTextTurned (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy, bool clockwise, CGFloat kern)
{
    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform* t = [NSAffineTransform transform];
    [t translateXBy: cx yBy: cy];
    [t rotateByDegrees: clockwise ? 90.0 : -90.0];
    [t concat];
    drawText (s, font, color, 0.0, 0.0, kern);
    [NSGraphicsContext restoreGraphicsState];
}

} // namespace

//==============================================================================
@interface TB117FlangerPanelView : NSView
{
    EditorHost* host;
    float shown[kNumParams];
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[64];
    NSImage* flecks;      // the hammertone finish, made once
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
@end

@implementation TB117FlangerPanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        dragCtl = -1;
        readoutParam = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        flecks = nil;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / 20.0 target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    if (flecks) { [flecks release]; flecks = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) redraw the panel.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
}

//------------------------------------------------------------------------------
// Drawing

// Fine light and dark flecks over the grey, like a hammertone paint finish.
- (NSImage*) fleckImage
{
    if (flecks) return flecks;
    const CGFloat w = [self bounds].size.width;
    flecks = [[NSImage alloc] initWithSize: NSMakeSize (w, kStripTop)];
    [flecks lockFocus];
    unsigned seed = 117u;
    for (int k = 0; k < 14000; ++k)
    {
        seed = seed * 1664525u + 1013904223u; const CGFloat x = (seed >> 8) / 16777216.0 * w;
        seed = seed * 1664525u + 1013904223u; const CGFloat y = (seed >> 8) / 16777216.0 * kStripTop;
        seed = seed * 1664525u + 1013904223u; const double r = (seed >> 8) / 16777216.0;
        seed = seed * 1664525u + 1013904223u; const double q = (seed >> 8) / 16777216.0;
        const CGFloat s = (CGFloat) (0.8 + 1.0 * q);
        if (r < 0.5)
            [[NSColor colorWithCalibratedWhite: 1.0 alpha: 0.05 + 0.13 * q] set];
        else
            [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.10 + 0.22 * q] set];
        NSRectFillUsingOperation (NSMakeRect (x, y, s, s), NSCompositeSourceOver);
    }
    [flecks unlockFocus];
    return flecks;
}

- (void) drawBackground
{
    const NSRect b = [self bounds];

    // Behind the pedal's rounded corners
    [rgb (34, 35, 37) set];
    NSRectFill (NSMakeRect (0, 0, b.size.width, kStripTop));

    // Battleship-grey face, a little lighter at the top
    const NSRect face = NSMakeRect (3.0, 3.0, b.size.width - 6.0, kStripTop - 6.0);
    NSBezierPath* facePath = [NSBezierPath bezierPathWithRoundedRect: face xRadius: 16 yRadius: 16];
    NSGradient* grey = [[[NSGradient alloc] initWithStartingColor: rgb (98, 99, 101)
                                                      endingColor: rgb (74, 75, 77)] autorelease];
    [grey drawInBezierPath: facePath angle: 90.0];
    [NSGraphicsContext saveGraphicsState];
    [facePath addClip];
    [[self fleckImage] drawInRect: NSMakeRect (0, 0, b.size.width, kStripTop) fromRect: NSZeroRect
                        operation: NSCompositeSourceOver fraction: 1.0];
    [NSGraphicsContext restoreGraphicsState];
    // Rounded edge of the box: a soft highlight on top, darker below
    [[NSColor colorWithCalibratedWhite: 1.0 alpha: 0.18] set];
    [facePath setLineWidth: 1.5];
    [facePath stroke];

    // Plugin strip, a slightly darker silver
    NSGradient* strip = [[[NSGradient alloc] initWithStartingColor: rgb (196, 199, 203)
                                                       endingColor: rgb (166, 170, 175)] autorelease];
    [strip drawInRect: NSMakeRect (0, kStripTop, b.size.width, b.size.height - kStripTop) angle: 90.0];
    [rgb (40, 40, 42) set];
    NSRectFill (NSMakeRect (0, kStripTop, b.size.width, 2.0));
    [rgb (238, 240, 242) set];
    NSRectFill (NSMakeRect (0, kStripTop + 2.0, b.size.width, 1.0));

    // The thin white line, broken for the POWER, OUTPUT and INPUT labels
    NSColor* white = paint();
    NSFont* jackFont = boldFont (8.5);
    const CGFloat kern = 1.0, gap = 7.0;
    const CGFloat x0 = NSMinX (kLine), x1 = NSMaxX (kLine), y0 = NSMinY (kLine), y1 = NSMaxY (kLine);
    const CGFloat midX = 320.0;
    const CGFloat pw = textWidth ("POWER", jackFont, kern) * 0.5 + gap;
    const CGFloat outY = 245.0, inY = 245.0;
    const CGFloat ow = textWidth ("OUTPUT", jackFont, kern) * 0.5 + gap;
    const CGFloat iw = textWidth ("INPUT", jackFont, kern) * 0.5 + gap;
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint: NSMakePoint (midX - pw, y0)];
    [line lineToPoint: NSMakePoint (x0, y0)];
    [line lineToPoint: NSMakePoint (x0, outY - ow)];
    [line moveToPoint: NSMakePoint (x0, outY + ow)];
    [line lineToPoint: NSMakePoint (x0, y1)];
    [line lineToPoint: NSMakePoint (x1, y1)];
    [line lineToPoint: NSMakePoint (x1, inY + iw)];
    [line moveToPoint: NSMakePoint (x1, inY - iw)];
    [line lineToPoint: NSMakePoint (x1, y0)];
    [line lineToPoint: NSMakePoint (midX + pw, y0)];
    [line setLineWidth: 1.6];
    [line setLineJoinStyle: NSMiterLineJoinStyle];
    [white set];
    [line stroke];
    drawText ("POWER", jackFont, white, midX, y0, kern);
    drawTextTurned ("OUTPUT", jackFont, white, x0, outY, true, kern);
    drawTextTurned ("INPUT", jackFont, white, x1, inY, true, kern);

    // TB electronic logo where the maker's badge sits; "electronic" in white
    tblogo::drawTBLogo (kLogoBox, white);

    // The model name in plain lowercase lettering
    drawText ("flanger", plainFont (44.0), white, kNameAt.x, kNameAt.y, 0.5);
}

// Black fluted knob with a white pointer line and a white dot on its skirt.
- (void) drawBigKnobAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    const CGFloat a = valueAngle (v);

    // Shadow and skirt
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (2.0, -4.0)];
    [sh setShadowBlurRadius: 7.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.6]];
    [sh set];
    NSBezierPath* skirt = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)];
    [rgb (14, 14, 16) set];
    [skirt fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* sk = [[[NSGradient alloc] initWithStartingColor: rgb (58, 58, 62) endingColor: rgb (8, 8, 10)] autorelease];
    [sk drawInBezierPath: skirt relativeCenterPosition: NSMakePoint (-0.3, 0.45)];

    // Fluted cap: ten rounded lobes that turn with the knob
    const CGFloat rc = r * 0.84;
    NSBezierPath* cap = [NSBezierPath bezierPath];
    for (int k = 0; k <= 180; ++k)
    {
        const CGFloat t = (CGFloat) (2.0 * M_PI * k / 180.0);
        const CGFloat rr = rc * (1.0 - 0.055 * (0.5 - 0.5 * std::cos (10.0 * t)));
        const NSPoint p = polar (ctr.x, ctr.y, rr, t + a);
        if (k == 0) [cap moveToPoint: p]; else [cap lineToPoint: p];
    }
    [cap closePath];
    NSGradient* cg = [[[NSGradient alloc] initWithColorsAndLocations:
                         rgb (92, 92, 98), 0.0, rgb (30, 30, 33), 0.45, rgb (6, 6, 8), 1.0, nil] autorelease];
    [cg drawInBezierPath: cap relativeCenterPosition: NSMakePoint (-0.45, 0.5)];
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.9] set];
    [cap setLineWidth: 1.0];
    [cap stroke];

    // Gloss on the top
    const CGFloat rt = rc * 0.80;
    NSBezierPath* top = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rt, ctr.y - rt, 2 * rt, 2 * rt)];
    NSGradient* gloss = [[[NSGradient alloc] initWithColorsAndLocations:
                            [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.16], 0.0,
                            [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.02], 0.55,
                            [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.0], 1.0, nil] autorelease];
    [gloss drawInBezierPath: top relativeCenterPosition: NSMakePoint (-0.4, 0.55)];

    // White pointer line and the dot on the skirt
    NSBezierPath* pl = [NSBezierPath bezierPath];
    [pl moveToPoint: polar (ctr.x, ctr.y, r * 0.10, a)];
    [pl lineToPoint: polar (ctr.x, ctr.y, r * 0.70, a)];
    [pl setLineWidth: 4.5];
    [pl setLineCapStyle: NSButtLineCapStyle];
    [paint() set];
    [pl stroke];
    const NSPoint d = polar (ctr.x, ctr.y, r * 0.925, a);
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (d.x - 2.8, d.y - 2.8, 5.6, 5.6)] fill];
}

// Grey knurled knob with a red position line (the strip's trims).
- (void) drawKnobAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -2.5)];
    [sh setShadowBlurRadius: 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (90, 93, 97) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)] fill];
    [NSGraphicsContext restoreGraphicsState];

    const int ridges = 40;
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / ridges);
        const CGFloat a1 = (CGFloat) (2.0 * M_PI * (k + 0.5) / ridges);
        NSBezierPath* w = [NSBezierPath bezierPath];
        [w moveToPoint: ctr];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a0)];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a1)];
        [w closePath];
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [[NSColor colorWithCalibratedWhite: 0.40 + 0.22 * lit alpha: 1.0] set];
        [w fill];
    }

    const CGFloat rc = r * 0.78;
    NSGradient* cap = [[[NSGradient alloc] initWithStartingColor: rgb (172, 175, 179)
                                                     endingColor: rgb (104, 107, 111)] autorelease];
    NSBezierPath* capPath = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rc, ctr.y - rc, 2 * rc, 2 * rc)];
    [cap drawInBezierPath: capPath angle: -60.0];
    [rgb (70, 72, 75) set];
    [capPath setLineWidth: 0.8];
    [capPath stroke];

    const CGFloat a = valueAngle (v);
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint: polar (ctr.x, ctr.y, r * 0.18, a)];
    [line lineToPoint: polar (ctr.x, ctr.y, r * 0.98, a)];
    [line setLineWidth: 2.4];
    [line setLineCapStyle: NSRoundLineCapStyle];
    [rgb (200, 30, 30) set];
    [line stroke];
}

// The red LED: lit with the effect in.
- (void) drawLed
{
    const bool on = isOn (shown[kEffect]);
    const NSPoint c = kLed;
    if (on)
    {
        const CGFloat gr = 22.0;
        NSGradient* halo = [[[NSGradient alloc] initWithColorsAndLocations:
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.12 blue: 0.06 alpha: 0.75], 0.0,
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.08 blue: 0.04 alpha: 0.28], 0.45,
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.05 blue: 0.02 alpha: 0.0], 1.0, nil] autorelease];
        [halo drawInBezierPath: [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - gr, c.y - gr, 2 * gr, 2 * gr)]
              relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    }
    // Bezel
    const CGFloat rb = 9.5, rl = 7.5;
    NSBezierPath* bezel = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rb, c.y - rb, 2 * rb, 2 * rb)];
    [rgb (28, 28, 30) set];
    [bezel fill];
    NSBezierPath* lens = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rl, c.y - rl, 2 * rl, 2 * rl)];
    NSGradient* lg = on
        ? [[[NSGradient alloc] initWithStartingColor: rgb (255, 190, 170) endingColor: rgb (226, 20, 12)] autorelease]
        : [[[NSGradient alloc] initWithStartingColor: rgb (176, 60, 56) endingColor: rgb (92, 12, 12)] autorelease];
    [lg drawInBezierPath: lens relativeCenterPosition: NSMakePoint (-0.3, 0.4)];
    [[NSColor colorWithCalibratedWhite: 1.0 alpha: on ? 0.75 : 0.45] set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - 3.6, c.y - 4.4, 2.6, 2.2)] fill];
}

// Chrome stomp switch: click to toggle.
- (void) drawFootswitch: (const Ctl&) c
{
    const NSPoint ctr = NSMakePoint (c.cx, c.cy);
    const bool on = isOn (shown[kEffect]);

    // Hex nut
    NSBezierPath* nut = [NSBezierPath bezierPath];
    for (int k = 0; k < 6; ++k)
    {
        const NSPoint p = polar (ctr.x, ctr.y, c.r + 16.0, (CGFloat) (k * M_PI / 3.0));
        if (k == 0) [nut moveToPoint: p]; else [nut lineToPoint: p];
    }
    [nut closePath];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* ns = [[[NSShadow alloc] init] autorelease];
    [ns setShadowOffset: NSMakeSize (1.5, -3.0)];
    [ns setShadowBlurRadius: 5.0];
    [ns setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.55]];
    [ns set];
    [rgb (150, 152, 156) set];
    [nut fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* ng = [[[NSGradient alloc] initWithStartingColor: rgb (232, 234, 237) endingColor: rgb (112, 115, 119)] autorelease];
    [ng drawInBezierPath: nut angle: -60.0];
    [rgb (50, 52, 55) set];
    [nut setLineWidth: 0.8];
    [nut stroke];
    // Threaded collar
    const CGFloat rcol = c.r + 6.0;
    NSBezierPath* collar = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rcol, ctr.y - rcol, 2 * rcol, 2 * rcol)];
    NSGradient* colg = [[[NSGradient alloc] initWithStartingColor: rgb (120, 123, 128) endingColor: rgb (214, 216, 219)] autorelease];
    [colg drawInBezierPath: collar angle: -60.0];
    [rgb (60, 62, 65) set];
    [collar setLineWidth: 0.8];
    [collar stroke];

    // Plunger, pressed in a touch with the effect in
    const CGFloat r = on ? c.r - 1.5 : c.r;
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.0, on ? -1.0 : -2.5)];
    [sh setShadowBlurRadius: on ? 2.0 : 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.5]];
    [sh set];
    NSBezierPath* dome = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)];
    [rgb (150, 153, 157) set];
    [dome fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* dg = [[[NSGradient alloc] initWithStartingColor: rgb (250, 251, 252) endingColor: rgb (110, 113, 118)] autorelease];
    [dg drawInBezierPath: dome relativeCenterPosition: NSMakePoint (-0.35, 0.4)];
    [rgb (55, 57, 60) set];
    [dome setLineWidth: 1.0];
    [dome stroke];
}

- (void) drawReadout
{
    const NSRect box = kReadoutRect;
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: box xRadius: 4 yRadius: 4];
    NSGradient* glow = [[[NSGradient alloc] initWithStartingColor: rgb (206, 255, 120)
                                                      endingColor: rgb (150, 232, 70)] autorelease];
    [glow drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    [rgb (60, 62, 65) set];
    [p setLineWidth: 1.0];
    [p stroke];

    if (status[0])
    {
        drawText (status, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
    else if (readoutParam >= 0 && host)
    {
        char disp[16] = { 0 };
        host->getParamDisplay (readoutParam, disp);
        char text[96];
        const char* name = 0;
        for (int i = 0; i < kNumCtls; ++i)
            if (kCtls[i].param == readoutParam) { name = kCtls[i].label; break; }
        const int kind = kParamKind[readoutParam];
        const char* unit = kind == kRateHz ? " Hz" : ((kind == kDb12 || kind == kDb24) ? " dB" : "");
        std::snprintf (text, sizeof (text), "%s   %s%s", name ? name : "", disp, unit);
        drawText (text, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
}

- (void) drawPresets
{
    NSColor* ink = [NSColor blackColor];
    drawText ("PRESETS", boldFont (10.0), ink, (NSMinX (kSaveRect) + NSMaxX (slotRect (kNumPresetSlots - 1))) * 0.5, kPresetLabelY);

    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: kSaveRect xRadius: 3 yRadius: 3];
    if (saveMode)
    {
        [rgb (200, 30, 30) set];
        [sp fill];
    }
    else
    {
        NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (232, 234, 236) endingColor: rgb (160, 163, 167)] autorelease];
        [g drawInBezierPath: sp angle: -90.0];
    }
    [rgb (40, 41, 43) set];
    [sp setLineWidth: 1.0];
    [sp stroke];
    drawText ("SAVE", boldFont (11.0), saveMode ? [NSColor whiteColor] : ink, NSMidX (kSaveRect), NSMidY (kSaveRect));

    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 3 yRadius: 3];
        NSColor* fill = (k == activePreset) ? rgb (178, 245, 96) : (presetUsed[k] ? rgb (46, 47, 50) : rgb (214, 216, 219));
        NSColor* text = (k == activePreset) ? ink : (presetUsed[k] ? [NSColor whiteColor] : rgb (120, 122, 126));
        [fill set];
        [bp fill];
        [rgb (40, 41, 43) set];
        [bp setLineWidth: 1.0];
        [bp stroke];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawText (num, boldFont (12.0), text, NSMidX (r), NSMidY (r));
    }
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    [self drawBackground];
    NSColor* ink = [NSColor blackColor];

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        switch (c.type)
        {
            case kCtlKnob:
                [self drawBigKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                drawText (c.label, plainFont (20.0), paint(), c.cx, kLabelY, 0.6);
                break;
            case kCtlFootswitch:
                [self drawFootswitch: c];
                break;
            case kCtlSmallKnob:
            {
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                drawText (c.label, boldFont (11.0), ink, c.cx, c.cy - c.r - 13.0);
                drawText (c.posA, plainFont (9.5), ink, c.cx - c.r - 15.0, c.cy + c.r - 2.0);
                drawText (c.posB, plainFont (9.5), ink, c.cx + c.r + 15.0, c.cy + c.r - 2.0);
                break;
            }
            default: break;
        }
    }
    [self drawLed];
    [self drawPresets];
    [self drawReadout];
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        {
            const CGFloat dx = p.x - c.cx, dy = p.y - c.cy;
            if (dx * dx + dy * dy < (c.r + 8) * (c.r + 8)) return i;
        }
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

// SAVE button and preset slots. Returns YES if the click was handled here.
- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, kSaveRect))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT 1-8" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok) activePreset = k;
            std::snprintf (status, sizeof (status), ok ? "PRESET %d SAVED" : "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d LOADED", k + 1);
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    shown[c.param] = host->getParam (c.param);   // the live value, even between timer ticks
    readoutParam = c.param;
    status[0] = 0;
    activePreset = -1;     // settings are being changed away from the preset

    if (c.type == kCtlFootswitch)
    {
        // Each press toggles, like the pedal's latching switch
        host->guiBeginEdit (c.param);
        [self setParam: c.param value: isOn (shown[c.param]) ? 0.0f : 1.0f];
        host->guiEndEdit (c.param);
        return;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlSmallKnob) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tb117
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TB117FlangerPanelView* v = [[TB117FlangerPanelView alloc]
                                  initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TB117FlangerPanelView* v = (TB117FlangerPanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tb117
