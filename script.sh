#!/bin/sh
#
# Example execution of a program (test) with PGOMP tracing it
#

# Set the environment variable LD_PRELOAD=<wherever libpgomp0.1.so is>.
# - may need to preload libdl.so also, depending on your system
#export LD_PRELOAD=/lib/i386-linux-gnu/libdl.so.2:./libpgomp.so.0.1
export LD_PRELOAD=./libpgomp.so.0.1

# Set the environment variable PGOMP_MODE to aggregate or trace.
export PGOMP_MODE=trace

# Execute OpenMP program
./test

# Unset environment (not strictly required with sub-shell execution, but
#                    it is if you execute it as "source script.sh")
unset LD_PRELOAD
unset PGOMP_MODE

