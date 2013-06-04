GOPATH ?= $(PWD)/godir

all: subsync

subsync: subsync.go
	GOPATH="$(GOPATH)" go get -d .
	GOPATH="$(GOPATH)" go build -o $@ subsync.go

clean:
	rm -rf subsync godir
