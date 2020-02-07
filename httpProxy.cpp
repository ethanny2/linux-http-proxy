
#include <sstream>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctime>
#include <chrono>
#include<sys/time.h>
//Not using C++ high resolution clock, too cumbersome
using namespace std;
#define BUFFER_SIZE 131072
//2^17
#define IP_SIZE_LIMIT 100
/*Used to gather information from HTTP requests from client */
struct info{
	 string full_url;
	 string host;
         int port;
};

/*Need to send tuple of socket started on and cache size to the thread start for the proxy */
struct proxystuff{
	int cacheSize;
	int proxySocket;
};

/*Helper functions for string manipulation */
bool has_any_digits(const string& s){
    return any_of(s.begin(), s.end(), ::isdigit);
}


info unpackResponse(string response){
	vector<string> tokens;
	info ret;
	istringstream stream(response);
	istringstream stream2(response);
	copy(istream_iterator<string>(stream), istream_iterator<string>(),back_inserter(tokens));
	/* Each token is part of string separated by a space*/
	string url = tokens.at(1);
	ret.full_url = url; 
	/* Find the host line in response and parse*/
	string line;
	while(getline(stream2,line)){
		if(line.find("Host: ")!= string::npos){
			//Found it 
			int pos = line.find(" ");
			string host_in = line.substr(pos+1, line.length());
			if(!has_any_digits(host_in)){
				//No specified port append 80 for default
				ret.port = 80;
			}else{
				//Get the port number from the string
				int pos = host_in.find(":");
				string portstr = host_in.substr(pos+1,host_in.length());
				host_in = host_in.substr(0,pos);
				//cout<<"Port number is "<<portstr<<endl;
				ret.port = stoi(portstr);
			}
			ret.host = host_in;
			//cout<<"Host name is..............."<<host_in<<endl;
		}
	}
	return ret;
}

/* Don't need the above*/


string getHostName(){
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	struct hostent* h;
	h = gethostbyname(hostname);
	return h->h_name;
}

int getPortNum(int sd){
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if(getsockname(sd, (struct sockaddr *)&addr, &len) == -1){
		perror("Error finding proxy port number: ");
		exit(1);
	}
	int num = ntohs(addr.sin_port);
	return num;
}

string get_ip_str(struct sockaddr_in data){
	char buf[IP_SIZE_LIMIT];
	inet_ntop(AF_INET, &(data.sin_addr) , buf , IP_SIZE_LIMIT);
	string retStr = string(buf);
	return retStr;
}


/*
HTTP GET header first line looks like this ...
GET http://www.cs.binghamton.edu/~yaoliu/courses/cs528/setup.html HTTP/1.1
*/
string relativeRequest(string absoluteRequest){
	string ret;
	/*Erase first 11 chars, should account for GET and the http:// */
	absoluteRequest.erase(0,11);
	/* Erase upto postion of next occurence of / char but not the / its self*/
	int pos = absoluteRequest.find("/");
	string temp = absoluteRequest.substr(pos,absoluteRequest.length());
	/* Add back the Get to the front of the string*/
	ret = "GET " + temp;
	//cout<<"Relative req: \n"<<ret<<endl; 
	return ret;
}



/*
	Reads from vaild HTTP GET response's Content-Length: filed to in order
	to determine the number of bytes to be sent back to the client
*/
int parseResponseLength(string response){
        string line;
	istringstream stream(response);
	int retLen = 0;
        while(getline(stream,line)){
		if(line.find("Content-Length:")!= string::npos){
			int pos = line.find(" ");
			string length = line.substr(pos+1,line.length());
			retLen = stoi(length); 
		}
	}
	return retLen;
}

/**********************LRU CACHE IMPLEMENTATION ************************/
//Whatever just gonna make this a global var
int lru_counter = 0;

class CacheEntry{
	public:
		int size; //in decimal bytes
		string response;
		string url;
		int lru_val;
		CacheEntry();
		CacheEntry(string url_in, string response_in , int size_in , int lru_val_in);
                friend ostream& operator<<(ostream &output, const CacheEntry &e);

};

ostream& operator<<(ostream &output, const CacheEntry &e){
	output<<"**************************"<<endl<<
	"url: "<<e.url<<endl<<
	"size: "<< e.size<<endl<<
	"lru_val: "<<e.lru_val<<endl<<
	//"response \n "<<e.response<<endl<<
	"**************************"<<endl;
	return output;
}



CacheEntry::CacheEntry(string url_in,string response_in, int size_in, int lru_val_in){
	url = url_in;
	size = size_in;
	response = response_in;
	lru_val = lru_val_in;
}

CacheEntry::CacheEntry(){
}

int calcCacheSize(vector<CacheEntry> &cache){
	int i;
        int cur_size = 0;
	if(!cache.empty()){
        	for(i = 0 ; i<cache.size();i++){
       	        	//Add the sizes of all the entries to the size of the whole cache
                	cur_size += cache.at(i).size;
        	}
	}
	return cur_size;
}

//Helper function to calculate if cache/vector is full
bool isCacheFull(vector<CacheEntry> &cache, int max_size, int incoming_entry_size){
	bool retVal = false;
	int i;
	int cur_size = calcCacheSize(cache);
	//cout<<"Current size of cache is "<<cur_size<<endl;
	//See if there is room with a the entry we wish to add as well
	cur_size += incoming_entry_size;
	//cout<<"Cache size with added incoming entry "<<cur_size<<endl;
	if(cur_size >= max_size){
		retVal = true;
		//cout<<"RETURING TRUE isCacheFull() "<<endl;
	}else{
		retVal = false;
		//cout<<"RETURNING FALSE isCacheFull() "<<endl;
	}
	return retVal;
}

//Helper function to loop through entries and evict the entry with the lowest lru_val (least recently added)
//Evict one, check length again (with size of incoming entry), if not evict another.... continue
void evictEntry(vector<CacheEntry> &cache , int max_size , int incoming_entry_size){
        int i;
        int cur_size;
	int smallest = 999999;
	int smallest_index = -1;
        for(i = 0 ; i<cache.size();i++){
		if( cache.at(i).lru_val < smallest){
			//Update
			smallest = cache.at(i).lru_val;
			smallest_index = i;
		}
        }
	//Evict the smallest lru_val, found. It was the least recently used
	cache.erase(cache.begin()+smallest_index);
	/*Evicting 1 entry may still not be enough to accomdate the incoming size*/
	if(isCacheFull(cache,max_size,incoming_entry_size) == true){
		//recall evict
		evictEntry(cache,max_size,incoming_entry_size);
	}
}

//Check if entry with specified url is in cache
// -1 if not in cache, else returns index

int getCachedResponse(vector<CacheEntry> &cache , string url_in){
	int retIndex = -1;
	int i;
	for(i = 0; i<cache.size(); i++){
		if(cache.at(i).url == url_in){
			retIndex = i;
		}
	}
	return retIndex;
}

/**********************************************/

float timedifference_msec(struct timeval t0, struct timeval t1)
{
    return (t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_usec - t0.tv_usec) / 1000.0f;
}




/*Main function of the proxy TCP server where select is implemented */
void *startProxy(void *instance){
	//Build cache on start of proxy
	vector<CacheEntry> cache;
	//Pointer instance is struct proxystuff
        proxystuff unpack =  *((proxystuff*)instance);
	int proxysock = unpack.proxySocket;
	int max_cache_size = unpack.cacheSize;
	fd_set set;
	FD_ZERO(&set);
	int maxsocket = 0;
	int retVal;
	maxsocket = proxysock+1;
	char buf[BUFFER_SIZE];
	//Contains socket descriptors of connected clients
	vector<int> clients;
	string client_ip;
	//Start communication loop
	for(;;){
		/*Timer/Clock stuff*/
		struct timeval start,end;
		//cout<<"Proxy waiting for client connections..."<<endl;
		FD_SET(proxysock,&set);
		retVal = select(maxsocket, &set, NULL, NULL ,NULL);
		if(retVal < 0){
			perror("Error in TCP proxy select() call: ");
			exit(1);
		}
		bzero(buf,BUFFER_SIZE);
		//Look for new connection requests from clients
		if(FD_ISSET(proxysock,&set)){
			struct sockaddr_in temp_addr;
			socklen_t l = sizeof(temp_addr);
			int clisock = accept(proxysock, (struct sockaddr*)&temp_addr,&l);
			/*Start clock after accepting client */
			gettimeofday(&start,NULL);
			//Update max if needed
			if(clisock == -1){
				perror("Error in TCP proxy accepting new client connection: ");
				exit(1);
			}
			if(clisock > maxsocket){
				maxsocket = clisock+1;
			}
			//Add to set of clients
			FD_SET(clisock , &set);
			//Add to vector of clients to loop through
			clients.push_back(clisock);
			client_ip = get_ip_str(temp_addr);
			//cout<<"CLIENT IP: "<<client_ip<<endl;
			//cout<<"Sucessfully accepted new client connection!"<<endl; 
		}
		int i;
		//Listen to client's HTTP requests via the vector loop
		for(i = 0 ; i< clients.size(); i++){
			if(FD_ISSET(clients.at(i),&set)){
				//cout<<"New HTTP request from connected client!"<<endl;
				read(clients.at(i),buf,sizeof(buf));
				//printf("Message is....\n\n%s\n\n", buf);
				/* Get hostname and port from message, if no port specified use 80, same redirect*/
				string str_response(buf);
				info httpReq = unpackResponse(str_response);
				const char * url = (httpReq.full_url).c_str();
				string temp = httpReq.host;
				//Yay more debugging of host name, turns out sometimes there was a /r (return key) in the input parsed causing errors
				for(char& c : temp) {
					/*None of these belong in a base URL */
					if(c == ' ' || c == '\n' || c == '\r'){
						temp.pop_back();
					}
				}
				const char* host =  (temp).c_str();
				int http_port = httpReq.port;
				/* Start connection with http server after unpacking host */
				/* Check if in cache! If so send back to client cache data, and break from current iteration of loop*/
				int index = getCachedResponse(cache,httpReq.full_url);
				//int index = -1;
				string cache_string;
				if(index != -1){
					//cout<<"CACHE HIT HAPPPENDED!!!!!!!!! "<<endl;
					cache_string = "CACHE_HIT";
					string cacheData = cache.at(index).response;
					string cur_url = cache.at(index).url;
					int len = cache.at(index).size;
                                        /*Forward response to client*/
                                        char* finalbuf = new char[len];
                                        memcpy(finalbuf,cacheData.data(),len);
                                        //length is len named var
                                        int retBytes = -1;
                                       	int sentBytes = 0;
                                       	int left = len;
                                        while(sentBytes < len){
                                        	retBytes = send(clients.at(i),finalbuf+sentBytes , left , 0);
                                                if(retBytes == -1){
                                                	perror("Send() data back to client failed: ");
                                                        exit(1);
                                                }
                                                sentBytes+= retBytes;
                                                left -= retBytes;
                                         }
					//auto end = std::chrono::steady_clock::now();
					//auto elasped_time = chrono::duration_cast<std::chrono::microseconds>(end - start);
		                        gettimeofday(&end,NULL);
                                        long elapsed_time = timedifference_msec(start,end);
          				FD_ZERO(&set);
                                        close(clients.at(i));
                                        clients.erase(clients.begin()+ i);
				        cout<<client_ip<<"|"<<cur_url<<"|"<<cache_string<<"|"<<len<<"|"<<elapsed_time<<endl;
				}else{
					//cout<<"CACHE MISS HAPPPENDED!!!!!!!1 "<<endl;
					cache_string = "CACHE_MISS";
 					struct hostent *server;
        				server = gethostbyname(host);
        				if(server == NULL){
                				perror("Problem resolving web server gethostbyname() :");
                				exit(1);
        				}
					struct sockaddr_in server_info;
        				bzero((char *)&server_info,sizeof(server_info));
        				server_info.sin_family = AF_INET;
        				server_info.sin_port = htons(http_port);
        				//Copy from hostent to sockaddr_in struct
        				bcopy((char *)server->h_addr,
        				(char *)&server_info.sin_addr.s_addr,
        				server->h_length);
        				socklen_t len = sizeof(server_info);
					/*NEED A NEW SOCKET  */
 					int newsock = socket(AF_INET,SOCK_STREAM,0);
        				if(newsock < 0 ){
                				perror("Error starting client TCP socket: ");
                				exit(1);
        				}
        				struct sockaddr_in cur_addr;
        				bzero((char *) &cur_addr, sizeof(cur_addr));
        				cur_addr.sin_family = AF_INET;
        				cur_addr.sin_addr.s_addr =  INADDR_ANY;
        				cur_addr.sin_port =  0; //System selected port
        				int result =  bind(newsock,(struct sockaddr *)&cur_addr,sizeof(struct sockaddr_in));
        				if(result < 0){
                				perror("Error binding client socket: ");
                				exit(1);
        				}
        				int connect_res = connect(newsock,(struct sockaddr *)&server_info,len);
        				if(connect_res < 0){
                				perror("Error in proxy attempting connect() to http web server :");
                				exit(1);
        				}
					//cout<<"SUCCESSFULLY CONNECTED TO WEB HTTP SERVER "<<endl;
					/*Step 4: Proxy sends prepared HTTP response to HTTP server (Should still be in buf) */
					//Should I create another mini select loop for this connection? Sure
        				fd_set set2;
        				FD_ZERO(&set2);
        				int maxsocket2 = newsock+1;
					bool processing = true;
       	 				while(processing){
						//printf("Checking buffer once again before I send \n\n%s\n\n",buf);
						//I see, now the connected socket to the website needs to use a relative get request not absolute.
						string temp_str(buf);
						string formattedReq = relativeRequest(temp_str);
						int len = formattedReq.length();
                				send(newsock,formattedReq.c_str(),len,0);
						//cout<<"Sent HTTP GET to website via proxy"<<endl;
                				FD_SET(newsock,&set2);
                				int retVal = select(maxsocket2 , &set2, NULL, NULL, NULL);
                				if(retVal < 0){
                        				perror("Error with select() call in client: ");
                        				exit(1);
                				}
                				if(FD_ISSET(newsock , &set2)){
                        				//Checking if there is a response from the http website
							char responsebuf[BUFFER_SIZE];
							bzero(responsebuf,BUFFER_SIZE);
							//int readval = read(newsock,responsebuf,BUFFER_SIZE);
							// MSG_PEEK 
							int readval = recv(newsock,responsebuf,BUFFER_SIZE,MSG_PEEK);
							//cout<<"Peeking at response from website via proxy =)"<<endl;
                                			//printf("Peeked Message is....\n\n%s\n\n", responsebuf);
							/* From the peeked msg in responsebuf calculate how long the header is and extract the contentlen*/
							string temp(responsebuf);
							int contentlen = parseResponseLength(temp);
							//cout<<"Content len from peek message is "<<contentlen<<endl; //Works
							//OR just loop and once send returns 0, then I know cumlative length is the length of the header+content
							int cumlative = 0;
							string whole_response;
							/* So why the hell doesn't this work for other files. Other than html files*/
							while(1){
	                                                	char responsebuf2[BUFFER_SIZE];
								ssize_t readval = recv(newsock,responsebuf2,BUFFER_SIZE,0);
								//cout<<"Read val currently is "<<readval<<endl;
								cumlative+=readval;
								//cout<<"Cumulative read val is "<<cumlative<<endl;
								if(readval==0){
									break;
								}
								if(readval == -1){
									perror("Recv() getting data from server failed: ");
									exit(1);
								}
								whole_response.append(responsebuf2,readval);
							}
							//Close that thing
							if(close(newsock) == -1){
								perror("close() for proxy connection to http server failed : ");
								exit(1);
							}

							//And size of header data should be  cumlative - contentlen
							int header_len = cumlative - contentlen;
							//cout<<"Header size is "<<header_len<<endl;
							//cout<<"ENTIRE COMPLETE MESSAGE is ----> \n"<<whole_response<<endl;


							/*Found the problem send() can abritrarily depending on OS stop reading the response from
							the socket at any byte, the length you provide is just the limit. I need to keep looping over
							read() or recv until the accumlated amount of bytes from each call is >= the contentlen+ headersize */

							/* Forward response to client*/
							char* finalbuf = new char[cumlative];
							//bzero(finalbuf,cumlative);
							memcpy(finalbuf,whole_response.data(),cumlative);
							//length is cumlative named var
							int retBytes = -1;
							int sentBytes = 0;
							int left = cumlative;
							while(sentBytes < cumlative){
								retBytes = send(clients.at(i),finalbuf+sentBytes , left , 0);
								if(retBytes == -1){
									perror("Send() data back to client failed: ");
									exit(1);
								}
								sentBytes+= retBytes;
								left -= retBytes;
							}
							/*Remove connected client by removing from set and vector and calling close*/
							//end = std::chrono::steady_clock::now();
							FD_ZERO(&set);
							close(clients.at(i));
							clients.erase(clients.begin()+ i);
							processing = false;
							//cout<<"Setting processing to false "<<endl;
							/* Temporary */
							//Cache the response and the used url
							lru_counter++;
							if( isCacheFull(cache,max_cache_size,cumlative)  ){
								//We need to evict then add
								//cout<<"Cache is too full, evicting an entry... "<<endl;
								evictEntry(cache,max_cache_size,cumlative);
							}
							//string rep(finalbuf);
							CacheEntry entry(httpReq.full_url,whole_response ,cumlative,lru_counter);
							cache.push_back(entry);
							//cout<<"Successfully pushed entry: \n"<<entry<<endl<<" into cache."<<endl;
							/* Print required stuff here */
                                        		//auto end = std::chrono::steady_clock::now();
                                        		//auto elasped_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
							gettimeofday(&end,NULL);
                                        		long elapsed_time = timedifference_msec(start,end);
							cout<<client_ip<<"|"<<httpReq.full_url<<"|"<<cache_string<<"|"<<cumlative<<"|"<<elapsed_time<<endl;


                			}//End of while processing
				   } //Else bracket for cache miss
        			}

			}
		}
	}
}//End of funct



int main(int argc, char *argv[]){
	/*Parse cache size */
	if(argc!=2 ){
		cout<<"Correct format is ./proxy [CACHE_SIZE]"<<endl;
		exit(1);
	}
	int cache_size = stoi(argv[1]);
	int sock;
	string hostname;
	int portnum;
	//fd_set clientsockets;
	pthread_t proxy_thread;
	pthread_t client_thread;
	/*Step 1a: Create TCP socket and listen for incoming connections.*/
	sock = socket(AF_INET,SOCK_STREAM,0);
	if(sock < 0 ){
		perror("Error starting HTTP proxy TCP socket: ");
		exit(1);
	}
	struct sockaddr_in cur_addr;
	bzero((char *) &cur_addr, sizeof(cur_addr));
	cur_addr.sin_family = AF_INET; 
	cur_addr.sin_addr.s_addr =  INADDR_ANY;
 	cur_addr.sin_port =  0; //System selected port
	int result =  bind(sock,(struct sockaddr *)&cur_addr,sizeof(struct sockaddr_in));
    	if(result < 0){
		perror("Error binding HTTP socket: ");
		exit(1);
    	}
	//Second arg is max num of connections that can be stored in backlog
	listen(sock, 5);

	/*Step 1b: Determine hostname and port number the proxy is running on */
	hostname = getHostName();
	portnum = getPortNum(sock);
	cout<<"Hostname: "<<hostname<<"\nPort Number: "<<portnum<<endl;

	/* Step 2: Wait for new connections from other tcp clients using select call.*/
	proxystuff s;
	s.cacheSize = cache_size;
	s.proxySocket = sock;
	int ret_proxy = pthread_create( &(proxy_thread), NULL, startProxy, (void*) &s);
	//Wait for the threads to finish before returning to main
	pthread_join(proxy_thread, NULL);
}

