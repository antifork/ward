/*
 * $Id$
 *
 * ward v1.0 - classic war dialer
 * Copyright (c) 2001 Raptor <raptor@0xdeadbeef.eu.org>
 *
 * WARD is a classic war dialer: it scans a list
 * of phone numbers, finding the ones where a modem
 * is answering the call. Wargames still r0cks.
 * WARD can generate phone numbers lists based on
 * a user-supplied mask, in incremental or random
 * order. Remember to change some defines to make
 * it fit your current system configuration.
 *
 * Inspired to the old sordial.c (thanx sorbo!).
 * Thanx also to Megat0n for his termios advice.
 *
 * Tested on Linux. Compile with gcc ward.c -o ward -lm.
 * OpenBSD still have some problems with non-gsm modems.
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

#define VERSION         "1.0"
#define AUTHOR          "Raptor"
#define MAIL_SUPPORT    "<raptor@0xdeadbeef.eu.org>"

#define OPT_NMASK      	0x01
#define OPT_RAND       	0x02
#define OPT_GENERATE   	0x04
#define OPT_SCAN	0x08
#define OPT_TIMEOUT   	0x10

/* Local setup - change if needed */
#define MODEM_DEV 	"/dev/modem"
#define MODEM_SPEED 	B9600
#define MODEM_TIMEOUT 	60
#define MAX_NUM_LEN 	15
/* Local setup end */


/* Function prototypes */
void scan(char *nfile);
int dial(char *number);
void writefile(int last, int incr, char *nfile);
void listgen(char *nmask, int incr, char *nfile);
int initmodem(char * device);
void closemodem(int fd);
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
	if (!(opt_line&OPT_GENERATE) && !(opt_line&OPT_SCAN))
		fatalerr("err: please select an action [generate or scan]");
	
	if ((opt_line&OPT_GENERATE) && (opt_line&OPT_SCAN))
		fatalerr("err: select only one action [generate or scan]");

	if (!*nfile)
		fatalerr("err: please specify a filename to use");


/* Signals handling */
        signal(SIGINT, cleanup);


/* List generation mode */
	if (opt_line&OPT_GENERATE) {

		if (!(opt_line&OPT_NMASK) || !*nmask)
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
	char n[MAX_NUM_LEN], status[10], c[2];
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

        fprintf(stderr,"WARD %s: classic war dialer\nCopyright (c) 2001 %s %s\n\n",VERSION,AUTHOR,MAIL_SUPPORT);
	fprintf(stdout, "Starting scan engine...\n");
	fprintf(stdout, "Resetting modem...\n");

        sendcmd(fd, 2, "ATZ\r");

	fprintf(stdout, "Done.                         \n");

                    
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

			result = dial(n);

                	switch (result) {
			case 1:
				fseek(f, -10, SEEK_CUR);
				fwrite("CONNECT  ", 9, 1, f);
				break;
			case 2:
				fseek(f, -10, SEEK_CUR);
                        	fwrite("BUSY     ", 9, 1, f);
				break;
			case 3:
				fseek(f, -10, SEEK_CUR);
                        	fwrite("TIMEOUT  ", 9, 1, f);
				break;
			}			
		}
				
        	bzero(n, MAX_NUM_LEN);
        	bzero(status, 10);
	}

/* Scan finished */
	fprintf(stdout, "Scan terminated.                    \n");
	sendcmd(fd, 2, "+++ATH0\r");
	sendcmd(fd, 2, "ATZ\r");
	closemodem(fd);

	return;
}



int dial(char *number) /* Actually dial a phone number */
{

/* 
 * Return values:
 *
 * 1) CONNECT
 * 2) BUSY
 * 3) Timeout
 * 4) EOF
 *
 */

	char buf[24];
	int i, rd;

        bzero(buf, sizeof(buf));

	if (!strlen(number)) 
		return(4);

/* Modem hangup */
	fprintf(stdout, "Hanging up...             \r");
	sendcmd(fd, 2, "ATH0\r");

        fprintf(stdout, "Dialing: %s (%i)        \r", number, timeout);
	
/* Create dial command string */
	sendcmd(fd, 2, "ATM0DT%s\r", number); /* modem volume set to 0 */

	for (i = timeout; i > 0; i--) {

		fprintf(stdout, "Dialing: %s (%i)    \r",number,i);

		if ((rd = read(fd, buf, 23))) {

			if (strstr(buf, "CONNECT") != NULL) {
				fprintf(stdout, "CONNECT: %s		 \n", number);

				sendcmd(fd, 2, "+++");

				return(1); /* CONNECT */
			}

			if (strstr(buf, "BUSY") != NULL) 
				return(2); /* BUSY */

		}
		sleep(1);
	}											
	return(3); /* Timeout */
}	



void writefile(int last, int incr, char *nfile) /* Write list to file */
{
	int i;
	FILE * f;

	if ((f = fopen(nfile, "a")) == NULL)
		fatalerr("err: unable to open file %s", nfile);
	
	fprintf(stdout, "Writing numbers to file...\n");

/* Use incremental mode writing to file */
	if (incr)
		for (i = 0; i < last; i++) {

			if (!fwrite(numbers[i], 1, strlen(numbers[i]), f))
				fatalerr("err: unable to write to file %s", nfile);

			fwrite("\tUNSCANNED\n", 1, 11, f); /* mark as new */

                        fprintf(stdout, "%d numbers left       \r", last - i);
		}

/* Use random mode (pHEAR) */
	else {
		int j;
		struct timeval rnd;

		while(last) {

/* Strong pseudo-random numbers generator */
			gettimeofday(&rnd, NULL);
			srand(rnd.tv_usec);

/* Some voodoo magic */
	                j= 0 + (int)((last + 0.0) * rand()/(RAND_MAX + 1.0));
			
                        if(!fwrite(numbers[j], 1, strlen(numbers[j]), f))
				fatalerr("err: unable to write to file %s", nfile);

			fwrite("\tUNSCANNED\n", 1, 11, f); /* mark as new */

			strncpy(numbers[j], numbers[last -1], MAX_NUM_LEN - 1);
			last--;
			
                        fprintf(stdout, "%d numbers left       \r", last);
		}		  

	}				
	fprintf(stdout, "Done.                 \n");

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
	
	if (!nextx) 
		fatalerr("err: only 1 possible number, dial it by hand");

	tot_numbers = pow(10, nextx);
	numbers = (char **)malloc(tot_numbers * sizeof(char *));

/* Fill the numbers array with all possibilities */
        fprintf(stderr,"WARD %s: classic war dialer\nCopyright (c) 2001 %s %s\n\n",VERSION,AUTHOR,MAIL_SUPPORT);
	fprintf(stdout, "Generating number list...\n");

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
		sendcmd(fd, 2, "+++ATH0\r");
		sendcmd(fd, 2, "ATZ\r");
		closemodem(fd);
	}

	fprintf(stderr, "\n");
	fatalerr("err: program interrupted... cleanup done");	
}



void fatalerr(char * pattern,...) /* Error handling routine */
{
        va_list ap;
        va_start(ap, pattern);

        fprintf(stderr,"ward-");
        vfprintf(stderr,pattern,ap);
        fprintf(stderr," (exit forced).\n");

        va_end(ap);

        exit(-1);
}



void usage(char * name) /* Print usage */
{
        fprintf(stderr,"WARD %s: classic war dialer\nCopyright (c) 2001 %s %s\n\n",VERSION,AUTHOR,MAIL_SUPPORT);
        fprintf (stderr, 
		"usage: %s -g <file> -n <nmask> [-r]\t(generation mode)\n"
        	"       %s -s <file> [-t <timeout>]\t\t(scan mode)\n\n", name, name);

        fprintf (stderr,
		"In generation mode:\n"
                "     -g  generate number list and save it to file\n"
                "     -n  number mask to be used in generation mode\n"
                "     -r  toggle random mode ON\n\n"
		"In scan mode:\n"
                "     -s  scan a list of phone numbers from file\n"
                "     -t  set the modem timeout (default=%dsecs)\n\n"
                "     -h  print this help\n", timeout);

        exit (0);
}
