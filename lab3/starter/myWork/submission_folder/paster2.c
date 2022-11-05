#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <curl/curl.h>
#include "main_write_header_cb.h"
#include "catpng.h"
#include <pthread.h>
#include <semaphore.h>

#define producer_img_url "http://ece252-1.uwaterloo.ca:2530/image?img=1&part=0"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define producer_buf_size 10240  /* 1024*1024 = 1M */
#define inflateBUF (10240*32)


typedef struct int_stack
{
    int size;               /* the max capacity of the stack */
    int pos;                /* position of last item pushed onto the stack */
    RECV_BUF *items;             /* stack of stored integers */
} ISTACK;


int sizeof_shm_recv_buf(size_t nbytes)
{
    return (sizeof(RECV_BUF) + sizeof(char) * nbytes);
}
int sizeof_shm_stack(int size)
{
    return (sizeof(ISTACK) + sizeof(RECV_BUF) * size);
}
int init_shm_stack(ISTACK *p, int stack_size) {
    if ( p == NULL || stack_size == 0 ) {
        return 1;
    }

    p->size = stack_size;
    p->pos  = -1;
    p->items = (RECV_BUF *) (p + sizeof(ISTACK));
    return 0;
}
ISTACK *create_stack(int size)
{
    int mem_size = 0;
    ISTACK *pstack = NULL;
    
    if ( size == 0 ) {
        return NULL;
    }

    mem_size = sizeof_shm_stack(size);
    pstack = malloc(mem_size);

    if ( pstack == NULL ) {
        perror("malloc");
    } else {
        char *p = (char *)pstack;
        pstack->items = (RECV_BUF *) (p + sizeof(ISTACK));
        pstack->size = size;
        pstack->pos  = -1;
    }

    return pstack;
}

void destroy_stack(ISTACK *p)
{
    if ( p != NULL ) {
        free(p);
    }
}
int is_full(ISTACK *p)
{
    if ( p == NULL ) {
        return 0;
    }
    return ( p->pos == (p->size -1) );
}
int is_empty(ISTACK *p)
{
    if ( p == NULL ) {
        return 0;
    }
    return ( p->pos == -1 );
}

int push(ISTACK *p, RECV_BUF item)
{
    if ( p == NULL ) {
        return -1;
    }

    if ( !is_full(p) ) {
        ++(p->pos);
        p->items[p->pos] = item;
        return 0;
    } else {
        return -1;
    }
}
int pop(ISTACK *p, RECV_BUF *p_item)
{
    if ( p == NULL ) {
        return -1;
    }

    if ( !is_empty(p) ) {
        *p_item = p->items[p->pos];
        (p->pos)--;
        return 0;
    } else {
        return 1;
    }
}

int doCurl(char threadURL[], int shared_mem_id, int sharedMemSize_buffers){
    ISTACK *stackPTR= shmat(shared_mem_id, NULL, 0); //attach to shared stack
    CURL *curl_handle;
    RECV_BUF *p_shm_recv_buf;
    p_shm_recv_buf = shmat(sharedMemSize_buffers, NULL, 0);
    shm_recv_buf_init(p_shm_recv_buf, producer_buf_size);
    CURLcode res;
    int seqNum;
    char url[256];
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
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl); 
    /* user defined data structure passed to the call back function */

    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)p_shm_recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)p_shm_recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    /* get it! */ 
    res = curl_easy_perform(curl_handle);

    if( res != CURLE_OK) {

        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {

        seqNum = p_shm_recv_buf->seq;
        // char fname[256];
        // sprintf(fname, "./in_curl_%d.png", p_shm_recv_buf->seq);
        // write_file(fname, p_shm_recv_buf->buf, p_shm_recv_buf->size);
        //  printf("%lu bytes received in memory %p, seq=%d.\n",  \
        //            p_shm_recv_buf->size, p_shm_recv_buf->buf, p_shm_recv_buf->seq);
        push(stackPTR, *p_shm_recv_buf);

        }
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();
    shmdt(stackPTR);
    shmdt(p_shm_recv_buf);

    return seqNum;
}

int compute_height(char *imgBuf){
    U32 * IHDR= (U32 *)malloc(4);
    memset(IHDR, 0, 4);
    U32 height = imgBuf[23];
    free(IHDR);
    return height;
}
int inflateIDAT(char *imgBuf, U8 * destination, U64* slotSize){
    U32 * actualLength= (U32 *)malloc(sizeof(U32));
    memcpy ( actualLength ,imgBuf+33 ,4 );
    *actualLength = ntohl(*actualLength) * 320;
    U8 * gp_buf_def_real = NULL;
    gp_buf_def_real = (U8 *) malloc(*actualLength); /*problem line*/
    memset(gp_buf_def_real, 0, *actualLength);
    U8 *p_buffer = NULL;
    int ret = 0;
    U64 len_inf = 0;
    p_buffer = ( U8 *) malloc(*actualLength);
    if (p_buffer == NULL) {
        perror("malloc");
	return errno;
    }
    memset(p_buffer, 0, *actualLength);
    memcpy(p_buffer, imgBuf+41 , (*actualLength )/ 320);
    ret = mem_inf(gp_buf_def_real, &len_inf, p_buffer, (*actualLength )/ 320);
    if (ret == 0) { /* success */
            totalInflatedLength += len_inf;
        } else { /* failure */
            fprintf(stderr,"mem_def failed. ret = %d.\n", ret);
            return ret;
        }
    // allPNG = fopen("all.png","a");
    // if(allPNG == NULL){
    //     perror("Failed: ");
    // }

    // fwrite(gp_buf_def_real, 1 , len_inf , allPNG );
    // printf("copying inflated data \n");
    memcpy(destination, gp_buf_def_real, len_inf);
    memcpy(slotSize, &len_inf, sizeof(U64));
    // fclose(allPNG);
    free(p_buffer);
    free(gp_buf_def_real);
    free(actualLength);
    return 0;
}

int producer(int imgNum, int n, int shared_mem_id, int img_num_shared, sem_t * sem_id, sem_t* empty_stack, sem_t* full_stack, int sharedMemSize_buffers)
{    
    int seq_num;
    int machineNum;
    char urlBuffer[60];
    int imgPartNum;
    while (1)
    {
            memset(&imgPartNum, 0, sizeof(int));
            void* checkImgNum = shmat(img_num_shared, NULL, 0);
            memcpy(&imgPartNum, checkImgNum, sizeof(int));
            shmdt(checkImgNum);
            if(imgPartNum >= 50){
                printf("producer %d exited \n", n);
                sem_post(sem_id);
                //producer exits
                return 0;
            }
            sem_wait(sem_id); //crit section
            printf("producer %d entering critical section\n", n);
            memset(&imgPartNum, 0, sizeof(int));
            checkImgNum = shmat(img_num_shared, NULL, 0);
            memcpy(&imgPartNum, checkImgNum, sizeof(int));
            shmdt(checkImgNum);
            
            if(imgPartNum >= 50){
                //producer exits
                printf("producer %d exited \n", n);
                sem_post(sem_id);
                return 0;
            }
            ISTACK *stackPTR= shmat(shared_mem_id, NULL, 0);
            if(is_full(stackPTR)){
                shmdt(stackPTR);
                sem_post(sem_id);
                printf("producer %d stuck before full_stack semaphore (expected) \n", n);
                sem_wait(full_stack);
            }
            else{
                machineNum = (imgPartNum%3) + 1;
                sprintf(urlBuffer, "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d",machineNum, imgNum, imgPartNum);
                printf("new string is %s \n", urlBuffer);
                seq_num = doCurl(urlBuffer, shared_mem_id, sharedMemSize_buffers);
                imgPartNum = seq_num + 1;
                void* sharedMemProducersWrite = shmat(img_num_shared, NULL, 0);
                memcpy(sharedMemProducersWrite, &imgPartNum, sizeof(int));
                shmdt(sharedMemProducersWrite);
                //critical section should end here
                sem_post(sem_id);
                sem_post(empty_stack);
            }
            printf("producer %d leaving critical section\n", n);

    }
    

    // while (1) {
    //     //if we have all 50 images , shouldnt even wait on semaphore
    //     memset(&imgPartNum, 0, sizeof(int));
    //     void* checkImgNum = shmat(img_num_shared, NULL, 0);
    //     memcpy(&imgPartNum, checkImgNum, sizeof(int));
    //     shmdt(checkImgNum);
    //     if (imgPartNum >= 50){
    //         sem_post(empty_stack);
    //         sem_post(sem_id);
    //         return 0;
    //     }
    //     // printf("producer %d waiting on sem_id \n", n);
    //     sem_wait(sem_id);
    //     ISTACK *stackPTR= shmat(shared_mem_id, NULL, 0); //attach to shared stack
    //     if(is_full(stackPTR)){
    //         // printf("buffer is full, just waiting  on full stack sem \n");
    //         shmdt(stackPTR);
    //         if (imgPartNum >= 50){
    //         sem_post(sem_id);
    //         sem_post(empty_stack);
    //         // printf("producer done all work and buffer full, terminating now \n");
    //         return 0;
    //         }
    //         sem_post(sem_id);
    //         sem_post(empty_stack);
    //         // printf("producer %d waiting on full_stack \n", n);
    //         sem_wait(full_stack);
    //     }
    //     else{
    //     shmdt(stackPTR);
    //     // printf("producer ID=%d, working\n", n);
    //     memset(&imgPartNum, 0, sizeof(int));
    //     void* sharedMemProducers = shmat(img_num_shared, NULL, 0);
    //     memcpy(&imgPartNum, sharedMemProducers, sizeof(int));
    //     shmdt(sharedMemProducers);
    //     if (imgPartNum >= 50){
    //         sem_post(sem_id);
    //         sem_post(empty_stack);
    //         return 0;
    //     }
    //     // printf("process: %d, is fetching img segment: %d\n", n, imgPartNum);
    //     machineNum = (imgPartNum%3) + 1;
    //     sprintf(urlBuffer, "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d",machineNum, imgNum, imgPartNum);
    //     printf("new string is %s \n", urlBuffer);
    //     seq_num = doCurl(urlBuffer, shared_mem_id, sharedMemSize_buffers);
    //     imgPartNum = seq_num + 1;
    //     void* sharedMemProducersWrite = shmat(img_num_shared, NULL, 0);
    //     memcpy(sharedMemProducersWrite, &imgPartNum, sizeof(int));
    //     shmdt(sharedMemProducersWrite);
    //     //critical section should end here
    //     sem_post(sem_id);
    //     sem_post(empty_stack);
    //     }
    //     // usleep(2000); //this should basically ensure process switching, just using for testing
    // }
    return 0;
}

int consumer(int n, int shared_mem_id, sem_t* empty_stack, sem_t* full_stack , int sharedMemSize_buffers, int allFilesID[], int slot_ids[], int item_sizes[], int sleepTime, sem_t * sem_id, int singleImg, int consumerCountID, int consumerHeightID, int inflatedSlotSizesID[], int prodDoneID)
{
    int count;
    int consumerHeight;

    // int prodExit;
    while (1)
    {
        memset(&count, 0, sizeof(int));
        void* countCheck = shmat(consumerCountID, NULL, 0);
        memcpy(&count, countCheck, sizeof(int));
        shmdt(countCheck);
        if (count >= 49){   
                sem_post(sem_id);
                printf("consumer %d done, terminating now \n", n);
                return 0;
            }
        sem_wait(sem_id); //crit section
        printf("consumer %d entering critical section\n", n);
        memset(&count, 0, sizeof(int));
        countCheck = shmat(consumerCountID, NULL, 0);
        memcpy(&count, countCheck, sizeof(int));
        shmdt(countCheck);
        if (count >= 49){   
                sem_post(sem_id);
                printf("consumer %d done, terminating now \n", n);
                return 0;
            }

        ISTACK *stackPTR= shmat(shared_mem_id, NULL, 0); //attach to shared stack
        if(is_empty(stackPTR)){
            shmdt(stackPTR);
            sem_post(sem_id);
            sem_wait(empty_stack);
    }
    else{
    RECV_BUF *item;
    item = shmat(sharedMemSize_buffers, NULL, 0);
    shm_recv_buf_init(item, producer_buf_size);
    pop(stackPTR, item);
    if(count == 0){ //copying single file so I can steal signature/ihdr header from it
        char * oneFile = shmat(singleImg, NULL, 0);
        memcpy(oneFile, item->buf, item->size);
        shmdt(oneFile);
    }
    memset(&consumerHeight, 0, sizeof(int));
    void* consHeightShared = shmat(consumerHeightID, NULL, 0);
    memcpy(&consumerHeight, consHeightShared, sizeof(int));
    shmdt(consHeightShared);

    U8 * destination = shmat(allFilesID[item->seq], NULL, 0);
    consumerHeight+=compute_height(item->buf);
    consHeightShared = shmat(consumerHeightID, NULL, 0);
    memcpy(consHeightShared, &consumerHeight, sizeof(int));
    shmdt(consHeightShared);
    U64 *slotSize = shmat(inflatedSlotSizesID[item->seq], NULL, 0);
    inflateIDAT(item->buf, destination, slotSize);
    shmdt(destination);
    shmdt(slotSize);
    count++;
    countCheck = shmat(consumerCountID, NULL, 0);
    memcpy(countCheck, &count, sizeof(int));
    shmdt(countCheck);
    shmdt(stackPTR);
    shmdt(item);
    sem_post(sem_id);
    sem_post(full_stack);
    usleep(sleepTime);
    }
    printf("consumer %d leaving critical section\n", n);

}
    
    // while(1){
    //     //if we have all images, no point waiting on semaphore
    // memset(&count, 0, sizeof(int));
    // void* countCheck = shmat(consumerCountID, NULL, 0);
    // memcpy(&count, countCheck, sizeof(int));
    // shmdt(countCheck);
    // if (count >= 49){   
    //     sem_post(sem_id);
    //     sem_post(full_stack);
    //     sem_post(empty_stack);
    //     printf("consumer done all work, terminating now \n");
    //     return 0;
    // }
    // printf("consumer %d waiting on sem_id \n", n);
    // sem_wait(sem_id);
    // memset(&count, 0, sizeof(int));
    // void* consumerCountShared = shmat(consumerCountID, NULL, 0);
    // memcpy(&count, consumerCountShared, sizeof(int));
    // shmdt(consumerCountShared);
    // ISTACK *stackPTR= shmat(shared_mem_id, NULL, 0); //attach to shared stack
    // if(is_empty(stackPTR)){
    //     // printf("empty stack, we just wait on empty stack sem  \n");
    //     shmdt(stackPTR);
    //     if (count >= 49){
    //         sem_post(sem_id);
    //         sem_post(full_stack);
    //         sem_post(empty_stack);
    //         printf("consumer done all work, terminating now \n");
    //         return 0;
    //         }
    //     sem_post(sem_id);
    //     sem_post(full_stack);
    //     printf("consumer %d waiting on empty stack,and stack ptr is : %d.  also, current count it : %d \n", n,  stackPTR->pos ,count);
    //     sem_wait(empty_stack);
    // }
    // else{
    // memset(&count, 0, sizeof(int));
    // void* countCheckBefore = shmat(consumerCountID, NULL, 0);
    // memcpy(&count, countCheckBefore, sizeof(int));
    // shmdt(countCheckBefore);
    // if (count >= 49){
    //     sem_post(sem_id);
    //     sem_post(full_stack);
    //     sem_post(empty_stack);
    //     printf("consumer done all work, terminating now \n");
    //     return 0;
    // }
    // RECV_BUF *item;
    // item = shmat(sharedMemSize_buffers, NULL, 0);
    // shm_recv_buf_init(item, producer_buf_size);
    // pop(stackPTR, item);
    // if(count == 0){ //copying single file so I can steal signature/ihdr header from it
    //     char * oneFile = shmat(singleImg, NULL, 0);
    //     memcpy(oneFile, item->buf, item->size);
    //     shmdt(oneFile);
    // }
    // memset(&consumerHeight, 0, sizeof(int));
    // void* consHeightShared = shmat(consumerHeightID, NULL, 0);
    // memcpy(&consumerHeight, consHeightShared, sizeof(int));
    // shmdt(consHeightShared);

    // U8 * destination = shmat(allFilesID[item->seq], NULL, 0);
    // consumerHeight+=compute_height(item->buf);
    // void* consHeightUpdated = shmat(consumerHeightID, NULL, 0);
    // memcpy(consHeightUpdated, &consumerHeight, sizeof(int));
    // shmdt(consHeightUpdated);
    // U64 *slotSize = shmat(inflatedSlotSizesID[item->seq], NULL, 0);
    // inflateIDAT(item->buf, destination, slotSize);
    // shmdt(destination);
    // shmdt(slotSize);
    // count = count+1;
    // void* consumerCountShared = shmat(consumerCountID, NULL, 0);
    // memcpy(consumerCountShared, &count, sizeof(int));
    // shmdt(consumerCountShared);
    // shmdt(stackPTR);
    // shmdt(item);
    // sem_post(sem_id);
    // sem_post(full_stack);
    // usleep(sleepTime);
    // if (count >=49)
    // {
    //     sem_post(sem_id);
    //     sem_post(full_stack);
    //     sem_post(empty_stack);

    // printf("consumer done pulling images , terminating\n");
    // return 0;
    // }
    // }
// }
}

int writeSignature(char * imgBuf){
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
    return 0;
}

int main(int argc, char **argv){
    if (argc<6){
        printf("missing args\n");
        return -1;
    } 
    int bufferSize = atoi(argv[1]); //buffer size, can this be a double ?
    int producerCount = atoi(argv[2]); //number of producers
    int consumerCount = atoi(argv[3]); //number of consumers
    int sleepTime = atof(argv[4]); //number of milliseconds that a consumer sleeps
    int imageNum = atoi(argv[5]); //image number
    printf("running with following args: B: %d, P: %d, C: %d, X: %d, N:%d \n", bufferSize, producerCount, consumerCount, sleepTime, imageNum);
   
   
   //sharing mem btwn parent and children
    int sharedMemSize = sizeof_shm_stack(bufferSize); //get size of stack
    int sharedMemSize_buffers = sizeof_shm_recv_buf(50 * producer_buf_size);
    
    int shmid_buffs = shmget(IPC_PRIVATE, 50 * producer_buf_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    int shared_mem_id = shmget( IPC_PRIVATE, sharedMemSize , IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR ); //allocate shared mem for stack
    int singleImg = shmget(IPC_PRIVATE, producer_buf_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

    //initialize global data
    int allFilesID[50]; 
    int inflatedSlotSizesID[50];

    int slot_ids[50];
    int item_sizes[50];
    for (int i = 0; i < 50; ++i) {
        inflatedSlotSizesID[i] = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);// to hold image size 
        allFilesID[i] = shmget(IPC_PRIVATE, inflateBUF , IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR );
        slot_ids[i] = shmget(IPC_PRIVATE, producer_buf_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
        item_sizes[i] = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);// to hold image size 
    }
    // shmdt(allFiles);
    

    ISTACK *shmemPtr = shmat(shared_mem_id, NULL, 0); //point the stack to the shared mem
    init_shm_stack(shmemPtr, bufferSize ); //intiialize the stack
    shmemPtr = create_stack(bufferSize); //create the stack
    shmdt(shmemPtr);
    // need to create shared mem for the receive buffers as well 
    pid_t pid=0;
    pid_t child_producers_ids[producerCount];
    pid_t child_consumers_ids[consumerCount];
    int state_producer;
    int state_consumer;
    
    //get height in consumer
    int consumerHeightID =   shmget( IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666 );
    void*  consHeight = shmat(consumerHeightID, NULL, 0);
    memset(consHeight, 0, sizeof(int));
    shmdt( consHeight );

    // prod done flag
    int prodDoneID =   shmget( IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666 );
    void*  prodDone = shmat(prodDoneID, NULL, 0);
    memset(prodDone, 0, sizeof(int));
    shmdt( prodDone );

    //sharing consumer count 
    int consumerCountID =   shmget( IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666 );
    void*  sharedConsumerCount = shmat(consumerCountID, NULL, 0);
    memset(sharedConsumerCount, 0, sizeof(int));
    shmdt( sharedConsumerCount );


    //sharing current image part to download (producers)
    int img_num_shared = shmget( IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666 );
    void*  sharedProd = shmat(img_num_shared, NULL, 0);
    memset(sharedProd, 0, sizeof(int));
    shmdt( sharedProd );

    // pthread_mutexattr_t mutexAttr;
    // int mutexID = shmget(IPC_PRIVATE, sizeof(mutex), IPC_CREAT | 0666);
    // &mutex

    sem_t *sem1;
    sem_t* empty_stack;
    sem_t* full_stack;

    
    sem1 = sem_open("/semaphore_prod3", O_CREAT,  0644, 1);
    empty_stack = sem_open("/sem_empty_stack3", O_CREAT,  0644, 0);
    full_stack = sem_open("/sem_full_stack3", O_CREAT,  0644, 0);
     sem_unlink("/semaphore_prod3"); //basically unlinks semaphore if previous run exited early
    sem_unlink("/sem_empty_stack3");
    sem_unlink("/sem_full_stack3");




    //forking P producers
    for ( int p= 0; p < producerCount; p++) {
        pid = fork();
        if ( pid > 0 ) {        /* parent proc */
            child_producers_ids[p] = pid;
        } else if ( pid == 0 ) { /* child proc */
            producer(imageNum, p, shared_mem_id, img_num_shared, sem1, empty_stack, full_stack, shmid_buffs);
            exit(0);
        } else {
            perror("fork");
            abort();
        }
        
    }
    
    //forking c consumers
    for ( int c= 0; c < consumerCount; c++) {        
        pid = fork();
        if ( pid > 0 ) {        /* parent proc */
            child_consumers_ids[c] = pid;
        } else if ( pid == 0 ) { /* child proc */
            consumer(c, shared_mem_id, empty_stack, full_stack, shmid_buffs, allFilesID, slot_ids, item_sizes, sleepTime, sem1, singleImg, consumerCountID, consumerHeightID, inflatedSlotSizesID, prodDoneID);
            exit(0);
        } else {
            perror("fork");
            abort();
        }
        
    }
    if ( pid > 0) {            /* parent process */
        //collecting producer result
        for ( int p = 0; p < producerCount; p++ ) {
            waitpid(child_producers_ids[p], &state_producer, 0);
            if (WIFEXITED(state_producer)) {
                printf("Child producer cpid[%d]=%d terminated with state: %d.\n", p, child_producers_ids[p], state_producer);
            } 
        }
            int prodDone = 1;
            void* prodDoneMem = shmat(prodDoneID, NULL, 0);
            memcpy(prodDoneMem, &prodDone, sizeof(int));
            shmdt(prodDoneMem);
        for ( int c = 0; c < consumerCount; c++ ) {
            waitpid(child_consumers_ids[c], &state_consumer, 0);
            if (WIFEXITED(state_consumer)) {
                printf("Child consumer cpid[%d]=%d terminated with state: %d.\n", c, child_consumers_ids[c], state_consumer);
            }     
        }




        printf("writing signature \n");
        char * oneFile = shmat(singleImg, NULL, 0);
        writeSignature(oneFile);
        shmdt(oneFile);
        int consumerHeight;
        memset(&consumerHeight, 0, sizeof(int));
        void* consHeightShared = shmat(consumerHeightID, NULL, 0);
        memcpy(&consumerHeight, consHeightShared, sizeof(int));
        totalHeight = consumerHeight;
        shmdt(consHeightShared);
        printf("total height in parent : %ld \n", totalHeight);
        
        // assuming we now have inflated data in array, lets just loop through array, store all the data into all.png, then call deflate, and we should be good I think?
        //just make sure you open file for appending
        allPNG = fopen("all.png","a");

        for(int i = 0; i<50; i++){
            U8 * destination = shmat(allFilesID[i], NULL, 0);
            U64 * slotSize = shmat(inflatedSlotSizesID[i], NULL, 0);
            totalInflatedLength+=*slotSize;
            fwrite(destination, 1, *slotSize, allPNG); // need inflated length for each slot in array so that we know how much to write
            shmdt(destination);
            shmdt(slotSize);

        }
        printf("total inflated Length %ld \n", totalInflatedLength);
        fclose(allPNG);
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

        for (int i = 0; i < 50; ++i) {
        shmctl(inflatedSlotSizesID[i], IPC_RMID, NULL);
        shmctl(allFilesID[i], IPC_RMID, NULL);
        shmctl(slot_ids[i], IPC_RMID, NULL);
        shmctl(item_sizes[i], IPC_RMID, NULL);
    }
        shmctl(shared_mem_id, IPC_RMID, NULL);
        shmctl(shmid_buffs, IPC_RMID, NULL);
        shmctl(img_num_shared, IPC_RMID, NULL );
        shmctl(singleImg, IPC_RMID, NULL );
        shmctl(consumerCountID, IPC_RMID, NULL);
        shmctl(consumerHeightID, IPC_RMID, NULL);
        
    }
    sem_close(sem1);
    sem_close(empty_stack);
    sem_close(full_stack);
    sem_unlink("/sem_empty_stack");
    sem_unlink("/sem_full_stack");


    return 0;
}