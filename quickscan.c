int main( int argc, char* argv[], char* envp[] ){
	char* buf;
	int   bsize = 4096;
	int   nr;
	int   nf;
	char  method[16];
	char  target[256]; // danger, danger
	char  version[16];

	buf = malloc( bsize );
	nr = read( 0, buf, bsize );
	if( nr >= 0 && nr < bsize ){
		buf[nr] = '\0'; // assume text and null terminate it

		nf = sscanf( buf, "%s %s %s\n", method, target, version );
		printf( "nf = %d\n", nf );
		printf( "method = %s\n", method );
		printf( "target = %s\n", target );
		printf( "version = %s\n", version );
	}
}

