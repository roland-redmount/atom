/**
 * Networking facilities
 *
 * Here we use conditional compilation to provide a uniform network
 * interface across Windows and Linux.
 *
 * TODO: the network code contains a lot of error checking, as connections
 * and transfers can fail for various reasons. It might be nice to have an
 * exception handling system rather to avoid axcessive error checking.
 * See for example http://www.on-time.com/ddj0011.htm
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <pthread.h>

#include "platform.h"

/**
 * Network address structure with IP and port
 * NOTE: currently we only support IPv4
 */

// the 4-byte IPv4 address a.b.c.d
typedef union s_IPv4Address {
	struct {
		byte a, b, c, d;
	} bytes;
	data32 value;
} IPv4Address;

typedef struct s_NetworkAddress{
	IPv4Address ipAddress; 
	uint16 port;
} NetworkAddress;

// create a NetworkAddress structure for an IPv4 address a.b.c.d:port
NetworkAddress CreateAddress(byte a, byte b, byte c, byte d, uint16 port);

// generate a string representation for an address
char* AddressToString(NetworkAddress addr);

/**
 * A network socket, common wrapper type for both Windows and Linux
 */
#ifdef WINDOWS
// NOTE: this is an alias for windows type SOCKET, to avoid including winsock.h here
// SOCKET appears to be a 64-bit value, but should verify that this is always true ...
typedef uint64 NetworkSocket;
#else
// Linux
typedef int NetworkSocket;
#endif

// this value is used to indicate errors
//extern NetworkSocket invalidSocket;


/**
 * Communication protocol header
 * Should precede all communication; following data depends on keyword
 *
 * NOTE: we now require the data size to be known when writing the header.
 * This makes it impossible to stream tuples;, we must always generate a complete
 * relation to determine the size field, and then send it as a block. For large
 * relations, this becomes wasteful.
 * The advantage is that the receiver knows precisely how much memory to allocate
 * to store the received data.
 */
typedef struct s_ComHeader {
	uint32 keyword;		// command or response code (we don't really need 4 bytes!)
	uint16 id;			// id to relate to other messages (should be named "requestId" ?)
	uint16 flags;
	uint32 size;			// size of remaining data (excluding this header)
} ComHeader;

// keyword definitions

// response keywords 0x0--0x100
#define COM_KEYWORD_OK					0x0		// success response, no data
#define COM_KEYWORD_ERROR				0x1		// error response, no data
#define COM_KEYWORD_EMPTY				0x3		// no data, but not an error

// keywords 0x100--0x200 reserved for directory
#define COM_KEYWORD_FINDNODE			0x100	// request for the adress of a given node
#define COM_KEYWORD_REGISTERNODE		0x102	// register a new node
#define COM_KEYWORD_NODEONLINE			0x103	// announce a node is online at given address
#define COM_KEYWORD_NODEOFFLINE			0x104	// announce a node is online at given address
#define COM_KEYWORD_REGISTERSERVICE		0x105	// register one or more services
#define COM_KEYWORD_FINDSERVICE			0x106	// find a service for a given query

#define COM_KEYWORD_NODEID				0x180	// response with node ID
#define COM_KEYWORD_NODEADDRESS			0x181	// response with node network address
#define COM_KEYWORD_SERVICEINFO			0x182	// response with service info

// general keywords 0x200--0x300
#define COM_KEYWORD_QUERY				0x200	// a query (sent to a node)
//#define COM_KEYWORD_DATUMARRAY			0x201	// a sequence of datums, e.g. a typed relation
#define COM_KEYWORD_RELATION			0x201	// a typed relation (response to query)

#define COM_KEYWORD_INVALID				0xFFFF	// used internally for uninitialized headers

// ComHeader flags
#define	COM_INCOMPLETE					0x1


// initialize the underlying network layer (sockets, etc)
// for the caller's process
void InitNetwork(void);

// create a server socket and bind to a given port on local host
// use port = 0 to obtain a free dynamic port 
NetworkSocket CreateServerSocket(uint16 port);
// set a socket to listening mode
void SetListenMode(NetworkSocket s);
// get the network address of a socket (own address)
NetworkAddress GetSocketAddress(NetworkSocket s);
// get the network address of the peer connected to a socket
NetworkAddress GetSocketPeerAddress(NetworkSocket s);

// socket IO wrapper functions
bool IsValidSocket(NetworkSocket socket);
void WriteToSocket(NetworkSocket s, byte* data, size_t nBytes);
bool ReadFromSocket(NetworkSocket s, byte* data, size_t nBytes);
void CloseSocket(NetworkSocket s);

// get a socket connection to a given server address
NetworkSocket ConnectToSocket(NetworkAddress address);

// setup a listening socket for a node ID
// needs a function handler() to call upon an incoming connection
// handler() must be re-entrant, a new thread will
// be created for each connection

// this struct is passed onto message handlers
typedef struct s_Connection Connection;
typedef struct {
	Connection * conn;		// connection with the waiting message
	void* data;				// used to pass e.g. a Node pointer
} HandlerInfo;

pthread_t CreateListener(NetworkSocket listenSocket, void* (*handler)(HandlerInfo*), void* data);



#endif	// NETWORK_H
