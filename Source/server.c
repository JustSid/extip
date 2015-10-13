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

#import <SystemConfiguration/SystemConfiguration.h>
#import <CFNetwork/CFNetwork.h>
#import <arpa/inet.h>
#import "server.h"

// IP Handling
typedef enum
{
	IPStateInvalid,
	IPStateFetching,
	IPStateFetched
} IPState;

static CFDataRef publicIPData = NULL;
static volatile IPState publicIPState = IPStateInvalid;
static SCNetworkReachabilityRef reachability;

static void IPStreamCallback(CFReadStreamRef stream, CFStreamEventType eventType, void *clientCallBackInfo)
{
	CFMutableDataRef responseData = (CFMutableDataRef)clientCallBackInfo;

	if(eventType & kCFStreamEventHasBytesAvailable)
	{
		UInt8 buffer[1024];
		CFIndex read;

		do {
			read = CFReadStreamRead(stream, buffer, sizeof(buffer));
			if(read > 0)
				CFDataAppendBytes(responseData, buffer, read);

		} while(read > 0);
	}

	if(eventType & (kCFStreamEventEndEncountered | kCFStreamEventErrorOccurred))
	{
		if((eventType & kCFStreamEventEndEncountered) && publicIPState == IPStateFetching)
		{
			if(publicIPData)
				CFRelease(publicIPData);

			publicIPData = CFDataCreateCopy(kCFAllocatorDefault, responseData);
			publicIPState = IPStateFetched;
		}

		if(eventType & kCFStreamEventErrorOccurred)
		{
			if(publicIPData)
				CFRelease(publicIPData);

			publicIPData = NULL;
			publicIPState = IPStateInvalid;
		}

		CFReadStreamClose(stream);
		CFReadStreamSetClient(stream, 0, NULL, NULL);
		CFReadStreamUnscheduleFromRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
		CFRelease(stream);

		CFRelease(responseData);
	}
}

static void FetchIPAddress()
{
	if(publicIPState == IPStateFetching)
		return;

	publicIPState = IPStateFetching;

	const UInt8 bodyBytes[] = {0};
	CFDataRef body = CFDataCreate(kCFAllocatorDefault, bodyBytes, 0);

	CFURLRef url = CFURLCreateWithString(kCFAllocatorDefault, CFSTR("http://icanhazip.com"), NULL);
	CFHTTPMessageRef request = CFHTTPMessageCreateRequest(kCFAllocatorDefault, CFSTR("GET"), url, kCFHTTPVersion1_1);
	CFHTTPMessageSetBody(request, body);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

	// Apple suggests the NSURLSession API instead of CFReadStreamCreateForHTTPRequest,
	// But obviously that doesn't really work here

	CFReadStreamRef stream = CFReadStreamCreateForHTTPRequest(kCFAllocatorDefault, request);

#pragma clang diagnostic pop

	CFRelease(body);
	CFRelease(url);
	CFRelease(request);

	CFMutableDataRef responseData = CFDataCreateMutable(kCFAllocatorDefault, 17);
	CFStreamClientContext context = { 0, responseData, NULL, NULL, NULL };

	if(!CFReadStreamSetClient(stream, kCFStreamEventOpenCompleted | kCFStreamEventHasBytesAvailable | kCFStreamEventEndEncountered | kCFStreamEventErrorOccurred, &IPStreamCallback, &context))
	{
		CFRelease(stream);

		publicIPState = IPStateInvalid;
		return;
	}

	// Add to the run loop and open the stream
	CFReadStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);

	if(!CFReadStreamOpen(stream))
	{
		CFReadStreamSetClient(stream, 0, NULL, NULL);
		CFReadStreamUnscheduleFromRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
		CFRelease(stream);

		publicIPState = IPStateInvalid;
		return;
	}


	// Run the run loop
	do {
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, TRUE);
	} while(publicIPState == IPStateFetching);
}

static void ReachabilityCallback(__unused SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, __unused void *info)
{
	if(flags & kSCNetworkReachabilityFlagsReachable)
	{
		FetchIPAddress();
	}
	else
	{
		CFRelease(publicIPData);
		publicIPData = NULL;
		publicIPState = IPStateInvalid;
	}
}

// IPC Server

static CFDataRef IPCCallback(__unused CFMessagePortRef port, SInt32 messageID, __unused CFDataRef data, __unused void *info)
{
	switch(messageID)
	{
		case ServerCommandGetIP:
		{
			if(publicIPState == IPStateInvalid)
			{
				SCNetworkReachabilityFlags flags;
				SCNetworkReachabilityGetFlags(reachability, &flags);

				ReachabilityCallback(reachability, flags, NULL);
			}

			if(publicIPData)
				return publicIPData;

			return CFStringCreateExternalRepresentation(kCFAllocatorDefault, CFSTR(""), kCFStringEncodingUTF8, 0);
		}
		case ServerCommandStop:
		{
			CFRunLoopStop(CFRunLoopGetMain());
			return NULL;
		}
		case ServerCommandReload:
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
	// Reachability
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_len = sizeof(address);
	address.sin_family = AF_INET;

	reachability = SCNetworkReachabilityCreateWithAddress(kCFAllocatorDefault, (const struct sockaddr *) &address);

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

	CFRunLoopRemoveSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);

	CFRelease(runLoopSource);
	CFRelease(localPort);
	CFRelease(reachability);

	return 0;
}

pid_t StartServer()
{
	pid_t pid = fork();

	if(pid == 0)
	{
		CFBundleRef bundle = CFBundleGetMainBundle();
		CFURLRef url = CFBundleCopyExecutableURL(bundle);

		CFStringRef urlString = CFURLCopyPath(url);

		const char *file = CFStringGetCStringPtr(urlString, kCFStringEncodingUTF8);
		const char *args[] = { file, "--start", NULL };

		execv(file, (char *const *)args);

		// Should not be reached, but let's exit gracefully anyway
		CFRelease(url);
		CFRelease(urlString);
		exit(EXIT_FAILURE);
	}

	return pid;
}
