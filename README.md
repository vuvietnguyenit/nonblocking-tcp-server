# NONBLOCKING-TCP-SERVER

12 hours of coding TCP server from scratch by C can handle multiple concurrency clients.

## Usage

![Demo](./nonblocking-tcp-server.gif)

Build it

```sh
> gcc async_tcp_server.c -o async_tcp_server
```

Run

```sh
> ./async_tcp_server 0.0.0.0 9999
Listen TCP server at 0.0.0.0 9999
```

Create client and connect (use `nc` or other tools)
