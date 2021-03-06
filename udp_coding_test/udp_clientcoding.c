#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include <kodoc/kodoc.h>

int ack = 0; /* 判斷是否換block的共享變數 */
pthread_t tcp_thread;
pthread_mutex_t mutex;
pthread_cond_t alternate;

void swap (int *a, int *b)
{
	int temp = *a;
	*a = *b;
	*b = temp;
}

void error(char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

void *tcp_ack()
{
	pthread_mutex_lock(&mutex);

	int sockfd;
	struct sockaddr_in my_addr;
	struct stat filestat;

	/* 建立tcp連線(Client) */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1 )
		error("socket");

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(2325);
	my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (connect(sockfd, (struct sockaddr*) &my_addr, sizeof(struct sockaddr)) == -1)
		error("connect");

	/* 取得檔案資訊並傳送 */
	if (lstat("123.mp4", &filestat) < 0)
		error("ERROR : get file size");

	printf("The file size is %lu bytes\n\n", filestat.st_size);

	if (write(sockfd, &filestat.st_size, sizeof(filestat.st_size)) < 0)
		error("Error:write file_size failed");

	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&alternate); // signal與lock位置無關

	/* 持續讀取ack */
	while (1) {
		if(read(sockfd, &ack, sizeof(ack)) < 0)
				error("Error:read ack failed\n");

		pthread_mutex_lock(&mutex);

		if (ack)
			ack = 0;

		pthread_mutex_unlock(&mutex);
		pthread_cond_signal(&alternate);
	}
}

void thread_create(void)
{
	if ((pthread_create(&tcp_thread, NULL, tcp_ack, NULL)) != 0)
		printf("create function : tcp_thread create fail\n");

	else
		printf("create function : tcp_thread is established\n");
}

int main()
{
	pthread_mutex_init(&mutex,NULL);

	pthread_mutex_lock(&mutex);

	/* 設定udp變數及相關變數 */
	int sockfd, numbytes, control_num = 0, swap_num = 1;
	struct sockaddr_in my_addr;
	struct sockaddr_in serv_addr;

	/* 1.建立encoder */
	uint32_t max_symbols = 10;
	uint32_t max_symbol_size = 1000;

	int32_t codec = kodoc_full_vector;
	int32_t finite_field = kodoc_binary;

	kodoc_factory_t encoder_factory =
	kodoc_new_encoder_factory(codec, finite_field,
                                 max_symbols, max_symbol_size);
	kodoc_coder_t encoder = kodoc_factory_build_coder(encoder_factory);

	uint32_t bytes_used;
	uint32_t payload_size = kodoc_payload_size(encoder);
	uint32_t block_size = kodoc_block_size(encoder);
	uint8_t* control = (uint8_t*) malloc(payload_size + 1);
	uint8_t* data_in = (uint8_t*) malloc(block_size);
	uint8_t* payload = control + 1;

	memset (control, 0, 1);

	/* 2.建立tcp_thread */
	thread_create();

	pthread_cond_wait(&alternate, &mutex);

	/* 3.建立udp連線(Client) */
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1 )
		error("socket");

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(2324);
	my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (bind(sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr)) == -1 )
		error("bind");

	serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(5134);

	/* 4.讀檔 */
	FILE *fp;
	fp = fopen("123.mp4", "r");

	/* 讀檔encode並傳檔 */
	while (!feof(fp)) {			
		numbytes = fread(data_in, sizeof(char), block_size, fp);

		if (numbytes == 0)
			break;

		printf("\nfread %d bytes\n", numbytes);

		kodoc_set_const_symbols(encoder, data_in, block_size);

		while (1) {

			if (ack) {
				swap (&control_num, &swap_num);
				pthread_cond_wait(&alternate, &mutex);
				break;
			}

			else {
				bytes_used = kodoc_write_payload(encoder, payload);
				memset (control, control_num, 1);
				printf("Payload generated by encoder, rank = %d, bytes used = %d\n", kodoc_rank(encoder), bytes_used);

				if (sendto(sockfd, control, payload_size + 1, 0, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
					error("Error:write payload failed");
			}
		} /* end of while (1) */
	} /* end of while (!feof(fp)) */

	pthread_mutex_unlock(&mutex);

	printf("\nblock_size:%d\n", block_size);
	printf("payload_size:%d\n\n", payload_size);

	printf("client finished\n");

	free(data_in);
	free(control);
	kodoc_delete_coder(encoder);
	kodoc_delete_factory(encoder_factory);
	close(sockfd);
	return 0;
}
