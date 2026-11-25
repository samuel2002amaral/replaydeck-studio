/* Mac: o navegador (CEF) exige que o NSApplication saiba quando está tratando um evento
 * (protocolo CrAppProtocol) e que o Cocoa esteja em modo de várias threads. É o mesmo que o
 * OBS faz antes de criar a janela (frontend/utility/platform-osx.mm). */
#import <AppKit/AppKit.h>

@protocol CrAppProtocol
- (BOOL)isHandlingSendEvent;
@end

@interface RDSApplication : NSApplication <CrAppProtocol>
@property (nonatomic, getter=isHandlingSendEvent) BOOL handlingSendEvent;
@end

@implementation RDSApplication
- (void)sendEvent:(NSEvent *)event
{
    _handlingSendEvent = YES;
    [super sendEvent:event];
    _handlingSendEvent = NO;
}
@end

void rdsMacPrepareApplication()
{
    [RDSApplication sharedApplication];
    [[NSThread new] start];
}

/* ---------- permissões do Mac (câmera, microfone, gravação de tela) ---------- */
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>

/* 0 = ainda não perguntado, 1 = negado/restrito, 2 = permitido */
int rdsMacMediaStatus(bool video)
{
    AVAuthorizationStatus s =
        [AVCaptureDevice authorizationStatusForMediaType:video ? AVMediaTypeVideo : AVMediaTypeAudio];
    if (s == AVAuthorizationStatusAuthorized)
        return 2;
    if (s == AVAuthorizationStatusNotDetermined)
        return 0;
    return 1;
}

void rdsMacMediaRequest(bool video)
{
    [AVCaptureDevice requestAccessForMediaType:video ? AVMediaTypeVideo : AVMediaTypeAudio
                             completionHandler:^(BOOL) {
                             }];
}

bool rdsMacScreenAllowed()
{
    return CGPreflightScreenCaptureAccess();
}

bool rdsMacScreenRequest()
{
    return CGRequestScreenCaptureAccess();
}

void rdsMacOpenPrivacy(const char *pane)
{
    NSString *url = [NSString
        stringWithFormat:@"x-apple.systempreferences:com.apple.preference.security?%s", pane];
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
}
