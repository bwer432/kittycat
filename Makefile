all: 	kc cn

kc: 	kittycat.o
	cc -o kc kittycat.o

cn: 	catnip.o
	cc -o cn catnip.o

kittycat.o:	kittycat.c
	cc -c kittycat.c

catnip.o:	catnip.c
	cc -c catnip.c

