Multi-threaded HTTP Proxy Server with Caching:
📌 Overview
This project is a multi-threaded HTTP proxy server written in C, designed to handle multiple client requests concurrently using POSIX threads (pthread).
It supports:

Forwarding HTTP requests from clients to remote servers.

Returning responses back to clients.

Caching fetched responses to improve performance for repeated requests.

It’s an educational project demonstrating:

Socket programming

Thread synchronization with semaphores and mutex locks

HTTP request parsing

Data caching

⚙️ Features
->Multi-threaded handling: Each client request is served in its own thread.

->Request parsing: Extracts method, host, path, and HTTP version from client requests.

->Caching system: Stores fetched responses and serves them on repeated requests without contacting the origin server.

->Debug logging: Detailed output for connections, parsing, cache hits/misses, and synchronization.

->Semaphore control: Limits maximum simultaneous client connections.
