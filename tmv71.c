/*
 * Copyright (c) 2014
 *      Tamotsu Kanoh <kanoh@kanoh.org>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither my name nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/wait.h>

#ifndef TTY_DEV
#define	TTY_DEV		"/dev/ttyU1"
#endif

#ifndef B_TMV71
#define B_TMV71		B9600
#endif

#define TIMEOUT		2

#define HZ		1.0
#define KHZ		1000.0
#define MHZ		1000000.0

#define	DEF_UNIT	KHZ
#define	UNIT_STR	"KHz"

struct freq_reng_t {
	int min;
	int max;
	int tx;
};

static struct freq_reng_t freq_reng[2][6] = {
	{
	118000, 144000, 0,
	144000, 146000, 1,
	146000, 430000, 0,
	430000, 440000, 1,
	440000, 524000, 0,
	440000, 524000, 0
	},{
	136000, 144000, 0,
	144000, 146000, 1, 
	146000, 430000, 0,
	430000, 440000, 1,
	440000, 524000, 0,
	800000,1300000, 0
	}
};

static char *md_str[] = {"FM","NFM","AM","ZZZ"};

static jmp_buf env;

char msg[BUFSIZ];

void usages(progname)
char *progname;
{
	printf("usages: %s [-s] [-f freq] [-m mode] [-l tty_dev]\n",progname);
	exit(0);
}

void p_error(progname,msg,p)
char *progname, *msg;
int p;
{
	char buf[BUFSIZ];
	if(p) {
		snprintf(buf,sizeof(char)*BUFSIZ,"%s (error): %s ",progname,msg);
		perror(buf);
	}
	else
		fprintf(stderr,"%s (error): %s\n",progname,msg);
	exit(1);
}

int str2freq(str)
char *str;
{
	char *buf, *p;
	float unit, freq;

	buf=(char *)malloc(sizeof(char)*BUFSIZ);
	p=buf;

	while(*str != 0x0 && 
		((*str > 0x2f && *str < 0x3a) || *str == '.' || *str == ',')) {
		if(*str != ',') *p++ = *str;
		str++;
	}
	*p = 0x0;
	
	unit = DEF_UNIT;
	if(*str == 'H' || *str == 'h') unit = HZ;
	else if(*str == 'K' || *str == 'k') unit = KHZ;
	else if(*str == 'M' || *str == 'm') unit = MHZ;

	sscanf(buf,"%f",&freq);
	freq *= unit;
	free(buf);
	return((int)freq);
}

int str2mode(str)
char *str;
{
	int i;
	char *buf, *p;

	buf=(char *)malloc(sizeof(char)*BUFSIZ);
	p=buf;

	while(*str != 0x0 && *str != '\n') {
		*p++ = toupper(*str);
		str++;
	}
	*p = 0x0;

	i = 0;
	while(*md_str[i] != 'Z') {
		if(strncmp(md_str[i],buf,strlen(md_str[i])) == 0) return(i);
		i++;
	}
	return(-1);
}

void wait_do_cmd(fd)
int fd;
{
	unsigned char c;

	write(fd,"\r",1);

	do {
		read(fd,(char *)&c,1);
	} while(c != '?');

	return;
}

char *cmd_write(fd,buf)
int fd;
char *buf;
{
	char *p;

	//printf("cmd_write(): %s\n",buf);
	write(fd,buf,strlen(buf));
	//printf("cmd_write(): call wait_do_cmd()\n");
	wait_do_cmd(fd);
	//printf("cmd_write(): exit wait_do_cmd()\n");
	return(p);
}

void static system_timeout(sig)
int sig;
{
	siglongjmp(env, 1);
}

int main(argc, argv)
int argc;
char **argv;
{
	int i, fd, freq, mode, sub, quiet, main_mode, sub_mode;
	int status, exit_code;
	char buf[BUFSIZ],*p,*tty_dev,c;
	double main_freq, sub_freq;
	static pid_t pid;
	struct termios term, term_def;
	extern char *optarg;

	freq = 0; mode = 0; sub = 0; quiet = 0; tty_dev = TTY_DEV;
	while ((i = getopt(argc, argv, "f:l:m:sqh?")) != -1) {
		switch (i) {
			case 'f':
				freq = str2freq(optarg);
				break;
			case 'l':
				tty_dev = optarg;
				break;
			case 'm':
				mode = str2mode(optarg);
				if(mode < 0)
					p_error(argv[0],"illgale mode",0);
				break;
			case 's':
				sub = 1;
				break;
			case 'h':
			case '?':
			default:
				usages(argv[0]);
		}
	}

	if(sigsetjmp(env, 1)) {
		alarm(0);
		signal(SIGALRM, SIG_DFL);
		if(pid > 0)
			kill(pid, SIGKILL);
		p_error(argv[0],"CAT access timeout",0);
	}

	pid=fork();
	switch(pid) {
		case -1:
			p_error(argv[0],buf,1);
			break;
		case 0:
			if((fd = open(tty_dev, O_RDWR | O_NONBLOCK)) < 0)
				p_error(argv[0],tty_dev,1);

			tcgetattr(fd,&term_def);

			term.c_iflag = IGNBRK | IGNPAR;
			term.c_oflag = 0;
			term.c_cflag = CS8 | CREAD | CLOCAL | HUPCL | MDMBUF;
			term.c_lflag = 0;
			term.c_cc[VMIN] = 1;
			term.c_cc[VTIME] = 0;

			cfsetispeed(&term,B_TMV71);
			cfsetospeed(&term,B_TMV71);

			tcsetattr(fd, TCSANOW, &term);
			tcflush(fd,TCIOFLUSH);

			if(freq) {
				snprintf(buf,sizeof(char)*BUFSIZ,
					"FO %d,%10.10d,0,0,0,0,0,0,08,08,000,05000000,%d\r",sub,freq,mode);
				cmd_write(fd,buf);
			} 

			tcsetattr(fd, TCSANOW, &term_def);
			close(fd);
			_exit(0);
			break;
		default:
			alarm(TIMEOUT);
			signal(SIGALRM, system_timeout);
			pid = waitpid(pid, &status, WUNTRACED);
			exit_code = WEXITSTATUS(status);
	}
	alarm(0);
	signal(SIGALRM, SIG_DFL);
	exit(exit_code);
}
