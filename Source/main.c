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

#import <CoreFoundation/CoreFoundation.h>
#import "server.h"

typedef enum
{
	CommandActionGetIP,
	CommandActionStartDaemon,
	CommandActionStopDaemon,
	CommandActionReloadDaemon
} CommandAction;

CFMessagePortRef GetRemotePort(bool createIfNeeded)
{
	CFMessagePortRef remotePort = CFMessagePortCreateRemote(kCFAllocatorDefault, kServerPortName);
	if(!remotePort && createIfNeeded)
	{
		pid_t pid = StartServer();
		if(pid > 0)
		{
			do {

				int status = 0;

				if(waitpid(pid, &status, WNOHANG) == pid)
				{
					if(WIFEXITED(status) || WIFSIGNALED(status))
					{
						fprintf(stderr, "Could not start extip daemon\n");
						break;
					}
				}

				usleep(500);
				remotePort = CFMessagePortCreateRemote(kCFAllocatorDefault, kServerPortName);

			} while(!remotePort);
		}
	}

	return remotePort;
}

static int PerformCommand(CommandAction command)
{
	switch(command)
	{
		case CommandActionStartDaemon:
			return ServerMain();

		case CommandActionStopDaemon:
		case CommandActionReloadDaemon:
		{
			CFMessagePortRef port = GetRemotePort(false);
			if(!port)
				return EXIT_SUCCESS;

			SInt32 message = (command == CommandActionStopDaemon) ? ServerCommandStop : ServerCommandReload;
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

			switch(status)
			{
				case kCFMessagePortSuccess:
				{
					CFStringRef string = CFStringCreateFromExternalRepresentation(kCFAllocatorDefault, data, kCFStringEncodingUTF8);
					fprintf(stdout, "%s", CFStringGetCStringPtr(string, kCFStringEncodingUTF8));

					CFRelease(string);
					CFRelease(data);
					break;
				}

				case kCFMessagePortIsInvalid:
				case kCFMessagePortTransportError:
				case kCFMessagePortBecameInvalidError:
					CFRelease(port);

					PerformCommand(CommandActionStopDaemon);
					return PerformCommand(command);
			}

			CFRelease(port);
			return (status == kCFMessagePortSuccess) ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}
}

int main(int argc, const char *argv[])
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

	return PerformCommand(action);
}
