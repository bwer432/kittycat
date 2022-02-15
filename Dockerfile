FROM alpine 
RUN apk update && apk add curl 
RUN mkdir /app
COPY cn /app
COPY kc /app
WORKDIR /var/local/kitty
COPY kitty/* ./
CMD ["sh","-c","/app/kc -w 86400 response.http body | nc -l 8000 | /app/cn -w /var/local/kitty"]
EXPOSE 8000

