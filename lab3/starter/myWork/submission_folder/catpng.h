#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include "crc.h"      /* for crc()                   */
#include "zutil.h"    /* for mem_def() and mem_inf() */
#include "lab_png.h"  /* simple PNG data structures  */
#include <errno.h>    /* for errno                   */

typedef unsigned long int U64;
FILE * allPNG;
U32 deflatedLength = 0;
long totalHeight = 0;
long totalInflatedLength = 0;
typedef unsigned char U8;
typedef unsigned int  U32;
U8 gp_buf_def[256*32];

int is_png2(char *imgBuf, size_t n);
int deflateData();
int getIDAT(char *imgBuf);

int deflateData(){
    U8 * beforeIDAT = (U8 *)malloc(41);
    allPNG = fopen("all.png","r");
    fread(beforeIDAT, 1, 41 , allPNG);
    U8 *p_buffer_def = NULL;
    U64 len_def = 0;
    int ret_def = 0;
    U8 * gp_buf_def_real_def = NULL;
    gp_buf_def_real_def = malloc(totalInflatedLength);
    memset(gp_buf_def_real_def, 0, totalInflatedLength );
    p_buffer_def = malloc(totalInflatedLength);
    memset(p_buffer_def, 0, totalInflatedLength );
    if (p_buffer_def == NULL) {
        perror("malloc");
	return errno;
    }
    fread(p_buffer_def, 1, totalInflatedLength , allPNG);

    fclose(allPNG);
    ret_def = mem_def(gp_buf_def_real_def, &len_def, p_buffer_def, totalInflatedLength, Z_DEFAULT_COMPRESSION);
    if (ret_def == 0) { 
        } else { 
            fprintf(stderr,"mem_def failed. ret = %d.\n", ret_def);
            return ret_def;
        }
    allPNG = fopen("all.png","wb+");
    
    /* sets IHDR new height */
    U8 IHDR_new_height[4];
    IHDR_new_height[0] = (totalHeight >> 24) & 0xFF;
    IHDR_new_height[1] = (totalHeight >> 16) & 0xFF;
    IHDR_new_height[2] = (totalHeight >> 8) & 0xFF;
    IHDR_new_height[3] = totalHeight & 0xFF;
    beforeIDAT[20] = IHDR_new_height[0];
    beforeIDAT[21] = IHDR_new_height[1];
    beforeIDAT[22] = IHDR_new_height[2];
    beforeIDAT[23] = IHDR_new_height[3];
    /* correctly sets length field of IDAT chunk */
    U32 length = (U32) len_def;
    U8 byteLength[4];
    byteLength[0] = (length >> 24) & 0xFF;
    byteLength[1] = (length >> 16) & 0xFF;
    byteLength[2] = (length >> 8) & 0xFF;
    byteLength[3] = length & 0xFF;
    beforeIDAT[33] = byteLength[0];
    beforeIDAT[34] = byteLength[1];
    beforeIDAT[35] = byteLength[2];
    beforeIDAT[36] = byteLength[3];

    /* compute IHDR CRC ,based on IHDR type and data field */
    U32 crc_val_IHDR = 0;
    U8 IHDR_TYPE_DATA[4+13];
    int counter = 0;
    for(int i =12; i< 29; i++){
        IHDR_TYPE_DATA[counter] = beforeIDAT[i];
        counter+=1;
    }
    crc_val_IHDR = crc(IHDR_TYPE_DATA, 17);
    U8 IHDR_CRC[4];
    IHDR_CRC[0] = (crc_val_IHDR >> 24) & 0xFF;
    IHDR_CRC[1] = (crc_val_IHDR >> 16) & 0xFF;
    IHDR_CRC[2] = (crc_val_IHDR >> 8) & 0xFF;
    IHDR_CRC[3] = crc_val_IHDR & 0xFF;
    beforeIDAT[29] = IHDR_CRC[0];
    beforeIDAT[30] = IHDR_CRC[1];
    beforeIDAT[31] = IHDR_CRC[2];
    beforeIDAT[32] = IHDR_CRC[3];
    /*compute IDAT CRC ,based on IDAT type and data field */
    U32 crc_val_idat = 0;
    U8 IDAT_TYPE_DATA[4+len_def];
    int counter_idat = 0;
    /* copy type into idat CRC buffer */
    for(int i=37; i< 41 ; i++){
        IDAT_TYPE_DATA[counter_idat] = beforeIDAT[i];
        counter_idat+=1;
    }
    int dataCounter = 0;
    for(int j=41; j< (41+ len_def) ; j++){
        IDAT_TYPE_DATA[counter_idat] = gp_buf_def_real_def[dataCounter];
        dataCounter+=1;
        counter_idat+=1;
    }
    crc_val_idat = crc(IDAT_TYPE_DATA, 4+len_def);
    U8 IDAT_CRC[4];
    IDAT_CRC[0] = (crc_val_idat >> 24) & 0xFF;
    IDAT_CRC[1] = (crc_val_idat >> 16) & 0xFF;
    IDAT_CRC[2] = (crc_val_idat >> 8) & 0xFF;
    IDAT_CRC[3] = crc_val_idat & 0xFF;
    fwrite(beforeIDAT,1, 41, allPNG); /* sets all bytes before new IDAT */
    fwrite(gp_buf_def_real_def, 1, len_def, allPNG); /* sets new deflated IDAT */
    fwrite(IDAT_CRC, 1, 4, allPNG);
    fclose(allPNG);
    free(beforeIDAT);
    free(gp_buf_def_real_def);
    free(p_buffer_def);
    return 0;
}

int getIDAT(char *imgBuf){
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
    allPNG = fopen("all.png","a");
    if(allPNG == NULL){
        perror("Failed: ");
    }
    fwrite(gp_buf_def_real, 1 , len_inf , allPNG );
    fclose(allPNG);
    free(p_buffer);
    free(gp_buf_def_real);
    free(actualLength);
    return 0;
}

int is_png2(char *imgBuf, size_t n){
    U8 * signature= (U8 *)malloc(n);
    /*check signature */

    memset(signature, 0, n);

    for(int i =0; i< 8; i++){
        signature[i] = imgBuf[i];
    }

    int secondByte = signature[1] == 0x50;
    int thirdByte = signature[2] == 0x4E;
    int fourthByte = signature[3] == 0x47;
    if(secondByte && thirdByte && fourthByte){


        free(signature);
        U32 * IHDR= (U32 *)malloc(n);
        memset(IHDR, 0, n);
        U32 height = imgBuf[23];
        free(IHDR);
        return height;
    }
    else{
        printf("invalid PNG \n");
        free(signature);
        return 0;
    }
};