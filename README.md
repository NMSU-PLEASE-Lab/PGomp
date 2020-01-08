# PGomp

## Gnu OpenMP monitoring tools


## Installing PGOMP:

   0. Download and extract the PGOMP package (already done if you are reading this)
   1. Edit config.h to see if you need to customize anything
   2. Run "make". This should build the library and the test program

## Running the test program

You can use the "script.sh" shell script to run the test program and
to see how to run it. Essentially, to use PGOMP you need to:

   1. Set the environment variable LD_PRELOAD to wherever libpgomp.so.0.1 is
      (see the shell script; you may need to include the libdl.so also depending
       on your system configuration)
   2. Set the environment variable PGOMP_MODE to the mode that you want, either
      "trace" or "aggregate"
   3. Unless you changed config.h, after running the test program with the
      proper environment variable settings, you should see a file "pgomp-out.txt"
      that contains the output.

## Output Mode Format:

   The PGOMP tool can generate two different outputs according to the choosing
   of environment variable PGOMP_MODE. 
   
   1. Trace mode:
         If environment variable PGOMP_MODE was set to trace, the output 
         file  will contain several lines with 5 column in each line. These
         columns separated by one space and represent data as following:
            - First column represents function name.
            - Second column represents the call location of the function.
            - Third column represents thread Id.
            - Fourth column represents the beginning time of the actual
              library function.
            - Fifth column represents the ending time of the actual library
              function. 

      Trace mode output format example:

         GOMP_parallel_start 5b8ab23d 0 1368427149.018893 1368427149.019088  
         GOMP_barrier 5be3f68a 1 1368427149.019242 1368427149.019418  
         GOMP_critical_start 5be3f68a 1 1368427149.019443 1368427149.019443  
         GOMP_barrier 5be3f68a 3 1368427149.019243 1368427149.019418  
         GOMP_barrier 5be3f68a 2 1368427149.019243 1368427149.019419  
         GOMP_critical_end 5be3f68a 1 1368427149.019553 1368427149.019557  
         GOMP_barrier 4009c2 0 1368427149.019287 1368427149.019418  
         GOMP_critical_start 5be3f68a 3 1368427149.019475 1368427149.019557
                -              -      -         -                 -
                -              -      -         -                 -

   2. Aggregate mode:
         If environment variable PGOMP_MODE was set to aggregate, the output 
         file  will contain several lines with 7 column in each line. These
         columns separated by one space and represent data as following:
            - First column represents function name.
            - Second column represents the call location of the start
              function.
            - Third column represents the call location of the end function.
            - Fourth column represents thread Id.
            - Fifth column represents waiting time which is the time thread
              spends waiting ( doing nothing ) for example waiting to 
              acquire a lock.
            - sixth column represents execution time which is the time 
              thread spends executing the corresponding section of the 
              function. For example the duration of execution of a critical
              section.
            - Seventh column represents the execution occurrence count of 
              the function.

      Aggregate Mode output format example:
  
         GOMP_barrier 0x40175a 0x40175a 0 1.550900 0.000000 18145
         GOMP_barrier 0x40175a 0x40175a 1 20.900596 0.000000 18145
         GOMP_barrier 0x40175a 0x40175a 2 3.376095 0.000000 18145
         GOMP_barrier 0x40175a 0x40175a 3 20.892837 0.000000 18145
         GOMP_critical_start 0x40175f 0x40177c 0 0.006614 0.004384 18145
         GOMP_critical_start 0x40175f 0x40177c 1 0.028061 0.003812 18145
         GOMP_critical_start 0x40175f 0x40177c 2 0.011448 0.005477 18145
         GOMP_critical_start 0x40175f 0x40177c 3 0.029741 0.004251 18145
         GOMP_parallel_start 0x400b68 0x400b7c 0 0.000000 0.000228 1
         GOMP_parallel_start 0x400bf8 0x400c0c 0 0.000000 0.009166 1
                -              -        -      -      -       -    -
                -              -        -      -      -       -    -

