/****
    This program does Puts for:
        num_work_units            (default 4)
        work_unit_size            (default 0)
        time_for_fake_work        (default 0.0)
        use_prio_for_reserve_flag (default 0 false because slows things down a lot)
        do_put_answer             (default 0 false because slows things down a lot)
****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pp.h"

#define SRVR_MAX_MALLOC_AMT  (1024 * 1024 * 1024)

#define DEFAULT_NUM_WORK_UNITS   4
#define DEFAULT_WORK_UNIT_SIZE   0
#define DEFAULT_NSECS_FAKE_WORK  0.0
#define DEFAULT_DO_PUT_ANSWER    0

#define WORK             1
#define ANSWER           2

int main(int argc, char *argv[])
{
    int rc, i, done, do_put_answer, work_unit_size;
    int my_world_rank, nranks, num_work_units, num_answers, provided;
    int work_prio, work_type, work_handle[PP_HANDLE_SIZE], work_len, answer_rank;
    int *num_handled_by_rank, num_handled_by_me;
    int dbgprintf_flag = 1, use_prio_for_reserve_flag = 0;
  
    int req_types[4];
    int num_types = 2;
    int type_vect[4] = {WORK,ANSWER};
    int num_type_in_req;

    char thread_type[32];
    char *work_unit_buf;

    double temptime, time_for_fake_work;
    double start_job_time, end_put_time, start_work_time, end_work_time;
    double total_work_time, total_loop_time;
    double total_reserve_time, total_get_time;

    do_put_answer      = DEFAULT_DO_PUT_ANSWER;  /* will halt by exhaustion after 5 secs */
    work_unit_size     = DEFAULT_WORK_UNIT_SIZE;
    num_work_units     = DEFAULT_NUM_WORK_UNITS;
    time_for_fake_work = DEFAULT_NSECS_FAKE_WORK;
    total_work_time = 0.0;
    total_loop_time = 0.0;
    total_reserve_time = 0.0;
    total_get_time = 0.0;

    for (i=1; i < argc; i++)
    {        
        // printf("av %s\n",argv[i]);
        if (strcmp(argv[i],"-a") == 0)
            do_put_answer = 1;
        else if (strcmp(argv[i],"-n") == 0)
            num_work_units = atoi(argv[++i]);
        else if (strcmp(argv[i],"-s") == 0)
            work_unit_size = atoi(argv[++i]);
        else if (strcmp(argv[i],"-t") == 0)
            time_for_fake_work = atof(argv[++i]);
        else
        {
            printf("st1: unrecognized cmd-line arg at %d :%s:\n",i,argv[i]);
            exit(-1);
        }
    }

    rc = MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
    if (rc != MPI_SUCCESS)
    {
        printf("st1: MPI_Init_thread failed with rc=%d\n",rc);
        exit(-1);
    }
    switch (provided)
    {
        case MPI_THREAD_SINGLE: strcpy(thread_type,"MPI_THREAD_SINGLE"); break;
        case MPI_THREAD_FUNNELED: strcpy(thread_type,"MPI_THREAD_FUNNELED"); break;
        case MPI_THREAD_SERIALIZED: strcpy(thread_type,"MPI_THREAD_SERIALIZED"); break;
        case MPI_THREAD_MULTIPLE: strcpy(thread_type,"MPI_THREAD_MULTIPLE"); break;
        default: strcpy(thread_type,"UNKNOWN"); break;
    }
    printf("st1: MPI provides %s\n",thread_type);
    MPI_Comm_size(MPI_COMM_WORLD,&nranks);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);

    num_handled_by_me = 0;
    if (my_world_rank == 0)
        num_handled_by_rank = malloc(nranks * sizeof(int));
    else
        num_handled_by_rank = NULL;
  
    work_unit_buf = malloc(work_unit_size);
  
    rc = PP_Init(SRVR_MAX_MALLOC_AMT,num_types,type_vect);
  
    rc = MPI_Barrier( MPI_COMM_WORLD );
    start_job_time = MPI_Wtime();
    end_work_time  = MPI_Wtime();  /* dummy val until set below */
  
    if ( my_world_rank == 0 )  /* if master app, put work */
    {
        num_answers = 0;
        for (i=0; i < num_work_units; i++)
        {
            memset(work_unit_buf,'X',work_unit_size);
            if (work_unit_size >= 18)
                sprintf(work_unit_buf,"workunit %d",i);
            rc = PP_Put( work_unit_buf, work_unit_size, WORK, -1, -1, &work_handle ); 
            // dbgprintf( 1, "put work_unit %d  rc %d\n", i, rc );
        }
        // dbgprintf(1,"st1: all work submitted after %f secs\n",MPI_Wtime()-start_job_time);
        printf("st1: all work submitted after %f secs\n",MPI_Wtime()-start_job_time);
    }
    rc = MPI_Barrier( MPI_COMM_WORLD );
    end_put_time = start_work_time = MPI_Wtime();
  
    done = 0;
    while ( !done )
    {
        if (do_put_answer)
        {
            if (my_world_rank == 0)
            {
                req_types[0] = ANSWER;
                req_types[1] = WORK;
                num_types_in_req = 2;
            }
            else
            {
                req_types[0] = WORK;
                num_types_in_req = 1;
            }
        }
        else
        {
            num_types_in_req = 0;
        }
        // dbgprintf( 1, "st1: reserving work\n" );
        temptime = MPI_Wtime();
        rc = PP_FindAndReserve(num_types_in_req,req_types,&work_len,
                               &work_type,&answer_rank,work_handle);
        // dbgprintf( 1, "st1: after reserve rc %d len %d type %d\n", rc, work_len, work_type );
        if ( rc == PP_EXHAUSTION )
        {
            // dbgprintf( 1, "st1: done by exhaustion\n" );
            printf( "st1: done by exhaustion\n" );
            break;
        }
        else if ( rc == PP_NO_MORE_WORK )
        {
            // dbgprintf( 1, "st1: done by no more work\n" );
            printf( "st1: done by no more work\n" );
            break;
        }
        else if (rc < 0)
        {
            // dbgprintf( 1, "st1: ** reserve failed, rc = %d\n", rc );
            printf( "st1: ** reserve failed, rc = %d\n", rc );
            ADLB_Abort(-1);
        }
        else if (work_type == WORK) 
        {
            total_reserve_time += MPI_Wtime() - temptime;  /* only count for work */
            temptime = MPI_Wtime();
            rc = PP_Get( work_unit_buf, work_handle );
            total_get_time += MPI_Wtime() - temptime;
            if (rc == PP_NO_MORE_WORK)
            {
                // dbgprintf( 1, "st1: no more work on get_reserved\n" );
                printf( "st1: no more work on get_reserved\n" );
                break;
            }
            else   /* got good work */
            {
                /* do dummy/fake work */
                num_handled_by_me++;
                if (time_for_fake_work == 0.0)
                {
                    // dbgprintf(1,"st1: worktime 0.0\n");
                }
                else
                {
                    temptime = MPI_Wtime();
                    while (1)
                    {
                        for (i=0; i < 1000000; i++)
                            ;
                        if (MPI_Wtime()-temptime > time_for_fake_work)
                            break;
                    }
                    // dbgprintf(1,"st1: worktime %f\n",MPI_Wtime()-temptime);
                }
                if (do_put_answer)
                {
                    rc = PP_Put( NULL, 0, ANSWER, -1, 0, handle ); 
                }
            }
            end_work_time = MPI_Wtime();  /* chgs on each work unit */
        }
        else if ( work_type == ANSWER) 
        {
            num_answers++;
            // dbgprintf(1111,"GENBATCH: GOT ANSWER %d\n",num_answers);
            if (num_answers >= num_work_units)
		PP_Set_problem_done();
        }
        else
        {
            // dbgprintf( 1, "st1: ** unexpected work type %d\n", work_type );
            printf( "st1: ** unexpected work type %d\n", work_type );
            PP_Abort( -1 );
        }
    }
    rc = MPI_Barrier( MPI_COMM_WORLD );
    // total_loop_time can be misleading since we have to wait for exhaustion
    // total_loop_time = MPI_Wtime() - start_work_time;
    // dbgprintf(1,"st1: total loop time %f\n",total_loop_time);
    /****
    total_work_time = end_work_time - start_work_time;
    dbgprintf(1,"st1: num handled by me %d\n",num_handled_by_me);
    dbgprintf(1,"st1: last end_work_time %f\n",end_work_time);
    dbgprintf(1,"st1: total work_time %f ; avg work time %f\n",
            total_work_time,total_work_time/((float)num_handled_by_me));
    dbgprintf(1,"st1: total reserve time %f ; avg reserve time %f\n",
            total_reserve_time,total_reserve_time/((float)num_handled_by_me));
    dbgprintf(1,"st1: total get time %f ; avg get time %f\n",
            total_get_time,total_get_time/((float)num_handled_by_me));
    ****/
    printf("st1: num handled by me %d\n",num_handled_by_me);
    printf("st1: last end_work_time %f\n",end_work_time);
    printf("st1: total work_time %f ; avg work time %f\n",
            total_work_time,total_work_time/((float)num_handled_by_me));
    printf("st1: total reserve time %f ; avg reserve time %f\n",
            total_reserve_time,total_reserve_time/((float)num_handled_by_me));
    printf("st1: total get time %f ; avg get time %f\n",
            total_get_time,total_get_time/((float)num_handled_by_me));
    MPI_Gather(&num_handled_by_me,1,MPI_INT,
               num_handled_by_rank,1,MPI_INT,
               0,MPI_COMM_WORLD);
    if (my_world_rank == 0)
    {
        for (i=0; i < nranks; i++)
            // dbgprintf(1,"st1: num handled by rank %d : total %d  per sec %.0f\n",
            printf("st1: num handled by rank %d : total %d  per sec %.0f\n",
                   i,num_handled_by_rank[i],
                   ((float)num_handled_by_rank[i])/total_work_time);
    }

    PP_Finalize();
    // printf("st1: calling mpi_finalize\n");
    rc = MPI_Finalized(&i);
    if ( ! i)
        MPI_Finalize();
    // printf("st1: past mpi_finalize\n");
  
    return 0;
}
