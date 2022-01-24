/*
 * This version of kittycat.c is derived from cat.c rather than starting from scratch.
 * Source for cat.c pulled from:
 *  https://opensource.apple.com/source/text_cmds/text_cmds-87/cat/cat.c
 * on 2022-01-16
 *
 * MIT License
 * 
 * Copyright (c) 2022 Brad Werner
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Fall.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#if 0
#ifndef lint
static char const copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)cat.c	8.2 (Berkeley) 4/27/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/bin/cat/cat.c,v 1.32 2005/01/10 08:39:20 imp Exp $");

#include <sys/param.h>
#include <sys/stat.h>
#ifndef NO_UDOM_SUPPORT
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#endif

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <signal.h>
#include <time.h>

int bflag, eflag, nflag, sflag, tflag, vflag;
int kflag; // kittycat (kc) extensions
int rval;
const char *filename;
const char *kitty;       // path to kitty marker (PID file) that facilitates signalling us
sigset_t kitty_catnip_invulnerabilities; // catnip (or other origin) signals to ignore
struct timespec kitty_catnap_request;    // set .tv_sec or .tv_nsec to requested nap time
struct timespec kitty_catnap_remainder;  // side effect of nanosleep() for premature wake

static void usage(void);
static void scanfiles(char *argv[], int cooked);
static void cook_cat(FILE *);
static void raw_cat(int);
static void create_kitty_marker();
static void parse_timespec( char* wait_time, struct timespec* result );
static void wait_for_catnip();

#ifndef NO_UDOM_SUPPORT
static int udom_open(const char *path, int flags);
#endif

int
main(int argc, char *argv[])
{
	int ch;

	setlocale(LC_CTYPE, "");
	kitty = ".kc"; // warning: default does not support concurrency in shared file namespace
	kitty_catnip_invulnerabilities = 0;
	kitty_catnap_request.tv_sec = 0;
	kitty_catnap_request.tv_nsec = 250000000; // default to quarter second

	create_kitty_marker(); // kittycat extension for backwash signalling along pipeline
	while ((ch = getopt(argc, argv, "benstuvk:w:")) != -1)
		switch (ch) {
		case 'b':
			bflag = nflag = 1;	/* -b implies -n */
			break;
		case 'e':
			eflag = vflag = 1;	/* -e implies -v */
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = vflag = 1;	/* -t implies -v */
			break;
		case 'u':
			setbuf(stdout, NULL);
			break;
		case 'v':
			vflag = 1;
			break;
		case 'k': 			/* kitty rendezvous path */
			++kflag;		/* why? for debugging. it's not like it changes behavior */
			kitty = optarg;
			break;
		case 'w':			/* kitty catnap wait time */
			parse_timespec( optarg, &kitty_catnap_request );
			break;
		default:
			usage();
		}
	argv += optind;

	if (bflag || eflag || nflag || sflag || tflag || vflag)
		scanfiles(argv, 1);
	else
		scanfiles(argv, 0);
	if (fclose(stdout))
		err(1, "stdout");
	exit(rval);
	/* NOTREACHED */
}

static void
usage(void)
{
	fprintf(stderr, "usage: kc [-benstuv] [-k  kitty_rendezvous_file] [-w kitty_catnap_wait_time] [file ...]\n");
	exit(1);
	/* NOTREACHED */
}

static void
scanfiles(char *argv[], int cooked)
{
	int i = 0;
	char *path;
	FILE *fp;

	while ((path = argv[i]) != NULL || i == 0) {
		int fd;

		wait_for_catnip(); // kittycat extension
		if (path == NULL || strcmp(path, "-") == 0) {
			filename = "stdin";
			fd = STDIN_FILENO;
		} else {
			filename = path;
			fd = open(path, O_RDONLY);
#ifndef NO_UDOM_SUPPORT
			if (fd < 0 && errno == EOPNOTSUPP)
				fd = udom_open(path, O_RDONLY);
#endif
		}
		if (fd < 0) {
			warn("%s", path);
			rval = 1;
		} else if (cooked) {
			if (fd == STDIN_FILENO)
				cook_cat(stdin);
			else {
				fp = fdopen(fd, "r");
				cook_cat(fp);
				fclose(fp);
			}
		} else {
			raw_cat(fd);
			if (fd != STDIN_FILENO)
				close(fd);
		}
		if (path == NULL)
			break;
		++i;
	}
}

static void
cook_cat(FILE *fp)
{
	int ch, gobble, line, prev;

	/* Reset EOF condition on stdin. */
	if (fp == stdin && feof(stdin))
		clearerr(stdin);

	line = gobble = 0;
	for (prev = '\n'; (ch = getc(fp)) != EOF; prev = ch) {
		if (prev == '\n') {
			if (sflag) {
				if (ch == '\n') {
					if (gobble)
						continue;
					gobble = 1;
				} else
					gobble = 0;
			}
			if (nflag && (!bflag || ch != '\n')) {
				(void)fprintf(stdout, "%6d\t", ++line);
				if (ferror(stdout))
					break;
			}
		}
		if (ch == '\n') {
			if (eflag && putchar('$') == EOF)
				break;
		} else if (ch == '\t') {
			if (tflag) {
				if (putchar('^') == EOF || putchar('I') == EOF)
					break;
				continue;
			}
		} else if (vflag) {
			if (!isascii(ch) && !isprint(ch)) {
				if (putchar('M') == EOF || putchar('-') == EOF)
					break;
				ch = toascii(ch);
			}
			if (iscntrl(ch)) {
				if (putchar('^') == EOF ||
				    putchar(ch == '\177' ? '?' :
				    ch | 0100) == EOF)
					break;
				continue;
			}
		}
		if (putchar(ch) == EOF)
			break;
	}
	if (ferror(fp)) {
		warn("%s", filename);
		rval = 1;
		clearerr(fp);
	}
	if (ferror(stdout))
		err(1, "stdout");
}

static void
raw_cat(int rfd)
{
	int off, wfd;
	ssize_t nr, nw;
	static size_t bsize;
	static char *buf = NULL;
	struct stat sbuf;

	wfd = fileno(stdout);
	if (buf == NULL) {
		if (fstat(wfd, &sbuf))
			err(1, "%s", filename);
		bsize = MAX(sbuf.st_blksize, 1024);
		if ((buf = malloc(bsize)) == NULL)
			err(1, "buffer");
	}
	while ((nr = read(rfd, buf, bsize)) > 0)
		for (off = 0; nr; nr -= nw, off += nw)
			if ((nw = write(wfd, buf + off, (size_t)nr)) < 0)
				err(1, "stdout");
	if (nr < 0) {
		warn("%s", filename);
		rval = 1;
	}
}

#ifndef NO_UDOM_SUPPORT

static int
udom_open(const char *path, int flags)
{
	struct sockaddr_un sou;
	int fd;
	unsigned int len;

	bzero(&sou, sizeof(sou));

	/*
	 * Construct the unix domain socket address and attempt to connect
	 */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd >= 0) {
		sou.sun_family = AF_UNIX;
		if ((len = strlcpy(sou.sun_path, path,
		    sizeof(sou.sun_path))) >= sizeof(sou.sun_path)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		len = offsetof(struct sockaddr_un, sun_path[len+1]);

		if (connect(fd, (void *)&sou, len) < 0) {
			close(fd);
			fd = -1;
		}
	}

	/*
	 * handle the open flags by shutting down appropriate directions
	 */
	if (fd >= 0) {
		switch(flags & O_ACCMODE) {
		case O_RDONLY:
			if (shutdown(fd, SHUT_WR) == -1)
				warn(NULL);
			break;
		case O_WRONLY:
			if (shutdown(fd, SHUT_RD) == -1)
				warn(NULL);
			break;
		default:
			break;
		}
	}
	return(fd);
}

#endif

/*
 * create_kitty_marker writes our PID to a file so kill(1), cn (1) (a.k.a. catnip), or others
 * can signal us when they have actually posted content to the files we are about to cat.
 * such signalling provides a more reliable mechanism than the assumption of those resources
 * being available after some amount of time.
 *
 * should be passed context, but in keeping with the cat(1) we're incorporating into,
 * we instead use dependencies (ick) on global variables (joy).
 * 	const char *kitty;       // path to kitty marker (PID file) that facilitates signalling us
 *
 * it is the caller's responsibility to provide a reasonable value for kitty, such as ".kc".
 */
static void 
create_kitty_marker()
{
	if( kitty ){ // else should warn of reduced functionality...
		FILE *fp;
		int fd;
		fd = open(kitty, O_WRONLY|O_CREAT|O_TRUNC, 0600); // allow cn et al to query and future us to overwrite
		if (fd < 0) {
			warn("%s", kitty);
		}else{
			dprintf( fd, "%d\n", getpid() );
			close( fd );
		}
	}
}

static void 
parse_timespec( char* wait_time, struct timespec* result )
{
	double seconds;
	sscanf( wait_time, "%lg", &seconds );
	result->tv_sec = (int)seconds;
	result->tv_nsec = (int)( ( seconds - (double)result->tv_sec ) / 1e-9 );
}

/*
 * wait_for_catnip can wait for either a signal or a time period.
 * this could be used with the catnip(1) command or any other kill(2) or signal initiator,
 * or just time delay.
 * implement with sigsuspend() or nanosleep() according to particular criteria needed.
 * 
 * should be passed context, but in keeping with the cat(1) we're incorporating into,
 * we instead use dependencies (ick) on global variables (joy).
 *
 * 	sigset_t kitty_catnip_invulnerabilities; // catnip (or other origin) signals to ignore
 * 	struct timespec kitty_catnap_request;    // set .tv_sec or .tv_nsec to requested nap time
 * 	struct timespec kitty_catnap_remainder;  // side effect of nanosleep() for premature wake
 */
static void 
wait_for_catnip()
{
	nanosleep( &kitty_catnap_request, &kitty_catnap_remainder );
}


