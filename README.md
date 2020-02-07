# C++ Linux HTTP Proxy and Cache 

[![Twitter Badge](https://img.shields.io/badge/chat-twitter-blue.svg)](https://twitter.com/ArrayLikeObj)
[![GitHub license](https://img.shields.io/github/license/ethanny2/linux-http-proxy)](https://github.com/ethanny2/linux-http-proxy/blob/master/LICENSE)
![Simple Diagram](https://www.techcoil.com/blog/wp-content/uploads/client-to-proxy-server-to-remote-server-communication.gif "HTTP proxy")


Uses HTTP proxy to get resources from a client's HTTP GET request. Responses are cached internally in the program
and the size of the cache is specified as a command line arguement. Once the cache size is too large to accmodate
more entries it evicts previous entries via the LRU scheme. 

## Requirements 
Requires some form of C++ compliation; preferably g++ and GNU make to build the project.


## Usage
 1) Make the project via GNU make.
 2) The format to run the executable named proxy is 
    ```
	./proxy [CACHE_SIZE]
    (Where cache size is the size in bytes of the cache you wish the proxy to have)
	```
 1) After running the above executable it should print out the host name and port number the proxy is running on
    ``` 
	Hostname: xxxxxxxxxxxxxxx
	Port Number: xxxxxx
	```
 1) Have the client initiate an HTTP GET request with the following Unix command.
    ```
	export http_proxy=[HOSTNAME_OF_PROXY] && wget [REQUESTED_URL]
	```
 4) Once the resource is finished downloading a string in printed to stdout via the proxy with the following info:
    ``` 
	CLIENT'S_IP|REQUESTED_URL|CACHE_STATUS|LENGTH_OF_RESPONSE_IN_BYTES|REQUEST_TIME
	```
 **(CACHE_STATUS is either CACHE_HIT (if resource was found in cache) or CACHE_MISS (if not in cache)).**

**REQUEST_TIME is how long the proxy took to return the requested resource to the client.**




## Constraints

- You must specify a cache *size equal to or larger than the largest resource you wish to download*.
If you specify a cache size smaller than the total length of the requested resource the proxy will segmentation
fault (but the client still gets the correct resource).
- This will only work with HTTP requests. *Not HTTPS.*
- Although the pthread library is included, and the proxy runs on a separate thread it can only handle *one client* at a time.
