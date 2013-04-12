/*

Header file for PP library calls.

*/

//PP Return Codes
#define PP_SUCCESS (1)
#define PP_FAIL (-1)
#define PP_EXHAUSTION (0)
#define PP_NO_MORE_WORK (-99)

//PP Tags
#define PP_PUT_REQ                  1001
#define PP_PUT_DATA                 1002
#define PP_READY_TO_RECV_PUT_DATA   1003
#define PP_PUT_RESP                 1004
#define PP_FIND_AND_RESERVE_REQ     1005
#define PP_FIND_AND_RESERVE_RESP    1006
#define PP_GET_REQ                  1007
#define PP_GET_RESP                 1008
#define PP_FIND_REQ                 1009
#define PP_FIND_RESP                1010
#define PP_LOCAL_APP_DONE           1011
#define PP_ALLOW_PUT				1012
#define PP_PUT_RING					1013
#define PP_HANDLE					1014
#define PP_ALLOW_GET				1015
#define PP_GET_DATA					1016
#define PP_DENY_GET					1017
#define PP_TYPES					1018
#define PP_FOUND					1019
#define PP_COPY_REQ					1020
#define PP_COPY_RESP				1021
#define PP_ALLOW_COPY				1022
#define PP_DENY_COPY				1023
#define PP_COPY						1024
#define PP_COPY_COUNT				1025
#define PP_EXHAUSTION_CHK			1026
#define PP_EXHAUST					1027
#define PP_NO_MORE					1028

//Hanlde Size for pp.cpp and mainFile
#define HANDLE_SIZE                 4

/*
PP Init is the first PP call that must me made. It Initializes MPI and creates a 
	server thread and gives it the amount of space specified by the parameter 
	max_mem_by_server. PP_Init also tells what and how many user types the user 
	will be using in their program. This function returns PP_SUCCESS if everything 
	was set up correctly.
*/
int PP_Init(int max_mem_by_server, int num_user_types, int user_types[]);

/*
PP_Finalize is the last PP function call that will be made by an app. It waits for
	everyone else to also call finalize, and then it tells the server to quit,
	rejoins the threads, and calls MPI_Finalize()
*/
int PP_Finalize();

/*
PP_Put is a function that allows an app to store data on a server. The user passes in 
	various parameters describing and including the data. The app asks a server if it 
	can take the data, if it cannot, the app asks another server. Returns PP_Success 
	if the data is put on a server. Returns PP_FAIL if it loops through every server 
	twice, and cannot find a place to put it.
*/
int PP_Put(void *buf, int length, int type, int answer_rank, int target_rank, int handle[]);

/*
PP_FindAndReserve has the app first ask it's server if it has any data that 
	matches any of the types in types_to_search_for. If it does, and the data is
	not already reserved, the server sets the reserve flag and sends back a response 
	to the app and with the handle of the info. If the data was not found on the server,
	the server sends the message to his right.
*/
int PP_FindAndReserve(int num_types_in_req, int types_to_search_for[], int *size_of_work_found,
                        int *type_of_work_found, int handle[]);

/*
PP_Find does the same as PP_FindAndReserve, except the reserver flag is never flipped.
*/						
int PP_Find(int num_types_in_req, int types_to_search_for[], int *size_of_work_found, int *type,
            int handle[]);

/*
PP_Get is meant to be used in conjunction with PP_FindAndReserve. It takes the handle
	returned from PP_FindAndReserve and then asks the server who has that data for it. 
	If the server allows the Get, then the server gives the data to the app. If not, 
	then the function returns a PP_FAIL in the app.
*/
int PP_Get(void *buf, int handle[]);

/*
PP_Copy is meant to be used in conjunction to a PP_Find or PP_FindAndReserve. In the
	handle returned from either function, it takes the handle, finds the data, and 
	then makes a copy of the info.
*/
int PP_Copy(void *buf, int *num_times_copied_previously_by_anyone, int handle[]);

/*
PP_Set_problem_done tells an apps server that there is no more work to do.
*/
int PP_Set_problem_done();

/*
PP_Abort calls MPI_Abort with an abort code.
*/
int PP_Abort(int code);