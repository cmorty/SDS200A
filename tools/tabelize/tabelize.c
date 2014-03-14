//	pcap_throughput
//
//	 reads in a pcap file and outputs basic throughput statistics 

#include <stdio.h>
#include <inttypes.h>
#include <pcap.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdlib.h>

#include "pcap_types.h"

void printTableheader(){
	printf("|------+---------------+----------+--------+--------+---------------------------------------------------|\n"
		   "| Type | bmRequestType | bRequest | wValue | wIndex | Data (hex)                                        |\n"
		   "|------+---------------+----------+--------+--------+---------------------------------------------------|\n");
}

void printDelim(){
	printf("|------+---------------+----------+--------+--------+---------------------------------------------------|\n");
}

int hostToDevice(unsigned char info) {
	return !(info & 1);
}

int getEndpoint(unsigned char endpoint) {
	return endpoint & 0b01111111;
}

int getDirection(unsigned char endpoint) {
	return endpoint >> 7;
}

const char *endpointToDirection(unsigned char endpoint) {
	if (getDirection(endpoint)) {
		// in
		return "i";
	} else {
		// out
		return "o";
	}
}

struct timeval start = {0, 0};

void normalizeTimeval(struct timeval *recorded, struct timeval *result) {
	if (!timerisset(&start)) {
		// This is the first packet
		start = *recorded;
		// This packet has an offset of 0
		timerclear(result);
	} else {
		// Get the offset
		timersub(recorded, &start, result);
	}
}

unsigned long timevalToMicroseconds(struct timeval *time) {
	return time->tv_sec * 1000000 + time->tv_usec;
}

int printData(FILE *fd, uint32_t length, unsigned char *pointer) {
	int charsprinted = 0;
	for(int i=0; i<length; i++) {
		fprintf(fd, "%02x", pointer[i]);
		charsprinted +=2;
		if(i != 0 && ((i + 1) % 4) == 0) {
			if ((i + 1) % 32 == 0) {
				fprintf(fd, "\n\t\t");
			} else {
				fprintf(fd, " ");
				charsprinted++;
			}
		}
	}
	return charsprinted;
}

//------------------------------------------------------------------- 
int main(int argc, char **argv) 
{ 
 
	//temporary packet buffers 
	struct pcap_pkthdr header; // The header that pcap gives us 
	const u_char *packet; // The actual packet 
	int packettodump = -1;
	
	//check command line arguments 
	if (!((argc == 2) || ((argc == 3) && (packettodump = atoi(argv[2])) > 0))) { 
  		fprintf(stderr, "Usage: %s <input pcap> <packet number>\n", argv[0]); 
		exit(EXIT_FAILURE); 
	} 
	
	//-------- Begin Main Packet Processing ------------------- 
	//loop through each pcap file in command line args 
	unsigned int pkt_counter=0;	// packet counter 
	timerclear(&start); // reset timervalues
 
	//----------------- 
	//open the pcap file 
	pcap_t *handle; 
	char errbuf[PCAP_ERRBUF_SIZE]; //not sure what to do with this, oh well 
	handle = pcap_open_offline(argv[1], errbuf);		//call pcap library function 
 
	if (handle == NULL) { 
		fprintf(stderr,"Couldn't open pcap file %s: %s\n", argv[1], errbuf); 
		return(2); 
	} 
 
	//----------------- 
	//begin processing the packets in this particular file, one at a time 
	int printdata = 0;

	printTableheader();
 
	while (packet = pcap_next(handle,&header)) { 
		if (pkt_counter != packettodump && packettodump != -1) {
			pkt_counter++;
			continue;
		}

		if (header.len < sizeof(USBPCAP_BUFFER_PACKET_HEADER)) {
			fprintf(stderr, "Packet %d smaller than usb-packet header\n", pkt_counter);
			exit(EXIT_FAILURE);
		}

		// header contains information about the packet (e.g. timestamp) 
		USBPCAP_BUFFER_PACKET_HEADER *pkt = (USBPCAP_BUFFER_PACKET_HEADER *)packet; //cast a pointer to the packet data 

		// endpoint (offset 17) is the endpoint number used on the USB bus
		// (the MSB describes transfer direction)
		int endpoint  = getEndpoint(pkt->endpoint);
		int direction = getDirection(pkt->endpoint);
		int bus = pkt->bus;
		int device = pkt->device;
		uint64_t requestid = pkt->irpId;
		unsigned char info = pkt->info;
		struct timeval normalized;
		normalizeTimeval(&header.ts, &normalized);

#if 0
		if (hostToDevice(info)) {
			printf("-> ");
		} else {
			printf("<- ");
		}

		printf("Packet %d found\n", pkt_counter);

		printf("\tID: %lx\n", requestid);
		printf("\tTime: %lu\n", timevalToMicroseconds(&normalized));
		if(!pkt->status) {
			if (hostToDevice(info) && !pkt->status) {
				printf("\tDirection: S\n");
			} else if (!hostToDevice(info) && !pkt->status) {
				printf("\tDirection: C\n");
			}
		} else {
			printf("\tDirection: E\n");
		}
		printf("\tirpInfo: %d\n", info);
		printf("\tbus: %03d\n", bus);
		printf("\tdevice: %03d\n", device);
		printf("\tendpoint: %d\n", endpoint);
#endif

		switch (pkt->transfer) {
		case USBPCAP_TRANSFER_CONTROL:
			if (header.len < sizeof(USBPCAP_BUFFER_CONTROL_HEADER)) {
				fprintf(stderr, "Packet %d is a control packet but too small for the controlheader\n", pkt_counter);
				exit(EXIT_FAILURE);
			}
			USBPCAP_BUFFER_CONTROL_HEADER *controlheader = (USBPCAP_BUFFER_CONTROL_HEADER *) pkt;

			switch (controlheader->stage) {
			case USBPCAP_CONTROL_STAGE_SETUP:
				if(header.len < sizeof(USBPCAP_BUFFER_CONTROL_HEADER) + sizeof(USB_SETUP)) {
					fprintf(stderr, "Packet %d is a setup packet but does not carry the setup header\n", pkt_counter);
					exit(EXIT_FAILURE);
				}

				USB_SETUP *setupdata = (USB_SETUP *) (((char *) pkt) + sizeof(USBPCAP_BUFFER_CONTROL_HEADER));
				//printf("\tSetupdata:\n\t\tbmRequesttype:\t%02x\n\t\tbRequest:\t%02x\n\t\twValue:\t\t%04x\n\t\twIndex:\t\t%04x\n\t\twLength:\t%04x\n", setupdata->bmRequestType, setupdata->bRequest, setupdata->wValue, setupdata->wIndex, setupdata->wLength);
				if(setupdata->bRequest != 0xc0){
					printf("| C%s   |", endpointToDirection(pkt->endpoint));
					printf(" 0x%02x          |", setupdata->bmRequestType);
					printf(" 0x%02x     |", setupdata->bRequest);
					printf(" 0x%02x   |", setupdata->wValue);
					printf(" 0x%02x   |", setupdata->wIndex);
					printdata = 1;
				}

				if (header.len > sizeof(USBPCAP_BUFFER_CONTROL_HEADER) + sizeof(USB_SETUP)) {
					fprintf(stderr, "Spare data in setup packet %d\n", pkt_counter);
				} else {
					//printf("\tData: <\n");
				}
				break;
			case USBPCAP_CONTROL_STAGE_DATA:
#if 0
				printf("\tURB-Statusword: %u\n", pkt->status);
				printf("\tLength: %d\n", pkt->dataLength);
				printf("\tData:\t");
#endif
				if(printdata){
					printf(" '");
					int dataprinted = printData(stdout, pkt->dataLength, ((char *) pkt) + sizeof(USBPCAP_BUFFER_CONTROL_HEADER));
					printf("'");
					for(int k=0; k< 47-dataprinted; k++)
						printf(" ");
					printf(" |\n");
					printDelim();
				}
				printdata = 0;
				break;
			case USBPCAP_CONTROL_STAGE_STATUS:
#if 0
				// Build a demo packet with the appropriate statuscode
				printf("\tURB-Statusword: %u\n", pkt->status);
				printf("\tLength: %d\n", pkt->dataLength);
				if (pkt->dataLength > 0) {
					fprintf(stderr, "Warning: a STAGE_STATUS package with datasize > 0 received, parsing assumptions may not hold\n");
				}
				printf("\tData:\t");
				printData(stdout, pkt->dataLength, ((char *) pkt) + sizeof(USBPCAP_BUFFER_CONTROL_HEADER));
				printf("\n");
#endif
				if(printdata) {
					printf("  ");
					printf(" ");
					for(int k=0; k< 47; k++)
						printf(" ");
					printf(" |\n");
					printdata = 0;
				}
				break;
			default:
				fprintf(stderr, "Unknown control stage received\n");
			}
			break;

		case USBPCAP_TRANSFER_BULK:
#if 0
			printf("\tTransfertype: B%s\n", endpointToDirection(pkt->endpoint));
			printf("\tData:\t");
			printData(stdout, pkt->dataLength, ((char *) pkt) + sizeof(USBPCAP_BUFFER_PACKET_HEADER));
			printf("\n");
#endif
			break;

		case USBPCAP_TRANSFER_ISOCHRONOUS:
			fprintf(stderr, "Isochronous Transfer not implemented, skipping\n");
			break;

		case USBPCAP_TRANSFER_INTERRUPT:
			fprintf(stderr, "Interrupt Transfer not implemented, skipping\n");
			break;

		default:
			fprintf(stderr, "Unknown transfertype found, skipping\n");
			break;
		}
 
		pkt_counter++; //increment number of packets seen 

	if (packettodump != -1) {
		// the was only this one packet to dump
		pcap_close(handle);  //close the pcap file 
		exit(EXIT_SUCCESS);
	}
 
	} //end for loop through each command line argument 

	pcap_close(handle);  //close the pcap file 
	exit(EXIT_SUCCESS);
	//---------- Done with Main Packet Processing Loop --------------  
	return 0; //done
} //end of main() function
