all: subsync

subsync: subsync.go
	go get .
	go build subsync.go

clean:
	rm -f subsync
