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
