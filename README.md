## Overview

`extip` is a braindead simple command line IP fetcher that I wrote to be used in my `tmux` status bar, because I didn’t want to curl my remote IP every *n* seconds. It's designed to run as a daemon in the background listening to reachability changes and then updating the remote IP via [icanhazip.com](http://icanhazip.com). The daemon also provides a simple Mach message interface that the client uses to get the IP, which is then printed to `stdout`. Dead simple, I’m telling you.

## Usage

The tool knows exactly three arguments (which are all mutually exclusive):

* `--ip` (*default* if nothing is specified) prints the external IP to stdout
* `--start` starts the extip server (this is done implicitly when you run `--ip` and there is no running server found)
* `--stop` stops the extip server

## Example

This is what I have in my tmux config to display external, internal and VPN IP:

> set -g status-left "#[fg=green]#h @ #[fg=cyan]#(extip | awk '{print \"ip \" $1}') #[fg=yellow]#(ifconfig en0 | grep 'inet ' | awk '{print \"en0 \" $2}')  #[fg=red]#(ifconfig tun0 | grep 'inet ' | awk '{print \"vpn \" $2}'"