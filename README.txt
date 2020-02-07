Project 3: HTTP PROXY WITH GET REQUESTS

Uses HTTP proxy to get resources from a client's HTTP GET request. Responses are cached internally in the program
and the size of the cache is specified as a command line arguement. Once the cache size is too large to accmodate
more entries it evicts previous entries via the LRU scheme. 

Steps
---------------------------------
 1) Make the project. Via make.
 2) Format to run the executable named proxy is 
	./proxy [CACHE_SIZE]
    (Where cache size is the size in bytes of the cache you wish the proxy to have)

 3) After running the above executable it should print out the host name and port number the proxy is running on 
	Hostname: xxxxxxxxxxxxxxx
	Port Number: xxxxxx

 3) Run the client request via the Unix command
    	export http_proxy=[HOSTNAME_OF_PROXY] && wget [REQUESTED_URL]

 4) Once the resource is finished downloading a string in printed to stdout via the proxy with the following info 
	CLIENT'S_IP|REQUESTED_URL|CACHE_STATUS|LENGTH_OF_RESPONSE_IN_BYTES|REQUEST_TIME


*(CACHE_STATUS is either CACHE_HIT (if resource in cache) or CACHE_MISS (if not in cache)).
*REQUEST_TIME is how long the proxy took to return the requested resource to the client.





*IMPORTANT: You must specify a cache size equal to or larger than the largest resource you wish to download.
If you specify a cache size smaller than the total length of the requested resource the proxy will segmentation
fault (but the client still gets the correct resource).

--> THIS IS NOT A BUG, THIS IS SPECFIED IN THE ASSIGNMENT DESCRIPTION.




---------------------------------
*ONLY WORKS ON HTTP NOT HTTPS

*Although the pthread library is included, and the proxy runs on a thread it can only handle one client at a time.
Multi-threading was optional for this assignment as per the PDFs instruction.
*Compiled to C++11 at first because high resoltion clocks were used at first to time request processing time, but
it was changed to the timeval structure; possible may be able to compile without C++ 11 flag.
