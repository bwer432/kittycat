

enum http_parse_state {
	WANT_METHOD,
	WANT_TARGET,
	WANT_VERSION,
	WANT_HEADER_KEY,
	WANT_HEADER_VALUE,
	WANT_BODY,
	ERROR_STATE
};

struct http_request {
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
	int	body_length; // req->nr - (p - req->buf) only assigned just before method call
	int	e; // error code
	enum http_parse_state state;
	// buffer allocation and population
	ssize_t nr, np;
	size_t bsize; // assumed header line maximum
	char *buf;
	// action context
	char*	webroot; // webroot "kitty" or other
	int	usefork; // use fork/chroot instead of path stripping
};

struct method_action {
	char* method;
	int (*action)( int body_fd, struct http_request* req );
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

