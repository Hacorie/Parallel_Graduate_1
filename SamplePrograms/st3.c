/****
    This program is like st2 except it has odd ranks look for work of a type
        that is never sent (put).  This makes them available with a server for
        holding work, but the app does not really do anything.
    This program does Puts for:
        num_work_units            (default 4)
        work_unit_size            (random values in a specified range)
        time_for_fake_work        (random values in a specified range)
        use_prio_for_reserve_flag (default 0 false because slows things down a lot)
        do_put_answer             (default 0 false because slows things down a lot)
****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adlb.h"

#define SRVR_MALLOC_AMT  (1024 * 1024 * 1024)

#define DEFAULT_NUM_WORK_UNITS   4
#define DEFAULT_WORK_UNIT_LEN   0
#define DEFAULT_NSECS_FAKE_WORK  0.0

#define WORK             1
#define NEVER_SENT       2

static int random_int_in_range(int lo, int hi);
static double random_dbl_in_range(double lo, double hi) ;

int main(int argc, char *argv[])
{
    int i, rc, done, rand_work_unit_len;
    int my_world_rank, nranks, num_work_units, provided;
    int work_prio, work_type, work_handle[ADLB_HANDLE_SIZE], work_len, answer_rank;
    int *num_handled_by_rank, num_handled_by_me, total_work_units_handled;
    int dbgprintf_flag = 1, use_prio_for_reserve_flag = 0;
  
    int req_types[3];
    int num_types = 2;
    int type_vect[2] = {WORK,NEVER_SENT};

    char thread_type[32];

    double adlb_dbls_args[ADLB_NUM_INIT_ARGS];
    double temptime, time_for_fake_work;
    double start_job_time, end_put_time, start_work_time, end_work_time;
    double total_work_time, total_loop_time;
    double total_reserve_time, total_get_time;
    double min_work_unit_time, max_work_unit_time, min_work_unit_len, max_work_unit_len;
    double rand_work_unit_time, *work_unit_buf;

    total_work_units_handled = 0;
    total_work_time = 0.0;
    total_loop_time = 0.0;
    total_reserve_time = 0.0;
    total_get_time = 0.0;
    num_work_units     = DEFAULT_NUM_WORK_UNITS;
    min_work_unit_time = DEFAULT_NSECS_FAKE_WORK;
    max_work_unit_time = DEFAULT_NSECS_FAKE_WORK;
    min_work_unit_len  = DEFAULT_WORK_UNIT_LEN;
    max_work_unit_len  = DEFAULT_WORK_UNIT_LEN;

    for (i=1; i < argc; i++)
    {        
        // printf("av %s\n",argv[i]);
        if (strcmp(argv[i],"-n") == 0)
            num_work_units = atoi(argv[++i]);
        else if (strcmp(argv[i],"-t") == 0)
        {
            min_work_unit_time = atof(argv[++i]);
            max_work_unit_time = atof(argv[++i]);
        }
        else if (strcmp(argv[i],"-l") == 0)
        {
            min_work_unit_len = atoi(argv[++i]);
            max_work_unit_len = atoi(argv[++i]);
        }
        else
        {
            printf("st3: unrecognized cmd-line arg at %d :%s:\n",i,argv[i]);
            exit(-1);
        }
    }
    if (min_work_unit_len < sizeof(double))
    {
        printf("st3: min len must be at least size of double %d\n",sizeof(double));
        exit(-1);
    }

    rc = MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
    if (rc != MPI_SUCCESS)
    {
        printf("st3: MPI_Init_thread failed with rc=%d\n",rc);
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
    printf("st3: MPI provides %s\n",thread_type);
    MPI_Comm_size(MPI_COMM_WORLD,&nranks);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);

    num_handled_by_me = 0;
    if (my_world_rank == 0)
        num_handled_by_rank = malloc(nranks * sizeof(int));
    else
        num_handled_by_rank = NULL;
  
    work_unit_buf = malloc(max_work_unit_len);
  
    adlb_dbls_args[0] = (double) SRVR_MALLOC_AMT;
    adlb_dbls_args[1] = (double) dbgprintf_flag;
    adlb_dbls_args[2] = (double) use_prio_for_reserve_flag;
    adlb_dbls_args[3] = (double) num_types;
    for (i=0; i < num_types; i++)
        adlb_dbls_args[i+4] = (double) type_vect[i];
    rc = ADLB_Init(adlb_dbls_args);
  
    rc = MPI_Barrier( MPI_COMM_WORLD );
    start_job_time = MPI_Wtime();
    end_work_time  = MPI_Wtime();  /* dummy val until set below */
  
    if ( my_world_rank == 0 )  /* if master app, put work */
    {
        for (i=0; i < num_work_units; i++)
        {
            memset(work_unit_buf,'X',max_work_unit_len);
            rand_work_unit_len = random_int_in_range(min_work_unit_len,max_work_unit_len);
            rand_work_unit_time = random_dbl_in_range(min_work_unit_time,max_work_unit_time);
            work_unit_buf[0] = rand_work_unit_time;
            dbgprintf( 1111, "st3: putting work_unit %d  len %d  time %f  buf0 %f\n",
                     i, rand_work_unit_len,rand_work_unit_time,work_unit_buf[0]);
            rc = ADLB_Put( work_unit_buf, rand_work_unit_len, -1, -1, WORK, 1 ); 
            // dbgprintf( 1111, "put work_unit %d  rc %d\n", i, rc );
        }
        dbgprintf(1,"st3: %d work submitted after %f secs\n",
                num_work_units,MPI_Wtime()-start_job_time);
    }
    rc = MPI_Barrier( MPI_COMM_WORLD );
    end_put_time = start_work_time = MPI_Wtime();
  
    done = 0;
    while ( !done )
    {
        if (my_world_rank % 2 == 1)
            req_types[0] = NEVER_SENT;
        else
            req_types[0] = -1;
        req_types[1] = -1;
        req_types[2] = -1;
        // dbgprintf( 1, "st3: reserving work\n" );
        temptime = MPI_Wtime();
        rc = ADLB_Reserve(req_types,&work_type,&work_prio,work_handle,&work_len,&answer_rank);
        // dbgprintf( 1, "st3: after reserve rc %d len %d type %d\n", rc, work_len, work_type );
        if ( rc == ADLB_DONE_BY_EXHAUSTION )
        {
            dbgprintf( 1, "st3: done by exhaustion\n" );
            break;
        }
        else if ( rc == ADLB_NO_MORE_WORK )
        {
            dbgprintf( 1, "st3: done by no more work\n" );
            break;
        }
        else if (rc < 0)
        {
            dbgprintf( 1, "st3: ** reserve failed, rc = %d\n", rc );
            ADLB_Abort(-1);
        }
        else if (work_type == WORK) 
        {
            total_reserve_time += MPI_Wtime() - temptime;  /* only count for work */
            temptime = MPI_Wtime();
            rc = ADLB_Get_reserved( work_unit_buf, work_handle );
            // dbgprintf( 1111, "st3: got buf0 %f\n", work_unit_buf[0] );
            total_get_time += MPI_Wtime() - temptime;
            if (rc == ADLB_NO_MORE_WORK)
            {
                dbgprintf( 1, "st3: no more work on get_reserved\n" );
                break;
            }
            else   /* got good work */
            {
                /* do dummy/fake work */
                num_handled_by_me++;
                time_for_fake_work = work_unit_buf[0];
                dbgprintf(1,"st3: fakeworktime %f\n",time_for_fake_work);
                if (time_for_fake_work == 0.0)
                {
                    // dbgprintf(1,"st3: worktime 0.0\n");
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
                    // dbgprintf(1,"st3: worktime %f\n",MPI_Wtime()-temptime);
                }
            }
            end_work_time = MPI_Wtime();  /* chgs on each work unit */
        }
        else
        {
            dbgprintf( 1, "st3: ** unexpected work type %d\n", work_type );
            ADLB_Abort( -1 );
        }
    }
    // dbgprintf( 1111, "st3: BEFORE BARRIER\n");
    rc = MPI_Barrier( MPI_COMM_WORLD );
    // dbgprintf( 1111, "st3: AFTER  BARRIER\n");
    // total_loop_time can be misleading since we have to wait for exhaustion
    // total_loop_time = MPI_Wtime() - start_work_time;
    // dbgprintf(1,"st3: total loop time %f\n",total_loop_time);
    total_work_time = end_work_time - start_work_time;
    dbgprintf(1,"st3: num handled by me %d\n",num_handled_by_me);
    dbgprintf(1,"st3: last end_work_time %f\n",end_work_time);
    if (num_handled_by_me > 0)
        dbgprintf(1,"st3: total work_time %f ; avg work time %f\n",
                total_work_time,total_work_time/((float)num_handled_by_me));
    else
        dbgprintf(1,"st3: total work_time 0.0 ; avg work time 0.0\n");
    if (num_handled_by_me > 0)
        dbgprintf(1,"st3: total reserve time %f ; avg reserve time %f\n",
                total_reserve_time,total_reserve_time/((float)num_handled_by_me));
    else
        dbgprintf(1,"st3: total work_time 0.0 ; avg work time 0.0\n");
    if (num_handled_by_me > 0)
        dbgprintf(1,"st3: total get time %f ; avg get time %f\n",
                total_get_time,total_get_time/((float)num_handled_by_me));
    else
        dbgprintf(1,"st3: total work_time 0.0 ; avg work time 0.0\n");
    MPI_Gather(&num_handled_by_me,1,MPI_INT,
               num_handled_by_rank,1,MPI_INT,
               0,MPI_COMM_WORLD);
    if (my_world_rank == 0)
    {
        for (i=0; i < nranks; i++)
        {
            if (num_handled_by_rank[i] > 0)
            {
                dbgprintf(1,"st3: num handled by rank %d : total %d  avg work time %f\n",
                        i,num_handled_by_rank[i],
                        (total_work_time/(float)num_handled_by_rank[i]));
                total_work_units_handled += num_handled_by_rank[i];
            }
            else
                dbgprintf(1,"st3: num handled by rank %d : total 0  avg work time 0.0\n",i);
        }
        if (total_work_units_handled != num_work_units)
        {
            dbgprintf( 1, "st3: wrong number of work units handled: %d of %d\n",
                     total_work_units_handled, num_work_units);
            ADLB_Abort( -1 );
        }
    }

    ADLB_Finalize();
    // printf("st3: calling mpi_finalize\n");
    MPI_Finalize();
    // printf("st3: past mpi_finalize\n");
  
    return 0;
}

static int random_int_in_range(int lo, int hi)
{
    return ( lo + random() / (RAND_MAX / (hi - lo + 1) + 1) );
}

static double random_dbl_in_range(double lo, double hi) 
{ 
    double rn = random()/((double)(RAND_MAX)+1); 
    return rn * (hi-lo)+lo; 
} 