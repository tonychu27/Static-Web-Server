# Static-Web-Server

## Introduction
A web server that can handle `HTTP` and `HTTPS` request concurrently.

Implement parts of the HTTP/1.0 specification defined in (RFC1945)[https://www.ietf.org/rfc/rfc1945.txt].

Achieving a transfer rate of 3.6 GB/s.

Implemented multiprocessing techniques to efficiently manage a large volume of connection
requests.

## How to run it

Please make sure you have docker in your environment.
```bash
docker-compose up -d
```

And then run
```bash
./test.sh
```

How to check it ?
```bash
curl http://localhost:10801/
curl -k https://localhost:10841/
```