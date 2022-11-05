#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include "main_write_header_cb.h"
#include "catpng.h"
#include <pthread.h>
#define IMG_URL_paster "http://ece252-3.uwaterloo.ca:2520/image?img=2"
#define DUM_URL "https://example.com/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
int images_received[50];
pthread_mutex_t lock;
char **allFiles;

typedef struct thread_arguments {
    pthread_t threadID;
    char threadUrl[256];
} thread_arguments;

int checkArray(int arr[], int size){
    for(int i =0; i <size;i++ ){
        if (arr[i] ==0){
            return 1;
        }
    }
    return 0;
}

int doCurl(char threadURL[]){
    CURL *curl_handle;
    CURLcode res;
    char url[256];
    RECV_BUF recv_buf;
    recv_buf_init(&recv_buf, BUF_SIZE);
    strcpy(url, threadURL); 

    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return 1;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    
    /* get it! */
    res = curl_easy_perform(curl_handle);

    if( res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        printf("putting memory: %p, at [%d]\n", recv_buf.buf, recv_buf.seq);

        // pthread_mutex_lock(&lock);
        // if (images_received[recv_buf.seq] == 0){
        // memcpy(allFiles[recv_buf.seq], recv_buf.buf, recv_buf.size);
        }
        // pthread_mutex_unlock(&lock);
    /* cleaning up */
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    recv_buf_cleanup(&recv_buf);
    return recv_buf.seq;
}

int concatenate(int num_images){
    char *imgBuf = allFiles[0];
    U8 * signature= (U8 *)malloc(8);
    U8 * IHDRChunk= (U8 *)malloc(25);
    memset(signature, 0, 8);
    memset(IHDRChunk, 0, 25);
    for(int i =0; i< 8; i++){
        signature[i] = imgBuf[i];
    }
    for(int i =0; i< 25; i++){
        IHDRChunk[i] = imgBuf[i+8];
    }
    allPNG = fopen("all.png","w");
    if(allPNG == NULL){
        perror("Failed: ");
    }
    fwrite(signature, 1 , 8 , allPNG );
    fwrite(IHDRChunk, 1 , 25 , allPNG );
    deflatedLength = htonl(deflatedLength); 
    fwrite(&deflatedLength, 1, sizeof(deflatedLength), allPNG);
    U8 IDAT_type[4] = { 0x49, 0x44, 0x41, 0x54};
    fwrite(IDAT_type, 1, sizeof(IDAT_type), allPNG);    
    fclose(allPNG);
    free(IHDRChunk);
    free(signature);
    for(int i =0 ; i<num_images; i++){
            totalHeight+= is_png2(allFiles[i], 50);
            getIDAT(allFiles[i]);
            
    }
    deflateData();
    U32 len_IEND = (U32) 0 ;
    len_IEND = htonl(len_IEND);
    allPNG = fopen("all.png","a");
    fwrite(&len_IEND, 1, sizeof(len_IEND), allPNG);
    U8 IEND_type[4] = { 0x49, 0x45, 0x4E, 0x44};
    fwrite(IEND_type, 1, sizeof(IEND_type), allPNG);
    U32 crc_val_IEND = 0;
    crc_val_IEND = crc(IEND_type, 4);
    U8 IEND_CRC[4];
    IEND_CRC[0] = (crc_val_IEND >> 24) & 0xFF;
    IEND_CRC[1] = (crc_val_IEND >> 16) & 0xFF;
    IEND_CRC[2] = (crc_val_IEND >> 8) & 0xFF;
    IEND_CRC[3] = crc_val_IEND & 0xFF;
    fwrite(IEND_CRC, 1, 4, allPNG);
    fclose(allPNG);
    return 0;
}

void *myThreadFun(void *vargp)
{
    thread_arguments *myStruct = (thread_arguments *)vargp;
    while(checkArray(images_received, 50) != 0){
    int sequence = doCurl(myStruct->threadUrl);
    pthread_mutex_lock(&lock);
    images_received[sequence] = 1;
    pthread_mutex_unlock(&lock);
    }
    return NULL;
}
int main(int argc, char **argv)
{
    int numThreads = 1;
    int imageNum = 1;
    int g;
    char *str = "option requires an argument";
    while ((g = getopt(argc, argv, "t:n:")) != -1) {
        switch (g) {
        case 't':
	    numThreads = strtoul(optarg, NULL, 10);
	    printf("option -t specifies a value of %d.\n", numThreads);
	    if (numThreads <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'n':
            imageNum = strtoul(optarg, NULL, 10);
	    printf("option -n specifies a value of %d.\n", imageNum);
            if (imageNum <= 0 || imageNum > 3) {
                fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
                return -1;
            }
            break;
        default:
            return -1;
        }
    }
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
    allFiles = malloc(50 * sizeof(char *));
    for (int i = 0; i < 50; ++i) {
    allFiles[i] = (char *)malloc(BUF_SIZE);
    }
    printf("number of threads: %d, image number : %d\n", numThreads, imageNum);
    memset(images_received, 0, sizeof(images_received));
    pthread_t tid[numThreads];    
    char setthreadURL[] = IMG_URL_paster;
    char image_num_url = imageNum+'0';
    setthreadURL[44] = image_num_url;
    printf("URL is:  %s \n ", setthreadURL);
    for(int i=0; i < numThreads; ++i){
        thread_arguments thread_args;
        int URLindex = (i%3) + 1; 
        char c = URLindex+'0';
        setthreadURL[14] = c;
        thread_args.threadID = tid[i]; 
        strcpy(thread_args.threadUrl, setthreadURL);
        pthread_create(&tid[i], NULL, myThreadFun, (void *)&thread_args);
    }
    for(int j=0; j < numThreads; ++j){
    pthread_join(tid[j], NULL);
    }
    pthread_mutex_destroy(&lock);
    concatenate(50);
    for (int i = 0; i < 50; ++i) {
        free(allFiles[i]);
        }
    free(allFiles);
}