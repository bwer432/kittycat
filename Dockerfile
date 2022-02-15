FROM alpine 
RUN apk update && apk add curl 
COPY cn /usr/bin/
COPY kc /usr/bin/
WORKDIR /var/local/kitty
COPY kitty/* ./
CMD ["sh","-c","kc response.http body | nc -l 8000 | cn"]


