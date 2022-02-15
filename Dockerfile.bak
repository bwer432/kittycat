FROM alpine AS build0
RUN apk update && apk add curl && apk add nc
RUN cc -c catnip.c kittycat.c && cc -o cn catnip.o && cc -o kc kittycat.o

FROM scratch
COPY --from=build0 cn /usr/local/bin
COPY --from=build0 kc /usr/local/bin
WORKDIR /var/local/kitty
COPY kitty/* ./


