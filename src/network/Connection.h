/**
 * Higher-level connection 
 */

#ifndef CONNECTION_H
#define CONNECTION_H

#include "network/Network.h"

#define CONNECTION_BUFFER_SIZE	0x1000	// standard buffer size is 4kb

#define CONNECTION_OPEN			0x1		// connection is alive
// TODO: this should be a mutex!
#define CONNECTION_READ			0x2		// connection has data for reading
#define CONNECTION_WRITE		0x4		// connection is in use for writing

// create a new Connection on an existing socket 
Connection* CreateConnection(NetworkSocket socket);
// close the connection
void CloseConnection(const Connection* conn);

const ComHeader* GetComHeader(const Connection* conn);

// check if a connection has additional data to read
bool ConnectionHasDataQ(const Connection* conn);

// check if a connection is free for reading/writing
bool ConnectionFreeQ(const Connection* conn);

// send status response messages
void SendOkMessage(Connection* conn, uint32 messageId);
void SendErrorMessage(Connection* conn, uint32 messageId);
void SendEmptyMessage(Connection* conn, uint32 messageId);

/**
 * TODO: review naming of the below methods, not super consistent
 *
 * SendSomeMessage():   send an entire messages, with comheader.
 * StreamSomething():   put a structure to a connection
 * ReceiveSomething():  receive a structure from a connection
 * no name for getting an entire message, with comheader?
 */

// send a message by streaming data (e.g. tuples)
// the socket will be busy until SendMessage() is called
// if messageId = 0, a unique ID will be generated
// returns the message Id
int32 BeginMessageStream(Connection* conn, uint32 keyword, int32 messageId);
// methods for streaming data
void StreamDataBlock(Connection* conn, const void* data, size_t nBytes);
// write simple types
void StreamData8(Connection* conn, data8 data);
void StreamData16(Connection* conn, data16 data);
void StreamData32(Connection* conn, data32 data);
void StreamData64(Connection* conn, data64 data);
// write a zero-terminated string
void StreamString(Connection* conn, char const * string);
// other functions like StreamTuple, StreamFormula in respective header files
// end the stream and send the message
void EndMessageStream(Connection* conn);

// read a message in streaming fashion

// wait for a given message, or any message if messageId = 0
bool WaitForMessage(Connection* conn, uint32 messageId);
// receive from message stream into an already allocated buffer
void ReceiveDataBlock(Connection* conn, void* buffer, size_t nBytes);
// receive simple types
data8 ReceiveData8(Connection* conn);
data16 Receiveata16(Connection* conn);
data32 ReceiveData32(Connection* conn);
data64 ReceiveData64(Connection* conn);
char* ReceiveString(Connection* conn);
// done with reading, discard message and reset connecton
void EndReceive(Connection* conn);

// receive an entire message as a data block, starting with a ComHeader
byte* ReceiveMessage(Connection* conn);



// sends and receive headers
//void SendComHeader(Connection conn, uint32 keyword, uint32 messageId);
//ComHeader ReceiveComHeader(NetworkSocket s);

// send a generic message, starting with a ComHeader 
//void SendMessage(NetworkSocket s, byte* message);

// send a datum array
//void SendDatumArrayMessage(NetworkSocket s, uint32 messageId, byte64_t* datums, size_t nDatums);


#endif	// CONNECTION_H
