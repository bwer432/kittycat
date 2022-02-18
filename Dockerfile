FROM alpine AS build-stage
RUN apk update && apk add curl && apk add build-base
RUN mkdir /src
WORKDIR /src
COPY ./kittycat.c ./catnip.c ./catnip.h ./
RUN cc -c kittycat.c \
    && cc -c catnip.c \
    && cc -static -o kc kittycat.o \
    && cc -static -o cn catnip.o
RUN cp cn /usr/bin/
RUN cp kc /usr/bin/

FROM alpine
RUN apk update && apk add curl
COPY --from=build-stage /usr/bin/cn /usr/bin
COPY --from=build-stage /usr/bin/kc /usr/bin
WORKDIR /var/local/kitty
COPY kitty/* ./
CMD ["sh","-c","kc -w 86400 response.http body | nc -v -l -p 80 | cn -w /var/local/kitty"]
EXPOSE 80

