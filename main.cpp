#include "pp.h"
#include <iostream>
#include <mpi.h>
#include <cstdlib>
#include <stdio.h>

using namespace std;

int main(int argv, char ** argc)
{ 
    int help[5] = {1,2,3,4,5};
    int handle[4];
	int handle2[4];
	int handle3[4];
	int handle4[4];
	int size_of_work_found;
	int type_of_work_found;
	int rc;
	int num_copied;
    int *data;
	int *data2;
	int *data3;
	int nranks, my_world_rank;
	

	
    data = (int*)malloc(100);
	data2 = (int*)malloc(100);
	data3 = (int*)malloc(100);
	data[0] = 1;
	data2[0] = 2;
	data3[0] = 3;
    PP_Init(14000000, 5, help);
	MPI_Comm_size(MPI_COMM_WORLD,&nranks);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_world_rank);
	if(my_world_rank%2 == 0)
	{
		for(int i = 0; i < 100; i++)
		{
			rc = PP_Put(data, 100, 1, 1, -1, handle);
		//rc = PP_Put(data2, 100, 1, 1, -1, handle);
		//rc = PP_Put(data3, 100, 1, 1, -1, handle);
		}
		if(rc != 1)
		{
			PP_Abort(rc);
		}
	}
	else if(my_world_rank%2 == 1)
	{
		for(int i = 0; i < 100; i++)
		{
			rc = PP_FindAndReserve(5, help, &size_of_work_found, &type_of_work_found, handle2);
			if(rc != 1)
				PP_Abort(rc);
		}
		//rc = PP_Find(5, help, &size_of_work_found, &type_of_work_found, handle3);
		//rc = PP_Find(5, help, &size_of_work_found, &type_of_work_found, handle4);
		//printf ("THE find RC IS: %d\n", rc);
		//rc = PP_Copy(data, &num_copied, handle2);
		//printf("1-number of times data has been copied: %d\n", num_copied);
		rc = PP_Copy(data, &num_copied, handle2);
		//printf("2-number of times data has been copied: %d\n", num_copied);
		//handle[0] = rank where data is
		//handle[1] = unique ID on that rank
		//handle[2] = size of work
		//handle[3] = type of work
		//printf("Rank where the data is: %d\n Unique ID on that Rank: %d\n Size of work: %d\n Type of Work: %d\n", handle2[0], handle2[1], handle2[2], handle2[3]);
		//cout << handle2[0] << " " << handle2[1] << " " << handle2[2] <<  " " << handle2[3] << endl;
		rc = PP_Get(data, handle2);
		//printf ("THE get RC IS: %d\n", rc);
	}
	
	PP_Finalize();
}
