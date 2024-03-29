#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef unsigned int dns_rr_ttl;
typedef unsigned short dns_rr_type;
typedef unsigned short dns_rr_class;
typedef unsigned short dns_rdata_len;
typedef unsigned short dns_rr_count;
typedef unsigned short dns_query_id;
typedef unsigned short dns_flags;

typedef struct {
	char *name;
	dns_rr_type type;
	dns_rr_class class;
	dns_rr_ttl ttl;
	dns_rdata_len rdata_len;
	unsigned char *rdata;
} dns_rr;

/* Extra credit for linked list */
// struct dns_answer_entry;
// struct dns_answer_entry {
// 	char *value;
// 	struct dns_answer_entry *next;
// };
// typedef struct dns_answer_entry dns_answer_entry;

#define MAX_SIZE 300

typedef struct {	// Format for first part of byte wire (everything before question and type/class)
	unsigned short id;
	unsigned short flags;
	unsigned short numQuestions;
	unsigned short answerRRs;
	unsigned int authorityAdditionalRRs;
} header;

#define NUM_HEADER_BYTES 12

// void free_answer_entries(dns_answer_entry *ans) {
// 	dns_answer_entry *next;
// 	while (ans != NULL) {
// 		next = ans->next;
// 		free(ans->value);
// 		free(ans);
// 		ans = next;
// 	}
// }

void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;
	unsigned char c;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
}

void canonicalize_name(char *name) {
	/*
	 * Canonicalize name in place.  Change all upper-case characters to
	 * lower case and remove the trailing dot if there is any.  If the name
	 * passed is a single dot, "." (representing the root zone), then it
	 * should stay the same.
	 *
	 * INPUT:  name: the domain name that should be canonicalized in place
	 */
	
	int namelen, i;

	// leave the root zone alone
	if (strcmp(name, ".") == 0) {
		return;
	}

	namelen = strlen(name);
	// remove the trailing dot, if any
	if (name[namelen - 1] == '.') {
		name[namelen - 1] = '\0';
	}

	// make all upper-case letters lower case
	for (i = 0; i < namelen; i++) {
		if (name[i] >= 'A' && name[i] <= 'Z') {
			name[i] += 32;
		}
	}
}

int name_ascii_to_wire(char *name, unsigned char *wire) {
	/* 
	 * Convert a DNS name from string representation (dot-separated labels)
	 * to DNS wire format, using the provided byte array (wire).  Return
	 * the number of bytes used by the name in wire format.
	 *
	 * INPUT:  name: the string containing the domain name
	 * INPUT:  wire: a pointer to the array of bytes where the
	 *              wire-formatted name should be constructed
	 * OUTPUT: the length of the wire-formatted name.
	 */
	char tempName[MAX_SIZE];
	strcpy(tempName, name);
	char* label = strtok(tempName, ".");

	int offset = NUM_HEADER_BYTES;	// FIXME

	// Loop through each label (each set of chars between .'s)
	while (label != NULL) {
		// Add bit representing label length
		wire[offset] = strlen(label);
		offset++;

		// Add remaining bits (converts the char into ascii)
		int i = 0;
		while (label[i] != '\0') {
			wire[offset] = label[i];
			offset++;
			i++;
		}
		label = strtok(NULL, ".");
	}

	// Add last 00 to signify question is done
	wire[offset] = ntohs(0x00);
	offset++;

	// Return how many bytes the question took up (end minus start)
	return offset - NUM_HEADER_BYTES;
}

char *name_ascii_from_wire(unsigned char *wire, unsigned char *indexp) {
	/* 
	 * Extract the wire-formatted DNS name at the offset specified by
	 * *indexp in the array of bytes provided (wire) and return its string
	 * representation (dot-separated labels) in a char array allocated for
	 * that purpose.  Update the value pointed to by indexp to the next
	 * value beyond the name.
	 *
	 * INPUT:  wire: a pointer to an array of bytes
	 * INPUT:  indexp, a pointer to the index in the wire where the
	 *              wire-formatted name begins
	 * OUTPUT: a string containing the string representation of the name,
	 *              allocated on the heap.
	 */

	// Counts how long the name is (how many characters from indexp until it hits null)
	int nameSize = 0;
	while (indexp[nameSize] != ntohs(0x00)) {
		nameSize++;
	}

	// Make char* for name (must be dynamic since we don't know how long name will be)
	unsigned char* name = (unsigned char *) malloc(MAX_SIZE);
	memcpy(name, indexp, nameSize);

	// Print bytes (debugging purposes)
	// print_bytes(name, nameSize);

	return name;
}

dns_rr rr_from_wire(unsigned char *wire, unsigned char *indexp) {
	/* 
	 * Extract the wire-formatted resource record at the offset specified by
	 * *indexp in the array of bytes provided (wire) and return a 
	 * dns_rr (struct) populated with its contents. Update the value
	 * pointed to by indexp to the next value beyond the resource record.
	 *
	 * INPUT:  wire: a pointer to an array of bytes
	 * INPUT:  indexp: a pointer to the index in the wire where the
	 *              wire-formatted resource record begins
	 * INPUT:  query_only: a boolean value (1 or 0) which indicates whether
	 *              we are extracting a full resource record or only a
	 *              query (i.e., in the question section of the DNS
	 *              message).  In the case of the latter, the ttl,
	 *              rdata_len, and rdata are skipped.
	 * OUTPUT: the resource record (struct)
	 */
	dns_rr resourceRec;
	 
	// Set name
	resourceRec.name = name_ascii_from_wire(wire, indexp);
	indexp += strlen(resourceRec.name);

	// Set type
	resourceRec.type = indexp[0] | indexp[1];	// Grabs both bits and puts them side by side in an unsigned short
	indexp += sizeof(unsigned short);

	// Set class
	resourceRec.class = indexp[0] | indexp[1];	// Grabs both bits and puts them side by side in an unsigned short
	indexp += sizeof(unsigned short);

	// Set ttl
	resourceRec.ttl = indexp[0] | indexp[1] | indexp[2] | indexp[3];
	indexp += sizeof(unsigned int);

	// Set rdata_len
	resourceRec.rdata_len = indexp[0] | indexp[1];	// Grabs both bits and puts them side by side in an unsigned short
	indexp += sizeof(unsigned short);

	//  Set rdata
	unsigned char* data = (unsigned char *) malloc(MAX_SIZE);
	memcpy(data, indexp, resourceRec.rdata_len);
	resourceRec.rdata = data;
	indexp += resourceRec.rdata_len;

	return resourceRec;
}


// int rr_to_wire(dns_rr rr, unsigned char *wire, int query_only) {
// 	/* 
// 	 * Convert a DNS resource record struct to DNS wire format, using the
// 	 * provided byte array (wire).  Return the number of bytes used by the
// 	 * name in wire format.
// 	 *
// 	 * INPUT:  rr: the dns_rr struct containing the rr record
// 	 * INPUT:  wire: a pointer to the array of bytes where the
// 	 *             wire-formatted resource record should be constructed
// 	 * INPUT:  query_only: a boolean value (1 or 0) which indicates whether
// 	 *              we are constructing a full resource record or only a
// 	 *              query (i.e., in the question section of the DNS
// 	 *              message).  In the case of the latter, the ttl,
// 	 *              rdata_len, and rdata are skipped.
// 	 * OUTPUT: the length of the wire-formatted resource record.
// 	 *
// 	 */
// }

unsigned short create_dns_query(char *qname, dns_rr_type qtype, unsigned char *wire) {
	/* 
	 * Create a wire-formatted DNS (query) message using the provided byte
	 * array (wire).  Create the header and question sections, including
	 * the qname and qtype.
	 *
	 * INPUT:  qname: the string containing the name to be queried
	 * INPUT:  qtype: the integer representation of type of the query (type A == 1)
	 * INPUT:  wire: the pointer to the array of bytes where the DNS wire
	 *               message should be constructed
	 * OUTPUT: the length of the DNS wire message
	 */
	int offset = NUM_HEADER_BYTES;

	// Build header (id, flags, numQuestions, answerRRs, authorityAdditionalRRs)
	header header;
	srand(time(0));
	header.id = ntohs(rand());	// Random
	header.flags = ntohs(0x0100);	// Hard-coded
	header.numQuestions = ntohs(0x0001);	// FIXME - Hard-coded for now, may need to be dynamic later
	header.answerRRs = ntohs(0x0000);	// FIXME - Hard-coded for now, may need to be dynamic later
	header.authorityAdditionalRRs = ntohs(0x00000000);	// Hard-coded

	// Add header to wire
	memcpy(wire, (unsigned char*)&header, sizeof(header));

	// Add question to wire
	int questionLen = name_ascii_to_wire(qname, wire);
	offset += questionLen;

	// Add type to wire
	unsigned short type = ntohs(0x0001);
	memcpy(&wire[offset], &type, sizeof(type));
	offset += sizeof(type);

	// Add class to wire
	unsigned short class = ntohs(0x0001);
	memcpy(&wire[offset], &class, sizeof(class));
	offset += sizeof(class);

	// Return total length of wire
	return offset;
}

void grabCNames(unsigned char *wire, unsigned char *indexp) {
	int i = 0;	// Needed because we want to ignore the first char if printing out the byte values themselves (it's the length)
	while(indexp[0] != 0x00) {	// While indexp isn't at null terminator
		if (indexp[0] >= 0xC0) {	// If it's a CNAME Alias (points elsewhere) - C0 is 192, meaning can't be ascii (too big)
			// printf("HERE: %x", indexp[1]);
			indexp = wire + indexp[1];	// Move indexp to the wire plus however many bytes are in the next byte
		}
		else if (indexp[0] < 0x2D) {	// If it's below where ascii starts, you know it's a label length, not an ascii char
			if (i != 0) {	// Print a dot unless it's the first one, since that's just signifying the number of bytes
				printf(".");
			}
			i++;
			indexp++;
		}
		else {
			printf("%c", indexp[0]);
			indexp++;
		}
	}
	printf("\n");
}

void *get_answer_address(char *qname, dns_rr_type qtype, unsigned char *wire, int wireLen, unsigned char *indexp) {
	/* 
	 * Extract the IPv4 address from the answer section, following any
	 * aliases that might be found, and return the string representation of
	 * the IP address.  If no address is found, then return NULL.
	 *
	 * INPUT:  qname: the string containing the name that was queried
	 * INPUT:  qtype: the integer representation of type of the query (type A == 1)
	 * INPUT:  wire: the pointer to the array of bytes representing the DNS wire message
	 * OUTPUT: a linked list of dns_answer_entrys the value member of each
	 * reflecting either the name or IP address.  If
	 */
	dns_rr resourceRec = rr_from_wire(wire, indexp);
	int resourceRecLen = strlen(resourceRec.name) + sizeof(resourceRec.type) + sizeof(resourceRec.class) + sizeof(resourceRec.ttl) + sizeof(resourceRec.rdata_len) + resourceRec.rdata_len;

	// Find out how many answer records there are (useful in for loop)
	short idx = sizeof(unsigned short) + sizeof(unsigned short) + sizeof(unsigned short);	// Skip past id, flags, numQuestions
	int numAnswerRecs = wire[numAnswerRecs] | wire[numAnswerRecs + 1];

	// Loop through each answer record
	for (int i = 0; i < numAnswerRecs; i++) {
		// Case 1 - print normal IPv4 address
		if (resourceRec.type == 1) {
			// Print IP Address
			char ipAddress[resourceRec.rdata_len];
			sprintf(ipAddress, "%d.%d.%d.%d", resourceRec.rdata[0], resourceRec.rdata[1], resourceRec.rdata[2], resourceRec.rdata[3]);
			printf("%s\n", ipAddress);
		}
		// Case 2 - CNAME aliases (step 7)
		else if (resourceRec.type == 5) {
			grabCNames(wire, indexp + resourceRecLen);	// Grab CNAMEs starting with next record
		}
		else {
			return NULL;
		}

		// Handle updates
		// Move indexp to end of the answer resource record (sets it up to be the next rr if there is one)
		indexp += resourceRecLen;
		resourceRec = rr_from_wire(wire, indexp);
		resourceRecLen = strlen(resourceRec.name) + sizeof(resourceRec.type) + sizeof(resourceRec.class) + sizeof(resourceRec.ttl) + sizeof(resourceRec.rdata_len) + resourceRec.rdata_len;
	}

	free(resourceRec.name);
	free(resourceRec.rdata);
}

int send_recv_message(unsigned char *request, int requestlen, unsigned char *response, char *server, char* port) {
	/* 
	 * Send a message (request) over UDP to a server (server) and port
	 * (port) and wait for a response, which is placed in another byte
	 * array (response).  Create a socket, "connect()" it to the
	 * appropriate destination, and then use send() and recv();
	 *
	 * INPUT:  request: a pointer to an array of bytes that should be sent
	 * INPUT:  requestlen: the length of request, in bytes.
	 * INPUT:  response: a pointer to an array of bytes in which the
	 *             response should be received
	 * OUTPUT: the size (bytes) of the response received
	 */

	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s, j;

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;	/* IPv4 */
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = IPPROTO_UDP;          /* UDP protocol */

	s = getaddrinfo(server, port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
				rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;                  /* Success */

		close(sfd);
	}

	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);           /* No longer needed */

	/* Send wire as datagram, read responses from server */

	// sendto
	send(sfd, request, requestlen, hints.ai_flags);

	// recvfrom
	char* buf[MAX_SIZE];
	ssize_t responseLen = recv(sfd, buf, MAX_SIZE, hints.ai_flags);
	memcpy(response, buf, MAX_SIZE);

	// close
	close(sfd);
	
	return responseLen;
}

void *resolve(char *qname, char *server, char *port) {
	// Clean up name (remove caps and trailing .)
	canonicalize_name(qname);		

	// Create variables for query, then make the query
	unsigned char* requestWire = (unsigned char *) malloc(MAX_SIZE);
	dns_rr_type qtype = ntohs(0x0001);
	unsigned short requestWireLen = create_dns_query(qname, qtype, requestWire);

	// Print byte wire (debugging purposes)
	// print_bytes(requestWire, requestWireLen);

	// Send off query wire from client to server using UDP
	unsigned char* responseWire = (unsigned char *) malloc(MAX_SIZE);
	int responseLen = send_recv_message(requestWire, requestWireLen, responseWire, server, port);

	// Print byte wire (debugging purposes)
	// print_bytes(responseWire, responseLen);

	// Extract answer from response
	char* indexp = responseWire + requestWireLen;
	get_answer_address(qname, qtype, responseWire, responseLen, indexp);

	// Clean up for malloc
	free(requestWire);
	free(responseWire);
}

int main(int argc, char *argv[]) {
	char *port;
	// dns_answer_entry *ans_list, *ans;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <domain name> <server> [ <port> ]\n", argv[0]);
		exit(1);
	}
	if (argc > 3) {
		port = argv[3];
	} else {
		port = "53";
	}
	resolve(argv[1], argv[2], port);
	printf("\n\n\n");
	// while (ans != NULL) {
	// 	printf("%s\n", ans->value);
	// 	ans = ans->next;
	// }
	// if (ans_list != NULL) {
	// 	free_answer_entries(ans_list);
	// }
}
