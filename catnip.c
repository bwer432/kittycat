/*
 * This version of catnip.c is derived from kill.c rather than starting from scratch.
 * Source for kill.c pulled from:
 *  https://opensource.apple.com/source/shell_cmds/shell_cmds-187/kill/kill.c
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

/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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

#ifndef lint
static char const copyright[] =
"@(#) Copyright (c) 1988, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)kill.c	8.4 (Berkeley) 4/28/95";
#endif
static const char rcsid[] =
  "$FreeBSD: src/bin/kill/kill.c,v 1.11.2.1 2001/08/01 02:42:56 obrien Exp $";
#endif /* not lint */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>	// catnip for open, read, write, close, etc.
#include <unistd.h>	// catnip for open, read, write, close, etc.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main __P((int, char *[]));
void nosig __P((char *));
int signame_to_signum __P((char *));
void usage __P((void));
static pid_t read_kitty_marker();
static void parse_request(int);

int http_trace( char* message );
int http_head( char* message );
int http_get( char* message );
int http_post( char* message );
int http_patch( char* message );
int http_put( char* message );
int http_options( char* message );
int http_delete( char* message );
int http_connect( char* message );

struct method_action {
	char* method;
	int (*action)( char* message );
};

struct method_action http_methods[] = {
	{ "TRACE",	http_trace },
	{ "HEAD",  	http_head },
	{ "GET",   	http_get }, 
	{ "POST",  	http_post }, 
	{ "PATCH", 	http_patch },
	{ "PUT", 	http_put },
	{ "OPTIONS", 	http_options },
	{ "DELETE", 	http_delete },
	{ "CONNECT", 	http_connect },
	{ NULL,		0 }
};

enum http_version { // singular
	HTTP_1_0,
	HTTP_1_1,
	HTTP_2_0,
	HTTP_VERSION_UNKNOWN
};

struct version_map {
	char* version;
	enum http_version http_version;
};

struct version_map http_versions[] = { // plural
	{ "HTTP/1.0", HTTP_1_0 },
	{ "HTTP/1.1", HTTP_1_1 },
	{ "HTTP/2.0", HTTP_2_0 },
	{ NULL, HTTP_VERSION_UNKNOWN }
};

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int errors, numsig, pid; // pid of kc (kittycat) or other process to signal
	char *ep;
	char *kitty; // kc (kittycat) signal file
	char *head;  // http response header
	char *body;  // body: html document, image, etc.

	if (argc < 1)
		usage();

	numsig = SIGCONT;	// catnip defaults to SIGCONT unlike kill's default SIGTERM;
	kitty = ".kc"; // warning: default does not support concurrency in shared file namespace
	pid = read_kitty_marker( kitty );
	head = "response.http"; // default response header output file path
	body = "body";		// default body output file path

	argc--, argv++;
	// removed support for -l option; use kill(1) with -l to list signals

	if (!strcmp(*argv, "-s")) {
		argc--, argv++;
		if (argc < 1) {
			warnx("option requires an argument -- s");
			usage();
		}
		if (strcmp(*argv, "0")) {
			if ((numsig = signame_to_signum(*argv)) < 0)
				nosig(*argv);
		} else
			numsig = 0;
		argc--, argv++;
	} else if (!strcmp(*argv, "-k")) { /* kitty rendezvous path */
		argc--, argv++;
		if (argc < 1) {
			warnx("option requires an argument -- k");
			usage();
		}
		kitty = *argv; // assign kittycat PID file path
		argc--, argv++;
	} else if (**argv == '-') {
		++*argv;
		if (isalpha(**argv)) {
			if ((numsig = signame_to_signum(*argv)) < 0)
				nosig(*argv);
		} else if (isdigit(**argv)) {
			numsig = strtol(*argv, &ep, 10);
			if (!**argv || *ep)
				errx(1, "illegal signal number: %s", *argv);
			if (numsig < 0 || numsig >= NSIG)
				nosig(*argv);
		} else
			nosig(*argv);
		argc--, argv++;
	}

	if (argc == 0)
		usage();

	parse_request( STDIN_FILENO );

	for (errors = 0; argc; argc--, argv++) {
		pid = strtol(*argv, &ep, 10);
		if (!**argv || *ep) {
			warnx("illegal process id: %s", *argv);
			errors = 1;
		} else if (kill(pid, numsig) == -1) {
			warn("%s", *argv);
			errors = 1;
		}
	}

	exit(errors);
}

int
signame_to_signum(sig)
	char *sig;
{
	int n;

	if (!strncasecmp(sig, "sig", (size_t)3))
		sig += 3;
	for (n = 1; n < NSIG; n++) {
		if (!strcasecmp(sys_signame[n], sig))
			return (n);
	}
	return (-1);
}

void
nosig(name)
	char *name;
{

	warnx("unknown signal %s.", name);
	exit(1);
}

// removed void printsignals( FILE* fp ) from cn(1); use the one in kill(1)

void
usage()
{

	(void)fprintf(stderr, "%s\n%s\n%s\n%s\n",
		"usage: cn [-k kitty_cat_file] [{-s signal_name | -signal_name | -signal_number}] [head [body]]");
	exit(1);
}


/*
 * read_kitty_marker reads a PID from a file we can signal the upstream command (e.g. kittycat)
 * that have actually posted content to the files we are about to cat.
 *
 * it is the caller's responsibility to provide a reasonable value for kitty, such as ".kc".
 */
static int 
read_kitty_marker( char* kitty )
{
	pid_t kitty_pid = 0;
	if( kitty ){ // else should warn of reduced functionality...
		FILE *fp;
		fp = fopen(kitty, "r"); // query value written by kc (kittycat)
		if (fp == 0) {
			warn("%s", kitty);
		}else{
			int status = fscanf( fp, "%d", &kitty_pid );
			fclose( fp );
		}
	}
	return kitty_pid;
}


static void
parse_request(int rfd)
{
	int off;
	ssize_t nr, np, nw;
	static size_t bsize = 4096; // assumed header line maximum
	static char *buf = NULL;
	char* p;
	char c;
	enum parse_state {
		WANT_METHOD,
		WANT_TARGET,
		WANT_VERSION,
		WANT_HEADER_KEY,
		WANT_HEADER_VALUE,
		WANT_BODY,
		ERROR_STATE
	} state;

	// pointers to request components should be in a struct
	char*	method;
	char*	target;
	char*	version;
	char*	hk;
	char*	hv;
	char*	server;
	char*	port;
	char*	body;
	struct method_action* map; // method-action-pointer = map
	struct version_map* vp; 
	int	e; // error code

	if ((buf = malloc(bsize)) == NULL)
		err(1, "buffer");
	for( p = buf; (nr = read(rfd, buf, bsize)) > 0; ){
		fprintf( stderr, "read %ld bytes\n", nr );
		// NOT sscanf( p, "%s %s %s\n", &method, &target, &version );
		state = WANT_METHOD; // that's how the request begins
		method = p; // assumption for entering WANT_METHOD
		target = NULL;
		version = NULL;
		hk = NULL;
		hv = NULL;
		server = NULL;
		port = NULL;
		body = NULL;
		map = NULL; // method-action pointer
		vp = NULL; // version-map pointer
		e = 0; // presumed innocent
		for( np = 0; np < nr && ( c = *p ) != '\0' && !e && state != WANT_BODY; ++p, ++np ){
			switch( c ){
			case ' ':
				switch( state ){
				case WANT_METHOD:
					*p = '\0'; // terminate the method name
					state = WANT_TARGET;
					target = p+1; // assumption when entering WANT_TARGET
					// check method at this point
					for( map = http_methods; map && map->method != NULL; ++map ){
						if( strcmp( method, map->method ) == 0 ){
							// we have a winner, retain map value
							break;
						}
					}
					if( map->method == NULL ){
						// no match
						map = NULL;
						e = 400; // bad request: method
					}
					break;
				case WANT_TARGET:
					*p = '\0'; // terminate the target (URL or short path)
					state = WANT_VERSION;
					version = p+1; // assumption when entering WANT_VERSION
					break;
				case WANT_VERSION:
					// not expecting spaces within the HTTP version
					// should flag an error
					e = 505; // unsupported version, or 400 bad request
					break;
				case WANT_HEADER_KEY:
					*p = '\0'; // this is *after* the colon hopefully
					state = WANT_HEADER_VALUE;
					hv = p+1; // set up to accumulate value next
					break;
				case WANT_HEADER_VALUE:
					// let them accumulate within the value
					if( p == hv ){ // except
						*p = '\0'; // eat the leading space
						hv = p+1; // as it is ignored
					}
					break;
				case WANT_BODY:
					// whatever - we should have stopped by now
					break;
				case ERROR_STATE:	
					// should be unreachable
					break;
				}
				break;
			case '\n':
				switch( state ){
				case WANT_VERSION:
					*p = '\0'; // terminate the version 
					state = WANT_HEADER_KEY;
					hk = p+1; // assuming a header-key comes next
					// check version at this point
					for( vp = http_versions; vp && vp->version != NULL; ++vp ){
						if( strcmp( version, vp->version ) == 0 ){
							// we have a winner, retain vp value
							break;
						}
					}
					if( vp->version == NULL ){
						// no match
						vp = NULL;
						e = 505; // unsupported version
					}
					break;
				case WANT_HEADER_VALUE:
					*p = '\0'; // terminate the header value
					state = WANT_HEADER_KEY;
					// preprocess this header now - must save (hk,hv)
					fprintf( stderr, "catnip: should process header: (%s,%s)\n", hk, hv );
					state = WANT_HEADER_KEY; // get set for the next one
					hk = p+1; // assuming a header-key comes next
					hv = NULL;
					break;
				case WANT_HEADER_KEY:
					// if at very beginning, is beginning of body
					if( p == hk ){
						state = WANT_BODY;
						body = p+1;
					}
					else
						e = 400; // bad request - null header value
					break;
				default:
					// signal unexpected newline
					e = 400; // bad request - malformed
					break;
				}
				break;
			default: // most other characters (non-delimiters) will ... 
				// ...accumulate in the current field
				break;
			}
		}
		if( e ){
			// some error in parsing
			fprintf( stderr, "got error code %d while parsing, state = %d\n", e, state );
			break;
		}
	}
	fprintf( stderr, "catnip: request parsing summary; " );
	if( method != NULL )fprintf( stderr, "method=%s; ", method );
	if( target != NULL )fprintf( stderr, "target=%s; ", target );
	if( version != NULL )fprintf( stderr, "version=%s; ", version );
	fprintf( stderr, "\n" );
	if( e == 0 && state == WANT_BODY ){
		if( map != NULL ){
			fprintf( stderr, "catnip: trying action %s\n", map->method );
			e = (*map->action)( body );
			fprintf( stderr, "catnip: back from action %s, e = %d\n", map->method, e );
		}
	}
}

static void
write_http_response( char* version, int status, char* message, char* server, char* content_type, char** other_headers )
{
	// HTTP/1.1 200 Everything Is Just Fine
	// Server: netcat!
	// Content-Type: text/html; charset=UTF-8
	// (this line intentionally left blank)
}

int http_trace( char* message ){
	return 500;
}

int http_head( char* message ){
	return 500;
}

int http_get( char* message ){
	return 500;
}

int http_post( char* message ){
	return 501; // not implemented
}

int http_patch( char* message ){
	return 501; // not implemented
}

int http_put( char* message ){
	return 501; // not implemented
}

int http_options( char* message ){
	return 501; // not implemented
}

int http_delete( char* message ){
	return 501; // not implemented
}

int http_connect( char* message ){
	return 501; // not implemented
}

