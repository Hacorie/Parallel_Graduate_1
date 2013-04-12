#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cstdlib>
#include <mpi.h>

#include "pp.h"

#define MASTER_RANK 0

#define TYPE_A                91
#define TYPE_B                92
#define TYPE_C                93
#define HANDLE_SIZE            4

#define SRVR_MAX_MALLOC_AMT  (1024 * 1024 * 1024)

int main(int argc, char *argv[]) {
    int i, rc, work_len, provided, my_world_rank, num_copies, size_of_work, type_of_work, num_types = 3, holder, id;
	int type_vect[num_types];
	type_vect[0] = TYPE_A;
	type_vect[1] = TYPE_B;
	type_vect[2] = TYPE_C;
	
    int Handle_A[HANDLE_SIZE], Handle_B[HANDLE_SIZE], Handle_C[HANDLE_SIZE], FindTypes[100];
    int workbuf[20];

    double start, end;

    rc = MPI_Init_thread(NULL,NULL,MPI_THREAD_MULTIPLE,&provided);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);

	start = MPI_Wtime();
    rc = PP_Init(SRVR_MAX_MALLOC_AMT, num_types, type_vect);
    rc = MPI_Barrier(MPI_COMM_WORLD);

    if (my_world_rank == MASTER_RANK) {
		int temp_handle1[HANDLE_SIZE];
		int temp_handle2[HANDLE_SIZE];
		int temp_handle3[HANDLE_SIZE];
		
        for (i=0; i < 20; i++)
            workbuf[i] = 100 + i;
		
		rc = PP_Put(workbuf, 20*sizeof(int), TYPE_A, my_world_rank, -1, temp_handle1); 
		
		workbuf[0] = 200;
		rc = PP_Put(workbuf, 20*sizeof(int), TYPE_B, my_world_rank, -1, temp_handle2);
		
		workbuf[0] = 300;
		rc = PP_Put(workbuf, 20*sizeof(int), TYPE_C, my_world_rank, -1, temp_handle3);
		
		// print meta data to help another script parse the output parameters
		printf("RC_META PP_NO_MORE_WORK=%d\n", PP_NO_MORE_WORK);
		printf("RC_META PP_SUCCESS=%d\n", PP_SUCCESS);
		printf("RC_META PP_FAIL=%d\n", PP_FAIL);
		printf("RC_META PP_EXHAUSTION=%d\n", PP_EXHAUSTION);
		
		printf("TYPE_META A=%d\n", TYPE_A);
		printf("TYPE_META B=%d\n", TYPE_B);
		printf("TYPE_META C=%d\n", TYPE_C);
		printf("***\n");
    }
	
	
    MPI_Barrier(MPI_COMM_WORLD);   // make sure the Put was done
	
	FindTypes[0] = TYPE_A;
	rc = PP_Find(1, FindTypes, &size_of_work, &type_of_work, Handle_A);
	printf("RANK %d FIND A RC=%d SIZE=%d FOUND_TYPE=%d\n", my_world_rank, rc, size_of_work, type_of_work);
		
	FindTypes[0] = TYPE_B;
	rc = PP_Find(1, FindTypes, &size_of_work, &type_of_work, Handle_B);
	printf("RANK %d FIND B RC=%d SIZE=%d FOUND_TYPE=%d\n", my_world_rank, rc, size_of_work, type_of_work);
		
	FindTypes[0] = TYPE_C;
	rc = PP_Find(1, FindTypes, &size_of_work, &type_of_work, Handle_C);
	printf("RANK %d FIND C RC=%d SIZE=%d FOUND_TYPE=%d\n", my_world_rank, rc, size_of_work, type_of_work);
	
	
    for (i=0; i < 20; i++) {
        workbuf[i] = 0;
	}
	
    for (i=0; i < 3; i++) {
		rc = PP_Copy(workbuf, &num_copies, Handle_A);
		printf("RANK %d COPY A RC=%d COPIES=%d WORK[0]=%d\n", my_world_rank, rc, num_copies, workbuf[0]);
		
		rc = PP_Copy(workbuf, &num_copies, Handle_B);
		printf("RANK %d COPY B RC=%d COPIES=%d WORK[0]=%d\n", my_world_rank, rc, num_copies, workbuf[0]);
		
		rc = PP_Copy(workbuf, &num_copies, Handle_C);
		printf("RANK %d COPY C RC=%d COPIES=%d WORK[0]=%d\n", my_world_rank, rc, num_copies, workbuf[0]);
    }
	
	end = MPI_Wtime();
	
	PP_Finalize();
	if (!MPI_Finalized(&i) == MPI_SUCCESS && i) {
		MPI_Finalize();
	}

	fflush(stdout);
	
	if (my_world_rank == 0) {
		double time = end - start;
		printf("***\n");
		printf("DONE! TIME=%1.4f (seconds)\n", time);
	}
	
    return 0;
}
