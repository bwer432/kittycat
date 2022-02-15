FROM alpine 
RUN apk update && apk add curl && apk add build-base
RUN mkdir /src
WORKDIR /src
COPY ./kittycat.c ./catnip.c ./catnip.h ./
RUN cc -c kittycat.c \
    && cc -c catnip.c \
    && cc -o kc kittycat.o \
    && cc -o cn catnip.o
COPY cn /usr/bin/
COPY kc /usr/bin/
WORKDIR /var/local/kitty
COPY kitty/* ./
CMD ["sh","-c","kc -w 86400 response.http body | nc -l 8000 | cn -w /var/local/kitty"]
EXPOSE 8000

