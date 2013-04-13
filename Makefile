GOPATH ?= $(PWD)/godir

all: subsync

subsync: subsync.go
	GOPATH="$(GOPATH)" go get .
	GOPATH="$(GOPATH)" go build subsync.go

clean:
	rm -rf subsync godir
