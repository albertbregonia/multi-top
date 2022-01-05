.PHONY: default
default:
	go build -ldflags="-w -s"
	./Multi-Top
clean:
	rm Multi-Top