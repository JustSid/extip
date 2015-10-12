//
//  main.m
//  extip
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
#import "server.h"

typedef NS_ENUM(NSInteger, CommandAction)
{
	CommandActionGetIP,
	CommandActionStartDaemon,
	CommandActionStopDaemon,
	CommandActionReloadDaemon
};

CFMessagePortRef GetRemotePort(bool createIfNeeded)
{
	CFMessagePortRef remotePort = CFMessagePortCreateRemote(nil, CFSTR("com.widerwille.extipd.port.server"));
	if(!remotePort && createIfNeeded)
	{
		pid_t pid = StartServer();
		if(pid > 0)
		{
			do {

				int status;
				waitpid(pid, &status, WNOHANG);

				if(WIFEXITED(status))
				{
					fprintf(stderr, "Could not find extpid daemon\n");
					break;
				}

				usleep(500);
				remotePort = CFMessagePortCreateRemote(nil, CFSTR("com.widerwille.extipd.port.server"));

			} while(!remotePort);
		}
	}

	return remotePort;
}

int main(int argc, const char *argv[])
{
	@autoreleasepool
	{
		CommandAction action;

		if(argc <= 1)
		{
			action = CommandActionGetIP;
		}
		else
		{
			const char *argument = argv[1];

			if(strcmp(argument, "--start") == 0)
			{
				action = CommandActionStartDaemon;
			}
			else if(strcmp(argument, "--stop") == 0)
			{
				action = CommandActionStopDaemon;
			}
			else if(strcmp(argument, "--reload") == 0)
			{
				action = CommandActionReloadDaemon;
			}
			else if(strcmp(argument, "--ip") == 0)
			{
				action = CommandActionGetIP;
			}
			else
			{
				fprintf(stderr, "Unknown argument %s", argument);
				return EXIT_FAILURE;
			}
		}

		switch(action)
		{
			case CommandActionStartDaemon:
				return ServerMain();

			case CommandActionStopDaemon:
			case CommandActionReloadDaemon:
			{
				CFMessagePortRef port = GetRemotePort(false);
				if(!port)
					return EXIT_SUCCESS;

				SInt32 message = (action == CommandActionStopDaemon) ? 1 : 2;
				SInt32 status = CFMessagePortSendRequest(port, message, nil, 10.0, 10.0, NULL, NULL);
				CFRelease(port);

				return (status == kCFMessagePortSuccess) ? EXIT_SUCCESS : EXIT_FAILURE;
			}

			case CommandActionGetIP:
			{
				CFMessagePortRef port = GetRemotePort(true);
				if(!port)
					return EXIT_FAILURE;

				CFDataRef data;
				SInt32 status = CFMessagePortSendRequest(port, 0, nil, 10.0, 10.0, kCFRunLoopDefaultMode, &data);

				if(status == kCFMessagePortSuccess)
				{
					NSString *string = [[NSString alloc] initWithData:CFBridgingRelease(data) encoding:NSUTF8StringEncoding];
					fprintf(stdout, "%s", [string UTF8String]);
				}

				CFRelease(port);
				return (status == kCFMessagePortSuccess) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
		}
	}

	return 0;
}
