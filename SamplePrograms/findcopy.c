#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "adlb.h"

#define MASTER_RANK 0

#define TYPE_A                91
#define TYPE_B                92
#define TYPE_C                93

#define SRVR_MAX_MALLOC_AMT  (1024 * 1024 * 1024)


int num_types = 5;
int type_vect[5] = { TYPE_A, TYPE_B, TYPE_C };

double start_time, end_time;

int main(int argc, char *argv[])
{
    int i, rc, work_len, provided, use_exhaustion;
    int num_slaves, num_world_nodes, my_world_rank, parent_rank;
    int num_As_gend, num_As_to_gen_per_rank, num_As_to_gen_per_put, b_count;
    int work_prio;
    int work_handle_A[ADLB_HANDLE_SIZE], work_handle_B[ADLB_HANDLE_SIZE],work_handle_C[ADLB_HANDLE_SIZE];
    int num_C_answers, num_C_answers_total;
    int workbuf[20], work_B[10], work_C[20], work_done[20];
    int total_num_puts, total_num_reserves, total_num_gets;
    int num_copies;
    double adlb_dbls_args[ADLB_NUM_INIT_ARGS];
    double time1, total_put_time, total_reserve_time, total_get_time;

    rc = MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
    MPI_Comm_size(MPI_COMM_WORLD,&num_world_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);

    adlb_dbls_args[0] = (double) SRVR_MAX_MALLOC_AMT;
    adlb_dbls_args[1] = (double) 1.0;  // dbgprintf_flag
    adlb_dbls_args[2] = (double) 1.0;  // use_prio_for_reserve_flag
    adlb_dbls_args[3] = (double) num_types;
    for (i=0; i < num_types; i++)
        adlb_dbls_args[i+4] = (double) type_vect[i];
    rc = ADLB_Init(adlb_dbls_args);
    num_slaves = num_world_nodes - 1;  // all but master_rank

    total_put_time = total_reserve_time = total_get_time = 0.0;
    total_num_puts = total_num_reserves = total_num_gets = 0;
    rc = MPI_Barrier(MPI_COMM_WORLD);
    start_time = MPI_Wtime();

    if (my_world_rank == MASTER_RANK)
    {
        for (i=0; i < 20; i++)
            workbuf[i] = 100 + i;
        rc = ADLB_Put(workbuf,20*sizeof(int),-1,my_world_rank,TYPE_A,0); 
        workbuf[0] = 200;
        rc = ADLB_Put(workbuf,20*sizeof(int),-1,my_world_rank,TYPE_B,0); 
        workbuf[0] = 300;
        rc = ADLB_Put(workbuf,20*sizeof(int),-1,my_world_rank,TYPE_C,0); 
    }
    rc = MPI_Barrier(MPI_COMM_WORLD);  // make sure the Put was done
    // if (my_world_rank != MASTER_RANK)
    {
        dbgprintf(1,"DOING FIND\n");
        rc = ADLB_Find(TYPE_A,&work_len,work_handle_A);
        dbgprintf(1,"findA RC %d  len %d  seqno %d  atrank %d\n",
                  rc,work_len,work_handle_A[0],work_handle_A[1]);
        rc = ADLB_Find(TYPE_B,&work_len,work_handle_B);
        dbgprintf(1,"findB RC %d  len %d  seqno %d  atrank %d\n",
                  rc,work_len,work_handle_B[0],work_handle_B[1]);
        rc = ADLB_Find(TYPE_C,&work_len,work_handle_C);
        dbgprintf(1,"findC RC %d  len %d  seqno %d  atrank %d\n",
                  rc,work_len,work_handle_C[0],work_handle_C[1]);
    }
    for (i=0; i < 20; i++)
        workbuf[i] = 0;
    for (i=0; i < 3; i++)
    {
        rc = ADLB_Copy(workbuf,&num_copies,work_handle_B);
        dbgprintf(1,"copyB RC %d numcopies %d work0 %d\n",rc,num_copies,workbuf[0]);
        rc = ADLB_Copy(workbuf,&num_copies,work_handle_A);
        dbgprintf(1,"copyA RC %d numcopies %d work0 %d\n",rc,num_copies,workbuf[0]);
        rc = ADLB_Copy(workbuf,&num_copies,work_handle_C);
        dbgprintf(1,"copyC RC %d numcopies %d work0 %d\n",rc,num_copies,workbuf[0]);
    }

    dbgprintf(1,"AT ADLB_FINALIZE\n");
    ADLB_Finalize();
    dbgprintf(1,"AT MPI_FINALIZE\n");
    MPI_Finalize();
    printf("DONE\n");

    return 0;
}
