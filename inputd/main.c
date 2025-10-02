#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define RET_OPEN_FAILED -1
#define RET_ERRNO -2
#define RET_DEV_NOT_FOUND -3

// A = Apple Magic Trackpad USB-c
#define A_VENDOR_ID  0x004c 
#define A_PRODUCT_ID 0x0324

// L = Lenovo Laptop Trackpad
#define L_VENDOR_ID  0x06cb
#define L_PRODUCT_ID 0xce67

unsigned int bus_id = 0, vendor_id = 0, product_id, version = 0;
char path[PATH_MAX] = "";

int find_trackpads(void);

int main(void)
{
	int server_socket, client_socket;
	struct sockaddr_un server_addr, client_addr;

	int ret = find_trackpads();
	if (ret != 0) {
		fprintf(stderr, "Error: failed to find trackpads\n");
		return ret;
	}

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror(path);
		return RET_OPEN_FAILED;
	}

	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);

	server_addr.sun_family = AF_UNIX;
	strcpy(server_addr.sun_path, "unix_socket");
	size_t slen = sizeof(server_addr);

	bind(server_socket, (struct sockaddr *) &server_addr, slen);

	listen(server_socket, 3);
	unsigned int clen = sizeof(client_addr);
	client_socket = accept(server_socket, &client_addr, &clen);

	struct input_event ev[64];
	size_t quit = 0;
	while (!quit) {
		size_t n = read(fd, ev, sizeof(ev));
		size_t ev_read = n / sizeof(struct input_event);
		for (size_t i = 0; i < ev_read; i++) {
			send(client_socket, &ev[i], n, 0);
			printf("%ld\n", ev[i].time.tv_usec);
		}
	}

	close(fd);
	return 0;
}

int find_trackpads(void)
{
	DIR *d = opendir("/dev/input");
	if (d == NULL) {
		perror("opendir failed \"/dev/input\"");
		return RET_OPEN_FAILED;
	}

	char p[PATH_MAX];
	struct dirent *de;
	while ( (de = readdir(d)) ) {
		if (strncmp(de->d_name, "event", 5)) continue;

		snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);

		int fd = open(p, O_RDONLY);
		if (fd < 0) { perror(p); continue; }
		
		struct input_id id = {0};
		if (ioctl(fd, EVIOCGID, &id) < 0) {
			perror("EVIOCGID");
			close(fd);
			continue;
		}
		close(fd);

		if (id.vendor == A_VENDOR_ID && id.product == A_PRODUCT_ID) {
			bus_id  = id.bustype;
			version = id.version;
			strlcpy(path, p, sizeof(path));
			break;
		} else if (id.vendor == L_VENDOR_ID && id.product == L_PRODUCT_ID) {
			bus_id  = id.bustype;
			version = id.version;
			if (!(*path)) 
				strlcpy(path, p, sizeof(path));
		}
	}
	if (!(*path)) 
		return RET_DEV_NOT_FOUND;

	return 0;
}





