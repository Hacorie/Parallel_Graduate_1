#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

#include "pp.h"

#define SRVR_MAX_MALLOC_AMT  (1024 * 1024 * 1024)

#define DEFAULT_NUM_WORK_UNITS  4
#define DEFAULT_WORK_UNIT_SIZE  0
#define DEFAULT_NSECS_FAKE_WORK 0.0
#define DEFAULT_DO_PUT_ANSWER	0
#define	DEFAULT_ALL_RANKS_PUT	0

#define WORK		1
#define ANSWER		2
#define HANDLE_SIZE	4

int main(int argc, char *argv[]) {
	int rc, i, done, do_put_answer, all_ranks_put, work_unit_size, time_for_fake_work;
	int my_world_rank, nranks, num_work_units, num_answers, provided;
	int work_type, work_handle[HANDLE_SIZE], work_len, answer_rank;
	int num_handled_by_me;
	int max_message_size = 50;
  
	int req_types[4];
	int num_types = 3;
	int type_vect[4] = { WORK, ANSWER };
	int num_types_in_req;
	int final_rc;

	char thread_type[32];
	char *work_unit_buf;
	
	char *findbuf = (char *)malloc(75);
	char *getbuf = (char *)malloc(75);
	char *ansbuf = (char *)malloc(75);
	

	double temptime;
	double start_job_time, end_put_time, start_work_time, end_work_time;
	double total_work_time, total_loop_time;
	double total_reserve_time, total_get_time;
	double total_put_time = 0.0;

	do_put_answer	= DEFAULT_DO_PUT_ANSWER;  /* will halt by exhaustion */
	all_ranks_put	= DEFAULT_ALL_RANKS_PUT;
	work_unit_size	= DEFAULT_WORK_UNIT_SIZE;
	num_work_units	= DEFAULT_NUM_WORK_UNITS;
	time_for_fake_work = DEFAULT_NSECS_FAKE_WORK;
	total_work_time = 0.0;
	total_loop_time = 0.0;
	total_reserve_time = 0.0;
	total_get_time = 0.0;

	for (i=1; i < argc; i++) {
		if (strcmp(argv[i],"-dpa") == 0)
			do_put_answer = 1;
		else if (strcmp(argv[i], "-alt") == 0)
			all_ranks_put = 1;
		else if (strcmp(argv[i],"-n") == 0)
			num_work_units = atoi(argv[++i]);
		else if (strcmp(argv[i],"-s") == 0)
			work_unit_size = atoi(argv[++i]);
		else if (strcmp(argv[i],"-t") == 0)
			time_for_fake_work = atoi(argv[++i]);
		else {
			printf("unrecognized cmd-line arg at %d :%s:\n", my_world_rank, i, argv[i]);
			exit(-1);
		}
	}

	rc = MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
	if (rc != MPI_SUCCESS) {
		printf("MPI_Init_thread failed with rc=%d\n", rc);
		exit(-1);
	}

	MPI_Comm_size(MPI_COMM_WORLD,&nranks);
	MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);

	num_handled_by_me = 0;

	work_unit_buf = (char *)malloc(work_unit_size);
  
	rc = PP_Init(SRVR_MAX_MALLOC_AMT,num_types,type_vect);
	if (rc != PP_SUCCESS) {
		MPI_Abort(MPI_COMM_WORLD, -1);
		exit(-1);
	}
	
	// print out info chart
	if (my_world_rank == 0) {
		printf("------------------------------------------------------------------------------\n");
		printf("%1s%30s%2s%23s%2s%18s%2s\n", "|", "ARGUMENTS", 	"|", "RETURN CODES", "|", "WORK UNITS", "|");
		printf("%1s%20s%3s%7d%2s%16s%3s%4d%2s%10s%3s%5d%2s\n", 	"|", "do_put_answer",      	"=", do_put_answer,	   "|", "PP_FAIL",         "=", PP_FAIL,         "|", "WORK",   "=", WORK,   "|");
		printf("%1s%20s%3s%7d%2s%16s%3s%4d%2s%10s%3s%5d%2s\n", 	"|", "all_ranks_put",      	"=", all_ranks_put,      "|",	"PP_SUCCESS",      "=", PP_SUCCESS,      "|", "ANSWER", "=", ANSWER, "|");
		printf("%1s%20s%3s%7d%2s%16s%3s%4d%2s%20s\n",         	"|", "num_work_units",     	"=", num_work_units,     "|",	"PP_NO_MORE_WORK", "=", PP_NO_MORE_WORK, "|", "|");
		printf("%1s%20s%3s%7d%2s%16s%3s%4d%2s%20s\n",           "|", "work_unit_size",     	"=", work_unit_size,     "|",	"PP_EXHAUSTION",   "=", PP_EXHAUSTION,   "|", "|");
		printf("%1s%20s%3s%7d%2s%25s%20s\n",                    "|", "time_for_fake_work", 	"=", time_for_fake_work, "|", "|", "|");
		printf("%1s%20s%3s%7d%2s%25s%20s\n",                    "|", "num_ranks", 			"=", nranks, "|", "|", "|");
		printf("------------------------------------------------------------------------------\n");
		printf("***\n");
	}
	
	rc = MPI_Barrier( MPI_COMM_WORLD );
	
	start_job_time = end_work_time = MPI_Wtime(); /* dummy val until set below */
	
	int my_put_count = 0;
	
	if (all_ranks_put == 1) {
		num_answers = 0;
		for (i = 0; i < num_work_units; i++) {
			memset(work_unit_buf, 'X', work_unit_size);
			if (work_unit_size >= 20)
				sprintf(work_unit_buf, "workunit:r%d:u%d", my_world_rank, i);
			rc = PP_Put(work_unit_buf, work_unit_size, WORK, -1, 0, work_handle);
			my_put_count++;
			printf("rank=%2d  PUT rc=%d data=%s handle_key=%d:%d\n", my_world_rank, rc, work_unit_buf, work_handle[0], work_handle[1]);
		}
		total_put_time = MPI_Wtime() - start_job_time;
		num_work_units *= nranks;
	}
	else {	
		if (my_world_rank == 0) { /* if master app, put work */ 
			num_answers = 0;
			for (i=0; i < num_work_units; i++) {
				memset(work_unit_buf, 'X', work_unit_size);
				if (work_unit_size >= 20)
					sprintf(work_unit_buf,"workunit:r%d:u%d", my_world_rank, i);		
				rc = PP_Put( work_unit_buf, work_unit_size, WORK, -1, -1, work_handle);
				my_put_count++;
				printf("rank=%2d  PUT rc=%d DATA=%s handle_key=%d:%d\n", my_world_rank, rc, work_unit_buf, work_handle[0], work_handle[1]);
			}
			total_put_time = MPI_Wtime() - start_job_time;
		}
	}
	
	rc = MPI_Barrier( MPI_COMM_WORLD );
	end_put_time = start_work_time = MPI_Wtime();
  
	done = 0;
	while ( !done ) {
		if (do_put_answer) {
			if (my_world_rank == 0) {
				req_types[0] = ANSWER;
				req_types[1] = WORK;
				num_types_in_req = 2;
			}
			else {
				req_types[0] = WORK;
				num_types_in_req = 1;
			}
		}
		else {
			req_types[0] = WORK;
			num_types_in_req = 1;
		}
		temptime = MPI_Wtime();
		
		memset(findbuf, ' ', max_message_size);
		memset(getbuf, ' ', max_message_size);
		memset(ansbuf, ' ', max_message_size);
		
		// if all ranks put data targeted to rank 0, rank 0 should do a "find" rather than find and reserve
		if (all_ranks_put) {
			//printf("Before find on Rank %d\n", my_world_rank);
			rc = PP_Find(num_types_in_req, req_types, &work_len, &work_type, work_handle);
			//printf("Find rc=%d\n", rc);
			if (rc == PP_SUCCESS) {
				sprintf(findbuf, "FIND: rc=%d h_key=%d:%d type=%d size=%d", rc, work_handle[0], work_handle[1], work_type, work_len);
			}
			else {
				sprintf(findbuf, "FIND: rc=%d", rc);
			}
		}
		else {
			rc = PP_FindAndReserve(num_types_in_req, req_types, &work_len, &work_type, work_handle);
			
			if (rc == PP_SUCCESS) {
				sprintf(findbuf, "FIND: rc=%d h_key=%d:%d type=%d size=%d", rc, work_handle[0], work_handle[1], work_type, work_len);
			}
			else {
				sprintf(findbuf, "FIND: rc=%d", rc);
			}
		}

		if (rc == PP_EXHAUSTION) {
			MPI_Barrier(MPI_COMM_WORLD);
			printf("rank=%2d Terminated by EXHAUSTION\n", my_world_rank);
			final_rc = rc;
			break;
		}
		else if ( rc == PP_NO_MORE_WORK ) {
			MPI_Barrier(MPI_COMM_WORLD);
			printf("rank=%2d Terminated by NO_MORE_WORK\n", my_world_rank);
			final_rc = rc;
			break;
		}
		
		if (work_type == WORK) {
			total_reserve_time += MPI_Wtime() - temptime;  /* only count for work */
			
			temptime = MPI_Wtime();
			//if(my_world_rank ==1)
			//	printf("Starting Get on rank1\n");
			rc = PP_Get(work_unit_buf, work_handle);
			total_get_time += (MPI_Wtime() - temptime);
			
			if (rc == PP_SUCCESS) {
				sprintf(getbuf, "GET: rc=%d h_key=%d:%d data=%s", rc, work_handle[0], work_handle[1], work_unit_buf);
			}
			else {
				sprintf(getbuf, "GET: rc=%d", rc);
			}
			
			/* got good work, do dummy/fake work */
			num_handled_by_me++;
			temptime = MPI_Wtime();
			
			while (1) {
				for (i=0; i < 1000000; i++)
					;
				if (((MPI_Wtime() - temptime) * 1000) > time_for_fake_work)
					break;		
			} 

			if (do_put_answer) {
				rc = PP_Put(NULL, 0, ANSWER, -1, -1, work_handle);

				sprintf(ansbuf, "PUT_ANS: rc=%d h_key=%d:%d", rc, work_handle[0], work_handle[1]);
			}
		
			end_work_time = MPI_Wtime();  /* chgs on each work unit */
		}
		else if (work_type == ANSWER) {
			num_answers++;
			
			sprintf(getbuf, "FOUND_ANS: num_answers: %d", num_answers);
			
			if (all_ranks_put) {
				rc = PP_Get(work_unit_buf, work_handle);
			}
			if (num_answers >= num_work_units) {
				PP_Set_problem_done();
			}
		}
		else {
			printf("rank=%2d ERROR UNEXPECTED_WORK_TYPE=%d\n", my_world_rank, work_type );
			PP_Abort(-1);
		}
		
		printf("rank=%2d %-45s %-45s %-45s\n", my_world_rank, findbuf, getbuf, ansbuf);
		
		if (work_type == ANSWER && num_answers >= num_work_units) {
			printf("rank=%2d PP_Set_problem_done() has been called\n", my_world_rank);
		}
	}
	
	rc = MPI_Barrier( MPI_COMM_WORLD );
	
	total_loop_time = MPI_Wtime() - start_work_time;
	
	float avg_work_time, avg_reserve_time, avg_get_time;
	avg_work_time = ((float)total_loop_time) / ((float)num_handled_by_me);


	int *ar_num_handled_by_rank 	= NULL;
	int *ar_final_rc				= NULL;
	int *ar_put_counts				= NULL;
	double *ar_total_reserve_time	= NULL;
	double *ar_total_get_time		= NULL;
	double *ar_total_put_time		= NULL;
	
	if (my_world_rank == 0) {
		ar_num_handled_by_rank 	= (int *)malloc(nranks * sizeof(int));
		ar_final_rc				= (int *)malloc(nranks * sizeof(int));
		ar_put_counts			= (int *)malloc(nranks * sizeof(int));
		ar_total_reserve_time	= (double *)malloc(nranks * sizeof(double));
		ar_total_get_time		= (double *)malloc(nranks * sizeof(double));
		ar_total_put_time		= (double *)malloc(nranks * sizeof(double));
	}
	
	MPI_Gather(&num_handled_by_me, 1, MPI_INT, ar_num_handled_by_rank, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Gather(&final_rc, 1, MPI_INT, ar_final_rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Gather(&my_put_count, 1, MPI_INT, ar_put_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Gather(&total_reserve_time, 1, MPI_DOUBLE, ar_total_reserve_time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	MPI_Gather(&total_get_time, 1, MPI_DOUBLE, ar_total_get_time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	MPI_Gather(&total_put_time, 1, MPI_DOUBLE, ar_total_put_time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	int 	grand_total_work	= 0;
	int		grand_total_puts	= 0;
	double 	grand_total_resv	= 0;
	double 	grand_total_get		= 0;
	double	grand_total_put		= 0;
	
	if (my_world_rank == 0) {
		printf("***\n");
		printf("----Stats:----\n");
		printf("%10s%15s%15s%20s%20s%20s%15s%20s%20s\n", "RANK", "NUM_PUTS", "WORK_UNITS", "TOTAL_PUT_TIME", "TOTAL_RESV_TIME", "TOTAL_GET_TIME", "FINAL_RC", "AVG_WORK_TIME", "WORK_PER_SECOND");
		for (i=0; i < nranks; i++) {
			printf("%10d%15d%15d%20.5f%20.5f%20.5f%15d%20.5f%20.5f\n", 
						i,
						ar_put_counts[i],
						ar_num_handled_by_rank[i], 
						ar_total_put_time[i],
						ar_total_reserve_time[i],
						ar_total_get_time[i],
						ar_final_rc[i],
						((double)total_loop_time / (double)ar_num_handled_by_rank[i]), 
						(((double)ar_num_handled_by_rank[i]) / total_loop_time));
			grand_total_work += ar_num_handled_by_rank[i];
			grand_total_puts += ar_put_counts[i];
			grand_total_resv += ar_total_reserve_time[i];
			grand_total_get += ar_total_get_time[i];
			grand_total_put += ar_total_put_time[i];
		}
		printf("%10s%15s%15s%20s%20s%20s\n", "-", "-", "-", "-", "-", "-");
		printf("%10s%15d%15d%20.5f%20.5f%20.5f\n", "TOTALS:", grand_total_puts, grand_total_work, grand_total_put, grand_total_resv, grand_total_get);
		printf("%11s%.5f\n", "TIME: ", total_loop_time);
	}

	PP_Finalize();
	rc = MPI_Finalized(&i);
	if ( ! i)
		MPI_Finalize();
	
	return 0;
}