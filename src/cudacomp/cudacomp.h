#ifndef _CUDACOMP_H
#define _CUDACOMP_H


#ifdef HAVE_CUDA

#include <cuda_runtime_api.h>
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <device_types.h>
#include <pthread.h>

 #endif

#ifdef HAVE_CUDA

// data passed to each thread
typedef struct
{
    int thread_no;
    long numl0;
    int cindex; // computation index
} THDATA;



/** \brief This structure holds the GPU computation setup
 *
 *  By declaring an array of these structures,
 * several parallel computations can be executed
 *
 */

typedef struct
{
	int init; /// 1 if initialized
	int alloc; /// 1 if memory has been allocated
	long CM_ID;
	long CM_cnt;
	
    int M;
    int N;

    // computer memory (host)
    float *cMat;
    float **cMat_part;
    float *wfsVec;
    float **wfsVec_part;
    float *dmVec;
    float *dmVecTMP;
    float **dmVec_part;

    // GPU memory (device)
    float **d_cMat;
    float **d_wfsVec;
    float **d_dmVec;

    // threads
    THDATA *thdata;
    int *iret;
    pthread_t *threadarray;
    int NBstreams;
    cudaStream_t *stream;
    cublasHandle_t *handle;

    // splitting limits
    long *Nsize;
    long *Noffset;

    int orientation;
    long IDout;

} GPUMATMULTCONF;
#endif




int CUDACOMP_init();

#ifdef HAVE_CUDA
void matrixMulCPU(float *cMat, float *wfsVec, float *dmVec, int M, int N);
int GPUloadCmat(int index);
int GPU_loop_MultMat_setup(int index, char *IDcontrM_name, char *IDwfsim_name, char *IDoutdmmodes_name, long NBGPUs, int orientation);
int GPU_loop_MultMat_execute(int index);
int GPU_loop_MultMat_free(int index);
void *compute_function( void *ptr );
#endif

#endif
