/*
 * $Id$
 *
 * ward v1.7 - Classic War Dialer with GSM enhancements
 * Copyright (c) 2001 Raptor <raptor@0xdeadbeef.eu.org>
 *
 * WARD is a classic "War Dialer": it scans a list of phone numbers, 
 * finding the ones where a modem is answering the call on the other
 * end and providing a nicely formatted output of the scan results.
 * WARD can generate phone numbers lists based on a user-supplied 
 * mask, in incremental or random order. WARD is one of the fastest 
 * PBX scanners around (and possibly the best for UNIX environment).
 * Remember to change some defines to make it fit your configuration. 
 *
 * Program inspired to the old sordial.c (thanx sorbo!).
 * Many thanx also go to Megat0n for his termios advice.
 *
 * Tested on Linux and OpenBSD. Compile with gcc ward.c -o ward -lm.
 *
 * FOR EDUCATIONAL PURPOSES.
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <math.h>
#include <string.h>

#define VERSION         "1.7"
#define AUTHOR          "Raptor"
#define MAIL_SUPPORT    "<raptor@0xdeadbeef.eu.org>"

#define OPT_NMASK      	0x01
#define OPT_RAND       	0x02
#define OPT_GENERATE   	0x04
#define OPT_SCAN	0x08
#define OPT_TIMEOUT   	0x10

/* Color definitions */
#define GREEN 		"\E[32m\E[1m"
#define YELLOW 		"\E[33m\E[1m"
#define RED		"\E[31m\E[1m"
#define BLUE		"\E[34m\E[1m"
#define BRIGHT  	"\E[m\E[1m"
#define NORMAL  	"\E[m"

/* Local setup - change if needed */
#define MODEM_DEV 	"/dev/modem"
#define MODEM_SPEED 	B9600
#define MODEM_TIMEOUT 	60
#define MAX_NUM_LEN 	15
#define MAX_RETRIES 	2
/* Local setup end */


/* Function prototypes */
void scan(char *nfile);
int dial(char *number, int retry);
void writefile(int last, int incr, char *nfile);
void listgen(char *nmask, int incr, char *nfile);
int initmodem(char * device);
void closemodem(int fd);
int hupmodem(int fd);
void sendcmd(int fd, int timewait, char * fmt,...);
void cleanup(int ignored);
void fatalerr(char * pattern,...);
void usage(char *name);
/* Prototypes end */


int fd, timeout = MODEM_TIMEOUT;
char **numbers;
struct termios newtio, oldtio;



int main(int argc, char **argv) 
{
	char nmask[MAX_NUM_LEN], nfile[255];
	int incr = 1;

	int opt_line = 0;

/* Disable buffering for stdout */
	setvbuf(stdout, NULL, _IONBF, 0);

/* Print header */
        fprintf(stderr,
		"%s\nWARD %s -- Classic War Dialer with GSM enhancements\n"
		"Copyright (c) 2001 %s %s\n\n%s", BLUE, VERSION, AUTHOR, 
		MAIL_SUPPORT, NORMAL);

/* Parse command line */
	if (argc < 2) usage(argv[0]);

	{
	int c=0;

	while ((c = getopt(argc, argv, "rn:g:s:t:h")) != EOF)

              	switch (c) {
		case 'h':
			usage(argv[0]);
			break;	
              	case 'n':
			opt_line |= OPT_NMASK;
              		strncpy(nmask, optarg, MAX_NUM_LEN - 1);
			break;
		case 'r': 
			opt_line |= OPT_RAND;
			incr = 0;
			break;
		case 'g': 
			opt_line |= OPT_GENERATE;
			strncpy(nfile, optarg, 254);
			break;				
 		case 's':
			opt_line |= OPT_SCAN;
 	                strncpy(nfile, optarg, 254);
 	                break;
		case 't':
			opt_line |= OPT_TIMEOUT;
			timeout = atoi(optarg);
			break;
		}			
	}		

/* Input control */
	if (!(opt_line & OPT_GENERATE) && !(opt_line & OPT_SCAN))
		fatalerr("err: please select an action [generate or scan]");
	
	if ((opt_line & OPT_GENERATE) && (opt_line & OPT_SCAN))
		fatalerr("err: select only one action [generate or scan]");

	if (!*nfile)
		fatalerr("err: please specify a filename to use");


/* Signals handling */
        signal(SIGINT, cleanup);


/* List generation mode */
	if (opt_line & OPT_GENERATE) {

		if (!(opt_line & OPT_NMASK) || !*nmask)
			fatalerr("err: -n/--nmask <arg> is required with -g");

		listgen(nmask, incr, nfile);

/* Scan mode */
     	} else 
		scan(nfile);


	exit(0);
}	 



void scan(char *nfile) /* Scanner engine */
{
	FILE * f;
	char n[MAX_NUM_LEN], status[80], c[2];
	int size = 1, result;

	bzero(n, MAX_NUM_LEN);
	bzero(status, 10);
	c[1] = 0;

/* Open numbers file */
	if ((f = fopen(nfile, "r+")) == NULL)
		fatalerr("err: unable to open file %s", nfile);

/* Open and init modem device */
	if ((fd = initmodem(MODEM_DEV)) < 0)
		fatalerr("err: unable to open %s", MODEM_DEV);

	fprintf(stdout, "%s[*]%s Starting scan engine...\n", BRIGHT, NORMAL);
	fprintf(stdout, "%s[*]%s Resetting modem...\n", BRIGHT, NORMAL);

	sendcmd(fd, 2, "ATZ\r");

	fprintf(stdout, "%s[*]%s Done.                         \n\n", BRIGHT, NORMAL);
	fprintf(stdout, "%s[*]%s Starting scan.                        \n", BRIGHT, NORMAL);

/* Parse the numbers file */
	while (size) {

/* Read phone number */
		while (1) {
			size = fread(c, 1, 1, f);  

			if (c[0] == '\t' || !size)
				break;

			if (c[0] != '\n')
				strncat(n, c, 1);
		}					

/* Read phone number status */
		while (1) {
			size = fread(c, 1, 1, f);

			if (c[0] == '\n' || !size)
				break;

			strncat(status, c, 1);
		}


/* Dial the number, if not already scanned */
		if (!strcmp(status, "UNSCANNED")) {

			result = dial(n, MAX_RETRIES);

                	switch (result) {
			case 1:
				fseek(f, -10, SEEK_CUR);
				fwrite("CONNECT  \n", 10, 1, f);
				break;
			case 2:
				fseek(f, -10, SEEK_CUR);
                        	fwrite("BUSY     \n", 10, 1, f);
				break;
			case 3:
				fseek(f, -10, SEEK_CUR);
                        	fwrite("-        \n", 10, 1, f);
				break;
			}			
		}
				
        	bzero(n, MAX_NUM_LEN);
        	bzero(status, 10);
	}

/* Scan finished */
	fprintf(stdout, "%s[*]%s Scan finished.                      \n\n",
		BRIGHT, NORMAL);
	sendcmd(fd, 1, "+++ATH0\r");
	sendcmd(fd, 1, "ATZ\r");
	closemodem(fd);

	return;
}



int dial(char *number, int retry) /* Actually dial a phone number */
{

/* 
 * Return values:
 *
 * 1) CONNECT
 * 2) BUSY
 * 3) NO ANSWER
 * 4) EOF
 *
 */

	char buf[24];
	int i;

        bzero(buf, sizeof(buf));

	if (!strlen(number)) 
		return(4);

/* Modem hangup */
	fprintf(stdout, "Hanging up...             \r");

	if (!hupmodem(fd)) { /* FIXME: ugly hack for a better error handling */
		if (!hupmodem(fd))	
			fatalerr("err: %s not responding", MODEM_DEV);
	}

        fprintf(stdout, "Dialing: %s (%i)        \r", number, timeout);
	
/* Create dial command string */
	sendcmd(fd, 2, "ATM0DT%s\r", number); /* modem volume set to 0 */

	for (i = timeout; i > 0; i--) {

		fprintf(stdout, "Dialing: %s (%i)    \r", number, i);

/* Read modem output */
		if (read(fd, buf, 23)) {


			if (strstr(buf, "CONNECT") != NULL) {
				fprintf(stdout, "%sCONNECT: %s\n%s", 
					GREEN, number, NORMAL);

				sendcmd(fd, 2, "+++");

				return(1); /* CONNECT */
			}


			if (strstr(buf, "BUSY") != NULL) {
				fprintf(stdout, "%sBUSY:    %s\n%s", 
					YELLOW, number, NORMAL);

				return(2); /* BUSY */
			}

/* Speed hacks */
			if (strstr(buf, "NO") != NULL) {

				if (timeout - i < 3) { /* Line problems */

					/* Error */
					if (!retry)
						fatalerr("err: NO CARRIER. Line problem?");

					/* Retry */
					fprintf(stdout, "RETRY:   %s\n",number);
					return(dial(number, retry - 1));

				} else 

					return(3); /* NO ANSWER */
			}


			if (strstr(buf, "OK") != NULL)

				return(3); /* NO ANSWER */

		}
		sleep(1);
	}											
	return(3); /* NO ANSWER */
}	



void writefile(int last, int incr, char *nfile) /* Write list to file */
{
	int i;
	FILE * f;

	if ((f = fopen(nfile, "a")) == NULL)
		fatalerr("err: unable to open file %s", nfile);
	
	fprintf(stdout, "%s[*]%s Writing numbers to file...\n", BRIGHT, NORMAL);

/* Use incremental mode writing to file */
	if (incr)
		for (i = 0; i < last; i++) {

			if (!fwrite(numbers[i], 1, strlen(numbers[i]), f))
				fatalerr("err: unable to write to file %s", nfile);

			fwrite("\tUNSCANNED\n", 1, 11, f); /* mark as new */

                        fprintf(stdout, "[ ] %d numbers left       \r", last - i);
		}

/* Use random mode (pHEAR) */
	else {
		int j;
		struct timeval rnd;

		while (last) {

/* Strong pseudo-random numbers generator */
			gettimeofday(&rnd, NULL);
			srand(rnd.tv_usec);

/* Some voodoo magic */
	                j = 0 + (int)((last + 0.0) * rand()/(RAND_MAX + 1.0));
			
                        if(!fwrite(numbers[j], 1, strlen(numbers[j]), f))
				fatalerr("err: unable to write to file %s", nfile);

			fwrite("\tUNSCANNED\n", 1, 11, f); /* mark as new */

			strncpy(numbers[j], numbers[last -1], MAX_NUM_LEN - 1);
			last--;
			
                        fprintf(stdout, "%d numbers left       \r", last);
		}		  

	}				
	fprintf(stdout, "%s[*]%s Done.                 \n\n", BRIGHT, NORMAL);

	return;
}		

	

void listgen(char *nmask, int incr, char *nfile) /* List generator engine */
{
	int i, j, tot_numbers, nextx = 0, nextn = 0, xpos[MAX_NUM_LEN];
	char n[MAX_NUM_LEN]; 
		

/* Find the position of the 'x' chars and substitute with '0' */
	for (i = 0; i < strlen(nmask); i++) {

		if (nmask[i] == 'x') {
			xpos[nextx] = i;
			nmask[i] = '0';
			nextx++;
		}
	}
	
	if ((!nextx) || (nextx > 4))
		fatalerr("err: please specify 1 to 4 x's");

	tot_numbers = pow(10, nextx);
	numbers = (char **)malloc(tot_numbers * sizeof(char *));

/* Fill the numbers array with all possibilities */
	fprintf(stdout, "%s[*]%s Generating numbers list...\n", BRIGHT, NORMAL);

	for (i = 0; i < tot_numbers; i++) {

		snprintf(n, MAX_NUM_LEN, "%d", i);

/* Use POW operation properties in decimal notation */
	 	if (strlen(n) == nextx) {
	
	 		for (j = 0; j < nextx; j++) 
				nmask[xpos[j]] = n[j];
        
			numbers[nextn] = strdup((const char *)nmask);
                        nextn++;

		} else {

			for (j = 0 ; j < nextx - strlen(n); j++) 
				nmask[xpos[j]] = '0';

			for (j = nextx - strlen(n); j < nextx; j++)
				nmask[xpos[j]] = n[j + strlen(n) - nextx];

			numbers[nextn] = strdup((const char *)nmask);
                        nextn++;
		}

	}						
		
/* Write to file and free() the memory */
	writefile(nextn, incr, nfile);		

	for (i = 0; i < nextn; i++)
       		free(numbers[nextn]);

	return;
}	



int initmodem(char *device) /* Open modem device and init serial port */
{
	int flags;

        if( (fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0 )
                return(fd);

/* Save old settings */	
        tcgetattr(fd, &oldtio);

/* Set up the new struct and init serial port */
        bzero(&newtio, sizeof(newtio));

        newtio.c_cflag          = MODEM_SPEED | CS8 | CLOCAL | CREAD;
        newtio.c_iflag          = IGNPAR;
        newtio.c_oflag          = 0;
        newtio.c_lflag          = 0;
        newtio.c_cc[VTIME]      = 0;
        newtio.c_cc[VMIN]       = 0;

        tcflush(fd, TCIFLUSH);
        tcsetattr(fd, TCSANOW, &newtio);

/* We no longer want to have the serial port non-blocking */
        flags = fcntl(fd, F_GETFL);
        if (flags == -1)
                 fatalerr("err: failed to get serial tty flags"); 
        flags &= ~O_NONBLOCK;
        if (fcntl(fd, F_SETFL, flags) == -1)
                 fatalerr("err: failed to set serial port ~O_NONBLOCK");

        return(fd);
}



void closemodem(int fd) /* Close modem device */
{

/* Set old attributes for the serial port */
        tcsetattr(fd, TCSANOW, &oldtio);

        close(fd);
}


int hupmodem(int fd) /* Hangup line and check modem response */
{
	char buf[24];

	bzero(buf, sizeof(buf));

	sendcmd(fd, 2, "ATH0\r");
	read(fd, buf, 23);

	if (strstr(buf, "OK") == NULL)
		return(0); /* ERROR: modem is not responding */

	return(1); /* OK: modem connected */
}



void sendcmd(int fd, int timewait, char * fmt,...) /* Send command to modem */
{
	char strbuf[128];
        va_list ap;

        bzero(strbuf, sizeof(strbuf));

        /* Flushes I/O */
        tcflush(fd, TCIOFLUSH);

        /* Transmits the command */
        va_start(ap, fmt);

        vsnprintf(strbuf, sizeof(strbuf) - 1, fmt, ap);
        write(fd, strbuf, strlen(strbuf));
        va_end(ap);

        /* Waits for whatever character has been transmitted */
        tcdrain(fd);
        sleep(timewait);

        return;
}



void cleanup(int ignored) /* SIGINT handler, close modem */
{
	if (fd) {
		sendcmd(fd, 1, "+++ATH0\r");
		sendcmd(fd, 1, "ATZ\r");
		closemodem(fd);
	}

	fprintf(stderr, "\n");
	fatalerr("err: program interrupted... cleanup done");	
}



void fatalerr(char * pattern,...) /* Error handling routine */
{
        va_list ap;
        va_start(ap, pattern);

        fprintf(stderr,"%sward-", RED);
        vfprintf(stderr,pattern,ap);
        fprintf(stderr," (exit forced).\n\n%s", NORMAL);

        va_end(ap);

        exit(-1);
}



void usage(char * name) /* Print usage */
{
        fprintf (stderr, 
		"%susage%s:\n"
		"\t%s -g <file> -n <nmask> [-r]\t(generation mode)\n"
        	"\t%s -s <file> [-t <timeout>] \t(scan mode)\n\n", 
		BRIGHT, NORMAL, name, name);

        fprintf (stderr,
		"%sgeneration mode%s:\n"
                "\t-g  generate number list and save it to file\n"
                "\t-n  number mask to be used in generation mode\n"
                "\t-r  toggle random mode ON\n\n"
		"%sscan mode%s:\n"
                "\t-s  scan a list of phone numbers from file\n"
                "\t-t  set the modem timeout (default=%dsecs)\n\n"
		"%shelp%s:\n"
                "\t-h  print this help\n\n", BRIGHT, NORMAL, BRIGHT, NORMAL, 
		timeout, BRIGHT, NORMAL);

        exit (0);
}
