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

int main __P((int, char *[]));
void nosig __P((char *));
int signame_to_signum __P((char *));
void usage __P((void));
static pid_t read_kitty_marker();
static void parse_request(int, int, int);
void reset_response_headers();
void add_response_header( char* key, char* value );
void write_response_headers( int head_fd );
static void write_http_response( int head_fd, char* version, int status, char* message, char* content_type, char** other_headers );

int http_trace( int body_fd, char* request_body, int length );
int http_head( int body_fd, char* request_body, int length );
int http_get( int body_fd, char* request_body, int length );
int http_post( int body_fd, char* request_body, int length );
int http_patch( int body_fd, char* request_body, int length );
int http_put( int body_fd, char* request_body, int length );
int http_options( int body_fd, char* request_body, int length );
int http_delete( int body_fd, char* request_body, int length );
int http_connect( int body_fd, char* request_body, int length );

struct method_action {
	char* method;
	int (*action)( int body_fd, char* request_body, int length );
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
	int ch;
	int errors, numsig, pid; // pid of kc (kittycat) or other process to signal
	char *ep;
	char *kitty; // kc (kittycat) signal file
	char *head;  // http response header
	char *body;  // body: html document, image, etc.
	int  head_fd;
	int  body_fd;

	if (argc < 1)
		usage();

	numsig = SIGCONT;	// catnip defaults to SIGCONT unlike kill's default SIGTERM;
	kitty = ".kc"; // warning: default does not support concurrency in shared file namespace
	head = "response.http"; // default response header output file path
	body = "body";		// default body output file path

	while ((ch = getopt(argc, argv, "k:s:")) != -1)
		switch (ch) {
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
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	pid = read_kitty_marker( kitty );
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

	parse_request( STDIN_FILENO, head_fd, body_fd );

	// nip the kittycat once for header
	close(head_fd);
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
		"usage: cn [-k kitty_cat_file] [-s {signal_name|signal_number}] [head [body]]");
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
parse_request(int rfd, int head_fd, int body_fd)
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
	char*	message;
	char*	content_type;
	char**	other_headers;
	struct method_action* map; // method-action-pointer = map
	struct version_map* vp; 
	int	e; // error code

	if ((buf = malloc(bsize)) == NULL)
		err(1, "buffer");
	//
	// BUF-BUG: though originally intended to chain buffers for multiple reads, only one buffer is tracked
	// in this current implementation. subsequent reads will overwrite the old data, rendering all
	// references into it invalid after the first pass. handling requests with large headers or 
	// body of any significant size are *not* currently supported.
	//
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
		message = NULL;
		content_type = NULL;
		other_headers = NULL;
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
						map = NULL; // simplifies check later
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
		// TEMPORARY, or PERMANENT, see BUF-BUG note.
		break; // because BUF-BUG mentioned at top does not allow multiple buf, we cannot allow overwrite!
	}
	fprintf( stderr, "catnip: request parsing summary; " );
	if( method != NULL )fprintf( stderr, "method=%s; ", method );
	if( target != NULL )fprintf( stderr, "target=%s; ", target );
	if( version != NULL )fprintf( stderr, "version=%s; ", version );
	fprintf( stderr, "\n" );
	fprintf( stderr, "e = %d, state = %d, map = %d\n", e, state, (int)map ); // debug
	if( e == 0 && state == WANT_BODY ){
		if( map != NULL ){
			reset_response_headers();
			fprintf( stderr, "catnip: trying action %s\n", map->method );
			e = (*map->action)( body_fd, body, nr - (p - buf) );
			fprintf( stderr, "catnip: back from action %s, e = %d\n", map->method, e );
			write_http_response( head_fd, version, e, message, content_type, other_headers );
		}
	}
	// be sure to save all data to either the response header file or the body output file
	// because this ship is going down...
	if( buf != NULL )
		free( buf );
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


#ifdef NOTYET
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
#endif /* defined(NOTYET) */

int http_trace( int body_fd, char* request_body, int length ){
	write( body_fd, request_body, length ); // that's all she wrote
	return 200;
}

int http_head( int body_fd, char* request_body, int length ){
	// there is no body, only head
	int e;
	char* path;
	struct stat docstat;
	struct tm   tm, *resulttm;
	char statbuf[64]; // temporary number to string conversion
	path = "index.html"; // need to get context from request...
	switch( e = access( path, F_OK|R_OK ) ){
	case 0:
		break;
	case -1:
		switch( errno ){
		case ENOENT:
			return 404;
		case EACCES:
			return 403;
		default:
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

	return 200;
}

int http_get( int body_fd, char* request_body, int length ){
	//NOTYET: -- and want to invert this really --  raw_cat(body_fd);
	return 500;
}

int http_post( int body_fd, char* request_body, int length ){
	return 501; // not implemented
}

int http_patch( int body_fd, char* request_body, int length ){
	return 501; // not implemented
}

int http_put( int body_fd, char* request_body, int length ){
	return 501; // not implemented
}

int http_options( int body_fd, char* request_body, int length ){
	return 501; // not implemented
}

int http_delete( int body_fd, char* request_body, int length ){
	return 501; // not implemented
}

int http_connect( int body_fd, char* request_body, int length ){
	return 501; // not implemented
}

