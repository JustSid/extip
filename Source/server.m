//
// server.m
// extipd
//
//  Created by Sidney Just
//  Copyright (c) 2015 by Sidney Just
//  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
//  documentation files (the "Software"), to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
//  and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
//  PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
//  FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

#include <arpa/inet.h>
#import "server.h"

// IP Handling

static NSData *publicIPData = nil;

static void FetchIPAddress()
{
	@autoreleasepool
	{
		NSData *data = [NSData dataWithContentsOfURL:[NSURL URLWithString:@"http://icanhazip.com"]];
		publicIPData = data;
	}
}

static void ReachabilityCallback(__unused SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, __unused void *info)
{
	if(flags & kSCNetworkReachabilityFlagsReachable)
	{
		FetchIPAddress();
	}
	else
	{
		publicIPData = nil;
	}
}

// IPC Server

static CFDataRef IPCCallback(__unused CFMessagePortRef port, SInt32 messageID, __unused CFDataRef data, __unused void *info)
{
	switch(messageID)
	{
		case 0:
		{
			if(publicIPData)
				return (CFDataRef)CFBridgingRetain(publicIPData);

			return (CFDataRef)CFBridgingRetain([@"" dataUsingEncoding:NSUTF8StringEncoding]);
		}

		case 1:
		{
			CFRunLoopStop(CFRunLoopGetMain());
			return NULL;
		}
		case 2:
		{
			FetchIPAddress();
			return NULL;
		}

		default:
			return NULL;
	}
}

int ServerMain()
{
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_len = sizeof(address);
	address.sin_family = AF_INET;

	@autoreleasepool
	{
		// Reachability
		SCNetworkReachabilityRef reachability = SCNetworkReachabilityCreateWithAddress(kCFAllocatorDefault, (const struct sockaddr *)&address);

		if(!SCNetworkReachabilitySetCallback(reachability, &ReachabilityCallback, NULL))
			return EXIT_FAILURE;

		if(!SCNetworkReachabilityScheduleWithRunLoop(reachability, CFRunLoopGetCurrent(), kCFRunLoopCommonModes))
			return EXIT_FAILURE;


		{
			SCNetworkReachabilityFlags flags;
			SCNetworkReachabilityGetFlags(reachability, &flags);

			ReachabilityCallback(reachability, flags, NULL);
		}

		// CFMessagePort
		CFMessagePortRef localPort = CFMessagePortCreateLocal(nil, CFSTR("com.widerwille.extipd.port.server"), &IPCCallback, nil, nil);
		CFRunLoopSourceRef runLoopSource = CFMessagePortCreateRunLoopSource(nil, localPort, 0);

		CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);

		CFRunLoopRun();


		CFRelease(localPort);
		CFRelease(reachability);
	}

	return 0;
}

pid_t StartServer()
{
	pid_t pid = fork();

	if(pid == 0)
	{
		CFBundleRef bundle = CFBundleGetMainBundle();
		CFURLRef url = CFBundleCopyExecutableURL(bundle);

		const char *file = [[(__bridge NSURL *)url path] UTF8String];

		const char *args[] = { file, "--start", NULL };
		execv(file, (char *const *)args);

		CFRelease(url);
		exit(EXIT_FAILURE); // Like... Alright then
	}

	return pid;
}
