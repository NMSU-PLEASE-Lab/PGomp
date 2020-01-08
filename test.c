#include <stdio.h>
#include <omp.h>
omp_lock_t my_lock;

int main(int argc, char **argv)
{
   int i, thread_id; 
   int global_nloops, private_nloops;
   global_nloops = 0;
   #pragma omp parallel private(private_nloops, thread_id)
   {
      private_nloops = 0;
      thread_id = omp_get_thread_num();
      #pragma omp for
      for (i=0; i<100000; ++i)
         {
            ++private_nloops;
         }
      #pragma omp critical
      {
         printf("Thread %d adding its iterations (%d) to the sum (%d)...\n",
                   thread_id, private_nloops, global_nloops);
         global_nloops += private_nloops;
         printf("CRITICAL ...total nloops now equals %d.\n", global_nloops);
      }
   }
//==================================================================================
   global_nloops = 0;
   omp_init_lock(&my_lock);
   #pragma omp parallel private(private_nloops, thread_id)
   {
      private_nloops = 0;
      thread_id = omp_get_thread_num();
      #pragma omp for
      for (i=0; i<100000; ++i)
      {
         ++private_nloops;
      }
      omp_set_lock(&my_lock);
      {
         printf("Thread %d adding its iterations (%d) to the sum (%d)...\n",
                 thread_id, private_nloops, global_nloops);
         global_nloops += private_nloops;
         printf("LOCK ...total nloops now equals %d.\n", global_nloops);
      }
         omp_unset_lock(&my_lock);
   }
   omp_destroy_lock(&my_lock);
   return 0;
}
