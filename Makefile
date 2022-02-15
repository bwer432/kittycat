# kittycat and catnip => kc & cn
# to be used on their own, together, or to complement netcat (nc)
# e.g.  kc -w 86400 response.http body | nc -l 8000 | cn

MAJOR?=0
MINOR?=1
PATCH?=6

VERSION=$(MAJOR).$(MINOR).$(PATCH)
APP_NAME = "kittycat"
HUB_NAMESPACE = "bwer432"
IMAGE_NAME = "${APP_NAME}"
DOCKERFILE = "Dockerfile"

# targets

all: 	kc cn

kc: 	kittycat.o
	cc -o kc kittycat.o

cn: 	catnip.o
	cc -o cn catnip.o

kittycat.o:	kittycat.c
	cc -c kittycat.c

catnip.o:	catnip.c
	cc -c catnip.c

# docker build and related targets borrow from https://www.docker.com/blog/containerizing-test-tooling-creating-your-dockerfile-and-makefile/

clean-image:
	@echo "+ $@"
	@docker rmi ${HUB_NAMESPACE}/${IMAGE_NAME}:latest || true
	@docker rmi ${HUB_NAMESPACE}/${IMAGE_NAME}:${VERSION} || true

image:
	@echo "+ $@"
	@docker build -t ${HUB_NAMESPACE}/${IMAGE_NAME}:${VERSION} -f ./${DOCKERFILE} .
	@docker tag ${HUB_NAMESPACE}/${IMAGE_NAME}:${VERSION} ${HUB_NAMESPACE}/${IMAGE_NAME}:latest
	@echo "Done."
	@docker images --format '{{.Repository}}:{{.Tag}}\t\t Built: {{.CreatedSince}}\t\tSize: {{.Size}}' | grep ${IMAGE_NAME}:${VERSION}

push: clean-image image
	@echo "+ $@"
	@docker push ${HUB_NAMESPACE}/${IMAGE_NAME}:${VERSION}
	@docker push ${HUB_NAMESPACE}/${IMAGE_NAME}:latest

