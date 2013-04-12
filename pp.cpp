#include <iostream>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <pthread.h>
#include <cstdlib>
#include <mpi.h>
#include <list>
#include <map>

#include "pp.h"

using namespace std;

#define BUFFER_SIZE                 10

//Container to hold the data and all information relating to the data
struct map_data
{
	void *data;
	int handle[BUFFER_SIZE];
};

//Function for the server thread
static void *pp_server(void* args);

//Flags to determine if it's time to stop
static int PP_EXHAUST_FLAG = 0;
static int PP_NO_MORE_WORK_FLAG;

//Communication variables for MPI calls
static MPI_Comm PP_COMM_WORLD;
static int myrank, num_ranks;

//Variables whose values apply to all threads
static int num_types;
static int server_size;
static int *types;
static int next_put_rank;

//Thread type; setup to create a thread.
static pthread_t server_thread;

//Variable declaring which rank will send exhaustion checks
static int master_rank_exhaustion = 0;

void *pp_server(void *args)
{
    //printf("this is a server thread from server: %d\n",myrank );

	//local variables for each server thread
	int rc;											//return code
    int done = 0;									//flag to end probe loop
    int pflag = 0, from_rank, from_tag;				//probe variables
	int server_space = 0;							//how much space on the server has been used
	int unique_id = 1;								//id for each data item in a map
	void *work_buffer;								//buffer that holds work to do
	int length, type, answer_rank, target_rank, reserved;	//item specific variables
	
	//PP_Get only variables
	int get_id, get_rank;
	
	//Exhaustion variables
    double exhaust_chk_start_time = MPI_Wtime();
    double exhaust_chk_interval = 5.0;
	int exhaust_chk_is_out = 0;
	int e_count;
	int ring_pass = 0;
	
	
	//Container creation and iterator creation to hold all data on a server
	map_data map_entry;
	map<int, map_data> data_library;
	map<int, map_data>::iterator it;

	//MPI variables 
    MPI_Status status;
    MPI_Request request;
	int resp;										//response to a request
	
	//left hand anf right hand ranks of the current rank.
	int rhs_rank;
	int lhs_rank;
	
	//make sure the ranks are set up in a ring fashion
	if(myrank == 0)
	{
		rhs_rank = (myrank + 1) % num_ranks; 
		lhs_rank= num_ranks - 1;
	}
	else //(myrank == (num_ranks - 1))
	{
		rhs_rank = (myrank + 1) % num_ranks; 
		lhs_rank= myrank - 1;
	}

	//Loop infinetly until done flag is set
    while(!done)
    {
		//Have rank 0 send out an exhaustion message every 5 seconds until the PP_EXHAUST_FLAG is flipped
		if(myrank == master_rank_exhaustion && (MPI_Wtime()-exhaust_chk_start_time > exhaust_chk_interval && !PP_EXHAUST_FLAG && num_ranks > 1))
		{
			exhaust_chk_start_time = MPI_Wtime();
			int count = 0;
			exhaust_chk_is_out = 1;
			MPI_Send(&count, 1, MPI_INT, rhs_rank, PP_EXHAUSTION_CHK, PP_COMM_WORLD);
		}
		
		//Probe for tags being sent to a server from anywhere
        rc = MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, PP_COMM_WORLD, &pflag ,&status);
		
		//record the tag and rank it came from
		from_rank = status.MPI_SOURCE;
        from_tag = status.MPI_TAG;
        
		//Create storage arrays for later on
        int buffer[BUFFER_SIZE];
        int handle[HANDLE_SIZE];
		
		//create a flag for finds
		int fflag = 0;

		//if the probe found a tag that can be recieved
		if(pflag)
		{
			//make sure to reset ring_pass if the message is from your app
			if (from_rank == myrank) 
			{
				ring_pass = 0;
			}
			
			//printf("my_rank = %d ;;; from_rank = %d ;;; from_tag = %d\n", myrank, from_rank, from_tag);
		
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////
			if(from_tag == PP_EXHAUSTION_CHK)
			{
				MPI_Recv(&e_count, 1, MPI_INT, from_rank, PP_EXHAUSTION_CHK, PP_COMM_WORLD, &status);
				//printf("checking for exhaustion ring_pass=%d\n", ring_pass);
				
				//if you the message has passed around the ring twice see what rank you are
				if(ring_pass > 1)
				{
					//cout << "TEST\n";
					
					//if the current rank is not the master rank, then exhaustion could not have occured
					if(myrank != master_rank_exhaustion)
					{
						MPI_Send(&e_count, 1, MPI_INT, rhs_rank, PP_EXHAUSTION_CHK, PP_COMM_WORLD);
					}
					else //check the ehaustion counter
					{
						//if the exhaustion counter is 2 or more then we are exhausted, if not, the check ocntinues
						if(e_count > 1)
						{
							MPI_Send(NULL, 0, MPI_BYTE, rhs_rank, PP_EXHAUST, PP_COMM_WORLD);
						}
						else
						{
							e_count++;
							MPI_Send(&e_count, 1, MPI_INT, rhs_rank, PP_EXHAUSTION_CHK, PP_COMM_WORLD);
						}
					}
				}
				//since nothing is here, we drop the check if a rank is not exhausted
			}
			
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////
			else if(from_tag == PP_EXHAUST)
			{
				//set the exhaustion flag, and tell the rank who asked you to find something
				PP_EXHAUST_FLAG = 1;
				MPI_Recv(NULL, 0, MPI_BYTE, from_rank, PP_EXHAUST, PP_COMM_WORLD, &status);
				
				//send the Exhaustion message around the ring to let everyone know
				if(myrank != master_rank_exhaustion)
				{
					MPI_Send(NULL, 0, MPI_BYTE, rhs_rank, PP_EXHAUST, PP_COMM_WORLD);
				}
			}
			
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////		
			else if(from_tag == PP_LOCAL_APP_DONE)
			{
				//printf("%d waiting for finalize\n", myrank);
				
				//The server is in PP_Finalize, so stop Iprobing for tags
				MPI_Recv(NULL, 0, MPI_BYTE, from_rank, PP_LOCAL_APP_DONE, PP_COMM_WORLD, &status);
				//printf("recieved data from rank %d\n", from_rank);
				done = true;
			}

			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////
			else if(from_tag == PP_PUT_REQ)
			{
				//send_data[0] = length;
				//send_data[1] = type;
				//send_data[2] = answer_rank;
				//send_data[4] = myrank;
				//send_data[3] = target_rank;
				//send_data[5] = target_rank;
				
				//if(myrank == master_rank_exhaustion)
				//printf("I am in the put_req server func from rank %d to rank %d\n", from_rank,  myrank);
				
				
				MPI_Recv(buffer, BUFFER_SIZE, MPI_INT, from_rank, PP_PUT_REQ, PP_COMM_WORLD, &status);
				//cout << "My rank in put server : " << myrank << endl;
				
				//store all the data sent from the app in easy to use variables
				length = buffer[0];
				type = buffer[1];
				answer_rank = buffer[2];
				target_rank = buffer[3];
				from_rank = buffer[4];
				reserved = buffer[5];
				
				//printf("target_Rank = %d\n", target_rank);
				
				//make sure you can put the item on this server, if you cannot pass it around the ring
				if( (server_space + length) < server_size)
				{
					server_space += length; //increase your server space because you are going to allow the put
					resp = PP_ALLOW_PUT;	//prepare a response to the app
					work_buffer = map_entry.data = malloc(length);	//set up the work buffer and container entry
					
					//send the response to the app and wait for the app to actually send the data
					MPI_Irecv(work_buffer, length, MPI_BYTE, from_rank, PP_PUT_DATA, PP_COMM_WORLD, &request);
					MPI_Rsend(&resp, 1, MPI_INT, from_rank, PP_PUT_RESP, PP_COMM_WORLD);
					MPI_Wait(&request, &status);
					
					//printf("target_Rank = %d\n", target_rank);
					
					//set up the handle and the container entry
					map_entry.handle[0] = handle[0] = myrank;
					map_entry.handle[1] = handle[1] = unique_id;
					map_entry.handle[2] = handle[2] = length;
					map_entry.handle[3] = handle[3] = type; 
					map_entry.handle[4] = target_rank;
					map_entry.handle[5] = answer_rank;
					map_entry.handle[6] = reserved;
					map_entry.handle[7] = 0;
					
					//put the entry in the container with a key of a unique ID
					data_library[unique_id] = map_entry;
					unique_id++;
					//printf("unique id: %d\n", map_entry.handle[1]);
					
					//Send the handle back to the app
					MPI_Send(handle, HANDLE_SIZE, MPI_INT, from_rank, PP_HANDLE, PP_COMM_WORLD);
				}
				else
				{
					//If there is only one rank, then call PP_Abort
					if(num_ranks > 1)
					{
						//printf("DO I EVER GET HERE?!\n");
						resp = PP_PUT_RING;
						MPI_Send(&resp, 1, MPI_INT, from_rank, PP_PUT_RESP, PP_COMM_WORLD);
					}
					else
					{
						//printf("I am sorry, but we cannot find a server to put the data!\n");
						PP_Abort(-1);
					}
				}
			}
			
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////		
			else if(from_tag == PP_GET_REQ)
			{
				MPI_Recv(handle, HANDLE_SIZE, MPI_INT, from_rank, PP_GET_REQ, PP_COMM_WORLD, &status);
				
				//Make sure the NO_MORE_WORK flag was not set already
				if(PP_NO_MORE_WORK_FLAG)
				{
					resp = PP_NO_MORE_WORK;
					MPI_Send(&resp, 1, MPI_INT, from_rank, PP_GET_RESP, PP_COMM_WORLD);
					continue;
				}
				
				//put handle information into easy to use variables with good names
				get_rank = handle[0];
				get_id = handle[1];
				length = handle[2];
				type = handle[3];
				
				//Check and make sure I was correctly sent the right handle, if not, DENY
				if(get_rank != myrank)
				{
					//printf("Inside the no privelages to get from rank: %d\n", myrank);
					resp = PP_DENY_GET;
					MPI_Send(&resp, 1, MPI_INT, from_rank, PP_GET_RESP, PP_COMM_WORLD);
				}
				else
				{
					//printf("Getting data form rank: %d ;;; origin rank=%d\n", myrank, from_rank);
				
					//Use the map built in functions to search for the data on this rank
					it = data_library.find(get_id);
					
					//If the data was found
					if(it != data_library.end() && !data_library.empty())
					{
						//If it is not targeted to anyone or if it is reserved for the requester then give it to them
						if(it->second.handle[4] == -1  || it->second.handle[6] == from_rank)
						{
							//printf("Allowing Get to happen at rank %d\n", myrank);
							resp = PP_ALLOW_GET;
							//printf("Sending a response from get on rank %d to rank %d\n", myrank, from_rank);
							MPI_Send(&resp, 1, MPI_INT, from_rank, PP_GET_RESP, PP_COMM_WORLD);
							//printf("Sending data from get on rank %d to rank %d\n", myrank, from_rank);
							MPI_Send(it->second.data, length, MPI_BYTE, from_rank, PP_GET_DATA, PP_COMM_WORLD);
							
							//erase the item from the map so no one else will try and get the data
							data_library.erase(it);
						}
						else
						{
							//if the requestor does not have rights to access the data, then deny him
							resp = PP_DENY_GET;
							MPI_Send(&resp, 1, MPI_INT, from_rank, PP_GET_RESP, PP_COMM_WORLD);
						}							
					}
					else //if the rank does not have the item, deny the requestor
					{
						//printf("Denying Get on rank%d\n", myrank);
						resp = PP_DENY_GET;
						MPI_Send(&resp, 1, MPI_INT, from_rank, PP_GET_RESP, PP_COMM_WORLD);	
					}
				}
			}
			
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////
			else if(from_tag == PP_FIND_AND_RESERVE_REQ)
			{
				MPI_Recv(buffer, BUFFER_SIZE, MPI_INT, from_rank, PP_FIND_AND_RESERVE_REQ, PP_COMM_WORLD, &status);
				
				//create scoped variables local to this else-if only to hold buffer information in easy to use var
				int find_num_types = buffer[0];
				int start_find_rank = buffer[1];
				
				//printf("Num types in req: %d ==> Original Rank: %d\n", find_num_types, start_find_rank);
				
				//allocate space depending on the number of types then recieve the types array 
				int *find_types;
				if(find_num_types > 0)
				{
					find_types = (int*)calloc(find_num_types, sizeof(int));
				}
				else
				{
					find_types = (int*)calloc(1, sizeof(int));
					find_num_types = 1;
				}
				MPI_Recv(find_types, find_num_types, MPI_INT, from_rank, PP_TYPES, PP_COMM_WORLD, &status);
				
				//Check for PP_NO_MORE_WORK_FLAG and PP_EXHAUST_FLAG before actually doing anything
				if(PP_NO_MORE_WORK_FLAG)
				{
					resp = PP_NO_MORE_WORK;
					MPI_Send(&resp, 1, MPI_INT, start_find_rank, PP_FIND_AND_RESERVE_RESP, PP_COMM_WORLD);
					continue;
				}
				
				if(PP_EXHAUST_FLAG)
				{
					resp = PP_EXHAUSTION;
					MPI_Send(&resp, 1, MPI_INT, start_find_rank, PP_FIND_AND_RESERVE_RESP, PP_COMM_WORLD);
					continue;
				}
				
				////set the fflag to 0 to make sure the item was not found
				fflag = 0;
				
				//If the container on this server is not empty, then look for the data
				if(!data_library.empty())
				{
					for(it=data_library.begin(); it != data_library.end() && fflag != 1; it++)
					{
						for(int i = 0; i < find_num_types && fflag != 1; i++)
						{
							//make sure you find data of the correct type, and make sure it is not already reserved
							if(it->second.handle[3] == (find_types[i] || find_types[i] == 0) && it->second.handle[6] == -1)
							{
								//fill in the handle information
								handle[0] = it->second.handle[0];
								handle[1] = it->second.handle[1];
								handle[2] = it->second.handle[2];
								handle[3] = it->second.handle[3];
								it->second.handle[6] = buffer[1]; //flip the reserved flag
								
								//send a response to the requestor
								resp = PP_FOUND;
								exhaust_chk_is_out = 0;
								MPI_Send(&resp, 1, MPI_INT, start_find_rank, PP_FIND_AND_RESERVE_RESP, PP_COMM_WORLD);
								MPI_Send(handle, HANDLE_SIZE, MPI_INT, start_find_rank, PP_FOUND, PP_COMM_WORLD);
								
								//set the find flag to 1 to signify we do not need to ask other servers
								fflag = 1;
							}
						}
					}
				}
				
				//if the data was not on this server, pass it around the ring
				if(fflag != 1)
				{
					//printf("Passing along to next rank\n");
					//make sure the current server is not the only rank. 
					if(num_ranks > 1 )
					{
						//increment ring_pass if message has passed around the ring
						if(myrank == start_find_rank)
						{
							ring_pass++;
						}
						MPI_Send(buffer, BUFFER_SIZE, MPI_INT, rhs_rank, PP_FIND_AND_RESERVE_REQ, PP_COMM_WORLD);
						MPI_Send(find_types, find_num_types, MPI_INT, rhs_rank, PP_TYPES, PP_COMM_WORLD);
					}
					else //If I am the only one, respond with Exhaustion
					{
						resp = PP_EXHAUSTION;
						MPI_Send(&resp, 1 , MPI_INT, start_find_rank, PP_FIND_AND_RESERVE_RESP, PP_COMM_WORLD);
					}
				}
			}
			
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////		
			else if(from_tag == PP_FIND_REQ)
			{
				MPI_Recv(buffer, BUFFER_SIZE, MPI_INT, from_rank, PP_FIND_REQ, PP_COMM_WORLD, &status);

				//Create easy to use variables with good names to hold find information
				int find_num_types = buffer[0];
				int start_find_rank = buffer[1];
				
				//allocate space for the amount of types there are
				int *find_types;
				if(find_num_types > 0)
				{
					find_types = (int*)calloc(find_num_types, sizeof(int));
				}
				else
				{
					find_types = (int*)calloc(1, sizeof(int));
					find_num_types = 1;
				}
				
				//Recieve the types array
				MPI_Recv(find_types, find_num_types, MPI_INT, from_rank, PP_TYPES, PP_COMM_WORLD, &status);

				//for(int i = 0; i < find_num_types; i++)
				//	printf("Types to search for: %d\n", find_types[i]);
				
				//Make sure we are not Exhausted and that the PP_NO_MORE_WORK flag has not been set
				if(PP_NO_MORE_WORK_FLAG)
				{
					resp = PP_NO_MORE_WORK;
					MPI_Send(&resp, 1, MPI_INT, start_find_rank, PP_FIND_RESP, PP_COMM_WORLD);
					continue;
				}
				
				if(PP_EXHAUST_FLAG)
				{
					resp = PP_EXHAUSTION;
					MPI_Send(&resp, 1, MPI_INT, start_find_rank, PP_FIND_RESP, PP_COMM_WORLD);
					continue;
				}
				
				//set the fflag to 0 to make sure the item was not found
				fflag = 0;
				
				//Check and see if the item is on this server
				if(!data_library.empty())
				{
					//printf("Should not be here if not rank 0\n");
					for(it=data_library.begin(); it != data_library.end() && fflag != 1; it++)
					{
						for(int i = 0; i < find_num_types && fflag != 1; i++)
						{
							if(it->second.handle[3] == find_types[i] || find_types[i] == 0)
							{
								//fill in the handle to return
								handle[0] = it->second.handle[0];
								handle[1] = it->second.handle[1];
								handle[2] = it->second.handle[2];
								handle[3] = it->second.handle[3];
								
								//printf("%d %d %d %d", handle[0], handle[1], handle[2], handle[3]);
								
								//Let the App who was looking for the item know this server has it.
								resp = PP_FOUND;
								exhaust_chk_is_out = 0;
								MPI_Send(&resp, 1, MPI_INT, start_find_rank, PP_FIND_RESP, PP_COMM_WORLD);
								MPI_Send(handle, HANDLE_SIZE, MPI_INT, start_find_rank, PP_FOUND, PP_COMM_WORLD);
								fflag = 1;
							}
						}
					}
				}
				
				//If this server does not have it, pass the request around the ring
				if(fflag != 1)
				{
					//printf("Rank 1 should be here\n");
					//Make sure this rank is not the only one
					if(num_ranks > 1 )
					{
						//increment ring_pass if the message has passed around the ring entirely
						if(myrank == start_find_rank)
						{
							ring_pass++;
						}
						//send the message to the right
						MPI_Send(buffer, BUFFER_SIZE, MPI_INT, rhs_rank, PP_FIND_REQ, PP_COMM_WORLD);
						MPI_Send(find_types, buffer[0], MPI_INT, rhs_rank, PP_TYPES, PP_COMM_WORLD);
					}
					else //If this rank is the only rank, send out an Exhaustion response
					{
						resp = PP_EXHAUSTION;
						MPI_Send(&resp, 1 , MPI_INT, find_num_types, PP_FIND_RESP, PP_COMM_WORLD);
					}
				}
			}
			
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////		
			else if(from_tag == PP_COPY_REQ)
			{
				MPI_Recv(handle, HANDLE_SIZE, MPI_INT, from_rank, PP_COPY_REQ, PP_COMM_WORLD, &status);
				
				//Check and make sure he NO_MORE_WORK_FLAG has not been set before actually doing anything
				if(PP_NO_MORE_WORK_FLAG)
				{
					resp = PP_NO_MORE_WORK;
					MPI_Send(&resp, 1, MPI_INT, from_rank, PP_COPY_RESP, PP_COMM_WORLD);
					continue;
				}
				
				//create easy to use variables for data storage
				int data_rank = handle[0];
				int data_id = handle[1];
				int data_size = handle[2];
				int data_type = handle[3];
				int copy_count;
				
				//make sure the server's container is not empty. If it is, Deny the copy
				if(data_library.find(data_id) == data_library.end())
				{
					resp = PP_DENY_COPY;
					MPI_Send(&resp, 1, MPI_INT, from_rank, PP_COPY_RESP, PP_COMM_WORLD);
				}
				else
				{
					//set up iterator and copy count
					it = data_library.find(data_id);
					copy_count = it->second.handle[7]++;
					
					//respond to the requestoer that you are allowing the copy
					resp = PP_ALLOW_COPY;
					MPI_Send(&resp, 1, MPI_INT, from_rank, PP_COPY_RESP, PP_COMM_WORLD);
					
					//send the requestor app the information he needs.
					MPI_Send(it->second.data, it->second.handle[2], MPI_BYTE, from_rank, PP_COPY, PP_COMM_WORLD);
					MPI_Send(&copy_count, 1, MPI_INT, from_rank, PP_COPY_COUNT, PP_COMM_WORLD);				
				}
			}
	
			////////////////////////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////////////////////////
			else if(from_tag == PP_NO_MORE)
			{
				//recieve the App's message and set the PP_NO_MORE_WORK_FLAG
				MPI_Recv(NULL, 0, MPI_BYTE, from_rank, PP_NO_MORE, PP_COMM_WORLD, &status);
				PP_NO_MORE_WORK_FLAG = 1;
			}
		}
	}
	return NULL; //void function should return NULL
}

/*
PP Init is the first PP call that must me made. It Initializes MPI and creates a 
	server thread and gives it the amount of space specified by the parameter 
	max_mem_by_server. PP_Init also tells what and how many user types the user 
	will be using in their program. This function returns PP_SUCCESS if everything 
	was set up correctly.
*/
int PP_Init(int max_mem_by_server, int num_user_types, int user_types[])
{
    //printf("Starting init\n");
	
	//initialize helper variables
    int rc, initialized, ts;

	//see if MPI_Init() has been called
    rc = MPI_Initialized(&initialized);

	//if it has not, then call it
    if(!initialized)
    {
        //printf("inside init if\n");
        MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &ts);
        //printf("finishing init if\n");
    }
    else //if it has, then check the thread support
    {
        MPI_Query_thread(&ts);
        if(ts != MPI_THREAD_MULTIPLE)
        {
            //printf("HERPDERP FAILED!");
        }
    }

	//set up our server variables
    server_size = max_mem_by_server;
    num_types = num_user_types;
    //printf("init: Before reassigning pointer crap\n");
	types = (int*)calloc(num_types, sizeof(int));
    for(int i = 0; i < num_types; i++)
    {
       //printf("init:before assignment\n");
       types[i] = user_types[i];
       //printf("init: after assignment\n");
    }
	
    //printf("Before comm_dup\n");
	//set up PP_COMM_WORLD
    MPI_Comm_dup(MPI_COMM_WORLD, &PP_COMM_WORLD);
    MPI_Comm_size(PP_COMM_WORLD, &num_ranks);
    MPI_Comm_rank(PP_COMM_WORLD, &myrank);
	
	//get ready for puts
	next_put_rank = myrank;
	PP_NO_MORE_WORK_FLAG = 0;
	
    //printf("Before pthread_create\n");
    //create a server thread for this app
	pthread_create(&server_thread, NULL, pp_server, NULL);
    //printf("Finished init from server: %d\n", myrank);

    return PP_SUCCESS;
}

/*
PP_Finalize is the last PP function call that will be made by an app. It waits for
	everyone else to also call finalize, and then it tells the server to quit,
	rejoins the threads, and calls MPI_Finalize()
*/
int PP_Finalize()
{
    //printf("Staring Finalize rank %d\n", myrank);
	//set up helper variables
    int rc, flag;

	//wait for everyone else to call PP_Finalize
    MPI_Barrier(PP_COMM_WORLD);
	
    //printf("Finished Barrier from server %d\n", myrank);
	//check and see if MPI_Finalize() has been called
	rc = MPI_Finalized(&flag);
	
	//printf("rc from server %d is %d\n", myrank, rc);
	//if it has not, then call it/let your server know its time to die, and then rejoin the server thread
    if(!flag)
    {
        //printf("inside finalize if from server %d\n", myrank);
        rc= MPI_Send(NULL, 0, MPI_BYTE, myrank, PP_LOCAL_APP_DONE, PP_COMM_WORLD);
		//printf("after send from server %d\n", myrank);
        rc=pthread_join(server_thread, NULL);
        MPI_Finalize();
        //printf("After MPI_Finalize()\n");
    }
	//printf("Before return at rank %d\n", myrank);
    return PP_SUCCESS;
}

/*
PP_Put is a function that allows an app to store data on a server. The user passes in 
	various parameters describing and including the data. The app asks a server if it 
	can take the data, if it cannot, the app asks another server. Returns PP_Success 
	if the data is put on a server. Returns PP_FAIL if it loops through every server 
	twice, and cannot find a place to put it.
*/
int PP_Put(void *buf, int length, int type, int answer_rank, int target_rank, int handle[])
{
    //printf("Starting PP_Put on server %d\n", myrank);
   //handle[0] = rank where data is
   //handle[1] = unique ID on that rank
   //handle[2] = size of work
   //handle[3] = type of work

	//set up helper variables
    int rc, resp, send_data[BUFFER_SIZE], target;
    MPI_Status status;
    MPI_Request request;
	
	//variable for put checkers
	bool putFlag = false;
	int put_counter = 1;

	//set up a buffer to send to a server
    send_data[0] = length;
    send_data[1] = type;
    send_data[2] = answer_rank;
    send_data[4] = myrank;

	//set the target and reserved flag if specified, if not, then put it on the next available rank
    if(target_rank >= 0)
	{
		//printf("I am going to put the data on server %d\n", target_rank);
        send_data[3] = target_rank;
		send_data[5] = target_rank;
		target = target_rank;
    }
	else
    {
		//printf("If none specified, i will move onto nextputrank: %d\n", next_put_rank);
        target = next_put_rank;
        send_data[3] = target;
		send_data[5] = -1;
		next_put_rank = (next_put_rank + 1) % num_ranks;
    }
    
	//have the app find a server who will take the data
	while(!putFlag)
	{
		//wait for a good response
		MPI_Irecv(&resp, 1, MPI_INT, MPI_ANY_SOURCE, PP_PUT_RESP, PP_COMM_WORLD, &request);
		//printf("target = %d", target);
		MPI_Send(send_data, BUFFER_SIZE, MPI_INT, target, PP_PUT_REQ, PP_COMM_WORLD);
		MPI_Wait(&request, &status);
		
		//then send the data to the server
		target = status.MPI_SOURCE;
		if(resp == PP_ALLOW_PUT)
		{
			MPI_Rsend(buf, length, MPI_BYTE, target, PP_PUT_DATA, PP_COMM_WORLD);
			MPI_Recv(handle, HANDLE_SIZE, MPI_INT, target, PP_HANDLE, PP_COMM_WORLD, &status);
			putFlag=true;
		}
		else if(resp == PP_PUT_RING && put_counter != num_ranks*2)
		{
			//keep track of how many times you have sent the message around the ring
			//printf("passing the item to next place in ring\n");
			target = next_put_rank;
			next_put_rank = (next_put_rank + 1) % num_ranks;
			++put_counter;
			//cout <<  "Put couner: " << put_counter << endl;
		}
		else //return fail if it's been too long
		{
			//printf("I made it into put fail state\n");
			return PP_FAIL;
		}
	}
	return PP_SUCCESS;
}

/*
PP_FindAndReserve has the app first ask it's server if it has any data that 
	matches any of the types in types_to_search_for. If it does, and the data is
	not already reserved, the server sets the reserve flag and sends back a response 
	to the app and with the handle of the info. If the data was not found on the server,
	the server sends the message to his right.
*/
int PP_FindAndReserve(int num_types_in_req, int types_to_search_for[], int *size_of_work_found,
                        int *type_of_work_found, int handle[])
{
	//create helper variables
	int rc, resp;
	
	MPI_Status status;
	MPI_Request request;
	
	//make a buffer to send data
	int send_data[BUFFER_SIZE];
	
	send_data[0] = num_types_in_req;
	send_data[1] = myrank;
	
	//send the data to current rank's associated server thread asking for a find and reserve
	MPI_Irecv(&resp, 1, MPI_INT, MPI_ANY_SOURCE, PP_FIND_AND_RESERVE_RESP, PP_COMM_WORLD, &request);
	//printf("Starting find and Reserve send data on rank: %d\n", myrank);
	MPI_Send(send_data, BUFFER_SIZE, MPI_INT, myrank, PP_FIND_AND_RESERVE_REQ, PP_COMM_WORLD);
	
	//printf("num_types_in_req: %d\n", num_types_in_req);
	//send the correct amount of items
	if(num_types_in_req != 0)
	{
		//printf("Starting find and Reserve send types if > 0 on rank: %d\n", myrank);
		MPI_Send(types_to_search_for, num_types_in_req, MPI_INT, myrank, PP_TYPES, PP_COMM_WORLD);
	}
	else
	{
		int types2[] = {0};
		//printf("Starting find and Reserve send on rank if 0: %d\n", myrank);
		MPI_Send(types2, 1, MPI_INT, myrank, PP_TYPES, PP_COMM_WORLD);
	}
	MPI_Wait(&request, &status);
	//printf("After find and Reserve wait on rank: %d\n", myrank);
	
	//once the data has been found, get the handle back and update pointer variables passed in through function
	if(resp == PP_FOUND)
	{
		//printf("Starting find and Reserve recv on rank: %d\n", myrank);
		MPI_Recv(handle, HANDLE_SIZE, MPI_INT, status.MPI_SOURCE, PP_FOUND, PP_COMM_WORLD, &status);
		*size_of_work_found = handle[2];
		*type_of_work_found = handle[3];
		//printf("Found the work!\n");
	}
	else if(resp == PP_NO_MORE_WORK) //make sure PP_NO_MORE_WORK flag has not been set
	{
		return PP_NO_MORE_WORK;
	}
	else //make sure we are not exhausted
	{
		return PP_EXHAUSTION;
	}
	
	return PP_SUCCESS;
	
}

//PP_Find does the same as PP_FindAndReserve, except the reserver flag is never flipped. Look above for comments.	
int PP_Find(int num_types_in_req, int types_to_search_for[], int *size_of_work_found, int *type, int handle[])
{
	//printf("Starting find\n");
	int rc, resp;
	
	MPI_Status status;
	MPI_Request request;
	
	int send_data[BUFFER_SIZE];
	send_data[0] = num_types_in_req;
	send_data[1] = myrank;
	
	//printf("Num types in req: %d\n", num_types_in_req);
	//for(int i = 0; i < num_types_in_req; i++)
	//	printf("%d ", types_to_search_for[i]);
	
	MPI_Irecv(&resp, 1, MPI_INT, MPI_ANY_SOURCE, PP_FIND_RESP, PP_COMM_WORLD, &request);
	//printf("Starting find send data on rank: %d\n", myrank);
	MPI_Send(send_data, BUFFER_SIZE, MPI_INT, myrank, PP_FIND_REQ, PP_COMM_WORLD);
	if(num_types_in_req != 0)
	{
		//printf("Starting find send types if > 0 on rank: %d\n", myrank);
		MPI_Send(types_to_search_for, num_types_in_req, MPI_INT, myrank, PP_TYPES, PP_COMM_WORLD);
	}
	else
	{
		int types2[] = {0};
		//printf("Starting find send on rank if 0: %d\n", myrank);
		MPI_Send(types2, 1, MPI_INT, myrank, PP_TYPES, PP_COMM_WORLD);
	}
	MPI_Wait(&request, &status);
	//printf("After find wait on rank: %d\n", myrank); 
	
	if(resp == PP_FOUND)
	{
		//printf("Starting find recv on rank: %d\n", myrank);
		MPI_Recv(handle, HANDLE_SIZE, MPI_INT, status.MPI_SOURCE, PP_FOUND, PP_COMM_WORLD, &status);
		//printf("Ending find recv on rank: %d\n", myrank);
		*size_of_work_found = handle[2];
		*type = handle[3];
	}
	else if(resp == PP_NO_MORE_WORK)
	{
		return PP_NO_MORE_WORK;
	}
	else
	{
		return PP_EXHAUSTION;
	}
	//if(myrank == 1)
	//	printf("SUCCESS!\n");
	return PP_SUCCESS;
}

/*
PP_Get is meant to be used in conjunction with PP_FindAndReserve. It takes the handle
	returned from PP_FindAndReserve and then asks the server who has that data for it. 
	If the server allows the Get, then the server gives the data to the app. If not, 
	then the function returns a PP_FAIL in the app.
*/
int PP_Get(void * buf, int handle[])
{
	//create helper variables
	int get_rank, size, resp;
	
	MPI_Request request;
	MPI_Status status;
	
	//create variables to handle relevent information from the handle.
	get_rank = handle[0];
	size = handle[2];
	
	//printf("Before Send in Get\n");
	//send the handle to the server from the handle and wait for a response
	MPI_Irecv(&resp, 1, MPI_INT, get_rank, PP_GET_RESP, PP_COMM_WORLD, &request);
	MPI_Send(handle, HANDLE_SIZE, MPI_INT, get_rank, PP_GET_REQ, PP_COMM_WORLD);
	MPI_Wait(&request, &status);
	
	//printf("Recieved response from rank %d and it says %d\n", get_rank, resp);
	//if you are allowed to get the data, recieve it
	if(resp == PP_ALLOW_GET)
	{
		//printf("Before Recieve in PP_Get on rank, %d\n", myrank);
		MPI_Recv(buf, size, MPI_BYTE, status.MPI_SOURCE, PP_GET_DATA, PP_COMM_WORLD, &status);
		//printf("After Recieve in PP_Get on rank, %d\n", myrank);
	}
	else if(resp == PP_NO_MORE_WORK) //make sure PP_NO_MORE_WORK flag is not set
	{
		return PP_NO_MORE_WORK;
	}
	else //otherwise return PP_FAIL
	{
		return PP_FAIL;
	}
	//printf("%d\n", PP_SUCCESS);
	return PP_SUCCESS;
}

/*
PP_Copy is meant to be used in conjunction to a PP_Find or PP_FindAndReserve. In the
	handle returned from either function, it takes the handle, finds the data, and 
	then makes a copy of the info.
*/
int PP_Copy(void *buf, int *num_times_copied_previously_by_anyone, int handle[])
{
	//initialize helper variables
	int rc, resp;
	
	MPI_Request request;
	MPI_Status status;
	
	//make easy to use variables to hold reletive handle info
	int data_rank = handle[0];
	int size = handle[2];

	//printf("Before Copy Send at rank: %d\n", myrank);
	//ask the appropriate server if you can copy the data
	MPI_Irecv(&resp, 1, MPI_INT, data_rank, PP_COPY_RESP, PP_COMM_WORLD, &request);
	MPI_Send(handle, HANDLE_SIZE, MPI_INT, data_rank, PP_COPY_REQ, PP_COMM_WORLD);
	MPI_Wait(&request, &status);
	//printf("After Copy at rank %d\n", myrank);
	
	//if he responds yes then recieve a copy of the data and the # of times it has been copied
	if(resp == PP_ALLOW_COPY)
	{
		MPI_Recv(buf, size, MPI_BYTE, data_rank, PP_COPY, PP_COMM_WORLD, &status);
		MPI_Recv(num_times_copied_previously_by_anyone, 1, MPI_INT, data_rank, PP_COPY_COUNT, PP_COMM_WORLD, &status);
	}
	else if(resp == PP_NO_MORE_WORK) //see if PP_NO_MORE_WORK flag was flipped at some point
	{
		return PP_NO_MORE_WORK;
	}
	else //return PP_Fail otherwise
		return PP_FAIL;
	
	return PP_SUCCESS;
}

//PP_Set_problem_done tells an apps server that there is no more work to do.
int PP_Set_problem_done()
{
    int rc;

    for(int i=0; i < num_ranks; i++)
    {
        MPI_Send(NULL, 0, MPI_BYTE, i, PP_NO_MORE, PP_COMM_WORLD);
    }
    return PP_SUCCESS;
}

//PP_Abort calls MPI_Abort with an abort code.
int PP_Abort(int code)
{
    MPI_Abort(PP_COMM_WORLD, code);
    return 1;
}