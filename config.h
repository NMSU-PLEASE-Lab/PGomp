
//
// PGOMP Configuration Parameters
//

// Output file name
#define OUTPUT_FILENAME "pgomp-out.txt"

// Maximum number of threads that libPGOMP can handle
#define MAX_THREADS 100 /**< Maximum number of threads can be used */

// By default, times are output as real value seconds since Jan 1, 1970.
// If you want times relative to the beginning of the program, uncomment
// the following #define. It will incur an extra double subtraction each
// time a timestamp is needed.
//#define RELATIVE_TIME

//
// All Gnu platforms should implement these built-in functions that provide
// the return address (i.e., the location from which we are called). If this
// is failing for you, just comment out the first, and uncomment the second.
//
#define getReturnAddress(v) __builtin_extract_return_addr(__builtin_return_address(v))
//#define getReturnAddress(v) 0

// - If you need to debug the library, uncomment out the #define and
//   then set debug level appropriately. 0 will turn off debugging output
//   but checking the variable will incur overhead if GOMP_DEBUG is defined.
//   - note: there is not much debugging output inserted into the source:
//     see GOMP_parallel_start() for an example of what it should look like.
//
//#define GOMP_DEBUG
#ifdef GOMP_DEBUG
static int gompDebug = 1;
#endif

#define NUM_EVENTS 2

#define ERROR_RETURN(retval) { fprintf(stderr, "Error %d %s:line %d: \n", retval,__FILE__,__LINE__);  exit(retval); }

//Starts counting the events named in the events array.
#define START_COUNTER\
   if ( (retval = PAPI_start_counters(Events, NUM_EVENTS)) != PAPI_OK)\
      ERROR_RETURN(retval);\
//Stops counting the events named in the events array.
#define STOP_COUNTER\
   if ((retval=PAPI_stop_counters(values, NUM_EVENTS)) != PAPI_OK)\
      ERROR_RETURN(retval);\

