#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "ply_core.h"

static NSString* const kRecentKey = @"RecentFiles";
static NSString* const kSettingsKey = @"ViewerSettings";

@class AppDelegate;
@class PLYGLView;
static AppDelegate* gApp = nil;

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSToolbarDelegate, NSMenuItemValidation>
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) PLYGLView* glView;
@property (nonatomic, strong) NSView* inspector;
@property (nonatomic, strong) NSLayoutConstraint* inspectorWidth;
@property (nonatomic, strong) NSTextField* statusLeft;
@property (nonatomic, strong) NSTextField* statusRight;
@property (nonatomic, strong) NSTextField* hintLabel;
@property (nonatomic, strong) NSVisualEffectView* loadingBox;
@property (nonatomic, strong) NSProgressIndicator* progress;
@property (nonatomic, strong) NSTextField* progressLabel;
@property (nonatomic, strong) NSTextField* infoLabel;
@property (nonatomic, strong) NSMenu* recentMenu;
@property (nonatomic, strong) NSString* pendingFile;
@property (nonatomic) BOOL loading;
@property (nonatomic) BOOL inspectorVisible;

@property (nonatomic, strong) NSSegmentedControl* modeControl;
@property (nonatomic, strong) NSSegmentedControl* toolbarMode;
@property (nonatomic, strong) NSSegmentedControl* projControl;
@property (nonatomic, strong) NSSlider* pointSizeSlider;
@property (nonatomic, strong) NSSlider* splatScaleSlider;
@property (nonatomic, strong) NSSlider* cutoffSlider;
@property (nonatomic, strong) NSSlider* edlSlider;
@property (nonatomic, strong) NSSlider* fovSlider;
@property (nonatomic, strong) NSPopUpButton* colorPopup;
@property (nonatomic, strong) NSPopUpButton* bgPopup;
@property (nonatomic, strong) NSPopUpButton* upPopup;
@property (nonatomic, strong) NSButton* edlCheck;
@property (nonatomic, strong) NSButton* shadingCheck;
@property (nonatomic, strong) NSButton* gridCheck;
@property (nonatomic, strong) NSButton* axesCheck;
@property (nonatomic, strong) NSTextField* pointSizeValue;
@property (nonatomic, strong) NSTextField* splatScaleValue;
@property (nonatomic, strong) NSTextField* cutoffValue;
@property (nonatomic, strong) NSTextField* edlValue;
@property (nonatomic, strong) NSTextField* fovValue;

@property (nonatomic, copy) NSString* cliSnapshot;
@property (nonatomic, copy) NSString* cliMode;
@property (nonatomic, copy) NSString* cliView;
- (void)loadFile:(NSString*)path;
- (void)settingsChanged;
@end

@interface FlippedView : NSView
@end
@implementation FlippedView
- (BOOL)isFlipped { return YES; }
@end

/* ------------------------------------------------------------------ */

@interface PLYGLView : NSOpenGLView
@property (nonatomic) NSPoint lastMouse;
@property (nonatomic) BOOL orbiting;
@property (nonatomic) BOOL panning;
@property (nonatomic) BOOL ready;
@property (nonatomic) BOOL dirty;
@property (nonatomic) double lastDrawTime;
@property (nonatomic) double fps;
@property (nonatomic, copy) NSString* pendingSnapshot;
@property (nonatomic, copy) void (^snapshotDone)(BOOL ok);
- (void)requestRedraw;
@end

@implementation PLYGLView

- (instancetype)initWithFrame:(NSRect)frame {
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 24,
        NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFAAccelerated,
        0
    };
    NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!fmt) {
        attrs[1] = NSOpenGLProfileVersion3_2Core;
        fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    }
    self = [super initWithFrame:frame pixelFormat:fmt];
    if (self) {
        self.wantsBestResolutionOpenGLSurface = YES;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return self;
}

- (void)prepareOpenGL {
    [super prepareOpenGL];
    [[self openGLContext] makeCurrentContext];
    GLint swap = 1;
    [[self openGLContext] setValues:&swap forParameter:NSOpenGLContextParameterSwapInterval];
    if (!ply_gl_init()) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleCritical;
        alert.messageText = @"OpenGL initialisation failed";
        alert.informativeText = @"The renderer needs an OpenGL 3.3 core profile context.";
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }
    self.ready = YES;
    self.dirty = YES;
    __weak PLYGLView* weakSelf = self;
    [NSTimer scheduledTimerWithTimeInterval:1.0/60.0 repeats:YES block:^(NSTimer* t) {
        PLYGLView* s = weakSelf;
        if (!s) return;
        if (s.dirty || ply_needs_redraw()) {
            s.dirty = NO;
            [s setNeedsDisplay:YES];
        }
    }];
}

- (void)requestRedraw { self.dirty = YES; }

- (void)reshape {
    [super reshape];
    self.dirty = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!self.ready) return;
    [[self openGLContext] makeCurrentContext];
    NSRect backing = [self convertRectToBacking:self.bounds];
    float pixelScale = (float)self.window.backingScaleFactor;
    if (pixelScale <= 0.0f) pixelScale = 1.0f;
    int w = (int)backing.size.width, h = (int)backing.size.height;
    ply_draw(w, h, pixelScale);
    if (self.pendingSnapshot && !ply_sort_settled()) {
        self.dirty = YES;
    } else if (self.pendingSnapshot) {
        NSString* path = self.pendingSnapshot;
        self.pendingSnapshot = nil;
        BOOL ok = [self writeSnapshotTo:path width:w height:h];
        if (self.snapshotDone) { self.snapshotDone(ok); self.snapshotDone = nil; }
    }
    [[self openGLContext] flushBuffer];
    double now = CACurrentMediaTime();
    if (self.lastDrawTime > 0) {
        double dt = now - self.lastDrawTime;
        if (dt > 0 && dt < 0.25) self.fps = self.fps * 0.8 + (1.0 / dt) * 0.2;
    }
    self.lastDrawTime = now;
}

- (BOOL)writeSnapshotTo:(NSString*)path width:(int)w height:(int)h {
    unsigned char* pixels = (unsigned char*)malloc((size_t)w * h * 4);
    if (!pixels) return NO;
    if (!ply_read_pixels(pixels, w, h)) { free(pixels); return NO; }
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
        bytesPerRow:w * 4 bitsPerPixel:32];
    unsigned char* dst = rep.bitmapData;
    for (int y = 0; y < h; y++) {
        unsigned char* row = pixels + (size_t)(h - 1 - y) * w * 4;
        for (int x = 0; x < w; x++) { row[x*4+3] = 255; }
        memcpy(dst + (size_t)y * rep.bytesPerRow, row, (size_t)w * 4);
    }
    free(pixels);
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [png writeToFile:path atomically:YES];
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

- (NSPoint)normalizedPoint:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSRect b = self.bounds;
    return NSMakePoint(b.size.width > 0 ? p.x / b.size.width : 0.5, b.size.height > 0 ? p.y / b.size.height : 0.5);
}

- (void)keyDown:(NSEvent*)event {
    NSString* chars = [event charactersIgnoringModifiers];
    if (chars.length == 0) { [super keyDown:event]; return; }
    unichar c = [chars characterAtIndex:0];
    PlySettings* s = ply_settings();
    BOOL handled = YES;
    switch (c) {
        case 'r': case 'R': ply_cam_reset(); break;
        case 'f': case 'F': ply_cam_fit(); break;
        case 'g': case 'G': s->showGrid = !s->showGrid; break;
        case 'e': case 'E': s->edl = !s->edl; break;
        case 'p': case 'P': s->ortho = !s->ortho; ply_cam_fit(); break;
        case 'm': case 'M': s->renderMode = s->renderMode == PLY_MODE_POINTS ? PLY_MODE_SPLATS : PLY_MODE_POINTS; break;
        case '1': ply_cam_view(PLY_VIEW_FRONT); break;
        case '2': ply_cam_view(PLY_VIEW_BACK); break;
        case '3': ply_cam_view(PLY_VIEW_LEFT); break;
        case '4': ply_cam_view(PLY_VIEW_RIGHT); break;
        case '5': ply_cam_view(PLY_VIEW_TOP); break;
        case '6': ply_cam_view(PLY_VIEW_BOTTOM); break;
        case '7': ply_cam_view(PLY_VIEW_ISO); break;
        case '+': case '=': ply_cam_zoom(0.85f, 0, 0, 0); break;
        case '-': case '_': ply_cam_zoom(1.0f / 0.85f, 0, 0, 0); break;
        case '[': s->pointSize = MAX(0.2f, s->pointSize / 1.2f); s->splatScale = MAX(0.2f, s->splatScale / 1.2f); break;
        case ']': s->pointSize = MIN(6.0f, s->pointSize * 1.2f); s->splatScale = MIN(3.0f, s->splatScale * 1.2f); break;
        case NSLeftArrowFunctionKey: ply_cam_orbit(-12, 0); break;
        case NSRightArrowFunctionKey: ply_cam_orbit(12, 0); break;
        case NSUpArrowFunctionKey: ply_cam_orbit(0, 12); break;
        case NSDownArrowFunctionKey: ply_cam_orbit(0, -12); break;
        default: handled = NO; break;
    }
    if (!handled) { [super keyDown:event]; return; }
    [gApp settingsChanged];
    [self requestRedraw];
}

- (void)scrollWheel:(NSEvent*)event {
    CGFloat dy = event.scrollingDeltaY;
    if (dy == 0) return;
    float factor;
    if (event.hasPreciseScrollingDeltas) factor = powf(1.1f, (float)(-dy * 0.06));
    else factor = dy > 0 ? 0.88f : 1.0f / 0.88f;
    NSPoint n = [self normalizedPoint:event];
    ply_cam_zoom(factor, (float)n.x, (float)n.y, 1);
    [self requestRedraw];
}

- (void)magnifyWithEvent:(NSEvent*)event {
    float factor = 1.0f / (1.0f + (float)event.magnification);
    NSPoint n = [self normalizedPoint:event];
    ply_cam_zoom(factor, (float)n.x, (float)n.y, 1);
    [self requestRedraw];
}

- (void)mouseDown:(NSEvent*)event {
    if (event.clickCount == 2) {
        NSPoint n = [self normalizedPoint:event];
        if (ply_cam_pick_pivot((float)n.x, (float)n.y)) [self requestRedraw];
        return;
    }
    NSEventModifierFlags m = event.modifierFlags;
    if (m & (NSEventModifierFlagControl | NSEventModifierFlagOption)) self.panning = YES;
    else self.orbiting = YES;
    self.lastMouse = [self convertPoint:event.locationInWindow fromView:nil];
}

- (void)mouseUp:(NSEvent*)event { self.orbiting = NO; self.panning = NO; }
- (void)rightMouseDown:(NSEvent*)event { self.panning = YES; self.lastMouse = [self convertPoint:event.locationInWindow fromView:nil]; }
- (void)rightMouseUp:(NSEvent*)event { self.panning = NO; }
- (void)otherMouseDown:(NSEvent*)event { self.panning = YES; self.lastMouse = [self convertPoint:event.locationInWindow fromView:nil]; }
- (void)otherMouseUp:(NSEvent*)event { self.panning = NO; }

- (void)handleDrag:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    float dx = (float)(p.x - self.lastMouse.x);
    float dy = (float)(p.y - self.lastMouse.y);
    if (self.orbiting) ply_cam_orbit(dx, dy);
    else if (self.panning) ply_cam_pan(dx, dy);
    self.lastMouse = p;
    [self requestRedraw];
}

- (void)mouseDragged:(NSEvent*)event { [self handleDrag:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [self handleDrag:event]; }
- (void)otherMouseDragged:(NSEvent*)event { [self handleDrag:event]; }

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray* urls = [sender.draggingPasteboard readObjectsForClasses:@[[NSURL class]]
                                                            options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (urls.count == 0) return NO;
    NSURL* url = urls.firstObject;
    if ([url.pathExtension caseInsensitiveCompare:@"ply"] == NSOrderedSame) {
        [gApp loadFile:url.path];
        return YES;
    }
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = @"Not a PLY file";
    alert.informativeText = @"Drop a file with the .ply extension.";
    [alert runModal];
    return NO;
}

@end

/* ------------------------------------------------------------------ */



static void progress_cb(float fraction, void* user) {
    AppDelegate* app = (__bridge AppDelegate*)user;
    dispatch_async(dispatch_get_main_queue(), ^{
        app.progress.doubleValue = fraction * 100.0;
        app.progressLabel.stringValue = fraction < 0.97f
            ? [NSString stringWithFormat:@"Reading… %d%%", (int)(fraction * 100)]
            : @"Analysing point spacing…";
    });
}

@implementation AppDelegate

#pragma mark - helpers

static NSTextField* MakeLabel(NSString* text, CGFloat size, BOOL bold, NSColor* color) {
    NSTextField* l = [NSTextField labelWithString:text];
    l.font = bold ? [NSFont systemFontOfSize:size weight:NSFontWeightSemibold] : [NSFont systemFontOfSize:size];
    l.textColor = color ?: [NSColor labelColor];
    l.translatesAutoresizingMaskIntoConstraints = NO;
    return l;
}

- (NSTextField*)sectionHeader:(NSString*)title {
    NSTextField* l = MakeLabel(title.uppercaseString, 11, YES, [NSColor secondaryLabelColor]);
    return l;
}

- (NSStackView*)row:(NSString*)title control:(NSView*)control value:(NSTextField*)value {
    NSTextField* label = MakeLabel(title, 12, NO, nil);
    [label.widthAnchor constraintEqualToConstant:92].active = YES;
    NSStackView* row = [NSStackView stackViewWithViews:value ? @[label, control, value] : @[label, control]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 8;
    row.distribution = NSStackViewDistributionFill;
    [control setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    if (value) [value.widthAnchor constraintEqualToConstant:38].active = YES;
    return row;
}

- (NSSlider*)slider:(double)min max:(double)max action:(SEL)sel {
    NSSlider* s = [NSSlider sliderWithValue:min minValue:min maxValue:max target:self action:sel];
    s.continuous = YES;
    s.controlSize = NSControlSizeSmall;
    s.translatesAutoresizingMaskIntoConstraints = NO;
    return s;
}

- (NSTextField*)valueLabel {
    NSTextField* v = MakeLabel(@"", 11, NO, [NSColor secondaryLabelColor]);
    v.alignment = NSTextAlignmentRight;
    v.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
    return v;
}

- (NSButton*)check:(NSString*)title action:(SEL)sel {
    NSButton* b = [NSButton checkboxWithTitle:title target:self action:sel];
    b.controlSize = NSControlSizeSmall;
    b.font = [NSFont systemFontOfSize:12];
    return b;
}

- (NSPopUpButton*)popup:(NSArray<NSString*>*)items action:(SEL)sel {
    NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [p addItemsWithTitles:items];
    p.target = self; p.action = sel;
    p.controlSize = NSControlSizeSmall;
    p.font = [NSFont systemFontOfSize:12];
    p.translatesAutoresizingMaskIntoConstraints = NO;
    return p;
}

- (NSButton*)viewButton:(NSString*)title tag:(NSInteger)tag {
    NSButton* b = [NSButton buttonWithTitle:title target:self action:@selector(viewPreset:)];
    b.tag = tag;
    b.bezelStyle = NSBezelStyleRounded;
    b.controlSize = NSControlSizeSmall;
    b.font = [NSFont systemFontOfSize:11];
    return b;
}

#pragma mark - inspector

- (NSView*)buildInspector {
    PlySettings* s = ply_settings();
    NSStackView* stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8;
    stack.edgeInsets = NSEdgeInsetsMake(14, 14, 14, 14);
    stack.translatesAutoresizingMaskIntoConstraints = NO;

    [stack addArrangedSubview:[self sectionHeader:@"Rendering"]];
    self.modeControl = [NSSegmentedControl segmentedControlWithLabels:@[@"Points", @"Splats"] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(modeChanged:)];
    self.modeControl.controlSize = NSControlSizeSmall;
    [stack addArrangedSubview:[self row:@"Mode" control:self.modeControl value:nil]];

    self.pointSizeSlider = [self slider:0.2 max:6.0 action:@selector(pointSizeChanged:)];
    self.pointSizeValue = [self valueLabel];
    [stack addArrangedSubview:[self row:@"Point size" control:self.pointSizeSlider value:self.pointSizeValue]];

    self.splatScaleSlider = [self slider:0.2 max:3.0 action:@selector(splatScaleChanged:)];
    self.splatScaleValue = [self valueLabel];
    [stack addArrangedSubview:[self row:@"Splat scale" control:self.splatScaleSlider value:self.splatScaleValue]];

    self.cutoffSlider = [self slider:0.0 max:1.0 action:@selector(cutoffChanged:)];
    self.cutoffValue = [self valueLabel];
    [stack addArrangedSubview:[self row:@"Min opacity" control:self.cutoffSlider value:self.cutoffValue]];

    self.colorPopup = [self popup:@[@"File colors", @"Height ramp", @"Normals", @"Uniform gray"] action:@selector(colorChanged:)];
    [stack addArrangedSubview:[self row:@"Color" control:self.colorPopup value:nil]];

    self.edlCheck = [self check:@"Eye-Dome Lighting (points)" action:@selector(edlChanged:)];
    [stack addArrangedSubview:self.edlCheck];
    self.edlSlider = [self slider:0.0 max:3.0 action:@selector(edlStrengthChanged:)];
    self.edlValue = [self valueLabel];
    [stack addArrangedSubview:[self row:@"EDL strength" control:self.edlSlider value:self.edlValue]];

    self.shadingCheck = [self check:@"Shade with normals when present" action:@selector(shadingChanged:)];
    [stack addArrangedSubview:self.shadingCheck];

    [stack addArrangedSubview:[self sectionHeader:@"Scene"]];
    self.bgPopup = [self popup:@[@"Studio gradient", @"Black", @"Gray", @"White"] action:@selector(bgChanged:)];
    [stack addArrangedSubview:[self row:@"Background" control:self.bgPopup value:nil]];
    self.gridCheck = [self check:@"Ground grid" action:@selector(gridChanged:)];
    self.axesCheck = [self check:@"Axis gizmo" action:@selector(axesChanged:)];
    NSStackView* toggles = [NSStackView stackViewWithViews:@[self.gridCheck, self.axesCheck]];
    toggles.spacing = 14;
    [stack addArrangedSubview:toggles];
    self.upPopup = [self popup:@[@"+Y (default)", @"+Z", @"−Y", @"−Z"] action:@selector(upChanged:)];
    [stack addArrangedSubview:[self row:@"Up axis" control:self.upPopup value:nil]];

    [stack addArrangedSubview:[self sectionHeader:@"Camera"]];
    self.projControl = [NSSegmentedControl segmentedControlWithLabels:@[@"Perspective", @"Ortho"] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(projChanged:)];
    self.projControl.controlSize = NSControlSizeSmall;
    [stack addArrangedSubview:[self row:@"Projection" control:self.projControl value:nil]];
    self.fovSlider = [self slider:15 max:100 action:@selector(fovChanged:)];
    self.fovValue = [self valueLabel];
    [stack addArrangedSubview:[self row:@"Field of view" control:self.fovSlider value:self.fovValue]];

    NSStackView* views1 = [NSStackView stackViewWithViews:@[[self viewButton:@"Front" tag:PLY_VIEW_FRONT], [self viewButton:@"Back" tag:PLY_VIEW_BACK], [self viewButton:@"Left" tag:PLY_VIEW_LEFT], [self viewButton:@"Right" tag:PLY_VIEW_RIGHT]]];
    NSStackView* views2 = [NSStackView stackViewWithViews:@[[self viewButton:@"Top" tag:PLY_VIEW_TOP], [self viewButton:@"Bottom" tag:PLY_VIEW_BOTTOM], [self viewButton:@"Iso" tag:PLY_VIEW_ISO], [self viewButton:@"Fit" tag:100], [self viewButton:@"Reset" tag:101]]];
    views1.spacing = 4; views2.spacing = 4;
    [stack addArrangedSubview:views1];
    [stack addArrangedSubview:views2];

    [stack addArrangedSubview:[self sectionHeader:@"File"]];
    self.infoLabel = MakeLabel(@"No file loaded.", 11, NO, [NSColor secondaryLabelColor]);
    self.infoLabel.lineBreakMode = NSLineBreakByWordWrapping;
    self.infoLabel.maximumNumberOfLines = 0;
    self.infoLabel.preferredMaxLayoutWidth = 250;
    self.infoLabel.selectable = YES;
    [stack addArrangedSubview:self.infoLabel];

    [stack addArrangedSubview:[self sectionHeader:@"Controls"]];
    NSTextField* help = MakeLabel(@"Drag: orbit\nRight-drag / ⌥-drag: pan\nScroll or pinch: zoom to cursor\nDouble-click: set orbit pivot\nF fit · R reset · 1–7 views\nM mode · E lighting · G grid · P projection\n[ ] point size · arrows orbit", 11, NO, [NSColor tertiaryLabelColor]);
    help.lineBreakMode = NSLineBreakByWordWrapping;
    help.maximumNumberOfLines = 0;
    help.preferredMaxLayoutWidth = 250;
    [stack addArrangedSubview:help];

    for (NSView* v in stack.arrangedSubviews) {
        if ([v isKindOfClass:[NSStackView class]] || v == self.edlCheck || v == self.shadingCheck) {
            [v.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-28].active = YES;
        }
    }

    NSScrollView* scroll = [[NSScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    FlippedView* doc = [[FlippedView alloc] init];
    doc.translatesAutoresizingMaskIntoConstraints = NO;
    [doc addSubview:stack];
    scroll.documentView = doc;
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:doc.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
        [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [doc.topAnchor constraintEqualToAnchor:scroll.contentView.topAnchor],
    ]];

    NSVisualEffectView* panel = [[NSVisualEffectView alloc] init];
    panel.material = NSVisualEffectMaterialSidebar;
    panel.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    panel.state = NSVisualEffectStateActive;
    panel.translatesAutoresizingMaskIntoConstraints = NO;
    [panel addSubview:scroll];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:panel.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor],
    ]];
    (void)s;
    return panel;
}

- (void)syncControls {
    PlySettings* s = ply_settings();
    self.modeControl.selectedSegment = s->renderMode;
    self.toolbarMode.selectedSegment = s->renderMode;
    BOOL splatOK = ply_stats()->splatCapable != 0;
    [self.modeControl setEnabled:splatOK forSegment:1];
    [self.toolbarMode setEnabled:splatOK forSegment:1];
    self.pointSizeSlider.doubleValue = s->pointSize;
    self.pointSizeValue.stringValue = [NSString stringWithFormat:@"%.1f×", s->pointSize];
    self.splatScaleSlider.doubleValue = s->splatScale;
    self.splatScaleValue.stringValue = [NSString stringWithFormat:@"%.1f×", s->splatScale];
    self.cutoffSlider.doubleValue = s->opacityCutoff;
    self.cutoffValue.stringValue = [NSString stringWithFormat:@"%.2f", s->opacityCutoff];
    [self.colorPopup selectItemAtIndex:s->colorMode];
    self.edlCheck.state = s->edl ? NSControlStateValueOn : NSControlStateValueOff;
    self.edlSlider.doubleValue = s->edlStrength;
    self.edlSlider.enabled = s->edl;
    self.edlValue.stringValue = [NSString stringWithFormat:@"%.1f", s->edlStrength];
    self.shadingCheck.state = s->shading ? NSControlStateValueOn : NSControlStateValueOff;
    [self.bgPopup selectItemAtIndex:s->background];
    self.gridCheck.state = s->showGrid ? NSControlStateValueOn : NSControlStateValueOff;
    self.axesCheck.state = s->showAxes ? NSControlStateValueOn : NSControlStateValueOff;
    [self.upPopup selectItemAtIndex:s->upAxis];
    self.projControl.selectedSegment = s->ortho;
    self.fovSlider.doubleValue = s->fov;
    self.fovValue.stringValue = [NSString stringWithFormat:@"%.0f°", s->fov];
    self.fovSlider.enabled = !s->ortho;
    const PlyInfo* info = ply_info();
    BOOL splatMode = s->renderMode == PLY_MODE_SPLATS && splatOK;
    self.pointSizeSlider.enabled = !splatMode;
    self.splatScaleSlider.enabled = splatMode;
    self.shadingCheck.enabled = info->hasNormals && !splatMode;
}

- (void)updateInfo {
    const PlyInfo* info = ply_info();
    if (info->numPoints == 0) {
        self.infoLabel.stringValue = @"No file loaded.";
        self.hintLabel.hidden = NO;
        return;
    }
    self.hintLabel.hidden = YES;
    NSNumberFormatter* nf = [[NSNumberFormatter alloc] init];
    nf.numberStyle = NSNumberFormatterDecimalStyle;
    NSMutableArray* props = [NSMutableArray array];
    if (info->isSplat) [props addObject:@"3D Gaussian splats"];
    if (info->hasColor) [props addObject:@"colors"];
    if (info->hasNormals) [props addObject:@"normals"];
    if (props.count == 0) [props addObject:@"positions only"];
    static NSString* const fmts[] = { @"ASCII", @"binary little-endian", @"binary big-endian" };
    float ex = info->bmax[0] - info->bmin[0], ey = info->bmax[1] - info->bmin[1], ez = info->bmax[2] - info->bmin[2];
    self.infoLabel.stringValue = [NSString stringWithFormat:
        @"%s\n%@ points · %@\n%@\nExtent: %.3g × %.3g × %.3g\nMedian spacing: %.3g\nLoaded in %.2f s",
        info->name, [nf stringFromNumber:@(info->numPoints)], fmts[info->format], [props componentsJoinedByString:@", "],
        ex, ey, ez, info->spacing, info->loadSeconds];
}

- (void)updateStatus {
    const PlyInfo* info = ply_info();
    PlySettings* s = ply_settings();
    PlyFrameStats* st = ply_stats();
    if (info->numPoints > 0) {
        NSNumberFormatter* nf = [[NSNumberFormatter alloc] init];
        nf.numberStyle = NSNumberFormatterDecimalStyle;
        self.statusLeft.stringValue = [NSString stringWithFormat:@"%s  ·  %@ points%@", info->name, [nf stringFromNumber:@(info->numPoints)], info->isSplat ? @"  ·  3DGS" : @""];
    } else {
        self.statusLeft.stringValue = @"Ready";
    }
    BOOL splat = s->renderMode == PLY_MODE_SPLATS && st->splatCapable;
    NSString* mode = splat ? @"Gaussian splats" : @"Points";
    NSString* sortInfo = splat && info->numPoints > 0 ? [NSString stringWithFormat:@"  ·  sort %.0f ms", st->sortMs] : @"";
    self.statusRight.stringValue = [NSString stringWithFormat:@"%@%@  ·  draw %.1f ms  ·  %.0f fps", mode, sortInfo, st->drawMs, self.glView.fps];
}

#pragma mark - settings persistence

- (void)settingsChanged {
    [self syncControls];
    [self saveSettings];
    [self.glView requestRedraw];
}

- (void)saveSettings {
    PlySettings* s = ply_settings();
    NSDictionary* d = @{
        @"pointSize": @(s->pointSize), @"splatScale": @(s->splatScale), @"opacityCutoff": @(s->opacityCutoff),
        @"edl": @(s->edl), @"edlStrength": @(s->edlStrength), @"background": @(s->background),
        @"showGrid": @(s->showGrid), @"showAxes": @(s->showAxes), @"ortho": @(s->ortho), @"fov": @(s->fov),
        @"shading": @(s->shading), @"inspector": @(self.inspectorVisible),
    };
    [[NSUserDefaults standardUserDefaults] setObject:d forKey:kSettingsKey];
}

- (void)loadSettings {
    NSDictionary* d = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kSettingsKey];
    PlySettings* s = ply_settings();
    self.inspectorVisible = YES;
    if (!d) return;
    if (d[@"pointSize"]) s->pointSize = [d[@"pointSize"] floatValue];
    if (d[@"splatScale"]) s->splatScale = [d[@"splatScale"] floatValue];
    if (d[@"opacityCutoff"]) s->opacityCutoff = [d[@"opacityCutoff"] floatValue];
    if (d[@"edl"]) s->edl = [d[@"edl"] intValue];
    if (d[@"edlStrength"]) s->edlStrength = [d[@"edlStrength"] floatValue];
    if (d[@"background"]) s->background = [d[@"background"] intValue];
    if (d[@"showGrid"]) s->showGrid = [d[@"showGrid"] intValue];
    if (d[@"showAxes"]) s->showAxes = [d[@"showAxes"] intValue];
    if (d[@"ortho"]) s->ortho = [d[@"ortho"] intValue];
    if (d[@"fov"]) s->fov = [d[@"fov"] floatValue];
    if (d[@"shading"]) s->shading = [d[@"shading"] intValue];
    if (d[@"inspector"]) self.inspectorVisible = [d[@"inspector"] boolValue];
}

#pragma mark - control actions

- (void)modeChanged:(id)sender { ply_settings()->renderMode = (int)[(NSSegmentedControl*)sender selectedSegment]; [self settingsChanged]; }
- (void)pointSizeChanged:(id)sender { ply_settings()->pointSize = (float)self.pointSizeSlider.doubleValue; [self settingsChanged]; }
- (void)splatScaleChanged:(id)sender { ply_settings()->splatScale = (float)self.splatScaleSlider.doubleValue; [self settingsChanged]; }
- (void)cutoffChanged:(id)sender { ply_settings()->opacityCutoff = (float)self.cutoffSlider.doubleValue; [self settingsChanged]; }
- (void)colorChanged:(id)sender { ply_settings()->colorMode = (int)self.colorPopup.indexOfSelectedItem; [self settingsChanged]; }
- (void)edlChanged:(id)sender { ply_settings()->edl = self.edlCheck.state == NSControlStateValueOn; [self settingsChanged]; }
- (void)edlStrengthChanged:(id)sender { ply_settings()->edlStrength = (float)self.edlSlider.doubleValue; [self settingsChanged]; }
- (void)shadingChanged:(id)sender { ply_settings()->shading = self.shadingCheck.state == NSControlStateValueOn; [self settingsChanged]; }
- (void)bgChanged:(id)sender { ply_settings()->background = (int)self.bgPopup.indexOfSelectedItem; [self settingsChanged]; }
- (void)gridChanged:(id)sender { ply_settings()->showGrid = self.gridCheck.state == NSControlStateValueOn; [self settingsChanged]; }
- (void)axesChanged:(id)sender { ply_settings()->showAxes = self.axesCheck.state == NSControlStateValueOn; [self settingsChanged]; }
- (void)upChanged:(id)sender { ply_cam_set_up_axis((int)self.upPopup.indexOfSelectedItem); [self settingsChanged]; }
- (void)projChanged:(id)sender { ply_settings()->ortho = (int)[(NSSegmentedControl*)sender selectedSegment]; ply_cam_fit(); [self settingsChanged]; }
- (void)fovChanged:(id)sender { ply_settings()->fov = (float)self.fovSlider.doubleValue; [self settingsChanged]; }

- (void)viewPreset:(id)sender {
    NSInteger tag = [(NSButton*)sender tag];
    if (tag == 100) ply_cam_fit();
    else if (tag == 101) ply_cam_reset();
    else ply_cam_view((int)tag);
    [self.glView requestRedraw];
}

#pragma mark - menu actions

- (void)openAction:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Open PLY Point Cloud";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    UTType* plyType = [UTType typeWithFilenameExtension:@"ply"];
    panel.allowedContentTypes = plyType ? @[plyType, UTTypeData] : @[UTTypeData];
    if ([panel runModal] == NSModalResponseOK && panel.URL) [self loadFile:panel.URL.path];
}

- (void)openRecent:(NSMenuItem*)item { [self loadFile:item.representedObject]; }

- (void)clearRecent:(id)sender {
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:kRecentKey];
    [self rebuildRecentMenu];
}

- (void)closeAction:(id)sender {
    [self.glView.openGLContext makeCurrentContext];
    ply_gl_clear();
    [self updateTitle];
    [self updateInfo];
    [self settingsChanged];
}

- (void)fitAction:(id)sender { ply_cam_fit(); [self.glView requestRedraw]; }
- (void)resetAction:(id)sender { ply_cam_reset(); [self.glView requestRedraw]; }
- (void)viewMenu:(NSMenuItem*)item { ply_cam_view((int)item.tag); [self.glView requestRedraw]; }
- (void)modeMenu:(NSMenuItem*)item { ply_settings()->renderMode = (int)item.tag; [self settingsChanged]; }
- (void)projMenu:(NSMenuItem*)item { ply_settings()->ortho = (int)item.tag; ply_cam_fit(); [self settingsChanged]; }
- (void)bgMenu:(NSMenuItem*)item { ply_settings()->background = (int)item.tag; [self settingsChanged]; }
- (void)colorMenu:(NSMenuItem*)item { ply_settings()->colorMode = (int)item.tag; [self settingsChanged]; }
- (void)toggleEdl:(id)sender { ply_settings()->edl = !ply_settings()->edl; [self settingsChanged]; }
- (void)toggleGrid:(id)sender { ply_settings()->showGrid = !ply_settings()->showGrid; [self settingsChanged]; }
- (void)toggleAxes:(id)sender { ply_settings()->showAxes = !ply_settings()->showAxes; [self settingsChanged]; }

- (void)toggleInspector:(id)sender {
    self.inspectorVisible = !self.inspectorVisible;
    [self applyInspectorVisibility];
    [self saveSettings];
}

- (void)applyInspectorVisibility {
    self.inspectorWidth.constant = self.inspectorVisible ? 282 : 0;
    self.inspector.hidden = !self.inspectorVisible;
    [self.glView requestRedraw];
}

- (void)snapshotAction:(id)sender {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Export Snapshot";
    UTType* png = [UTType typeWithFilenameExtension:@"png"];
    if (png) panel.allowedContentTypes = @[png];
    const PlyInfo* info = ply_info();
    NSString* base = info->numPoints > 0 ? [[NSString stringWithUTF8String:info->name] stringByDeletingPathExtension] : @"snapshot";
    panel.nameFieldStringValue = [base stringByAppendingPathExtension:@"png"];
    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        self.glView.pendingSnapshot = panel.URL.path;
        __weak AppDelegate* weakSelf = self;
        self.glView.snapshotDone = ^(BOOL ok) {
            if (!ok) {
                NSAlert* a = [[NSAlert alloc] init];
                a.messageText = @"Snapshot failed";
                a.informativeText = @"The image could not be written.";
                [a runModal];
            } else {
                weakSelf.statusLeft.stringValue = [NSString stringWithFormat:@"Saved %@", panel.URL.lastPathComponent];
            }
        };
        [self.glView requestRedraw];
    }
}

- (void)showControls:(id)sender {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"PLY Viewer controls";
    a.informativeText = @"Drag: orbit around the pivot\nRight-drag, ⌥-drag or middle-drag: pan\nScroll or pinch: zoom towards the cursor\nDouble-click a point: make it the orbit pivot\n\nF: fit to view    R: reset view    1–7: canonical views\nM: points / splats    E: eye-dome lighting    G: grid\nP: perspective / orthographic    [ ]: point size\n⌘O open    ⌘W close    ⌘S export snapshot    ⌘I inspector";
    [a runModal];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    PlySettings* s = ply_settings();
    SEL a = item.action;
    if (a == @selector(modeMenu:)) { item.state = s->renderMode == item.tag; return ply_stats()->splatCapable || item.tag == PLY_MODE_POINTS; }
    if (a == @selector(projMenu:)) item.state = s->ortho == item.tag;
    if (a == @selector(bgMenu:)) item.state = s->background == item.tag;
    if (a == @selector(colorMenu:)) item.state = s->colorMode == item.tag;
    if (a == @selector(toggleEdl:)) item.state = s->edl;
    if (a == @selector(toggleGrid:)) item.state = s->showGrid;
    if (a == @selector(toggleAxes:)) item.state = s->showAxes;
    if (a == @selector(toggleInspector:)) item.title = self.inspectorVisible ? @"Hide Inspector" : @"Show Inspector";
    if (a == @selector(closeAction:) || a == @selector(snapshotAction:) || a == @selector(fitAction:)) return ply_info()->numPoints > 0;
    return YES;
}

#pragma mark - recent files

- (void)addRecent:(NSString*)path {
    NSMutableArray* list = [[[NSUserDefaults standardUserDefaults] arrayForKey:kRecentKey] mutableCopy] ?: [NSMutableArray array];
    [list removeObject:path];
    [list insertObject:path atIndex:0];
    while (list.count > 10) [list removeLastObject];
    [[NSUserDefaults standardUserDefaults] setObject:list forKey:kRecentKey];
    [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:[NSURL fileURLWithPath:path]];
    [self rebuildRecentMenu];
}

- (void)rebuildRecentMenu {
    [self.recentMenu removeAllItems];
    NSArray* list = [[NSUserDefaults standardUserDefaults] arrayForKey:kRecentKey];
    for (NSString* p in list) {
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:p.lastPathComponent action:@selector(openRecent:) keyEquivalent:@""];
        it.representedObject = p;
        it.toolTip = p;
        it.target = self;
        [self.recentMenu addItem:it];
    }
    if (list.count > 0) {
        [self.recentMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* clear = [[NSMenuItem alloc] initWithTitle:@"Clear Menu" action:@selector(clearRecent:) keyEquivalent:@""];
        clear.target = self;
        [self.recentMenu addItem:clear];
    } else {
        NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"No Recent Files" action:nil keyEquivalent:@""];
        none.enabled = NO;
        [self.recentMenu addItem:none];
    }
}

#pragma mark - loading

- (void)updateTitle {
    const PlyInfo* info = ply_info();
    if (info->numPoints > 0) {
        self.window.title = [NSString stringWithUTF8String:info->name];
        self.window.representedURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:info->path]];
    } else {
        self.window.title = @"PLY Viewer";
        self.window.representedURL = nil;
    }
}

- (void)loadFile:(NSString*)path {
    if (self.loading || !path) return;
    if (!self.glView.ready) {
        self.pendingFile = path;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            NSString* p = self.pendingFile; self.pendingFile = nil; [self loadFile:p];
        });
        return;
    }
    self.loading = YES;
    self.loadingBox.hidden = NO;
    self.hintLabel.hidden = YES;
    self.progress.doubleValue = 0;
    self.progressLabel.stringValue = [NSString stringWithFormat:@"Opening %@…", path.lastPathComponent];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        char err[512] = "";
        PlyCloud* cloud = ply_load(path.fileSystemRepresentation, progress_cb, (__bridge void*)self, err, sizeof err);
        NSString* errText = [NSString stringWithUTF8String:err];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.loading = NO;
            self.loadingBox.hidden = YES;
            if (!cloud) {
                [self updateInfo];
                NSAlert* a = [[NSAlert alloc] init];
                a.alertStyle = NSAlertStyleWarning;
                a.messageText = [NSString stringWithFormat:@"Could not open %@", path.lastPathComponent];
                a.informativeText = errText;
                [a runModal];
                return;
            }
            [self.glView.openGLContext makeCurrentContext];
            ply_gl_set_cloud(cloud);
            [self applyCliOptions];
            [self addRecent:path];
            [self updateTitle];
            [self updateInfo];
            [self settingsChanged];
            [self updateStatus];
            if (self.cliSnapshot) {
                self.glView.pendingSnapshot = self.cliSnapshot;
                self.glView.snapshotDone = ^(BOOL ok) {
                    fprintf(ok ? stdout : stderr, ok ? "snapshot written\n" : "snapshot failed\n");
                    dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
                };
                [self.glView requestRedraw];
            }
        });
    });
}

- (void)applyCliOptions {
    PlySettings* s = ply_settings();
    if (self.cliMode) s->renderMode = [self.cliMode isEqualToString:@"splats"] ? PLY_MODE_SPLATS : PLY_MODE_POINTS;
    if (self.cliView) {
        NSDictionary* map = @{ @"front": @(PLY_VIEW_FRONT), @"back": @(PLY_VIEW_BACK), @"left": @(PLY_VIEW_LEFT), @"right": @(PLY_VIEW_RIGHT), @"top": @(PLY_VIEW_TOP), @"bottom": @(PLY_VIEW_BOTTOM), @"iso": @(PLY_VIEW_ISO) };
        NSNumber* v = map[self.cliView.lowercaseString];
        if (v) ply_cam_view(v.intValue);
    }
}

#pragma mark - toolbar

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar {
    return @[@"open", @"fit", @"reset", @"mode", @"snapshot", @"inspector", NSToolbarFlexibleSpaceItemIdentifier, NSToolbarSpaceItemIdentifier];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar {
    return @[@"open", NSToolbarSpaceItemIdentifier, @"fit", @"reset", NSToolbarSpaceItemIdentifier, @"mode", NSToolbarFlexibleSpaceItemIdentifier, @"snapshot", @"inspector"];
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar itemForItemIdentifier:(NSToolbarItemIdentifier)id willBeInsertedIntoToolbar:(BOOL)flag {
    if ([id isEqualToString:@"mode"]) {
        NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
        self.toolbarMode = [NSSegmentedControl segmentedControlWithLabels:@[@"Points", @"Splats"] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(modeChanged:)];
        item.view = self.toolbarMode;
        item.label = @"Render mode";
        item.toolTip = @"Points: crisp opaque discs with eye-dome lighting. Splats: alpha-blended gaussians.";
        return item;
    }
    NSDictionary* defs = @{
        @"open": @[@"Open", @"folder", NSStringFromSelector(@selector(openAction:)), @"Open a PLY file (⌘O)"],
        @"fit": @[@"Fit", @"arrow.up.left.and.arrow.down.right", NSStringFromSelector(@selector(fitAction:)), @"Fit the cloud in the view (F)"],
        @"reset": @[@"Reset", @"arrow.counterclockwise", NSStringFromSelector(@selector(resetAction:)), @"Reset the camera (R)"],
        @"snapshot": @[@"Snapshot", @"camera", NSStringFromSelector(@selector(snapshotAction:)), @"Export a PNG of the view (⌘S)"],
        @"inspector": @[@"Inspector", @"sidebar.right", NSStringFromSelector(@selector(toggleInspector:)), @"Show or hide the inspector (⌘I)"],
    };
    NSArray* d = defs[id];
    if (!d) return nil;
    NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:id];
    item.label = d[0];
    item.paletteLabel = d[0];
    item.image = [NSImage imageWithSystemSymbolName:d[1] accessibilityDescription:d[0]];
    item.target = self;
    item.action = NSSelectorFromString(d[2]);
    item.toolTip = d[3];
    item.bordered = YES;
    return item;
}

#pragma mark - menu

- (NSMenuItem*)item:(NSString*)title action:(SEL)sel key:(NSString*)key tag:(NSInteger)tag {
    NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:sel keyEquivalent:key];
    it.tag = tag;
    it.target = self;
    return it;
}

- (void)buildMenu {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"About PLY Viewer" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hide PLY Viewer" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"].keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit PLY Viewer" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [mainMenu addItem:appItem];

    NSMenuItem* fileItem = [[NSMenuItem alloc] init];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenu addItem:[self item:@"Open…" action:@selector(openAction:) key:@"o" tag:0]];
    NSMenuItem* recentItem = [[NSMenuItem alloc] initWithTitle:@"Open Recent" action:nil keyEquivalent:@""];
    self.recentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
    recentItem.submenu = self.recentMenu;
    [fileMenu addItem:recentItem];
    [fileMenu addItem:[self item:@"Close" action:@selector(closeAction:) key:@"w" tag:0]];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItem:[self item:@"Export Snapshot…" action:@selector(snapshotAction:) key:@"s" tag:0]];
    fileItem.submenu = fileMenu;
    [mainMenu addItem:fileItem];

    NSMenuItem* viewItem = [[NSMenuItem alloc] init];
    NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
    [viewMenu addItem:[self item:@"Points" action:@selector(modeMenu:) key:@"" tag:PLY_MODE_POINTS]];
    [viewMenu addItem:[self item:@"Gaussian Splats" action:@selector(modeMenu:) key:@"" tag:PLY_MODE_SPLATS]];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* colorItem = [[NSMenuItem alloc] initWithTitle:@"Color" action:nil keyEquivalent:@""];
    NSMenu* colorMenu = [[NSMenu alloc] initWithTitle:@"Color"];
    [colorMenu addItem:[self item:@"File Colors" action:@selector(colorMenu:) key:@"" tag:PLY_COLOR_RGB]];
    [colorMenu addItem:[self item:@"Height Ramp" action:@selector(colorMenu:) key:@"" tag:PLY_COLOR_HEIGHT]];
    [colorMenu addItem:[self item:@"Normals" action:@selector(colorMenu:) key:@"" tag:PLY_COLOR_NORMAL]];
    [colorMenu addItem:[self item:@"Uniform Gray" action:@selector(colorMenu:) key:@"" tag:PLY_COLOR_UNIFORM]];
    colorItem.submenu = colorMenu;
    [viewMenu addItem:colorItem];
    NSMenuItem* bgItem = [[NSMenuItem alloc] initWithTitle:@"Background" action:nil keyEquivalent:@""];
    NSMenu* bgMenu = [[NSMenu alloc] initWithTitle:@"Background"];
    [bgMenu addItem:[self item:@"Studio Gradient" action:@selector(bgMenu:) key:@"" tag:PLY_BG_STUDIO]];
    [bgMenu addItem:[self item:@"Black" action:@selector(bgMenu:) key:@"" tag:PLY_BG_BLACK]];
    [bgMenu addItem:[self item:@"Gray" action:@selector(bgMenu:) key:@"" tag:PLY_BG_GRAY]];
    [bgMenu addItem:[self item:@"White" action:@selector(bgMenu:) key:@"" tag:PLY_BG_WHITE]];
    bgItem.submenu = bgMenu;
    [viewMenu addItem:bgItem];
    [viewMenu addItem:[self item:@"Eye-Dome Lighting" action:@selector(toggleEdl:) key:@"" tag:0]];
    [viewMenu addItem:[self item:@"Ground Grid" action:@selector(toggleGrid:) key:@"" tag:0]];
    [viewMenu addItem:[self item:@"Axis Gizmo" action:@selector(toggleAxes:) key:@"" tag:0]];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:[self item:@"Perspective" action:@selector(projMenu:) key:@"" tag:0]];
    [viewMenu addItem:[self item:@"Orthographic" action:@selector(projMenu:) key:@"" tag:1]];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:[self item:@"Fit to View" action:@selector(fitAction:) key:@"f" tag:0]];
    [viewMenu addItem:[self item:@"Reset Camera" action:@selector(resetAction:) key:@"r" tag:0]];
    NSMenuItem* presetsItem = [[NSMenuItem alloc] initWithTitle:@"Camera Presets" action:nil keyEquivalent:@""];
    NSMenu* presets = [[NSMenu alloc] initWithTitle:@"Camera Presets"];
    NSArray* names = @[@"Front", @"Back", @"Left", @"Right", @"Top", @"Bottom", @"Isometric"];
    for (NSInteger i = 0; i < 7; i++) [presets addItem:[self item:names[i] action:@selector(viewMenu:) key:[NSString stringWithFormat:@"%ld", (long)(i + 1)] tag:i]];
    presetsItem.submenu = presets;
    [viewMenu addItem:presetsItem];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:[self item:@"Show Inspector" action:@selector(toggleInspector:) key:@"i" tag:0]];
    NSMenuItem* fs = [[NSMenuItem alloc] initWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
    fs.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    [viewMenu addItem:fs];
    viewItem.submenu = viewMenu;
    [mainMenu addItem:viewItem];

    NSMenuItem* windowItem = [[NSMenuItem alloc] init];
    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    windowItem.submenu = windowMenu;
    [mainMenu addItem:windowItem];
    NSApp.windowsMenu = windowMenu;

    NSMenuItem* helpItem = [[NSMenuItem alloc] init];
    NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
    [helpMenu addItem:[self item:@"Viewer Controls" action:@selector(showControls:) key:@"?" tag:0]];
    helpItem.submenu = helpMenu;
    [mainMenu addItem:helpItem];
    NSApp.helpMenu = helpMenu;

    NSApp.mainMenu = mainMenu;
    [self rebuildRecentMenu];
}

#pragma mark - window

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    gApp = self;
    ply_settings_default(ply_settings());
    [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
    [self parseArguments];
    [self buildMenu];

    NSRect frame = NSMakeRect(0, 0, 1500, 960);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                              NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView;
    self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    self.window.delegate = self;
    self.window.releasedWhenClosed = NO;
    self.window.titlebarAppearsTransparent = NO;
    self.window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
    self.window.backgroundColor = [NSColor colorWithCalibratedWhite:0.09 alpha:1.0];
    [self.window setContentMinSize:NSMakeSize(720, 480)];
    self.window.tabbingMode = NSWindowTabbingModeDisallowed;

    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"main"];
    toolbar.delegate = self;
    toolbar.displayMode = NSToolbarDisplayModeIconOnly;
    toolbar.allowsUserCustomization = NO;
    self.window.toolbar = toolbar;
    if (@available(macOS 11.0, *)) self.window.toolbarStyle = NSWindowToolbarStyleUnified;

    NSView* content = self.window.contentView;
    content.wantsLayer = YES;

    self.glView = [[PLYGLView alloc] initWithFrame:NSMakeRect(0, 0, 1000, 800)];
    self.glView.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:self.glView];

    self.inspector = [self buildInspector];
    [content addSubview:self.inspector];

    NSVisualEffectView* status = [[NSVisualEffectView alloc] init];
    status.material = NSVisualEffectMaterialTitlebar;
    status.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    status.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:status];
    self.statusLeft = MakeLabel(@"Ready", 11, NO, [NSColor secondaryLabelColor]);
    self.statusRight = MakeLabel(@"", 11, NO, [NSColor secondaryLabelColor]);
    self.statusRight.font = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];
    self.statusLeft.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [status addSubview:self.statusLeft];
    [status addSubview:self.statusRight];

    self.hintLabel = MakeLabel(@"Drop a .ply file here, or press ⌘O to open one", 15, NO, [NSColor colorWithCalibratedWhite:0.7 alpha:0.8]);
    [content addSubview:self.hintLabel];

    self.loadingBox = [[NSVisualEffectView alloc] init];
    self.loadingBox.material = NSVisualEffectMaterialHUDWindow;
    self.loadingBox.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    self.loadingBox.state = NSVisualEffectStateActive;
    self.loadingBox.wantsLayer = YES;
    self.loadingBox.layer.cornerRadius = 12;
    self.loadingBox.layer.masksToBounds = YES;
    self.loadingBox.translatesAutoresizingMaskIntoConstraints = NO;
    self.loadingBox.hidden = YES;
    [content addSubview:self.loadingBox];
    self.progress = [[NSProgressIndicator alloc] init];
    self.progress.style = NSProgressIndicatorStyleBar;
    self.progress.indeterminate = NO;
    self.progress.minValue = 0; self.progress.maxValue = 100;
    self.progress.translatesAutoresizingMaskIntoConstraints = NO;
    self.progressLabel = MakeLabel(@"Loading…", 12, NO, nil);
    self.progressLabel.alignment = NSTextAlignmentCenter;
    [self.loadingBox addSubview:self.progress];
    [self.loadingBox addSubview:self.progressLabel];

    self.inspectorWidth = [self.inspector.widthAnchor constraintEqualToConstant:282];
    [NSLayoutConstraint activateConstraints:@[
        [self.glView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [self.glView.topAnchor constraintEqualToAnchor:content.topAnchor],
        [self.glView.trailingAnchor constraintEqualToAnchor:self.inspector.leadingAnchor],
        [self.glView.bottomAnchor constraintEqualToAnchor:status.topAnchor],
        self.inspectorWidth,
        [self.inspector.topAnchor constraintEqualToAnchor:content.topAnchor],
        [self.inspector.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [self.inspector.bottomAnchor constraintEqualToAnchor:status.topAnchor],
        [status.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [status.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [status.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [status.heightAnchor constraintEqualToConstant:24],
        [self.statusLeft.leadingAnchor constraintEqualToAnchor:status.leadingAnchor constant:12],
        [self.statusLeft.centerYAnchor constraintEqualToAnchor:status.centerYAnchor],
        [self.statusRight.trailingAnchor constraintEqualToAnchor:status.trailingAnchor constant:-12],
        [self.statusRight.centerYAnchor constraintEqualToAnchor:status.centerYAnchor],
        [self.statusLeft.trailingAnchor constraintLessThanOrEqualToAnchor:self.statusRight.leadingAnchor constant:-16],
        [self.hintLabel.centerXAnchor constraintEqualToAnchor:self.glView.centerXAnchor],
        [self.hintLabel.centerYAnchor constraintEqualToAnchor:self.glView.centerYAnchor],
        [self.loadingBox.centerXAnchor constraintEqualToAnchor:self.glView.centerXAnchor],
        [self.loadingBox.centerYAnchor constraintEqualToAnchor:self.glView.centerYAnchor],
        [self.loadingBox.widthAnchor constraintEqualToConstant:320],
        [self.loadingBox.heightAnchor constraintEqualToConstant:84],
        [self.progressLabel.topAnchor constraintEqualToAnchor:self.loadingBox.topAnchor constant:18],
        [self.progressLabel.leadingAnchor constraintEqualToAnchor:self.loadingBox.leadingAnchor constant:20],
        [self.progressLabel.trailingAnchor constraintEqualToAnchor:self.loadingBox.trailingAnchor constant:-20],
        [self.progress.topAnchor constraintEqualToAnchor:self.progressLabel.bottomAnchor constant:10],
        [self.progress.leadingAnchor constraintEqualToAnchor:self.loadingBox.leadingAnchor constant:20],
        [self.progress.trailingAnchor constraintEqualToAnchor:self.loadingBox.trailingAnchor constant:-20],
    ]];

    [self loadSettings];
    [self applyInspectorVisibility];
    [self updateTitle];
    [self updateInfo];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.glView];
    [NSApp activateIgnoringOtherApps:YES];

    __weak AppDelegate* weakSelf = self;
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer* t) { [weakSelf updateStatus]; }];

    dispatch_async(dispatch_get_main_queue(), ^{
        [self syncControls];
        NSString* p = self.pendingFile; self.pendingFile = nil;
        if (p) [self loadFile:p];
    });
}

- (void)parseArguments {
    NSArray* args = [[NSProcessInfo processInfo] arguments];
    for (NSUInteger i = 1; i < args.count; i++) {
        NSString* a = args[i];
        if ([a isEqualToString:@"--snapshot"] && i + 1 < args.count) { self.cliSnapshot = args[++i]; }
        else if ([a isEqualToString:@"--mode"] && i + 1 < args.count) { self.cliMode = args[++i]; }
        else if ([a isEqualToString:@"--view"] && i + 1 < args.count) { self.cliView = args[++i]; }
        else if ([a hasPrefix:@"-"]) { continue; }
        else if (!self.pendingFile) { self.pendingFile = a; }
    }
}

- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename {
    if (self.glView && self.glView.ready) [self loadFile:filename];
    else self.pendingFile = filename;
    return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }

- (void)applicationWillTerminate:(NSNotification*)note {
    if (self.glView.ready) {
        [self.glView.openGLContext makeCurrentContext];
        ply_gl_shutdown();
    }
}

@end

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
