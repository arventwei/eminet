#import "EEEmiNetTest.h"

#import <EmiNet/EmiNet.h>

@interface EEEmiNetTest () <EmiSocketDelegate, EmiConnectionDelegate>

@end

@implementation EEEmiNetTest

- (void)run
{
    EmiSocketConfig *sc = [[EmiSocketConfig alloc] init];
    sc.serverPort = 1234;
    sc.acceptConnections = YES;
    
    EmiSocket *socket = [[EmiSocket alloc] initWithDelegate:self delegateQueue:dispatch_get_main_queue()];
    NSError *error;
    if (![socket startWithConfig:sc error:&error]) {
        NSLog(@"FAIL listen! %@", error);
        return;
    }
    
    if (![socket connectToHost:@"127.0.0.1"
                        onPort:1234
                      delegate:self
                 delegateQueue:dispatch_get_main_queue()
                      userData:nil
                         error:&error]) {
        NSLog(@"FAIL connect! %@", error);
        return;
    }
    
    NSLog(@"Hej!");
}

- (void)emiSocket:(EmiSocket *)socket gotConnection:(EmiConnection *)connection
{
    NSLog(@"GOT SOCKET!");
}

- (void)emiConnectionOpened:(EmiConnection *)connection userData:(id)userData
{
    NSLog(@"Connection opened");
}

- (void)emiConnectionFailedToConnect:(EmiSocket *)socket error:(NSError *)error userData:(id)userData
{
    NSLog(@"Connection failed");
}

- (void)emiConnectionDisconnect:(EmiConnection *)connection forReason:(EmiDisconnectReason)reason
{
    NSLog(@"Disconnect");
}

- (void)emiConnectionMessage:(EmiConnection *)connection
            channelQualifier:(EmiChannelQualifier)channelQualifier
                        data:(NSData *)data
{
    NSLog(@"Got message");
}

@end
