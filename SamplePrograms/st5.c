/*
    Do a lot of Puts reserved for rank 0.  
    Works best when we do NOT use priority for Reserve.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "adlb.h"

#define MASTER_RANK 0

#define TYPE_A                1
#define TYPE_B                2
#define TYPE_C                3
#define TYPE_C_ANSWER         4
#define TYPE_HANDLED_B_AND_Cs 5

#define NUM_CS_PER_B          4

#define SRVR_MAX_MALLOC_AMT  (1024 * 1024 * 1024)

#define TAG_END_DATA         99

int num_types = 5;
int type_vect[5] = { TYPE_A, TYPE_B, TYPE_C, TYPE_C_ANSWER, TYPE_HANDLED_B_AND_Cs };

double start_time, end_time;

int main(int argc, char *argv[])
{
    int i, j, rc, provided, use_exhaustion;
    int num_slaves, num_world_nodes, my_world_rank;
    int num_As_gend, num_As_to_gen_per_rank, num_As_to_gen_per_put;
    int req_types[4], work_type, answer_rank, work_len, num_iters_for_fake_work;
    int work_prio, work_handle[ADLB_HANDLE_SIZE], num_C_answers, num_C_answers_total;
    int work_A[20], work_C[20];
    int total_num_puts, total_num_reserves, total_num_gets;
    double tempval;
    double adlb_dbls_args[ADLB_NUM_INIT_ARGS];
    double time1, total_put_time, total_reserve_time, total_get_time;

    num_iters_for_fake_work = 1000000;
    use_exhaustion = 0;
    num_C_answers = 0;
    num_C_answers_total = 0;
    num_As_gend   = 0;
    num_As_to_gen_per_put  =  4;
    num_As_to_gen_per_rank = 10;  // not nec a multiple of num_As_to_gen_per_put
    for (i=1; i < argc; i++)
    {
        if (strcmp(argv[i],"-nas") == 0)
            num_As_to_gen_per_rank = atoi(argv[i+1]);
        else if (strcmp(argv[i],"-niters") == 0)
            num_iters_for_fake_work = atoi(argv[i+1]);
        else if (strcmp(argv[i],"-exhaust") == 0)
            use_exhaustion = 1;
    }
    rc = MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
    MPI_Comm_size(MPI_COMM_WORLD,&num_world_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);

    if (num_world_nodes < 2)
    {
        dbgprintf(1,"**** TOO FEW RANKS - pgm requires at least a master and one slave\n");
        ADLB_Abort(-1);
    }

    adlb_dbls_args[0] = (double) SRVR_MAX_MALLOC_AMT;
    adlb_dbls_args[1] = (double) 1.0;  // dbgprintf_flag
    adlb_dbls_args[2] = (double) 0.0;  // use_prio_for_reserve_flag
    adlb_dbls_args[3] = (double) num_types;
    for (i=0; i < num_types; i++)
        adlb_dbls_args[i+4] = (double) type_vect[i];
    rc = ADLB_Init(adlb_dbls_args);
    num_slaves = num_world_nodes - 1;  // all but master_rank

    total_put_time = total_reserve_time = total_get_time = 0.0;
    total_num_puts = total_num_reserves = total_num_gets = 0;
    rc = MPI_Barrier(MPI_COMM_WORLD);
    start_time = MPI_Wtime();

    if (my_world_rank == MASTER_RANK)  /* if master */
    {
        dbgprintf(1,"STAT nums to handle: As %d  Bs %d  Cs %d  total %d\n",
                  ( num_slaves * num_As_to_gen_per_rank ),
                  ( num_slaves * num_As_to_gen_per_rank ),  // one B per A
                  ( num_slaves * num_As_to_gen_per_rank * NUM_CS_PER_B),
                  ( num_slaves * num_As_to_gen_per_rank * 2 +            // As + Bs
                    num_slaves * num_As_to_gen_per_rank * NUM_CS_PER_B)  //    + Cs
                 );
        time1 = MPI_Wtime();
        tempval = (double)work_A[0];
        for (j=0; j < num_iters_for_fake_work; j++)
            tempval = sqrt(tempval + 5000000.0) + 1;
        dbgprintf(1,"STAT fake work time for %d iters is %f\n",
                  num_iters_for_fake_work,MPI_Wtime()-time1);

        for (i=0; i < (num_As_to_gen_per_rank * num_slaves); i++)
        {
            req_types[0] = TYPE_HANDLED_B_AND_Cs;
            req_types[1] = req_types[2] = req_types[3] = -1;
            time1 = MPI_Wtime();
            rc = ADLB_Reserve(req_types,&work_type,&work_prio,work_handle,
                              &work_len,&answer_rank);
            total_reserve_time += MPI_Wtime() - time1;
            total_num_reserves++;
            if (rc < 0)
            {
                dbgprintf(1,"**** STAT  RESERVE  NEGATIVE rc %d\n",rc);
                if (use_exhaustion)
                    break;
                else
                   ADLB_Abort(-1);
            }
            time1 = MPI_Wtime();
            rc = ADLB_Get_reserved(work_A,work_handle);
            total_get_time += MPI_Wtime() - time1;
            total_num_gets++;
            if (rc < 0)
            {
                dbgprintf(1,"**** STAT  RESERVE  NEGATIVE rc %d\n",rc);
                if (use_exhaustion)
                    break;
                else
                   ADLB_Abort(-1);
            }
            num_C_answers_total += work_A[0];
            dbgprintf(1,"HANDLED b_and_cs %d of %d ; c_answers %d of %d\n",
                      i+1,
                      ( num_slaves * num_As_to_gen_per_rank ),
                      num_C_answers_total,
                      ( num_slaves * num_As_to_gen_per_rank * NUM_CS_PER_B) );
        }
        dbgprintf(1,"STAT total Cs handled %d; should be %d\n",
                  num_C_answers_total,
                  ( num_slaves * num_As_to_gen_per_rank * NUM_CS_PER_B) );
        dbgprintf(1,"********** SETTING NO MORE WORK ***********************************\n");
        ADLB_Set_problem_done();
        dbgprintf(1,"********** done\n");
    }
    else  /* slave */
    {
        while (num_As_gend < num_As_to_gen_per_rank)
        {
            num_As_gend++;
            for (i=0; i < NUM_CS_PER_B; i++)
            {
                tempval = (double)work_C[0];
                for (j=0; j < num_iters_for_fake_work; j++)
                    tempval = sqrt(tempval + 5000000.0) + 1;
                num_C_answers = 4;
                time1 = MPI_Wtime();
                rc = ADLB_Put(&num_C_answers,sizeof(int),0,0,TYPE_HANDLED_B_AND_Cs,0); 
                total_put_time += MPI_Wtime() - time1;
                total_num_puts++;
                // dbgprintf(1,"PUT %d \n",i);
            }
        }
    }  /* slave */

    if (my_world_rank == MASTER_RANK)  /* if master */
    {
        end_time = MPI_Wtime();
        dbgprintf(1,"STAT total time = %10.2f\n",end_time-start_time);
    }
    else
    {
        dbgprintf(1,"total put time %14.6f  numputs %d  avg %14.6f\n",
                  total_put_time,total_num_puts,total_put_time/total_num_puts);
        dbgprintf(1,"total reserve time %14.6f  numreserves %d  avg %14.6f\n",
                  total_reserve_time,total_num_reserves,
                  total_reserve_time/total_num_reserves);
        dbgprintf(1,"total get time %14.6f  numgets %d  avg %14.6f\n",
                  total_get_time,total_num_gets,total_get_time/total_num_gets);
    }
    dbgprintf(1,"AT ADLB_FINALIZE\n");
    ADLB_Finalize();
    dbgprintf(1,"AT MPI_FINALIZE\n");
    MPI_Finalize();
    printf("DONE\n");

    return 0;
}
