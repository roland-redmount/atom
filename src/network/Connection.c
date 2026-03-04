
#include <stdlib.h>

#include "network/Connection.h"

/**
 * A Connection is a socket with a buffer and state
 * This should be used by nodes
 */ 
struct s_Connection
{
	NetworkSocket socket;			// underlying network socket
	byte* buffer;					// buffer for data
	uint32 offset;				// last read/written byte in buffer + 1
	uint32 flags;					// see below
	uint32 nextMessageId;
};


/**
 * Create a new Connection on an existing socket
 * The new Connection assumes ownership of the socket
 */
Connection* CreateConnection(NetworkSocket socket)
{
	Connection* conn = malloc(sizeof(Connection));
	conn->socket = socket;
	conn->buffer = malloc(CONNECTION_BUFFER_SIZE);
	conn->flags = CONNECTION_OPEN;		// connection free to use
	conn->nextMessageId = 1;
	return conn;
}


/**
 * Close a connection
 * Also closes the underlying socket
 */
void CloseConnection(const Connection* conn)
{
	CloseSocket(conn->socket);
	free(conn->buffer);
	free((void*) conn);
}

/**
 * Get the current com header (first bytes of buffer)
 */
const ComHeader* GetComHeader(const Connection* conn)
{
	return (ComHeader*) conn->buffer;
}


/**
 * Send a message by streaming data (e.g. tuples)
 * the connection must be open and free 
 * the connection will become busy until SendMessage() is called
 * if messageId == 0, generates a new message ID; if messageId > 0,
 * it must equal to a messageId previously received on this connection
 * return the messageId
 */
int32 BeginMessageStream(Connection* conn, uint32 keyword, int32 messageId)
{
	// TODO: this should use a mutex to block until connection is free
	ASSERT(conn->flags == CONNECTION_OPEN);
	// set busy, write mode
	conn->flags |= CONNECTION_WRITE;
	// set header fields
	ComHeader* header = (ComHeader*) conn->buffer;
	header->keyword = keyword;
	if(messageId == 0)
		header->id = conn->nextMessageId++;
	else
		header->id = messageId;
	header->size = 0;			// unknown at this point
	// writing starts after header
	conn->offset = sizeof(ComHeader);
	return header->id;
}


/**
 * Stream a generic block of data
 */
void StreamDataBlock(Connection * conn, const void * data, size_t nBytes)
{
	// check that connection is in write mode
	ASSERT(conn->flags & CONNECTION_WRITE);
	byte const * p = (byte*) data;
	while(true) {
		// check that data fits in buffer
		size_t bytesFree = CONNECTION_BUFFER_SIZE - conn->offset;
		size_t copySize = nBytes <= bytesFree ? nBytes : bytesFree;
		// copy data and advance offset
		CopyMemory(p, conn->buffer + conn->offset, copySize);
		conn->offset += copySize;
		p += copySize;
		nBytes -= copySize;
		if(nBytes == 0) {
			// done
			return;
		}
		else {
			// data is larger than available buffer space
			// send the current buffer contents
			ComHeader* header = (ComHeader*) conn->buffer;
			header->size = CONNECTION_BUFFER_SIZE - sizeof(ComHeader);
			header->flags = COM_INCOMPLETE;
			PrintF("StreamDataBlock(): WriteToSocket\n");
			WriteToSocket(conn->socket, conn->buffer, CONNECTION_BUFFER_SIZE);
			// reset buffer and continue
			conn->offset = sizeof(ComHeader);
		}
	}
}

// write simple types
void StreamData8(Connection* conn, data8 data)
{
	StreamDataBlock(conn, (byte*) &data, 1);
}

void StreamData16(Connection* conn, data16 data)
{
	StreamDataBlock(conn, (byte*) &data, 2);
}

void StreamData32(Connection* conn, data32 data)
{
	StreamDataBlock(conn, (byte*) &data, 4);
}

void StreamData64(Connection* conn, data64 data)
{
	StreamDataBlock(conn, (byte*) &data, 8);
}

/**
 * Send a zero-terminated string to a connection
 * We stream the string as {uint32 size, char[] string} so that
 * receiving the string doesn't require look-ahead
 */
void StreamString(Connection* conn, char const * string)
{
	uint32 length = CStringLength(string);
	//printf("StreamString() length = %u\n", length);
	StreamData32(conn, length);
	StreamDataBlock(conn, string, length);
}

/**
 * End the stream and send the message
 */
void EndMessageStream(Connection* conn)
{
	// check that connection is in write mode
	ASSERT(conn->flags & CONNECTION_WRITE);
	// set com header fields 
	ComHeader* header = (ComHeader*) conn->buffer;
	header->size = conn->offset - sizeof(ComHeader);
	header->flags = 0;
	WriteToSocket(conn->socket, conn->buffer, conn->offset);
	// free connection
	conn->flags = CONNECTION_OPEN;
}


/**
 * Send an OK message
 */
void SendOkMessage(Connection* conn, uint32 messageId)
{
	BeginMessageStream(conn, COM_KEYWORD_OK, messageId);
	EndMessageStream(conn);
}

/**
 * Send an ERROR message
 */
void SendErrorMessage(Connection* conn, uint32 messageId)
{
	BeginMessageStream(conn, COM_KEYWORD_ERROR, messageId);
	EndMessageStream(conn);
}

/**
 * Send an EMPTY message
 */
void SendEmptyMessage(Connection* conn, uint32 messageId)
{
	BeginMessageStream(conn, COM_KEYWORD_EMPTY, messageId);
	EndMessageStream(conn);
}

/**
 * Read one message part (there may be multiple)
 * This function will block until data is available
 * Return true if successful, false if an error occured
 */
static bool readMessagePart(Connection* conn)
{
	ASSERT(conn->flags == (CONNECTION_OPEN | CONNECTION_READ));
	if(ReadFromSocket(conn->socket, conn->buffer, sizeof(ComHeader)) == false) {
		// connection closed, dropped, or other error
		conn->flags = 0;
		return false;
	}
	const ComHeader* header = GetComHeader(conn);
	if(header->size > 0) {
		// receive message body
		ReadFromSocket(conn->socket, conn->buffer + sizeof(ComHeader), header->size);
	}
	// begin reading after header 
	conn->offset = sizeof(ComHeader);
	return true;
}

/**
 * Wait for a given message, or any message if messageId = 0
 *
 * NOTE: waiting for a specific messageId should perhaps be a different
 * function, WaitForResponse(), called by a node that has initiated communication (client)
 * Waiting for _any_ message is different, occurs on the "server" node
 */
bool WaitForMessage(Connection* conn, uint32 messageId)
{
	ASSERT(conn->flags == CONNECTION_OPEN);
	// set busy, read mode
	conn->flags |= CONNECTION_READ;
	// read first message part
	if(readMessagePart(conn) == false) {
		// an error occured
		return false;
	}
	if(messageId != 0) {
		// TODO: waiting for a given message (response) requires a concurrent
		// system where this thread can sleep until the wanted message arrives,
		// while not blocking the connection for other threads.
		// for now we require the received message to match the requested ID
		const ComHeader* header = GetComHeader(conn);
		if(header->id != messageId) {
			PrintF("WaitForMessage(): messageId = %u but header->id = %u, keyword = %u\n",
				messageId, header->id, header->keyword);
			ASSERT(false);
		}
	}
	return true;
}

/**
 * Check if a connection is free
 */
bool ConnectionFreeQ(const Connection* conn)
{
	return conn->flags == CONNECTION_OPEN;
}

/**
 * Check if a connection has additional data for reading
 */
bool ConnectionHasDataQ(const Connection* conn)
{
	const ComHeader* header = GetComHeader(conn);
	return (conn->offset < sizeof(ComHeader) + header->size) ||
		 (header->flags & COM_INCOMPLETE);
}

/**
 * Receive a data block from message stream into an allocated buffer
 * Handles incomplete (chunked) messages by repeated reading from the socket
 * Frees the connection when the message is completely received
 */
void ReceiveDataBlock(Connection* conn, void* buffer, size_t nBytes)
{
//	printf("ReceiveDataBlock() nBytes = %llu\n", nBytes);
	ASSERT(conn->flags == (CONNECTION_OPEN | CONNECTION_READ));
	const ComHeader* header = GetComHeader(conn);
	byte* p = (byte*) buffer;
	while(true) {
		// check if there is sufficient data in connection buffer
		size_t bytesAvail = sizeof(ComHeader) + header->size - conn->offset;
		// number of bytes to copy in this iteration
		size_t copySize = nBytes <= bytesAvail ? nBytes : bytesAvail;
		// copy data and advance offset
		CopyMemory(p, conn->buffer + conn->offset, copySize);
		conn->offset += copySize;
		p += copySize;
		nBytes -= copySize;
		if(nBytes == 0) {
			// received all requested bytes
			return;
		}
		// else requested more data than available in connection buffer
		// check if there is a next part to the message
		if(header->flags & COM_INCOMPLETE) {
			// read next message part and continue
			PrintF("StreamDataBlock(): readMessage()\n");
			readMessagePart(conn);
		}
		else {
			// attempt to read too many bytes
			// TODO: return no. bytes read?
			ASSERT(false);
		}
	}
}


/**
 * Signal reading is done, discard message and reset connection
 * This function must be called when a message is complete read
 */
void EndReceive(Connection* conn)
{
	// check for bytes not read
	size_t bytesAvail = sizeof(ComHeader) + GetComHeader(conn)->size - conn->offset;
	// check for additional parts not read
	while(GetComHeader(conn)->flags & COM_INCOMPLETE) {
		readMessagePart(conn);
		bytesAvail += sizeof(ComHeader) + GetComHeader(conn)->size - conn->offset;
	}
	if(bytesAvail > 0)
		PrintF("EndReceive(): discarded %llu bytes\n", bytesAvail);
	// free connection
	conn->flags = CONNECTION_OPEN;
}


/**
 * Receive simple types
 */
data8 ReceiveData8(Connection* conn)
{
	data8 x;
	ReceiveDataBlock(conn, &x, 1);
	return x;
}

data16 ReceiveData16(Connection* conn)
{
	data16 x;
	ReceiveDataBlock(conn, &x, 2);
	return x;
}

data32 ReceiveData32(Connection* conn)
{
	data32 x;
	ReceiveDataBlock(conn, &x, 4);
	return x;
}

data64 ReceiveData64(Connection* conn)
{
	data64 x;
	ReceiveDataBlock(conn, &x, 8);
	return x;
}

/**
 * Receive a character string
 * The returned string is owned by the caller
 * The string must be zero-terminated in the message,
 * or NULL is returned
 */
char* ReceiveString(Connection* conn)
{
	uint32 length = ReceiveData32(conn);
	//printf("ReceiveString() length = %u\n", length);
	char* str = malloc(length + 1);
	ReceiveDataBlock(conn, str, length);
	str[length] = '\0';
	return str;
}

// receive an entire message as a data block, starting with a ComHeader
byte* ReceiveMessage(Connection* conn)
{
	// NOTE: is this used anymore?
	ASSERT(false);
	return NULL;
}
