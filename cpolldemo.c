#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define TRUE  1
#define FALSE 0
#define OPEN_MAX 100

//int close_conn = 0;
void* thrd_func(void* arg) {
	int fd = *(int*)arg;
	//printf("Create a new echo thread! thread id=[%lu], fd=%d\n", getpid(), fd);

	while(1) {
		char recv_buff[512] = {0};
		int rc = recv(fd, recv_buff, sizeof(recv_buff), 0);
		if(rc < 0) {
			if (errno != EWOULDBLOCK){
				perror("Recv failed!");
				//close_conn = 1;
				break;
			}
		} else if(rc == 0) {
			//printf("Connection [%d] has been closed\n", fd);
			//close_conn = 1;
			break;
		} else {
			printf("Conn=[%d], recv len=[%d], recv data=[%s]\n", fd, rc, recv_buff);
		}

		if(send(fd, recv_buff, strlen(recv_buff), 0) < 0) {
			perror("Send failed!");
			//close_conn = 1;
			break;
		}
	}
}

int main() {
	unsigned int port = 8000;
	printf("Hello,this is echo server! 192.168.99.100:%d\n",port);

	//pollfd
	int timeout = 1000;
	struct pollfd pfds[OPEN_MAX];

	//socket
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	pfds[0].fd = listen_fd;
	pfds[0].events = POLLIN|POLLRDHUP;

	//setsockopt
	struct timeval tout_reuse = {1,0};
	int rc = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&tout_reuse, sizeof(tout_reuse));
	if (rc < 0) {
		perror("setsockopt SO_REUSERADDR failed");
		close(listen_fd);
		return -1;
	}

	struct timeval tout_recv = {0,1000};
	rc = setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tout_recv, sizeof(tout_recv));
	if (rc < 0) {
		perror("setsockopt SO_RCVTIMEO failed");
		close(listen_fd);
		return -1;
	}

	//noblock
	int flags = fcntl(listen_fd, F_GETFL, 0);
	fcntl(listen_fd, F_SETFL, flags|O_NONBLOCK);

	//bind
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	//addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = htons(port);

	int nBind = bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
	if(nBind < 0) {
		printf("bind socket errro! [%s], errno=[%d]\n", strerror(errno), errno);
		return -1;
	}

	//listen
	int nListen = listen(listen_fd, 3);
	if(nListen < 0) {
		printf("listen socket error! [%s], errno=[%d]", strerror(errno), errno);
		return -1;
	}
	printf("listen_fd=%d\n", listen_fd);

	int end_server = FALSE;
	int nfds = 1;
	while(end_server != TRUE) {
		int rc = poll(pfds, nfds, timeout);
		if(rc < 0) {
			perror("poll");
			continue;
		}

		if(rc == 0) {
			//printf("poll timeout.\n");
			continue;
		}

		int i = 0;
		int comp_array = 0;
		int current_size = nfds;
		//printf("current_size=%d\n", current_size);
		for(i=0; i<current_size; ++i) {
			if(pfds[i].revents == 0) {
				continue;
			}

			//client disconnect
			if(pfds[i].revents & POLLRDHUP) {
				printf("i=[%d], Client fd=[%d] disconnect! Return!\n", i, pfds[i].fd);
				close(pfds[i].fd);
				pfds[i].fd = -1;
				comp_array = 1;
				goto comp;
				break;
			}

			if(!(pfds[i].revents & POLLIN)) {
				printf("pfds[%d].revents!=POLLIN, revents=[%lu]\n", i, pfds[i].revents);
				end_server = TRUE;
				sleep(5);
				break;
			}

			//accept socket, proform accept
			if(pfds[i].fd == listen_fd) {
				while(1) {
					struct sockaddr_in client_addr;
					char cli_ip[INET_ADDRSTRLEN] = "";
					socklen_t cliaddr_len = sizeof(client_addr);

					printf("-----------%d-----------\n", nfds);
					printf("listen_fd = %d, cliaddr_len = %d\n", listen_fd, cliaddr_len);

					int connfd = accept(listen_fd, (struct sockaddr*)&client_addr, &cliaddr_len);
					if(connfd < 0) {
						perror("accept");
						printf("accept error:%d\n", errno);
						break;
					}

					inet_ntop(AF_INET, &client_addr.sin_addr, cli_ip, INET_ADDRSTRLEN);
					printf("client ip=%s, port=%d\n", cli_ip, ntohs(client_addr.sin_port));

					if(nfds >= OPEN_MAX) {
						printf("max conntion. connected=[%d]\n", nfds);
						break;
					} else {
						pfds[nfds].fd = connfd;
						pfds[nfds].events = POLLIN|POLLRDHUP;
						nfds++;
					}
				}
			} else {
				pthread_t thrd;
				if(pthread_create(&thrd, NULL, thrd_func, &pfds[i].fd) != 0) {
					printf("create thread failed!\n");
					return -1;
				}

				if(pthread_detach(thrd) != 0) {
					printf("detach thread failed!\n");
					return -1;
				}

				/*
				char recv_buff[512] = {0};
				while(1) {
					int rc = recv(pfds[i].fd, recv_buff, sizeof(recv_buff), 0);
					if(rc < 0) {
						if (errno != EWOULDBLOCK){
							perror("recv failed!");
							close_conn = 1;
						}
						break;
					} else if(rc == 0) {
						printf("Connection[%d] closed\n", i);
						close_conn = 1;
						break;
					} else {
						printf("i=[%d], recv len=[%d], pfds.fd=[%d], recv data=[%s]\n", i, rc, pfds[i].fd, recv_buff);
					}

					if(send(pfds[i].fd, recv_buff, strlen(recv_buff), 0) < 0) {
						perror("send failed!");
						close_conn = 1;
						break;
					}
				}

				if(close_conn) {
					close_conn = 0;
					close(pfds[i].fd);
					pfds[i].fd = -1;
					comp_array = 1;
					printf("client closed! nfds=%d\n", nfds);
				} */
			}

comp:
			int j, k = 0;
			if(comp_array == 1) {
				comp_array = 0;
				for (j = 0; j < nfds; ++j) {
					if (pfds[j].fd == -1) {
						for(k = j; k < nfds; ++k) {
							pfds[k].fd = pfds[k+1].fd;
						}
						nfds--;
					}
				}
			}

		}

		//printf("client closed!\n");
	}

	close(listen_fd);
	return 0;
}
