
#include <stdlib.h>

#include "network/Network.h"
#include "network/Connection.h"


#ifdef WINDOWS

#include <winsock2.h>

// the windows includes #defines SendMessage to either SendMessageW or SendMessageA
// (this is a window messaging function)
// it conflicts with our SendMessage() so we undefine it;
// hope this doesn't break something ...
#undef SendMessage

#endif


bool IsValidSocket(NetworkSocket socket)
{
#ifdef WINDOWS
	return socket == INVALID_SOCKET;
#else
	return socket < 0;
#endif
}

void InitNetwork(void)
{
#ifdef WINDOWS

	// info about winsock library, filled out by WSAStartup
	WSADATA wsa;
	// initialize winsock (load ws2_32.dll)
	// require version 2.2
	ASSERT(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

#endif
}

/**
 * Create a network address structure
 * An IPv4 address a.b.c.d is stored in "network order" (big endian),
 * with byte a first. This is the same used by Windows sockaddr_in structure
 * The port number is assumed to be in local machine endianess (little endian on x86)
 */ 
NetworkAddress CreateAddress(byte a, byte b, byte c, byte d, uint16 port)
{
	NetworkAddress addr;
	addr.ipAddress = (IPv4Address) {.bytes = {a, b, c, d}};
	addr.port = port;
	return addr;
}

/**
 * Generate a string representation of a network address
 * The string is malloc'ed and must be free'd by the caller
 */
char* AddressToString(NetworkAddress addr)
{
	// longest IP string aaa.bbb.ccc.ddd:12345 = 21 chars plus \0
	char* string = (char*) malloc(22);
	FormatString(string, 22, "%u.%u.%u.%u:%u", 
		addr.ipAddress.bytes.a, addr.ipAddress.bytes.b, addr.ipAddress.bytes.c, addr.ipAddress.bytes.d, 
		addr.port); 
	return string;
}


/**
 * Create a "server" socket on the local machine, binding to all interfaces (INADDR_ANY)
 */
NetworkSocket CreateServerSocket(uint16 port)
{
#ifdef WINDOWS

	// socket description, IPv4 streaming, any protocol
	SOCKET winSocket = socket(AF_INET, SOCK_STREAM, 0);
	ASSERT(winSocket != INVALID_SOCKET);
	//printf("Socket created.\n");
	
	// address information for socket
	struct sockaddr_in socketAddress; 
	socketAddress.sin_family = AF_INET;				// IPv4
	socketAddress.sin_addr.s_addr = INADDR_ANY;		// IP assigned by OS
	socketAddress.sin_port = htons(port);			// htons() converts to TCP/IP byte order
	// bind socket to address
	if(bind(winSocket, (struct sockaddr*) &socketAddress, sizeof(socketAddress)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d" , WSAGetLastError());
	}
	return winSocket;

#else
	// TODO for linux
	return 0;
#endif
}

void SetListenMode(NetworkSocket s)
{
#ifdef WINDOWS

	// put socket in listening mode
	// SOMAXCONN lets the OS choose the maximum backlog size
	// to be a "reasonably large" value 
	ASSERT(listen(s, SOMAXCONN) == 0);

#endif
}

/**
 * Find the network address associated with a socket (own address)
 * NOTE: when binding a socket to "any" interface with INET_ANY,
 * this returns IPv4 address 0.0.0.0
 */
NetworkAddress GetSocketAddress(NetworkSocket s)
{
#ifdef WINDOWS

	struct sockaddr_in addr;
	int addrSize = sizeof(addr);
	ASSERT(getsockname(s, (struct sockaddr*) &addr, &addrSize) == 0);

	// convert address format
	NetworkAddress socketAddress;
	socketAddress.ipAddress.value = addr.sin_addr.s_addr;
	socketAddress.port = ntohs(addr.sin_port);	
	return socketAddress;
#else
	// TODO for Linux
	return (NetworkAddress) {{.value = 0}, 0};
#endif
}

/**
 * Find the network address of the peer connected to a socket (remote address)
 * TODO: this seems to return the port number - 1 ?
 * Is "peer" port always odd (-1), and "own" port always even? 
 */
NetworkAddress GetSocketPeerAddress(NetworkSocket s)
{
#ifdef WINDOWS

	struct sockaddr_in addr;
	int addrSize = sizeof(addr);
	ASSERT(getpeername(s, (struct sockaddr*) &addr, &addrSize) == 0);

	// convert address format
	NetworkAddress peerAddress;
	peerAddress.ipAddress.value = addr.sin_addr.s_addr;
	peerAddress.port = ntohs(addr.sin_port);	
	return peerAddress;
#else
	// TODO for Linux
	return (NetworkAddress) {{.value = 0}, 0}; 
#endif
}


/**
 * Connect to a server and return a client socket
 * NOTE: should just be called Connect(), it doesn't connect *to*
 * a socket, it *creates* a socket, which is a part of the connectoon
 */
NetworkSocket ConnectToSocket(NetworkAddress address)
{
#ifdef WINDOWS

	// create socket, IPv4 streaming, any protocol
	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(clientSocket == INVALID_SOCKET) {
		printf("Failed to create network socket, winsock error 0x%x\n", WSAGetLastError());
		return invalidSocket;
	}
	// address information for connection
	struct sockaddr_in serverAddress; 
	serverAddress.sin_family = AF_INET;						// IPv4
	serverAddress.sin_addr.s_addr = address.ipAddress.value;
	serverAddress.sin_port = htons(address.port);			// htons() converts to TCP/IP byte order

//	printf("Connecting to %lx:%u\n", serverAddress.sin_addr.s_addr, serverAddress.sin_port);

	// connect socket to remote address
	int result = connect(clientSocket,
		(struct sockaddr *) &serverAddress, sizeof(serverAddress));
	if (result != 0)
	{
		// failed to connect
		char* addressString = AddressToString(address);
		printf("Failed to connect to %s, winsock error 0x%x\n", addressString, WSAGetLastError());
		free(addressString);
		closesocket(clientSocket);
		return invalidSocket;
	}
	return clientSocket;
#else
	// TODO for Linux
	return 0;
#endif
}

/**
 * Write to a network socket
 */
void WriteToSocket(NetworkSocket s, byte* buffer, size_t nBytes)
{
#ifdef WINDOWS
	ASSERT(send(s, (char*) buffer, nBytes, 0) == nBytes);
#endif
}

/**
 * Read a given number of bytes from a network socket
 * This function blocks until the desired number of bytes have been read.
 * Returns true if successful, or false the client closes the connection
 */
bool ReadFromSocket(NetworkSocket s, byte* buffer, size_t nBytes)
{
#ifdef WINDOWS
	int result = recv(s, (char*) buffer, nBytes, 0);
	if(result == 0) {
		// client closed connection
		printf("ReadFromSocket(): client closed connection\n");
		return false;
	}
	if(result < 0) {
		printf("ReadFromSocket(): winsock error %d\n", WSAGetLastError());
		return false;
	}
	// else result holds no. bytes read, must match nBytes
	if(result != nBytes) {
		printf("ReadFromSocket(): failed to read %llu bytes, got %d bytes\n",
			nBytes, result);
		return(false);
	}
	// read ok
	return true;
#else
	// TODO for Linux
	return false;
#endif
}

/**
 * Close a socket
 */
void CloseSocket(NetworkSocket s)
{
#ifdef WINDOWS
	closesocket(s);
#else
	// TODO for Linux
#endif
}

/**
 * Thread for spawning handlers upon incoming connections
 */
typedef struct {
	NetworkSocket listenSocket;
	void* data;
	void* (*handler)(HandlerInfo*);
} ListenerInfo;

/**
 * Listener thread, waits for a socket connection and spawns handler,
 * specified in the info structure
 */
static void* listener(ListenerInfo* info)
{
#ifdef WINDOWS
	// socket address
	struct sockaddr_in socketAddress;
	int addressSize = sizeof(struct sockaddr_in);
	NetworkSocket newSocket;

	while(TRUE) {
		// wait for new connection and create new socket
		newSocket = accept(info->listenSocket, (struct sockaddr*) &socketAddress, &addressSize);
		ASSERT(newSocket != INVALID_SOCKET);
/*		if(newSocket == INVALID_SOCKET) {
			printf("accept failed with error code : %d" , WSAGetLastError());
			ASSERT(false);
		} */
		// create data structure for handler
		HandlerInfo handlerInfo = {CreateConnection(newSocket), info->data};
		// create handler thread
		pthread_t thread;
		pthread_create(&thread, NULL, (void* (*)(void*)) info->handler, (void*) &handlerInfo);
		// thread should exit when socket connection is lost
	}
	// this statement is never reached
	return NULL;
#else
	// TODO for Linux
	return NULL;
#endif
}

/**
 * Creates a "listener" thread that listens for incoming connections
 * on the given socket, and spawns a new thread entering handler()
 * for each incoming connection
 * The data pointer is passed on to each new handler() thread, can be
 * used to pass a Node* for example 
 */
pthread_t CreateListener(NetworkSocket listenSocket, void* (*handler)(HandlerInfo*), void* data)
{
	//printf("CreateListener()\n");
	//printf("listenSocket = %llu\n", listenSocket);
	SetListenMode(listenSocket);

	// create the listener thread
	// NOTE: info structure must be heap allocated here
	// since the thread may access is after this frame exits
	ListenerInfo* info = malloc(sizeof(ListenerInfo));
	info->listenSocket = listenSocket;
	info->data = data;
	info->handler = handler;
	pthread_t thread;
	pthread_create(&thread, NULL, (void* (*)(void*)) &listener, info);
	return thread;
}

/**
 * Send a ComHeader without additional data
 * NOTE: this ignores the message size field?
 */
/*
void SendComHeader(NetworkSocket s, uint32 keyword, uint32 messageId)
{
	ComHeader header;
	header.keyword = keyword;
	header.id = messageId;
	WriteToSocket(s, (byte*) &header, sizeof(ComHeader));
}
*/

/**
 * Wait for a new message and receive it
 * First bytes contains ComHeader
 * Caller is responsible for deallocating the message
 * Return NULL if the client closes the connection
 */

/*
byte* ReceiveMessage(NetworkSocket s)
{
	ComHeader header;
	if(ReadFromSocket(s, (byte*) &header, sizeof(ComHeader)) == 0) {
		// connection was closed
		return NULL;
	}
	// allocate space for header + message
	byte* message = malloc(sizeof(ComHeader) + header.size);
	// copy header to beginning of message
	memcpy(message, &header, sizeof(ComHeader));
	if(header.size > 0) {
		// receive message body
		ReadFromSocket(s, message + sizeof(ComHeader), header.size);
	}
	return message;
}
*/

/**
 * Send a message, first bytes contains ComHeader
 */
/*
void SendMessage(NetworkSocket s, byte* message)
{
	ComHeader* header = (ComHeader*) message;
	WriteToSocket(s, message, sizeof(ComHeader) + header->size);
}
*/

