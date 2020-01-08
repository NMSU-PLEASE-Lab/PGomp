/**
   @mainpage  PGOMP
   @version pgomp0.1 \n
   @author Mustafa Elfituri \n
   @date Last update Tuesday, April 30, 2013 \n

    PGOMP is an easy to use tool that measure various aspects of OpenMP program
    performance. It is a small library-based profiling tool for the
    Gnu OpenMP implementation. It measures overhead within the Gnu
    OpenMP implementation.

    PGOMP is a useful addition to OpenMP toolsets because it does
    not interfere with source-processing tools, it is easy to use and
    self-contained, and it produces output that supports a variety of
    analyses. For example, PGOMP would be an ideal tool to slip into
    a nightly build and test process, giving performance feedback on
    the tests that can be tracked from build to build.

    Also, PGOMP would be an ideal tool in an
    educational environment, where simplicity, ease of use, and
    ease of installation and maintenance are at a premium.

   \n This tool is still under development.
**/

#define _GNU_SOURCE // required -- PGOMP is Gnu specific, not useful for other compilers

#define BILLION  1000000000.0
#define HTABLE_SIZE 5000 /**< Hash table size */
#define MAX_MODE_FLAG 2 /**< Maximum value for mode variable */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <omp.h>
#include <sys/time.h>
#include <time.h>
#include "config.h"
#include"papi.h"


// 
// Proper definition for static function name according to
// http://gcc.gnu.org/onlinedocs/gcc/Function-Names.html#Function-Names
// -- then use "__func__" in source code
//
#if __STDC_VERSION__ < 199901L
# if __GNUC__ >= 2
#  define __func__ __FUNCTION__
# else
#  define __func__ "<unknown>"
# endif
#endif

typedef enum { false, true } bool;

/**
   Records per-thread, per-function, per-invocation-site performance information
**/
typedef struct
{
/*@{*/
   const char *startName; /**< Start function name */
   const char *endName; /**< End function name */
   void* beginAddr; /**< Start function return address */
   void* endAddr; /**< End function return address */
   double startTime_1; /**< Time thread reach the start function */
   double startTime_2; /**< Time thread reach the end function */
   double startExTime; /**< Start execution time of the function section */
   double endTime; /**< Time thread finsh end function */
   long long iCount;
/*@}*/
} PerThreadInfo;

/**
   Records per-function, per-invocation-site performance information
**/
typedef struct
{
/*@{*/
   const char *funName; /**< function name */
   void* beginAddr; /**<  beginning return address */
   void* endAddr; /**<  end return address */
   int thId; /**< thread Id */
   double wTime; /**< time thread locking. */
   double exTime; /**< time thread spends executing the critical section */
   long count; /**< times of repetition */
   long long iCount; /** instructions count */
/*@}*/
} AggregateInfo;

static PerThreadInfo
   lock[MAX_THREADS],
   critical[MAX_THREADS],
   namedCritical[MAX_THREADS],
   barrier[MAX_THREADS],
   nestedLock[MAX_THREADS],
   parallel[MAX_THREADS],
   single[MAX_THREADS];

static AggregateInfo hTable[HTABLE_SIZE];

static FILE * outFile = NULL;

//static double seqTime, seqStartTime, totalSeqTime, totalParaTime, endProgTime;
//static double ParallelTotalTime=0.0, parallelTime;
static int modeFlag,  papiFlag=0;

//
// Function pointers for real GOMP/OMP functions
//
static int(*real_omp_unset_lock)(omp_lock_t *pLock) = NULL;
static int (*real_omp_set_nest_lock)(omp_nest_lock_t *pLock) = NULL;
static int (*real_omp_test_nest_lock)(omp_nest_lock_t *pLock) = NULL;
static int(*real_omp_unset_nest_lock)(omp_nest_lock_t *pLock) = NULL;
static void (*real_GOMP_barrier)(void) = NULL;
static int (*real_omp_test_lock)(omp_lock_t *pLock) = NULL;
static int (*real_omp_set_lock)(omp_lock_t *pLock) = NULL;
static int (*real_omp_destroy_lock)(omp_lock_t *pLock) = NULL;
static int (*real_omp_init_lock)(omp_lock_t *pLock) = NULL;
static int (*real_GOMP_critical_start)(void) = NULL;
static int (*real_GOMP_critical_end)(void) = NULL;
static int (*real_GOMP_critical_name_start)(void ** name) = NULL;
static int (*real_GOMP_critical_name_end)(void ** name) = NULL;
static int (*real_GOMP_parallel_start)(void (*fn)(void *),
            void *data, unsigned num_threads) = NULL;
static int (*real_GOMP_parallel_end)(void) = NULL;
static bool (*real_GOMP_single_start)(void) = NULL;
char errstring[PAPI_MAX_STR_LEN];

#ifdef BUILD_PAPI
   static long long instCount[MAX_THREADS],noCycle[MAX_THREADS];
   static int  ioverhead,cOverhead=0;
#endif
int numOfThreads;


/*--------------------------------------------------------------------*
 * Open file function                                                 *
 *--------------------------------------------------------------------*/

/**
   @brief Open a new file.
           Stop the program and exit if can not open the file.
   @return Void.
**/
static void openFile()
{
   outFile = fopen (OUTPUT_FILENAME, "w");
   if (outFile == NULL)
   {
      fprintf(stderr,"LIBPGOMP ERROR: Thread %d cannot open file\n", omp_get_thread_num());
         exit (0);
   }
}

/*--------------------------------------------------------------------*
 * getTime function to return the time.                               *
 *--------------------------------------------------------------------*/
#ifdef RELATIVE_TIME
static double initialTime=0.0;
#endif
/**
   @brief Gets the clock time.
   @return The number of seconds and nanoseconds since 1/1/1970
**/
static double getTime()
{
   struct timespec tim;
   if (clock_gettime(CLOCK_REALTIME, &tim) == -1)
   {
      perror("clock gettime");
      exit(EXIT_FAILURE);
   }
#ifdef RELATIVE_TIME
   return tim.tv_sec-initialTime+tim.tv_nsec/BILLION;
#else
   return tim.tv_sec+tim.tv_nsec/BILLION;
#endif
}

/*-------------------------------------------------------------------*
 * hash function                                                     *
 *-------------------------------------------------------------------*/

/**
   @brief Calculates hash table index.
   @param add - Function return address.
   @param tId - Thread Id.
   @return Hash table index.
**/
static unsigned int hash(void* add, int tId)
{
   return (((unsigned long) add) + tId) % HTABLE_SIZE;
}

/*-------------------------------------------------------------------*
 * addBucket function                                                *
 *-------------------------------------------------------------------*/

/**
   @brief Adds new bucket to hash table.
   @param index - hash table index.
   @param thId - Thread Id.
   @param name - Function name
   @param beginAddr - Start function return address.
   @param endAddr - End function return address.
   @param wTime - Thread waiting time.
   @param exTime - execution time.
   @return Hash void.
**/
static void addBucket(int index, int thId,  const char *name, void* beginAddr,
                void* endAddr, double wTime, double exTime, long long insCount)
{
   hTable[index].funName = name;
   hTable[index].beginAddr = beginAddr;
   hTable[index].endAddr = endAddr;
   hTable[index].thId = thId;
   hTable[index].wTime = wTime;
   hTable[index].exTime = exTime;
   hTable[index].count = 1;
   hTable[index].iCount = insCount;
}

/*-------------------------------------------------------------------*
 * updateBucket function                                             *
 *-------------------------------------------------------------------*/

/**
   @brief Updates an existing bucket in hash table.
   @param index - hash table index.
   @param wTime - thread waiting time.
   @param exTime - execution time.
   @return Hash void.
**/
static void updateBucket(int index, double wTime, double exTime,long long insCount)
{
   hTable[index].count++;
   hTable[index].wTime += wTime;
   hTable[index].exTime += exTime;
   hTable[index].iCount +=insCount;
}

/*-------------------------------------------------------------------*
 * editBucket function                                               *
 *-------------------------------------------------------------------*/

/**
   @brief If the bucket does not exist in the hash table adds it as a new
          bucket to hash table. If the bucket is already existed updates it.
   @param index - hash table index.
   @param thId - Thread Id.
   @param name - Function name
   @param beginAddr - Start function return address.
   @param endAddr - End function return address.
   @param wTime - Thread waiting time.
   @param exTime - execution time.
   @return Hash void.
**/
static void editBucket(int index, int thId, const char *name, void* beginAddr,
                 void* endAddr,double wTime, double exTime,long long insCount)
{
   int count = 0, found = 0;
   while (hTable[index].funName != NULL && found == 0)
   {
      if (hTable[index].beginAddr == beginAddr
          && strcmp(hTable[index].funName , name) == 0
          && hTable[index].thId == thId)
      {
         found=1;// Bucket already existed
      }
      else
      {
         index = (index + 1) % HTABLE_SIZE;
         count++;
         if (count >= HTABLE_SIZE - 1)
         {
            fprintf(stderr,"LIBPGOMP ERROR: Hash table is full\n");
            exit(0);
         }
      }
   }
   if (hTable[index].funName != NULL)
   {
      // Bucket already existed.
      updateBucket(index, wTime, exTime,insCount);
   }
   else
   {
      // Bucket not found.
      addBucket(index, thId, name, beginAddr, endAddr, wTime, exTime, insCount);
   }
}

/*---------------------------------------------------------------*
 *  Initialize the PAPI library                                  *
 *---------------------------------------------------------------*/
void initPapi()
{
   int retval;
   long long values[NUM_EVENTS];
   int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
   if((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT )
   {
      fprintf(stderr, "Error: %d %s\n",retval, errstring);
      exit(1);
   }
   if ( PAPI_thread_init((unsigned long (*)(void))(omp_get_thread_num)) != PAPI_OK)
      ERROR_RETURN(retval);
   START_COUNTER;
   STOP_COUNTER;
}

/*--------------------------------------------------------------------*
 * Instruction counts overhead
 *--------------------------------------------------------------------*/
int instOverhead()
{
   int retval;
   int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
   long long values[NUM_EVENTS];
   START_COUNTER;
   STOP_COUNTER;
   return values[0];
}

/*-------------------------------------------------------------------*
 * printResult function                                               *
 *-------------------------------------------------------------------*/

/**
 @brief Prints hash table data.
   @param table[] - Hash table.
   @return Hash void.
**/
static void printResult(AggregateInfo table[])
{
   int index;
if(papiFlag)
   for (index = 0; index < HTABLE_SIZE ; index++)
   {
      if (table[index].count > 0)
         fprintf(outFile, " %s %p %p %d %lf %lf %ld %lld\n",
                    table[index].funName, table[index].beginAddr,
                    table[index].endAddr, table[index].thId,
                    table[index].wTime, table[index].exTime,
                    table[index].count,
                    table[index].iCount);
   }
else
   for (index = 0; index < HTABLE_SIZE ; index++)
   {
  if (table[index].count > 0)
         fprintf(outFile, " %s %p %p %d %lf %lf %ld \n",
                    table[index].funName, table[index].beginAddr,
                    table[index].endAddr, table[index].thId,
                    table[index].wTime, table[index].exTime,
                    table[index].count);
}
}

/**
   @brief Error checking wrapper around library dlsym() symbol lookup.
**/
static void* lookupFunction(char* name)
{
   char* dlerr;
   void* functionPtr;
   dlerror(); // clear dlerror flag
   functionPtr = dlsym(RTLD_NEXT, name);
   dlerr = dlerror();
   if (dlerr || !functionPtr) {
      fprintf(stderr,"LIBPGOMP ERROR: Unable to resolve real %s: (%s)\n",name,
              (dlerr? dlerr : "no error message available"));
      exit(0);
   }
   return functionPtr;
}

/**
   @brief Lookup function name that an address is inside of.
**/
//
// This will not be reliable because dladdr() requires the symbol to be
// accessible as an exported dynamic symbol. If you compile your program
// with "-Wl,--export-dynamic" then it might work relatively well. For
// now we do not have it integrated into the code.
//
static const char* lookupFunctionName(void* address)
{
   Dl_info info;
   if (dladdr(address,&info))
      return 0;
   return info.dli_sname;
}

/*-------------------------------------------------------------------*
 * Constructor attribute                                             *
 *-------------------------------------------------------------------*/

/**
   @brief It will execute automatically when the tool runs.
          It calls open file function and gets and checks Environment variable
          PGOMP_MODE's value.
**/

__attribute__((constructor)) void pgomp_init()
{
   char* mode;
#ifdef RELATIVE_TIME
   initialTime = getTime();
#endif
   openFile();
   if (getenv("PGOMP_MODE") == NULL)
   {
      mode = "aggregate";
   }
   else
   {
      mode = getenv("PGOMP_MODE"); // Get Environment variable PGOMP_MODE's value.
   }
   if (strcmp(mode , "trace") == 0)// Trace mode
      modeFlag = 1;
   else if (strcmp(mode , "aggregate") == 0)// Aggregate mode
      modeFlag = 2;
   if (modeFlag < 1 || modeFlag> MAX_MODE_FLAG)
   {
      fprintf(stderr,"LIBPGOMP ERROR: Environment variable "
                     "PGOMP_MODE not 'trace' or 'aggregate'\n");
      exit(0);
   }
#ifdef BUILD_PAPI
   char* papiMode;
   if (getenv("PGOMP_PAPI") == NULL)
   {
      papiMode = "false";
      papiFlag = 0;
   }
   else
   {
      papiMode = getenv("PGOMP_PAPI"); // Get Environment variable PGOMP_PAPI's value.
      if (strcmp(papiMode , "true") == 0)// Using PAPI
      {
         papiFlag = 1;
         initPapi();
         ioverhead=instOverhead();
      }
      else if (strcmp(papiMode , "false") == 0)// Do not use PAPI
         papiFlag = 0;
         else //(papiFlag is not true or false )
         {
            fprintf(stderr,"LIBPGOMP ERROR: Environment variable PAPI_MODE "
                           "set incorrectly it should be 'true',false or unset\n");
            exit(0);
         }
     }
#else
   papiFlag = 0;
#endif
   //
   // Function lookups (do all at initialization, so runtime is faster)
   //
   real_omp_init_lock = lookupFunction("omp_init_lock");
   real_omp_destroy_lock = lookupFunction("omp_destroy_lock");
   real_omp_set_lock = lookupFunction("omp_set_lock");
   real_omp_test_lock = lookupFunction("omp_test_lock");
   real_omp_unset_lock = lookupFunction("omp_unset_lock");
   real_omp_set_nest_lock = lookupFunction("omp_set_nest_lock");
   real_omp_test_nest_lock = lookupFunction("omp_test_nest_lock");
   real_omp_unset_nest_lock = lookupFunction("omp_unset_nest_lock");
   real_GOMP_barrier = lookupFunction("GOMP_barrier");
   real_GOMP_critical_start = lookupFunction("GOMP_critical_start");
   real_GOMP_critical_end = lookupFunction("GOMP_critical_end");
   real_GOMP_critical_name_start = lookupFunction("GOMP_critical_name_start");
   real_GOMP_critical_name_end = lookupFunction("GOMP_critical_name_end");
   real_GOMP_parallel_start = lookupFunction("GOMP_parallel_start");
   real_GOMP_parallel_end = lookupFunction("GOMP_parallel_end");
   real_GOMP_single_start = lookupFunction("GOMP_single_start");
}

/*-------------------------------------------------------------------*
  Destructor attribute run at the end of the using of the library    *
 *-------------------------------------------------------------------*/
/**
   @brief It will execute automatically at the end of the using of the library.
          It Prints the result and close the output file.
**/
__attribute__((destructor)) void pgomp_end (void)
{
   if (modeFlag == 2)
      printResult(hTable);
   fclose(outFile);
}

/*-------------------------------------------------------------------*
 * omp_init_lock function                                            *
 *-------------------------------------------------------------------*/

void omp_init_lock(omp_lock_t *pLock)
{
   //int thId;
   //thId = omp_get_thread_num(); // maybe will use someday
   real_omp_init_lock(pLock);
}

/*-------------------------------------------------------------------*
 * omp_destroy_lock function                                         *
 *-------------------------------------------------------------------*/

void omp_destroy_lock(omp_lock_t *pLock)
{
   //int thId;
   //thId = omp_get_thread_num();  // maybe will use someday
   real_omp_destroy_lock(pLock);
}

/*-------------------------------------------------------------------*
 * omp_set_lock function                                             *
 *-------------------------------------------------------------------*/
/**
   @brief Gets the needed time values to calculate the thread locking
          overhead. Also, gets call location.
          Gets the start time which is the time when the current thread
          reach this function.
          Gets start execution time which is the time when the current
          thread acquired the lock[thId].
          Gets the return address of the function.
   @param lock - A variable of type omp_lock_t that was initialized
                 with omp_init_lock[thId].
   @return void
**/

void omp_set_lock(omp_lock_t *pLock)
{
   int thId;
   thId = omp_get_thread_num();
   lock[thId].beginAddr = getReturnAddress(0);
   lock[thId].startName = __func__;
   lock[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_omp_set_lock(pLock);
   if(papiFlag)
   {
      STOP_COUNTER;
      instCount[thId] =values[0]-ioverhead;
 //     noCycle[thId]+=values[1];
   }
   lock[thId].startExTime = getTime();
   if (modeFlag == 1)
   {
      if(papiFlag)
      {
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", lock[thId].startName,
              lock[thId].beginAddr, thId, lock[thId].startTime_1,
              lock[thId].startExTime,values[0]-ioverhead);
      }
      else
      {
          fprintf(outFile,"  %s %p %d %lf %lf \n", lock[thId].startName,
              lock[thId].beginAddr, thId, lock[thId].startTime_1,
              lock[thId].startExTime);
      }
   }
}

/*-------------------------------------------------------------------*
 * omp_test_lock function                                            *
 *-------------------------------------------------------------------*/
/**
   @brief Gets the needed time values to calculate the thread locking
          overhead. Also, gets call location.
   @param lock - A variable of type omp_lock_t that was initialized
          with omp_init_lock[thId].
   @return If attempts to set the lock specified by the variable
           succeed, the function returns TRUE; otherwise, the function
           returns FALSE
**/

int omp_test_lock(omp_lock_t *pLock)
{
   int thId, result;
   thId = omp_get_thread_num();
   lock[thId].beginAddr = getReturnAddress(0);
   lock[thId].startName = __func__;
   lock[thId].startTime_1 =  getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   result = real_omp_test_lock(pLock);
   if(papiFlag)
   {
      STOP_COUNTER;
      instCount[thId]=values[0]-ioverhead;
   //   noCycle[thId]+=values[1];
   }
   lock[thId].startExTime = getTime();
   if (modeFlag == 1)
   { 
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld  \n", lock[thId].startName,
                  lock[thId].beginAddr, thId, lock[thId].startTime_1,
                  lock[thId].startExTime,instCount[thId]);
      else
         fprintf(outFile," %s %p %d %lf %lf  \n", lock[thId].startName,
                  lock[thId].beginAddr, thId, lock[thId].startTime_1,
                  lock[thId].startExTime);
   }
   else if (modeFlag == 2)
       lock[thId].startTime_1 = lock[thId].startExTime;
   return result;
}

/*-------------------------------------------------------------------*
 * omp_unset_lock function                                           *
 *-------------------------------------------------------------------*/

/**
   @brief Calculates thread locking overhead which is the time thread
          spent waiting to acquire a lock[thId].
   @param lock - A variable of type omp_lock_t that was initialized
          with omp_init_lock[thId].
   @return void
**/

void omp_unset_lock(omp_lock_t *pLock)
{
   int thId, index;
   thId = omp_get_thread_num();
   lock[thId].startTime_2 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_omp_unset_lock(pLock);
   if(papiFlag)
   {
      STOP_COUNTER;
      instCount[thId]+=values[0]-ioverhead;
     // noCycle[thId]+=values[1];
   }
   if (modeFlag == 1)
   {
      lock[thId].endAddr = getReturnAddress(0);
      lock[thId].endTime = getTime();
      lock[thId].endName = __func__;
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld  \n", lock[thId].endName,
               lock[thId].endAddr, thId, lock[thId].startTime_2,
               lock[thId].endTime,values[0]-ioverhead);
      else
         fprintf(outFile,"  %s %p %d %lf %lf \n", lock[thId].endName,
               lock[thId].endAddr, thId, lock[thId].startTime_2,
               lock[thId].endTime);
   }
   else if (modeFlag == 2)
   {
      index = hash(lock[thId].beginAddr,thId);
      if(papiFlag)
         editBucket(index, thId, lock[thId].startName,
                     lock[thId].beginAddr,lock[thId].endAddr,
                     lock[thId].startExTime - lock[thId].startTime_1 ,
                     lock[thId].startTime_2 - lock[thId].startExTime,
                     instCount[thId]);
      else
         editBucket(index, thId, lock[thId].startName,
                     lock[thId].beginAddr,lock[thId].endAddr,
                     lock[thId].startExTime - lock[thId].startTime_1 ,
                     lock[thId].startTime_2 - lock[thId].startExTime,0);

   }
}

/*-------------------------------------------------------------------*
 * omp_set_nest_lock function                                        *
 *-------------------------------------------------------------------*/

/**
   @brief Gets the needed time values to calculate the thread locking
          overhead. Also, gets call location.
          Gets the start time which is the time when the current thread
          reach this function.
          Gets start execution time which is the time when the current
          thread acquired the lock[thId].
          Gets the return address of the function.
   @param lock - A variable of type omp_lock_t that was initialized
                 with omp_init_lock[thId].
   @return void
**/

void omp_set_nest_lock(omp_nest_lock_t *pLock)
{
   int thId;
   thId = omp_get_thread_num();
   nestedLock[thId].beginAddr = getReturnAddress(0);
   nestedLock[thId].startName = __func__;
   nestedLock[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_omp_set_nest_lock(pLock);
   if(papiFlag)
   {
      STOP_COUNTER;
      instCount[thId] =values[0]-ioverhead;
   //   noCycle[thId] =values[1];
   }
   nestedLock[thId].startExTime = getTime();
   if (modeFlag == 1)
   {
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", nestedLock[thId].startName,
                  nestedLock[thId].beginAddr, thId, nestedLock[thId].startTime_1,
                  nestedLock[thId].startExTime, instCount[thId]);
     else
        fprintf(outFile,"  %s %p %d %lf %lf  \n", nestedLock[thId].startName,
                  nestedLock[thId].beginAddr, thId, nestedLock[thId].startTime_1,
                  nestedLock[thId].startExTime);
   }
}

/*-------------------------------------------------------------------*
 * omp_test_nest_lock function                                       *
 *-------------------------------------------------------------------*/

/**
   @brief Gets the needed time values to calculate the thread locking
          overhead. Also, gets call location.
   @param lock - A variable of type omp_lock_t that was initialized
          with omp_init_lock[thId].
   @return If attempts to set the lock specified by the variable
           succeed, the function returns TRUE; otherwise, the function
           returns FALSE
**/

int omp_test_nest_lock(omp_nest_lock_t *pLock)
{
   int thId, result;
   thId = omp_get_thread_num();
   nestedLock[thId].beginAddr = getReturnAddress(0);
   nestedLock[thId].startName = __func__;
   nestedLock[thId].startTime_1 =  getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   result = real_omp_test_nest_lock(pLock);
   if(papiFlag)
   {
      STOP_COUNTER;
      instCount[thId]=values[0]-ioverhead;
 //   noCycle[thId]+=values[1];
   }
   nestedLock[thId].startExTime = getTime();
   if (modeFlag == 1)
   {
      if(papiFlag)
      
         fprintf(outFile," %s %p %d %lf %lf %lld \n", nestedLock[thId].startName,
                  nestedLock[thId].beginAddr, thId, nestedLock[thId].startTime_1,
                  nestedLock[thId].startExTime,instCount[thId]);
      else
         fprintf(outFile," %s %p %d %lf %lf  \n", nestedLock[thId].startName,
                  nestedLock[thId].beginAddr, thId, nestedLock[thId].startTime_1,
                  nestedLock[thId].startExTime);
   }
   else if (modeFlag == 2)
       nestedLock[thId].startTime_1 = nestedLock[thId].startExTime;
   return result;
}

/*-------------------------------------------------------------------*
 * omp_unset_nest_lock function                                      *
 *-------------------------------------------------------------------*/

/**
   @brief Calculates thread locking overhead which is the time thread
          spent waiting to acquire a lock[thId].
   @param lock - A variable of type omp_lock_t that was initialized
          with omp_init_lock[thId].
   @return void
**/

void omp_unset_nest_lock(omp_nest_lock_t *pLock)
{
   int thId, index;
   thId = omp_get_thread_num();
   nestedLock[thId].startTime_2 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_omp_unset_nest_lock(pLock);
   if(papiFlag)
   {
      STOP_COUNTER;
      instCount[thId]+=values[0]-ioverhead;
    //  noCycle[thId]+=values[1];
   }
   nestedLock[thId].endAddr = getReturnAddress(0);
   if (modeFlag == 1)
   {
      nestedLock[thId].endTime = getTime();
      nestedLock[thId].endName = __func__;
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld  \n", nestedLock[thId].endName,
                  nestedLock[thId].endAddr, thId, nestedLock[thId].startTime_2,
                  nestedLock[thId].endTime, values[0]-ioverhead);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", nestedLock[thId].endName,
                  nestedLock[thId].endAddr, thId, nestedLock[thId].startTime_2,
                  nestedLock[thId].endTime);

   }
   else if (modeFlag == 2)
   {
      index = hash(nestedLock[thId].beginAddr,thId);
      if(papiFlag)
         editBucket(index, thId, nestedLock[thId].startName,
                     nestedLock[thId].beginAddr,nestedLock[thId].endAddr,
                     nestedLock[thId].startExTime - nestedLock[thId].startTime_1 ,
                     nestedLock[thId].startTime_2 - nestedLock[thId].startExTime,
                     instCount[thId]);
      else
         editBucket(index, thId, nestedLock[thId].startName,
                     nestedLock[thId].beginAddr,nestedLock[thId].endAddr,
                     nestedLock[thId].startExTime - nestedLock[thId].startTime_1 ,
                     nestedLock[thId].startTime_2 - nestedLock[thId].startExTime,0);
   }
}

/*-------------------------------------------------------------------*
 * GOMP_barrier function                                             *
 *-------------------------------------------------------------------*/

/**
   @brief Calculates barrier Overhead which is the time earlier threads
          wait for later threads.
   @param void
   @return void
**/

void GOMP_barrier(void)
{
   int thId,index;
   thId=omp_get_thread_num();
   barrier[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];//To store our list of results
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_GOMP_barrier();
   if(papiFlag)
   {
      STOP_COUNTER;  
      instCount[thId]=values[0]-ioverhead;
    //  noCycle[thId]+=values[1];
   }
   barrier[thId].endTime = getTime();
   barrier[thId].beginAddr = barrier[thId].endAddr = getReturnAddress(0);
   barrier[thId].startName = __func__;
   if (modeFlag == 1)
   {
   if(papiFlag)
      fprintf(outFile,"  %s %p %d %lf %lf  %lld \n", barrier[thId].startName,
               barrier[thId].beginAddr, thId, barrier[thId].startTime_1,
               barrier[thId].endTime,instCount[thId]);
   else
      fprintf(outFile,"  %s %p %d %lf %lf \n", barrier[thId].startName,
               barrier[thId].beginAddr, thId, barrier[thId].startTime_1,
               barrier[thId].endTime); 
   }
   else if (modeFlag == 2)
   {
      index = hash(barrier[thId].beginAddr,thId);
      if(papiFlag)
         editBucket(index, thId, barrier[thId].startName,
                  barrier[thId].beginAddr,barrier[thId].endAddr,
                  barrier[thId].endTime - barrier[thId].startTime_1 ,
                  0.0,values[0]-ioverhead);
      else
         editBucket(index, thId, barrier[thId].startName,
                  barrier[thId].beginAddr,barrier[thId].endAddr,
                  barrier[thId].endTime - barrier[thId].startTime_1 ,
                  0.0,0);
   }
}

/*-------------------------------------------------------------------*
 *  GOMP_critical_start function                                     *
 *-------------------------------------------------------------------*/

/**
   @brief Gets the needed time values to calculate the Critical Section
          overhead. Also, gets call location.
          Gets the start time which is the time when the current thread
          reach this function.
          Gets start execution time which is the time when the current
          thread starts execution the corresponded critical section.
          Gets the return address of the function.
   @param void
   @return void
**/

void GOMP_critical_start(void)
{
   int thId;
   thId=omp_get_thread_num();
   critical[thId].beginAddr = getReturnAddress(0);
   critical[thId].startName = __func__;
   critical[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_GOMP_critical_start();
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]=values[0]-ioverhead;;
     // noCycle[thId]+=values[1];
   }
   critical[thId].startExTime = getTime();
   if (modeFlag == 1)
   {
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", critical[thId].startName,
                  critical[thId].beginAddr, thId, critical[thId].startTime_1,
                  critical[thId].startExTime,instCount[thId]);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", critical[thId].startName,
                  critical[thId].beginAddr, thId, critical[thId].startTime_1,
                  critical[thId].startExTime);
   }
}

/*-------------------------------------------------------------------*
 * GOMP_critical_end function                                        *
 *-------------------------------------------------------------------*/

/**
   @brief Calculates Critical Section overhead which is the time thread
          spent waiting to start executing the correspond critical
          section.
   @param lock - A variable of type omp_lock_t that was initialized
          with omp_init_lock[thId].
   @return void
**/

void GOMP_critical_end(void)
{
   int thId, index;
   thId = omp_get_thread_num();
   critical[thId].startTime_2 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
       int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
       START_COUNTER;
   }
#endif
   real_GOMP_critical_end();
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]+=values[0]-ioverhead;
     // noCycle[thId]+=values[1];
   }
   critical[thId].endAddr = getReturnAddress(0);
#ifdef GOMP_DEBUG
   if (gompDebug) fprintf(stderr,"GOMP Debug: GOMP_critical_end called from %s\n",
                          lookupFunctionName(critical[thId].endAddr));
#endif
   if (modeFlag == 1)
   {
      critical[thId].endTime = getTime();
      critical[thId].endName = __func__;
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld  \n", critical[thId].endName,
                  critical[thId].endAddr, thId, critical[thId].startTime_2,
                  critical[thId].endTime, values[0]-ioverhead);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", critical[thId].endName,
                  critical[thId].endAddr, thId, critical[thId].startTime_2,
                  critical[thId].endTime);    
   }
   else if (modeFlag == 2)
   {
      index = hash(critical[thId].beginAddr,thId);
      if(papiFlag)
         editBucket(index, thId, critical[thId].startName,
                     critical[thId].beginAddr,critical[thId].endAddr,
                     critical[thId].startExTime - critical[thId].startTime_1 ,
                     critical[thId].startTime_2 - critical[thId].startExTime,
                     instCount[thId]);
      else
          editBucket(index, thId, critical[thId].startName,
                     critical[thId].beginAddr,critical[thId].endAddr,
                     critical[thId].startExTime - critical[thId].startTime_1 ,
                     critical[thId].startTime_2 - critical[thId].startExTime,0);
   }
}

/*-------------------------------------------------------------------*
 * GGOMP_critical_name_start function                                *
 *-------------------------------------------------------------------*/

/**
   @brief Gets the needed time values to calculate the Critical Section
          overhead. Also, gets call location.
          Gets the start time which is the time when the current thread
          reach this function.
          Gets start execution time which is the time when the current
          thread starts execution the corresponded critical section.
          Gets the return address of the function.
   @param name - Critical Section name.
   @return void
**/

void GOMP_critical_name_start(void** name)
{
   int thId;
   thId = omp_get_thread_num();
   namedCritical[thId].beginAddr = getReturnAddress(0);
   namedCritical[thId].startName = __func__;
   namedCritical[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif
   real_GOMP_critical_name_start(name);
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]=values[0]-ioverhead;;
     // noCycle[thId]+=values[1];
   }
   namedCritical[thId].startExTime = getTime();
   if (modeFlag == 1)
   {
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", namedCritical[thId].startName,
               namedCritical[thId].beginAddr, thId, namedCritical[thId].startTime_1,
               namedCritical[thId].startExTime, values[0]-ioverhead);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", namedCritical[thId].startName,
               namedCritical[thId].beginAddr, thId, namedCritical[thId].startTime_1,
               namedCritical[thId].startExTime);
   }
}

/*-------------------------------------------------------------------*
 * GOMP_critical_name_end function                                   *
 *------------------------------------------------------------------ */

/**
   @brief Calculates Critical Section overhead which is the time thread
          spent waiting to start executing the correspond critical
          section.
   @param name - Critical Section name.
   @return void
**/

void GOMP_critical_name_end(void** name)
{
   int thId, index;
   thId = omp_get_thread_num();
   namedCritical[thId].startTime_2 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
      START_COUNTER;
   }
#endif   
   real_GOMP_critical_name_end(name);
   if(papiFlag)
   {
      STOP_COUNTER 
      instCount[thId]+=values[0]-ioverhead;
    //  noCycle[thId]+=values[1];
   }
   namedCritical[thId].endAddr = getReturnAddress(0);
   if (modeFlag == 1)
   {
      namedCritical[thId].endTime = getTime();
      namedCritical[thId].endName = __func__;
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", namedCritical[thId].endName,
                  namedCritical[thId].endAddr, thId, namedCritical[thId].startTime_2,
                  namedCritical[thId].endTime, values[0]-ioverhead);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", namedCritical[thId].endName,
                  namedCritical[thId].endAddr, thId, namedCritical[thId].startTime_2,
                  namedCritical[thId].endTime);
   }
   else if (modeFlag == 2)
   {
      index = hash(namedCritical[thId].beginAddr,thId);
      if(papiFlag)
         editBucket(index, thId, namedCritical[thId].startName,
                     namedCritical[thId].beginAddr,namedCritical[thId].endAddr,
                     namedCritical[thId].startExTime - namedCritical[thId].startTime_1 ,
                     namedCritical[thId].startTime_2 - namedCritical[thId].startExTime,
                     instCount[thId]);
      else
         editBucket(index, thId, namedCritical[thId].startName,
                     namedCritical[thId].beginAddr,namedCritical[thId].endAddr,
                     namedCritical[thId].startExTime - namedCritical[thId].startTime_1 ,
                     namedCritical[thId].startTime_2 - namedCritical[thId].startExTime,0);
   }
}

/*-------------------------------------------------------------------*
 * GOMP_parallel_start function                                      *
 *-------------------------------------------------------------------*/

/**
   @brief Gets the time values that related to parallel section.
   @return void
**/

void GOMP_parallel_start (void (*fn) (void *),
                           void *data, unsigned num_threads)
{
   int thId;
   thId = omp_get_thread_num();
#ifdef GOMP_DEBUG
   if (gompDebug) fprintf(stderr,"GOMP Debug: GOMP_parallel_start, thid=%d\n",thId);
#endif
   parallel[thId].beginAddr = getReturnAddress(0);
#ifdef GOMP_DEBUG
   if (gompDebug) fprintf(stderr,"GOMP Debug: GOMP_parallel_start called from %s\n",
                          lookupFunctionName(parallel[thId].beginAddr));
#endif
   parallel[thId].startName = __func__;
#ifdef GOMP_DEBUG
   if (gompDebug) fprintf(stderr,"GOMP Debug: starting GOMP_parallel_start, thid=%d\n",thId);
#endif
   parallel[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
      {
          int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
          START_COUNTER;
      }
#endif
   real_GOMP_parallel_start(fn, data,  num_threads);
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]=values[0]-ioverhead;;
    //  noCycle[thId]+=values[1];
   }
   parallel[thId].startExTime = getTime();
#ifdef GOMP_DEBUG
   if (gompDebug) fprintf(stderr,"GOMP Debug: finished GOMP_parallel_start, thid=%d\n",thId);
#endif
   if (modeFlag == 1)
   {
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", parallel[thId].startName,
                  parallel[thId].beginAddr, thId, parallel[thId].startTime_1,
                  parallel[thId].startExTime, instCount[thId]);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", parallel[thId].startName,
                  parallel[thId].beginAddr, thId, parallel[thId].startTime_1,
                  parallel[thId].startExTime);
   }
}

/*-------------------------------------------------------------------*
 * GOMP_parallel_end function                                        *
 *-------------------------------------------------------------------*/

/**
   @brief Calculates the time that spend in parallel section. Also, gets
          call location.
   @return void
**/

void GOMP_parallel_end (void)
{
   int thId, index;
   thId = omp_get_thread_num();
   parallel[thId].startTime_2 = getTime(); // = seqStartTime
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
      {
          int Events[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC};
          START_COUNTER;
      }
#endif
   real_GOMP_parallel_end();
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]+=values[0]-ioverhead;;
  //    noCycle[thId]+=values[1];
   }
   parallel[thId].endAddr = getReturnAddress(0);
    if (modeFlag == 1)
   {
      parallel[thId].endTime = getTime();
      parallel[thId].endName = __func__;
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld \n", parallel[thId].endName,
                  parallel[thId].endAddr, thId, parallel[thId].startTime_2,
                  parallel[thId].endTime, values[0]-ioverhead);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", parallel[thId].endName,
                  parallel[thId].endAddr, thId, parallel[thId].startTime_2,
                  parallel[thId].endTime);
   }
   else if (modeFlag == 2)
   {
      index = hash(parallel[thId].beginAddr,thId);
      if(papiFlag)
         editBucket(index, thId, parallel[thId].startName,
                     parallel[thId].beginAddr,parallel[thId].endAddr,0.0,
                     parallel[thId].startTime_2 - parallel[thId].startExTime,
                     instCount[thId]);
      else
         editBucket(index, thId, parallel[thId].startName,
                     parallel[thId].beginAddr,parallel[thId].endAddr,0.0,
                     parallel[thId].startTime_2 - parallel[thId].startExTime,0);

   }
}



/*-------------------------------------------------------------------*
 * GOMP_single_start                                                 *
 *-------------------------------------------------------------------*/

/**
   @brief gets the time related to single section for each thread.
**/

bool GOMP_single_start(void)
{
   int thId; //,index;
   bool result;
   thId = omp_get_thread_num();
   single[thId].startTime_1 = getTime();
#ifdef BUILD_PAPI
   int retval;
   long long values[NUM_EVENTS];
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]+=values[0]-ioverhead;;
      noCycle[thId]+=values[1];
   }
#endif
   result = real_GOMP_single_start();
   if(papiFlag)
   {
      STOP_COUNTER
      instCount[thId]=values[0]-ioverhead;;
     // noCycle[thId]+=values[1];
   }
   single[thId].endTime = getTime();
   single[thId].beginAddr = single[thId].endAddr = getReturnAddress(0);
   single[thId].startName = __func__;
   if (modeFlag == 1)
   {
      if(papiFlag)
         fprintf(outFile,"  %s %p %d %lf %lf %lld  \n", single[thId].startName,
                  single[thId].beginAddr, thId, single[thId].startTime_1,
                  single[thId].endTime, instCount[thId]);
      else
         fprintf(outFile,"  %s %p %d %lf %lf  \n", single[thId].startName,
                  single[thId].beginAddr, thId, single[thId].startTime_1,
                  single[thId].endTime);
   }
   return result;
}

