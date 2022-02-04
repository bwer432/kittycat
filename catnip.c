/*
 * This version of catnip.c is derived from kill.c rather than starting from scratch.
 * Source for kill.c pulled from:
 *  https://opensource.apple.com/source/shell_cmds/shell_cmds-187/kill/kill.c
 * on 2022-01-16
 *
 * raw_cat() brought in from cat.c via kittycat.c 2022-01-24
 * original sourced from same site as kill.c above.
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

// reflect multiple heritage - cat(1) like kittycat(1), and kill(1) for catnip(1)

#ifndef lint
#if 0
static char sccsid[] = "@(#)kill.c	8.4 (Berkeley) 4/28/95";
static char sccsid2[] = "@(#)cat.c	8.2 (Berkeley) 4/27/95";
#endif
static const char rcsid[] =
  "$FreeBSD: src/bin/kill/kill.c,v 1.11.2.1 2001/08/01 02:42:56 obrien Exp $";
static const char rcsid2[] =
  "$FreeBSD: src/bin/cat/cat.c,v 1.32 2005/01/10 08:39:20 imp Exp $";
#endif /* not lint */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>	// catnip for open, read, write, close, etc.
#include <sys/stat.h>   // for stat used in borrowed cat and header gen
#include <time.h>	// for mtime in stat
#include <unistd.h>	// catnip for open, read, write, close, etc.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip.h"	// for http_parse_state and http_request

int main __P((int, char *[]));
void nosig __P((char *));
int signame_to_signum __P((char *));
void usage __P((void));
static pid_t read_kitty_marker();
static void parse_request(int, int, int, char*, int);
void reset_response_headers();
void add_response_header( char* key, char* value );
void write_response_headers( int head_fd );
static void write_http_response( int head_fd, char* version, int status, char* message, char* content_type, char** other_headers );

// prior version of action methods took ( int body_fd, char* request_body, int length )
int http_trace( int body_fd, struct http_request* req );
int http_head( int body_fd, struct http_request* req );
int http_get( int body_fd, struct http_request* req );
int http_post( int body_fd, struct http_request* req );
int http_patch( int body_fd, struct http_request* req );
int http_put( int body_fd, struct http_request* req );
int http_options( int body_fd, struct http_request* req );
int http_delete( int body_fd, struct http_request* req );
int http_connect( int body_fd, struct http_request* req );

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
	int ch;
	int errors, numsig, pid; // pid of kc (kittycat) or other process to signal
	char *ep;
	char *kitty; // kc (kittycat) signal file
	char *head;  // http response header
	char *body;  // body: html document, image, etc.
	int  head_fd;
	int  body_fd;
	char *webroot; // kitty (instead of www or webroot etc.)
	int  usefork;  // use fork/chroot instead of path stripping

	if (argc < 1)
		usage();

	numsig = SIGCONT;	// catnip defaults to SIGCONT unlike kill's default SIGTERM;
	kitty = ".kc"; // warning: default does not support concurrency in shared file namespace
	head = "response.http"; // default response header output file path
	body = "body";		// default body output file path
	webroot = "kitty";	// default web root instead of www
	usefork = 0;		// use path stripping by default, can use fork/chroot instead

	while ((ch = getopt(argc, argv, "fk:s:w:")) != -1)
		switch (ch) {
		case 'f':
			++usefork;		/* use fork/chroot instead of path stripping */
			fprintf( stderr, "catnip: fork/chroot style not yet implemented.\n" );
			break;
		case 'k': 			/* kitty rendezvous path */
			kitty = optarg;
			break;
		case 's':			/* kitty signal name/number */
			if (isalpha(*optarg)) {
				if ((numsig = signame_to_signum(optarg)) < 0)
					nosig(optarg);
			} else if (isdigit(*optarg)) {
				numsig = strtol(optarg, &ep, 10);
				if (!*optarg || *ep)
					errx(1, "illegal signal number: %s", optarg);
				if (numsig < 0 || numsig >= NSIG)
					nosig(optarg);
			} else
				nosig(optarg);
			break;
		case 'w':			/* kitty webroot */
			webroot = optarg;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	pid = 0; // for now. we had a race condition when we called read_kitty_marker() here.
	// could check for webroot here, but for trace and others we don't need it
	// printf( "catnip: argc = %d, pid = %d, numsig = %d, kitty = %s, head = %s, body = %s\n", argc, pid, numsig, kitty, head, body );
	if( argc >= 1 ){
		head = *argv++;
		--argc;
	}
	if( argc >= 1 ){
		body = *argv++;
		--argc;
	}
	printf( "catnip: argc = %d, pid = %d, numsig = %d, kitty = %s, head = %s, body = %s\n", argc, pid, numsig, kitty, head, body );

	head_fd = open(head, O_WRONLY|O_CREAT|O_TRUNC, 0600); // allow kc to pick up contents of response header
	if (head_fd < 0) {
		warn("%s", head);
		errors = 1;
	} 

	body_fd = open(body, O_WRONLY|O_CREAT|O_TRUNC, 0600); // allow kc to pick up contents of body
	if (body_fd < 0) {
		warn("%s", body);
		errors = 1;
	} 

	parse_request( STDIN_FILENO, head_fd, body_fd, webroot, usefork );

	// nip the kittycat once for header
	close(head_fd);
	pid = read_kitty_marker( kitty ); // moved down here because of race condition
	if( pid ){
		if (kill(pid, numsig) == -1) {
			warn("signalling %s", head);
			errors = 1;
		}
	}

	// nip the kittycat again for body
	close(body_fd);
	if( pid ){
		if (kill(pid, numsig) == -1) {
			warn("signalling %s", body);
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
	// removing support for [{-s signal_name | -signal_name | -signal_number}]
	// in favor of simply [-s {signal_name|signal_number}], for we're merely *derived* from kill(1)
	// *not* forward compatible.
	(void)fprintf(stderr, "%s\n",
		"usage: cn [-f] [-k kitty_cat_file] [-s {signal_name|signal_number}] [-w webroot] [head [body]]");
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

struct http_request* alloc_http_request(){
	struct http_request* req;
	
	if((req = malloc(sizeof(struct http_request))) == NULL)
		err(1, "struct");
	else{
		req->bsize = 4096; // assumed header line maximum
		req->buf = NULL;
		if((req->buf = malloc(req->bsize)) == NULL)
			err(1, "buffer");
		else{
			req->state = WANT_METHOD; // that's how the request begins
			req->method = NULL; 
			req->target = NULL;
			req->version = NULL;
			req->hk = NULL;
			req->hv = NULL;
			req->server = NULL;
			req->port = NULL;
			req->body = NULL;
			req->message = NULL;
			req->content_type = NULL;
			req->other_headers = NULL;
			req->map = NULL; // method-action pointer
			req->vp = NULL; // version-map pointer
			req->e = 0; // presumed innocent
		}
	}
	return req;
}

/*
 * Parse the input stream from:
 * - nc (netcat)
 * - kc (kittycat) | nc (netcat)
 * - anywhere else (e.g. file redirect)
 * for an HTTP request.
 * Then call the appropriate handler per the method in the request.
 * Output any data to the head and body output files.
 * The caller opened those files, will close them when we return, and will signal 
 * any upstream process such as kc (kittycat).
 */
static void
parse_request(int rfd, int head_fd, int body_fd, char* webroot, int usefork)
{
	struct http_request* req;
	char* p;
	char c;

	if( ( req = alloc_http_request() ) == NULL || req->buf == NULL )
		return;
	req->webroot = webroot;
	req->usefork = usefork;
	//
	// BUF-BUG: though originally intended to chain buffers for multiple reads, only one buffer is tracked
	// in this current implementation. subsequent reads will overwrite the old data, rendering all
	// references into it invalid after the first pass. handling requests with large headers or 
	// body of any significant size are *not* currently supported.
	//
	for( p = req->buf; (req->nr = read(rfd, req->buf, req->bsize)) > 0; ){
		fprintf( stderr, "read %ld bytes\n", req->nr );
		req->method = p; // assumption for entering WANT_METHOD
		// NOT sscanf( p, "%s %s %s\n", &method, &target, &version );
		for( req->np = 0; req->np < req->nr && ( c = *p ) != '\0' && !req->e && req->state != WANT_BODY; ++p, ++req->np ){
			switch( c ){
			case '\r': // need to change some logic if this is real - percolated up!
				*p = '\0'; // terminate any field, ahead of '\n'
				break; // but do *NOT* change state...
			case ' ':
				switch( req->state ){
				case WANT_METHOD:
					*p = '\0'; // terminate the method name
					req->state = WANT_TARGET;
					req->target = p+1; // assumption when entering WANT_TARGET
					// check method at this point
					for( req->map = http_methods; req->map && req->map->method != NULL; ++req->map ){
						if( strcmp( req->method, req->map->method ) == 0 ){
							// we have a winner, retain map value
							break;
						}
					}
					if( req->map->method == NULL ){
						// no match
						req->map = NULL; // simplifies check later
						req->e = 400; // bad request: method
						req->message = "Bad Request - method";
					}
					break;
				case WANT_TARGET:
					*p = '\0'; // terminate the target (URL or short path)
					req->state = WANT_VERSION;
					req->version = p+1; // assumption when entering WANT_VERSION
					break;
				case WANT_VERSION:
					// not expecting spaces within the HTTP version
					// should flag an error
					fprintf( stderr, "spaces within http version\n" );
					req->e = 505; // unsupported version, or 400 bad request
					break;
				case WANT_HEADER_KEY:
					*p = '\0'; // this is *after* the colon hopefully
					req->state = WANT_HEADER_VALUE;
					req->hv = p+1; // set up to accumulate value next
					break;
				case WANT_HEADER_VALUE:
					// let them accumulate within the value
					if( p == req->hv ){ // except
						*p = '\0'; // eat the leading space
						req->hv = p+1; // as it is ignored
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
				switch( req->state ){
				case WANT_VERSION:
					*p = '\0'; // terminate the version 
					req->state = WANT_HEADER_KEY;
					req->hk = p+1; // assuming a header-key comes next
					// check version at this point
					fprintf( stderr, "checking http version %s;\n", req->version );
					for( req->vp = http_versions; req->vp && req->vp->version != NULL; ++req->vp ){
						fprintf( stderr, "is http version %s?\n", req->vp->version );
						if( strcmp( req->version, req->vp->version ) == 0 ){
							// we have a winner, retain vp value
							break;
						}
					}
					if( req->vp->version == NULL ){
						// no match
						fprintf( stderr, "no matching http version for %s;\n", req->version );
						req->vp = NULL;
						req->e = 505; // unsupported version
					}
					break;
				case WANT_HEADER_VALUE:
					*p = '\0'; // terminate the header value
					req->state = WANT_HEADER_KEY;
					// preprocess this header now - must save (hk,hv)
					fprintf( stderr, "catnip: should process header: (%s,%s)\n", req->hk, req->hv );
					req->state = WANT_HEADER_KEY; // get set for the next one
					req->hk = p+1; // assuming a header-key comes next
					req->hv = NULL;
					break;
				case WANT_HEADER_KEY:
					// if at very beginning, is beginning of body
					if( p == req->hk ){
						req->state = WANT_BODY;
						req->body = p+1;
					}
					else {
						*p = '\0'; // terminate only to show error
						fprintf( stderr, "null header value, k=[%s]\n", req->hk );
						req->e = 400; // bad request - null header value
						req->message = "Bad Request - null header";
					}
					break;
				default:
					// signal unexpected newline
					req->e = 400; // bad request - malformed
					req->message = "Bad Request - unexpected newline";
					break;
				}
				break;
			default: // most other characters (non-delimiters) will ... 
				// ...accumulate in the current field
				break;
			}
		}
		if( req->e ){
			// some error in parsing
			fprintf( stderr, "got error code %d while parsing, state = %d\n", req->e, req->state );
			break;
		}
		// TEMPORARY, or PERMANENT, see BUF-BUG note.
		break; // because BUF-BUG mentioned at top does not allow multiple buf, we cannot allow overwrite!
	}
	fprintf( stderr, "catnip: request parsing summary; " );
	if( req->method != NULL )fprintf( stderr, "method=%s; ", req->method );
	if( req->target != NULL )fprintf( stderr, "target=%s; ", req->target );
	if( req->version != NULL )fprintf( stderr, "version=%s; ", req->version );
	fprintf( stderr, ";\n" );
	fprintf( stderr, "e = %d, state = %d, map = %ld;\n", req->e, req->state, (long)req->map ); // debug
	if( req->e == 0 && req->state == WANT_BODY ){
		if( req->map != NULL ){
			reset_response_headers();
			fprintf( stderr, "catnip: trying action %s\n", req->map->method );
			req->body_length = req->nr - (p - req->buf);
			// old method action took ( body_fd, req->body, req->nr - (p - req->buf) )
			req->e = (*req->map->action)( body_fd, req );
			fprintf( stderr, "catnip: back from action %s, e = %d\n", req->map->method, req->e );
		}
	}
	write_http_response( head_fd, req->version, req->e, req->message, req->content_type, req->other_headers );
	// be sure to save all data to either the response header file or the body output file
	// because this ship is going down...
	if( req->buf != NULL )
		free( req->buf );
	if( req != NULL )
		free( req );
}

// globals. ew!
#define CATNIP_MAX_RESPONSE_HEADERS 16
int response_header_count;
struct key_value_pair {
	char*	key;
	char*	value;
};
struct key_value_pair response_headers[CATNIP_MAX_RESPONSE_HEADERS];

void reset_response_headers(){
	response_header_count = 0;
}

void add_response_header( char* key, char* value ){
	if( response_header_count < CATNIP_MAX_RESPONSE_HEADERS ){
		struct key_value_pair* kvp;
		kvp = &response_headers[response_header_count];
		kvp->key = malloc( strlen( key ) + 1 );
		kvp->value = malloc( strlen( value ) + 1 );
		if( kvp->key == NULL || kvp->value == NULL )
			return; // silently return for now: BUG, & leak: should free other
		strcpy( kvp->key, key );
		strcpy( kvp->value, value );
		++response_header_count;
	}
	// else silently drop for now: BUG
}

void write_response_headers( int head_fd ){
	for( int i = 0; i < response_header_count; ++i ){
		dprintf( head_fd, "%s: %s\n", response_headers[i].key, response_headers[i].value );
	}
}

// Write a response to the "header" channel - perhaps to be relayed by kc to nc:
//
// HTTP/1.1 200 Everything Is Just Fine
// Server: netcat!
// Content-Type: text/html; charset=UTF-8
// (this line intentionally left blank)
static void
write_http_response( int head_fd, char* version, int status, char* message, char* content_type, char** other_headers )
{
	dprintf( head_fd, "%s %d %s\n", version, status, message == NULL ? "nominal" : message );
	dprintf( head_fd, "Server: catnip (cn) 0.0.1\n" );
	dprintf( head_fd, "Content-Type: %s\n", content_type == NULL ? "text/html; charset=UTF-8" : content_type );
	// consider other headers too? which request headers should also be response headers?
	// which additional headers should be added in? such as size? 
	write_response_headers( head_fd );
	dprintf( head_fd, "\n" ); // done with the headers, on to the body! (well, the end of this file, let kc cat them)
}

/*
 * if we are running in fork/chroot mode, then we can just use our webroot 
 * as the chroot path, such as "kitty" which should just be effectively 
 * "./kitty" as a relative path to the present working directory from
 * which we (cn) were run. certainly, if we are running in-process without
 * the forking, we could merely catenate (oh, the humanity, er, felinity?)
 * our webroot (e.g. "kitty" or "www" or "webroot") to the target path, 
 * thus:
 *	/		--> kitty/
 *	/index.html	--> kitty/index.html
 *	/css/style.css	--> kitty/css/style.css
 *	/image/my.png	--> kitty/image/my.png
 * and should we allow these? or handle those differently?
 *	http://localhost--> kittyhttp://localhost
 *	http://x:8080	--> kittyhttp://x:8080
 * however, without stripping, someone could give us a lovely url including
 * a path such as ../../usr/local/bin/cn or some such nonsense, and with
 * just the prefixing we would have kitty/../../usr/local/bin/cn or whatever
 * which taint a whole bag of tricks better than just using the target verbatim.
 *
 * also, do we imply index.html or default.htm or those beauteous assumptions
 * in this realm of kittycat and catnip? let's say yes for now, but just 
 * index.html for now and not a list we check for existence nor as a parameter.
 *
 * which all begs the question, are we (wrangle_path) responsible for
 * vetting the paths with stat and such or do we just leave that to the 
 * action methods themselves?
 */
char* wrangle_path( struct http_request* req ){
	char* herded_path = NULL;
	char* default_doc = "index.html"; // should really be a parameter
	// length of / (1) --> kitty/index.html\0 (17)
	int   effective_length = strlen( req->webroot ) + strlen( req->target ) + strlen( default_doc ) + 2; // has margin if not default_doc...
	fprintf( stderr, "into wrangle: target=%s\n", req->target );
	if( ( herded_path = malloc( effective_length ) ) == NULL ){
		req->e = 500; // not enough memory for path
	}
	else{
		char* p;
		if( !req->usefork )
			strcpy( herded_path, req->webroot );
		strcat( herded_path, req->target );
		if( ( p = strrchr( herded_path, '/' ) ) != NULL && p[1] == '\0' ) // ends with /
			strcat( herded_path, default_doc );
		fprintf( stderr, "wrangle out: %s\n", herded_path );
	}
	return herded_path;
}

/* 
 * inherited from cat.c as noted at the top:
 * raw_cat() brought in from cat.c via kittycat.c 2022-01-24
 * but now we're making it more like cp since we're taking both rfd and wfd 
 * in and not assuming wfd = fileno(stdout) as the inherited one had done in cat.
 */
static int
raw_cat(char* filename, int rfd, int wfd) // are you a copy *of* a cat or are you a cp *for* a cat? 
{
	int off;
	ssize_t nr, nw;
	static size_t bsize;
	static char *buf = NULL;
	struct stat sbuf;
	int rval = 0;

	if (buf == NULL) {
		if (fstat(wfd, &sbuf))
			err(1, "%s", filename);
		bsize = sbuf.st_blksize > 1024 ? sbuf.st_blksize : 1024; // old school instead of MAX
		if ((buf = malloc(bsize)) == NULL)
			err(1, "buffer");
	}
	while ((nr = read(rfd, buf, bsize)) > 0)
		for (off = 0; nr; nr -= nw, off += nw)
			if ((nw = write(wfd, buf + off, (size_t)nr)) < 0)
				err(1, "body");
	if (nr < 0) {
		warn("%s", filename);
		rval = 1;
	}
	return rval;
}

int http_trace( int body_fd, struct http_request* req ){
	write( body_fd, req->body, req->body_length ); // that's all she wrote
	req->message = "OK";
	return 200;
}

int http_head( int body_fd, struct http_request* req ){
	// there is no body, only head
	int e;
	char* path;
	struct stat docstat;
	struct tm   tm, *resulttm;
	char statbuf[64]; // temporary number to string conversion
	path = wrangle_path( req );
	switch( e = access( path, F_OK|R_OK ) ){
	case 0:
		break;
	case -1:
		switch( errno ){
		case ENOENT:
			req->message = "Not Found";
			return 404;
		case EACCES:
			req->message = "Forbidden";
			return 403;
		default:
			req->message = "Internal Server Error";
			return 500;
		}
		break;
	}
	switch( e = stat( path, &docstat ) ){
	case 0:
		sprintf( statbuf, "%lld", docstat.st_size );
		add_response_header( "Content-Length", statbuf ); // that will copy, we can reuse statbuf
		resulttm = gmtime_r( &docstat.st_mtimespec.tv_sec, &tm );
		if( resulttm != NULL ){
			int l;
			l = strftime( statbuf, 64, "%a, %d %b %Y %H:%M:%S %Z", &tm );
			if( l ){
				add_response_header( "Date", statbuf );
			}
		}
		// want other headers? 
		// what about content type - a *simple* suffix to type assumption table?
		fprintf( stderr, "HEAD got okay stat.\n" );
		break;
	case -1:
		fprintf( stderr, "HEAD got stat = %d, errno = %d\n", e, errno );
		break;
	}
		// docstat.st_mode
         // mode_t   st_mode;   /* inode protection mode */
         // uid_t    st_uid;    /* user-id of owner */
         // gid_t    st_gid;    /* group-id of owner */
         // struct timespec st_atimespec;  /* time of last access */
         // struct timespec st_mtimespec;  /* time of last data modification */
         // struct timespec st_ctimespec;  /* time of last file status change */
         // off_t    st_size;   /* file size, in bytes */
	req->message = "OK";
	return 200;
}

int http_get( int body_fd, struct http_request* req ){
	int e;
	char* path;
	int doc_fd;
	switch( e = http_head( body_fd, req ) ){
	/* case 403: */
	/* case 404: */
	/* case 500: */
	default: // all of the above
		return e;
	case 200:
		// had already done this in http_head(), can we borrow? (e.g. stash in req before relayed back to us)
		path = wrangle_path( req ); 
		doc_fd = open( path, O_RDONLY );
		if( doc_fd < 0 ){
			// why, when http_head cleared it? 
			// *should* interpret errno here and give more guidance than 500...
			req->message = "Internal Server Error";
			e = 500;
		}
		else{
			// now would be the time to check usefork and do the fork()/chroot() here
			// alas, not yet...
			// inverting use of raw_cat - instead of going to stdout, we use it like cp would
			if( raw_cat( path, doc_fd, body_fd ) ){  // or want req->target instead of path?
				req->message = "Internal Server Error - raw_cat"; // distinguish it
				e = 500;
			}
			close( doc_fd );
		}
	}
	return e;
}

int http_post( int body_fd, struct http_request* req ){
	req->message = "Not Implemented";
	return 501; // not implemented
}

int http_patch( int body_fd, struct http_request* req ){
	req->message = "Not Implemented";
	return 501; // not implemented
}

int http_put( int body_fd, struct http_request* req ){
	req->message = "Not Implemented";
	return 501; // not implemented
}

int http_options( int body_fd, struct http_request* req ){
	req->message = "Not Implemented";
	return 501; // not implemented
}

int http_delete( int body_fd, struct http_request* req ){
	req->message = "Not Implemented";
	return 501; // not implemented
}

int http_connect( int body_fd, struct http_request* req ){
	req->message = "Not Implemented";
	return 501; // not implemented
}

